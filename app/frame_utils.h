#pragma once

#include "shared_data.h"
#include <opencv2/core.hpp>
#include <memory>

/** Upload a BGR CPU frame into a pooled UYVYFrame on the GPU. */
bool uploadBgrToUyvyFrame(const cv::Mat& bgr, UYVYFrame& uyvy_out);

/** Create a FrameData with monotonically increasing id and UYVY payload. */
std::shared_ptr<FrameData> makeFrameFromBgr(const cv::Mat& bgr, int frame_id);
