#pragma once
#include <opencv2/opencv.hpp>
#include <map>
#include <vector>

class Tracker {
public:
    Tracker();
    // Takes in a list of bounding boxes, returns boxes with assigned IDs
    std::vector<std::vector<int>> update(const std::vector<cv::Rect>& objects_rect);

private:
    std::map<int, cv::Point> center_points;
    int id_count;
};