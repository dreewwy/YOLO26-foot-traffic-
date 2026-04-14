#include "tracker.h"
#include <cmath>

Tracker::Tracker() {
    id_count = 0;
}

std::vector<std::vector<int>> Tracker::update(const std::vector<cv::Rect>& objects_rect) {
    std::vector<std::vector<int>> objects_bbs_ids;

    for (const auto& rect : objects_rect) {
        int x = rect.x;
        int y = rect.y;
        int w = rect.width;
        int h = rect.height;
        
        // Calculate center point
        int cx = x + w / 2;
        int cy = y + h / 2;

        bool same_object_detected = false;
        for (auto it = center_points.begin(); it != center_points.end(); ++it) {
            int id = it->first;
            cv::Point pt = it->second;

            // Euclidean distance
            double distance = std::hypot(cx - pt.x, cy - pt.y);

            if (distance < 35.0) {
                center_points[id] = cv::Point(cx, cy);
                objects_bbs_ids.push_back({x, y, w, h, id});
                same_object_detected = true;
                break;
            }
        }

        if (!same_object_detected) {
            center_points[id_count] = cv::Point(cx, cy);
            objects_bbs_ids.push_back({x, y, w, h, id_count});
            id_count++;
        }
    }

    // Clean up dictionary by removing IDs not used in this frame
    std::map<int, cv::Point> new_center_points;
    for (const auto& obj_bb_id : objects_bbs_ids) {
        int id = obj_bb_id[4];
        new_center_points[id] = center_points[id];
    }
    center_points = new_center_points;

    return objects_bbs_ids;
}