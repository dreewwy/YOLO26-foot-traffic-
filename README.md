# YOLO26 Foot Traffic Counter & SharePoint Integration

## Overview
This project is an automated, end-to-end foot traffic counting system designed to run on headless Ubuntu environments. It captures an RTSP video stream, processes the frames through a custom C++ OpenCV and OpenVINO pipeline using a YOLO model, and logs directional foot traffic (IN/OUT) into a local JSONL file. 

A scheduled Python script handles then sends the data to a sharepoint List using Microsoft Graph API

## Flow
1. **Video Feed:** Camera RTSP stream
2. **Inference (C++):** YOLO model detects people; OpenCV point-polygon tests track movement across on screen boxes. 
3. **Local Storage:** Events are written in real-time to `traffic_log.jsonl`.
4. **Data Push (Python):** `upload.py` reads the JSONL, authenticates with Microsoft Entra ID, pushes to SharePoint, and archives the local log.
---

## Directory Structure
```text
YOLO26-foot-traffic-/
├── .env                  # (Unversioned) Microsoft Entra App credentials
├── .venv/                # Isolated Python virtual environment
├── CMakeLists.txt        # C++ build configuration
├── build/                # Compiled binaries and alignment checks
├── cron.log              # Logs from the automated daily upload
├── main.cpp              # C++ entry point and OpenCV drawing/logic
├── setup.sh              # Python environment and dependency setup script
├── tracker.cpp           # Tracking logic implementation
├── tracker.h             # Tracking headers
├── traffic_log.jsonl     # Active daily foot traffic data
├── upload.py             # Python script for SharePoint integration
└── yolo26s.onnx          # OpenVINO optimized YOLO weights
```
## Prerequisites
1. **C++ Environment:** g++, cmake, make
2. **Computer Vision:** OpenCV 4.x, Intel OpenVINO Toolkit
3. **Python Environment:** Python 3, python3-venv

These are all installed by running ```bash setup.sh```

## Compiling
setup.sh runs cmake and make but once prereqs are installed changes can be compiled by running:
```
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```
## .env Variables
The program needs both an RTSP feed and MSAL configurations, these are added in a .env file with the following names:
```
RTSP_STREAM=Your_Stream_RTSP
TENANT_ID=Your_Tenat_ID
CLIENT_ID=Your_Client_ID
CLIENT_SECRET=Your_Client_Secret
SITE_ID=Your_Sharepoint_site_ID
LIST_ID=Your_Sharepoint_list_ID
```
**The Microsoft values can be found by creating an App within Entra or Azure Admin centre**

## Headless and video display modes for configuration
Within Main.cpp you can toggle Headless mode, by turning headless mode off you are able to see the RTSP stream on your linux device and see the In and Out boxes. 
Configure your boxes to desired location by updated the area1 and area2 vectors inside Main.cpp. Once done, headless mode can be turn on. If you are running completely headless on a server you can either install a lightdm/xvfcb virtual display adapter or configure the headless PC to capture a screenshot of the RTSP render.

## Running the Code 
Due to the location of the .env file, it is recommends to run the program with ./build/people_counter, in the base directory. _you will need to update the main.cpp dotenv config to run within the build directory_

## Automation of Python 
The Python Upload script should be configured to upload on a set schedule using a **Cron Job** It will process the data in the json and upload to sharepoint, archiving the old data.

To configure open the crontab with:
```crontab -e ```
Add your desired timing (59 23 for 11:59 each day):
```
59 23 * * * cd /home/it-test/YOLO26-foot-traffic- && ./.venv/bin/python upload.py >> cron.log 2>&1
```
