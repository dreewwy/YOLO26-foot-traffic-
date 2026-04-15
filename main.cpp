#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <openvino/openvino.hpp>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include "tracker.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <iomanip>
#pragma warning(disable : 4996) // Prevents MSVC from complaining about localtime
#include <fstream>
#include <ctime>
using namespace cv;
using namespace std;

// ─────────────────────────────────────────
//  Config
// ─────────────────────────────────────────
const bool  HEADLESS_MODE  = false; // Set to true to disable window & drawing
const float  INPUT_WIDTH      = 640.0f;
const float  INPUT_HEIGHT     = 640.0f;
const float  CONF_THRESHOLD   = 0.10f;
const int    DETECT_EVERY_N   = 4;
const string MODEL_ONNX       = "yolo26s.onnx";

// Display resolution — 16:9, kept separate from inference size
const int DISPLAY_W = 640;
const int DISPLAY_H = 360;

// ─────────────────────────────────────────
//  Draw text with background box
// ─────────────────────────────────────────
void putTextRect(Mat& img, const string& text, Point pos,
                 double fontScale, int thickness,
                 Scalar color, Scalar bgColor)
{
    int baseline = 0;
    Size textSize = getTextSize(text, FONT_HERSHEY_COMPLEX, fontScale, thickness, &baseline);
    Rect bgRect(pos.x, pos.y - textSize.height - 3, textSize.width, textSize.height + 6);
    rectangle(img, bgRect, bgColor, -1);
    putText(img, text, pos, FONT_HERSHEY_COMPLEX, fontScale, color, thickness);
}

// ─────────────────────────────────────────
//  Threaded RTSP capture
//  Uses grab()/retrieve() split to always
//  drain the buffer and return the latest frame
// ─────────────────────────────────────────
class RTSPCapture {
public:
    Mat          frame;
    mutex        mtx;
    atomic<bool> running{true};
    atomic<bool> has_frame{false};

    RTSPCapture(const string& url) {
        cap.open(url, CAP_FFMPEG, {
            CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
            CAP_PROP_READ_TIMEOUT_MSEC, 5000,
        });
        cap.set(CAP_PROP_BUFFERSIZE, 1);
        cap.set(CAP_PROP_FPS, 30);

        if (!cap.isOpened()) {
            cerr << "Error: Could not open RTSP stream." << endl;
            running = false;
            return;
        }

        worker = thread([this]() {
            Mat f;
            while (running) {
                // grab() discards at FFmpeg level — drains buffer
                if (!cap.grab()) {
                    this_thread::sleep_for(chrono::milliseconds(5));
                    continue;
                }
                // retrieve() only decodes what we actually need
                cap.retrieve(f);
                if (!f.empty()) {
                    lock_guard<mutex> lock(mtx);
                    frame = f.clone();
                    has_frame = true;
                }
            }
        });
    }

    bool getFrame(Mat& out) {
        if (!has_frame) return false;
        lock_guard<mutex> lock(mtx);
        out = frame.clone();
        return true;
    }

    ~RTSPCapture() {
        running = false;
        if (worker.joinable()) worker.join();
        cap.release();
    }

private:
    VideoCapture cap;
    thread       worker;
};

// ─────────────────────────────────────────
//  OpenVINO inference
//  Expects a square frame (INPUT_WIDTH x INPUT_HEIGHT)
//  Returns boxes in the same coordinate space as the input frame
// ─────────────────────────────────────────
vector<Rect> runYOLO(ov::InferRequest& request,
                     const Mat& frame,
                     float conf_threshold)
{
    Mat blob;
    dnn::blobFromImage(frame, blob, 1.0 / 255.0,
                       Size((int)INPUT_WIDTH, (int)INPUT_HEIGHT),
                       Scalar(), true, false, CV_32F);

    auto input_tensor = request.get_input_tensor(0);

    size_t expected = input_tensor.get_size();
    size_t actual   = (size_t)blob.total();

    // cout << "Input tensor size: " << expected << endl;
    // cout << "Blob total: " << actual << endl;

    if (expected != actual) {
        cerr << "[WARN] Input tensor size mismatch: model expects "
             << expected << " got " << actual << endl;
        return {};
    }

    memcpy(input_tensor.data<float>(), blob.ptr<float>(),
           expected * sizeof(float));

    request.infer();

    // Declare shape and data BEFORE using them
    auto output_tensor = request.get_output_tensor(0);
    auto shape         = output_tensor.get_shape();

    if (shape.size() < 3) {
        cerr << "[WARN] Unexpected output shape rank: " << shape.size() << endl;
        return {};
    }

    size_t num_det  = shape[1];
    size_t row_size = shape[2];
    const float* data = output_tensor.data<float>();

    // Now safe to use shape and data
    // cout << "Inference ran. Output shape: "
    //      << shape[0] << "x" << shape[1] << "x" << shape[2] << endl;
    // cout << "First 5 output values: ";
    // for (int i = 0; i < 5; i++)
    //     cout << data[i] << " ";
    // cout << endl;

    // Debug: print all detections above very low threshold
    // for (size_t i = 0; i < num_det; i++) {
    //     const float* row = data + i * row_size;
    //     if (row[4] > 0.05f)
    //         cout << "cls=" << (int)row[5] << " conf=" << fixed
    //              << setprecision(2) << row[4]
    //              << " box=[" << row[0] << "," << row[1]
    //              << "," << row[2] << "," << row[3] << "]" << endl;
    // }

    float x_factor = (float)frame.cols / INPUT_WIDTH;
    float y_factor = (float)frame.rows / INPUT_HEIGHT;

    vector<Rect> detections;
    detections.reserve(32);

    for (size_t i = 0; i < num_det; ++i) {
        const float* row = data + i * row_size;
        float conf = row[4];
        int   cls  = (int)row[5];

        if (conf < conf_threshold || cls != 0) continue;

        float x1 = row[0], y1 = row[1], x2 = row[2], y2 = row[3];
        int left = (int)(x1 * x_factor);
        int top  = (int)(y1 * y_factor);
        int iw   = (int)((x2 - x1) * x_factor);
        int ih   = (int)((y2 - y1) * y_factor);

        left = max(0, left);
        top  = max(0, top);
        iw   = min(iw, frame.cols - left);
        ih   = min(ih, frame.rows - top);

        if (iw > 0 && ih > 0)
            detections.emplace_back(left, top, iw, ih);
    }

    return detections;
}

void logTraffic(const string& event_type, int in_count, int out_count) {
    ofstream log_file("traffic_log.jsonl", ios::app); // Open in append mode
    if (!log_file.is_open()) return;

    time_t now = time(nullptr);
    tm* ltm = localtime(&now);

    // Writes a single line like: {"time": "2026-04-14 11:45:00", "event": "IN", "in": 5, "out": 2}
    log_file << "{\"time\": \"" << put_time(ltm, "%Y-%m-%d %H:%M:%S") 
             << "\", \"event\": \"" << event_type 
             << "\", \"in\": " << in_count 
             << ", \"out\": " << out_count << "}\n";
}
// Load rtsp from .env
void loadEnv(const string& path = ".env"){
  ifstream file(path);
  string line;
  while (getline(file, line)) {
    line.erase(remove(line.begin(), line.end(), '\r'), line.end());
    
    if (line.empty() || line[0] == '#') continue;
    
    auto pos = line.find("=");
    if (pos != string::npos) {
      string key = line.substr(0,pos);
      string value = line.substr(pos + 1);
      setenv(key.c_str(), value.c_str(), 1);
    }
  }
}

// ─────────────────────────────────────────
//  Main
// ─────────────────────────────────────────
int main()
{
    try {
    
        // FIX: ensure WSLg display is set before any OpenCV window call
        loadEnv();
        cv::setNumThreads(4);

        // ── Load model ────────────────────────
        cout << "Loading YOLO26m model via OpenVINO..." << endl;
        ov::Core core;

        for (auto& d : core.get_available_devices())
            cout << "  Device: " << d << endl;

        auto model = core.read_model(MODEL_ONNX);

        ov::AnyMap config = {
            ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY),
            ov::hint::num_requests(1),
            ov::hint::inference_precision(ov::element::f32)
        };

        // Try GPU first, fall back to CPU
        string device = "CPU";
        for (auto& d : core.get_available_devices()) {
            if (d.find("GPU") != string::npos) { device = d; break; }
        }
        cout << "Compiling for device: " << device << endl;

        auto compiled  = core.compile_model(model, device, config);
        auto infer_req = compiled.create_infer_request();

        for (auto& inp : compiled.inputs())
            cout << "Input  shape: " << inp.get_partial_shape()
                << "  type: " << inp.get_element_type() << endl;
        for (auto& out : compiled.outputs())
            cout << "Output shape: " << out.get_partial_shape()
                << "  type: " << out.get_element_type() << endl;

        // ── Connect to camera ─────────────────
        cout << "Connecting to RTSP stream..." << endl;
        const char* rtsp_env = getenv("RTSP_STREAM");
        string RTSP_URL = rtsp_env ? rtsp_env : "";
        if (RTSP_URL.empty()){
          cerr << "[FATAL ERROR] RTSP_URL not found in .env file" << endl;
          return -1;
        }
        RTSPCapture camera(RTSP_URL);
        if (!camera.running) return -1;

        cout << "Waiting for first frame..." << endl;
        Mat initial;
        while (!camera.getFrame(initial))
            this_thread::sleep_for(chrono::milliseconds(50));
        cout << "Stream connected." << endl;

        // ── Create display window upfront ─────
        namedWindow("people_counter", WINDOW_NORMAL);
        resizeWindow("people_counter", DISPLAY_W, DISPLAY_H);
        moveWindow("people_counter", 100, 100);

        ::Tracker tracker;

        // ── Counting zones ────────────────────
        // Vertical strip regions on the 640x360 display frame.
        // Adjust these once you can see the video feed to align with your doorway.
        // OUT: person crosses area2 → area1
        // IN:  person crosses area1 → area2
        vector<Point> area1 = {Point(220,0), Point(220,DISPLAY_H),
                                Point(300,DISPLAY_H), Point(300,0)};
        vector<Point> area2 = {Point(320,0), Point(320,DISPLAY_H),
                                Point(400,DISPLAY_H), Point(400,0)};

        map<int, Point> going_out, going_in;
        vector<int>     counter_out, counter_in;

        Mat    process_frame;
        auto   start_time   = chrono::high_resolution_clock::now();
        int    frame_count  = 0;
        int    total_frames = 0;
        double fps          = 0.0;

        vector<Rect>        last_detected;
        vector<vector<int>> objects_bbs_ids;

        while (true)
        {
            Mat raw;
            if (!camera.getFrame(raw)) {
                this_thread::sleep_for(chrono::milliseconds(1));
                continue;
            }

            // FIX: separate display frame (640x360) and inference frame (640x640)
            // Passing a 640x360 frame to a 640x640 model squishes people vertically
            // and destroys detection accuracy
            resize(raw, process_frame, Size(DISPLAY_W, DISPLAY_H));  // for UI

            Mat infer_frame;
            resize(raw, infer_frame, Size(640, 640));                 // square for YOLO

            total_frames++;
            bool run_detection = (total_frames % DETECT_EVERY_N == 0);

            if (run_detection) {
                // runYOLO returns boxes in 640x640 space
                last_detected = runYOLO(infer_req, infer_frame, CONF_THRESHOLD);

                // FIX: scale boxes from 640x640 inference space → 640x360 display space
                // x scale = 640/640 = 1.0 (no change needed)
                // y scale = 360/640 = 0.5625
                const float scale_y = (float)DISPLAY_H / 640.0f;
                for (auto& r : last_detected) {
                    r.y      = (int)(r.y      * scale_y);
                    r.height = (int)(r.height * scale_y);
                }
            }

            // Tracker runs every frame — interpolates positions between detections
            objects_bbs_ids = tracker.update(last_detected);
            // Debug: yellow boxes = raw detections
            // for (const auto& r : last_detected)
            //     rectangle(process_frame, r, Scalar(0,255,255), 2);
            // putText(process_frame, "Raw: " + to_string(last_detected.size()),
            //         Point(20,200), FONT_HERSHEY_COMPLEX, 0.6, Scalar(0,255,255), 2);
            // ── Tracking & counting ───────────
            for (const auto& bbox : objects_bbs_ids)
            {
                int x1     = bbox[0], y1 = bbox[1];
                int x2     = bbox[0] + bbox[2];
                int y2     = bbox[1] + bbox[3];
                int obj_id = bbox[4];
                Point br(x2, y2);  // bottom-right corner used for zone test

                // OUT: seen in area2 first, then crosses into area1
                if (pointPolygonTest(area2, br, false) >= 0)
                    going_out[obj_id] = br;

                if (going_out.count(obj_id) && pointPolygonTest(area1, br, false) >= 0) {
                    if (!HEADLESS_MODE) {
                        circle(process_frame, br, 4, Scalar(0,255,0), -1);
                        rectangle(process_frame, Point(x1,y1), Point(x2,y2), Scalar(255,255,255), 2);
                        putTextRect(process_frame, to_string(obj_id), Point(x1,y1), 1, 1, Scalar(0,0,0), Scalar(255,255,255));
                    }
                    
                    if (find(counter_out.begin(), counter_out.end(), obj_id) == counter_out.end()) {
                        counter_out.push_back(obj_id);
                        // LOG THE EVENT!
                        logTraffic("OUT", counter_in.size(), counter_out.size());
                        if (HEADLESS_MODE) cout << "[LOG] Person walked OUT. Total Out: " << counter_out.size() << endl;
                    }
                }

                // IN: seen in area1 first, then crosses into area2
                if (pointPolygonTest(area1, br, false) >= 0)
                    going_in[obj_id] = br;

                if (going_in.count(obj_id) && pointPolygonTest(area2, br, false) >= 0) {
                    if (!HEADLESS_MODE) {
                        circle(process_frame, br, 4, Scalar(0,255,0), -1);
                        rectangle(process_frame, Point(x1,y1), Point(x2,y2), Scalar(255,0,0), 2);
                        putTextRect(process_frame, to_string(obj_id), Point(x1,y1), 1, 1, Scalar(255,255,255), Scalar(255,0,0));
                    }

                    if (find(counter_in.begin(), counter_in.end(), obj_id) == counter_in.end()) {
                        counter_in.push_back(obj_id);
                        // LOG THE EVENT!
                        logTraffic("IN", counter_in.size(), counter_out.size());
                        if (HEADLESS_MODE) cout << "[LOG] Person walked IN. Total In: " << counter_in.size() << endl;
                    }
                }
            }

            // ── FPS counter ───────────────────
            frame_count++;
            auto now = chrono::high_resolution_clock::now();
            chrono::duration<double> elapsed = now - start_time;
            if (elapsed.count() >= 1.0) {
                fps         = frame_count / elapsed.count();
                frame_count = 0;
                start_time  = now;
            }

            // ── Draw UI ───────────────────────
            if (!HEADLESS_MODE) {
                putText(process_frame, "In:  " + to_string(counter_in.size()),
                        Point(20,50),  FONT_HERSHEY_COMPLEX, 0.8, Scalar(0,255,0), 2);
                putText(process_frame, "Out: " + to_string(counter_out.size()),
                        Point(20,90),  FONT_HERSHEY_COMPLEX, 0.8, Scalar(0,0,255), 2);

                char fps_str[20];
                snprintf(fps_str, sizeof(fps_str), "FPS: %.1f", fps);
                putText(process_frame, fps_str,
                        Point(20,130), FONT_HERSHEY_COMPLEX, 0.6, Scalar(255,255,0), 2);
                putText(process_frame, run_detection ? "DETECT" : "TRACK",
                        Point(20,160), FONT_HERSHEY_COMPLEX, 0.5,
                        run_detection ? Scalar(0,255,255) : Scalar(180,180,180), 1);

                polylines(process_frame, area1, true, Scalar(0,255,0), 2);
                polylines(process_frame, area2, true, Scalar(0,255,0), 2);

                imshow("people_counter", process_frame);
                if (waitKey(1) == 27) break;  // ESC to quit
            }
        }

        destroyAllWindows();

        return 0;
    } catch (const std::exception& e) {
        // This will catch any OpenVINO crashes and print the exact reason
        cerr << "\n[FATAL ERROR] OpenVINO Exception: " << e.what() << endl;
        return -1;
    }
}
