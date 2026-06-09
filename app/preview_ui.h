#pragma once

#include "shared_state.h"

#include <opencv2/core.hpp>

struct GLFWwindow;

struct PreviewUiActions {
    bool quit = false;
    bool toggle_pause = false;
    bool start_align = false;
    bool apply_align = false;
    bool place_graphic = false;
    bool request_reinit = false;
    bool transform_changed = false;
};

/** ImGui video preview + grid alignment controls (rotation, pitch, depth, etc.). */
class PreviewUi {
public:
    bool init(GLFWwindow* window);
    void shutdown();

    void syncTransformFromConfig(const SponsorGridState::Transform& transform);
    void setAligningUi(bool aligning);

    PreviewUiActions render(const cv::Mat& display_bgr,
                            const SponsorGridState::Results& results,
                            SponsorGridState::Config& cfg,
                            int frame_id,
                            bool paused);

private:
    void uploadFrameTexture(const cv::Mat& bgr);
    bool drawAlignmentPanel(SponsorGridState::Config& cfg, float panel_width);

    GLFWwindow* window_ = nullptr;
    unsigned int texture_id_ = 0;
    int tex_w_ = 0;
    int tex_h_ = 0;
    bool aligning_ui_ = false;

    SponsorGridState::Transform ui_transform_{};
};
