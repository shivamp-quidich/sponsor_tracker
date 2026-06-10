#pragma once

#include "shared_state.h"

#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

struct PreviewUiActions {
    bool quit = false;
    bool toggle_pause = false;
    bool start_align = false;
    bool apply_align = false;
    /** Right-click placement confirmed; placement_point is in frame pixels. */
    bool place_graphic_at = false;
    cv::Point2f placement_point{-1.f, -1.f};
    bool request_reinit = false;
    bool transform_changed = false;
    /** User picked a sponsor image from the data/ folder list. */
    bool graphic_changed = false;
    /** Slot 1 scale/pitch/yaw/roll/opacity changed. */
    bool graphic_transform_changed = false;
};

/** ImGui video preview + grid alignment controls (rotation, pitch, depth, etc.). */
class PreviewUi {
public:
    bool init(GLFWwindow* window);
    void shutdown();

    void syncTransformFromConfig(const SponsorGridState::Transform& transform);
    void syncGraphicSettingsFromConfig(const SponsorGridState::Config& cfg);
    void setAligningUi(bool aligning);
    /** Arm placement mode: next right-click on the video sets the graphic anchor. */
    void armGraphicPlacement();

    PreviewUiActions render(const cv::Mat& display_bgr,
                            const SponsorGridState::Results& results,
                            SponsorGridState::Config& cfg,
                            int frame_id,
                            bool paused);

private:
    void uploadFrameTexture(const cv::Mat& bgr);
    bool drawAlignmentPanel(SponsorGridState::Config& cfg, float panel_width);
    bool drawGraphicTransformPanel(SponsorGridState::Config& cfg, float panel_width);
    bool drawGraphicPanel(SponsorGridState::Config& cfg, float panel_width);
    void refreshAvailableGraphics();
    int graphicIndexForPath(const std::string& path) const;

    GLFWwindow* window_ = nullptr;
    unsigned int texture_id_ = 0;
    int tex_w_ = 0;
    int tex_h_ = 0;
    bool aligning_ui_ = false;
    bool placement_armed_ = false;

    SponsorGridState::Transform ui_transform_{};
    std::vector<std::string> available_graphics_;
    int selected_graphic_index_ = -1;

    float graphic_scale_ = 1.f;
    float graphic_rotation_ = 0.f;
    float graphic_yaw_deg_ = 0.f;
    float graphic_pitch_deg_ = 0.f;
    float graphic_roll_deg_ = 0.f;
    float graphic_opacity_ = 1.f;
};
