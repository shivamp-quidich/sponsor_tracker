#include "frame_utils.h"

#include <opencv2/imgproc.hpp>
#include <cuda_runtime.h>
#include <cstring>

bool uploadBgrToUyvyFrame(const cv::Mat& bgr, UYVYFrame& uyvy_out)
{
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return false;
    }

    const int width = bgr.cols;
    const int height = bgr.rows;
    const int pitch = width * 2;
    const size_t bytes = static_cast<size_t>(pitch) * static_cast<size_t>(height);

    cv::Mat uyvy_cpu;
    cv::cvtColor(bgr, uyvy_cpu, cv::COLOR_BGR2YUV_UYVY);
    if (uyvy_cpu.empty() || uyvy_cpu.type() != CV_8UC2) {
        return false;
    }

    if (!uyvy_out.allocate(width, height, pitch)) {
        return false;
    }

    if (uyvy_cpu.isContinuous() && uyvy_cpu.step == static_cast<size_t>(pitch)) {
        cudaMemcpy(uyvy_out.d_data, uyvy_cpu.data, bytes, cudaMemcpyHostToDevice);
    } else {
        cudaMemcpy2D(uyvy_out.d_data, pitch, uyvy_cpu.data, uyvy_cpu.step,
                     pitch, height, cudaMemcpyHostToDevice);
    }
    return true;
}

std::shared_ptr<FrameData> makeFrameFromBgr(const cv::Mat& bgr, int frame_id)
{
    auto frame = std::make_shared<FrameData>();
    frame->id = frame_id;
    frame->timestamp = std::chrono::steady_clock::now();
    frame->sync_timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    auto uyvy = std::make_unique<UYVYFrame>();
    if (!uploadBgrToUyvyFrame(bgr, *uyvy)) {
        return nullptr;
    }
    frame->uyvy_frame = std::move(uyvy);
    return frame;
}
