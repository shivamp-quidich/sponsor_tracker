#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "preview_ui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const char* statusLabel(SponsorGridState::TrackingStatus s)
{
    switch (s) {
        case SponsorGridState::TrackingStatus::IDLE: return "IDLE";
        case SponsorGridState::TrackingStatus::ALIGNING: return "ALIGNING";
        case SponsorGridState::TrackingStatus::TRACKING: return "TRACKING";
        case SponsorGridState::TrackingStatus::LOST: return "LOST";
        case SponsorGridState::TrackingStatus::RECOVERING: return "RECOVERING";
    }
    return "?";
}

void applyUiTransformToConfig(SponsorGridState::Config& cfg,
                              const SponsorGridState::Transform& ui)
{
    cfg.transform = ui;
    cfg.transform_changed = true;
}

} // namespace

bool PreviewUi::init(GLFWwindow* window)
{
    window_ = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        return false;
    }
    return true;
}

void PreviewUi::shutdown()
{
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void PreviewUi::syncTransformFromConfig(const SponsorGridState::Transform& transform)
{
    ui_transform_ = transform;
}

int PreviewUi::graphicIndexForPath(const std::string& path) const
{
    if (path.empty()) {
        return -1;
    }
    for (size_t i = 0; i < available_graphics_.size(); ++i) {
        if (available_graphics_[i] == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PreviewUi::syncGraphicSettingsFromConfig(const SponsorGridState::Config& cfg)
{
    refreshAvailableGraphics();
    selected_graphic_index_ = graphicIndexForPath(cfg.graphic_path);
    graphic_scale_ = cfg.graphic_scale;
    graphic_rotation_ = cfg.graphic_rotation;
    graphic_yaw_deg_ = cfg.graphic_yaw_deg;
    graphic_pitch_deg_ = cfg.graphic_pitch_deg;
    graphic_roll_deg_ = cfg.graphic_roll_deg;
    graphic_opacity_ = cfg.graphic_opacity;
}

void PreviewUi::refreshAvailableGraphics()
{
    available_graphics_.clear();
    namespace fs = std::filesystem;
    try {
        for (const auto& entry : fs::directory_iterator("data/")) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                available_graphics_.push_back(entry.path().string());
            }
        }
        std::sort(available_graphics_.begin(), available_graphics_.end());
    } catch (const std::exception&) {
        // data/ may not exist yet
    }
}

void PreviewUi::setAligningUi(bool aligning)
{
    aligning_ui_ = aligning;
}

void PreviewUi::armGraphicPlacement()
{
    placement_armed_ = true;
}

void PreviewUi::uploadFrameTexture(const cv::Mat& bgr)
{
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return;
    }

    if (texture_id_ == 0 || tex_w_ != bgr.cols || tex_h_ != bgr.rows) {
        if (texture_id_ != 0) {
            glDeleteTextures(1, &texture_id_);
        }
        glGenTextures(1, &texture_id_);
        tex_w_ = bgr.cols;
        tex_h_ = bgr.rows;
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex_w_, tex_h_, 0, GL_BGR, GL_UNSIGNED_BYTE, bgr.data);
    } else {
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (bgr.isContinuous()) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w_, tex_h_, GL_BGR, GL_UNSIGNED_BYTE, bgr.data);
        } else {
            cv::Mat continuous;
            bgr.copyTo(continuous);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w_, tex_h_, GL_BGR, GL_UNSIGNED_BYTE, continuous.data);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void drawVideoOverlays(const SponsorGridState::Results& results,
                              const ImVec2& image_pos,
                              float scale_x,
                              float scale_y)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();

    auto toScreen = [&](const cv::Point2f& p) {
        return ImVec2(image_pos.x + p.x * scale_x, image_pos.y + p.y * scale_y);
    };

    if (results.status == SponsorGridState::TrackingStatus::ALIGNING) {
        for (size_t i = 0; i < results.grid_points.size(); i += 4) {
            const ImVec2 pt = toScreen(results.grid_points[i]);
            draw->AddCircleFilled(pt, 3.f, IM_COL32(100, 200, 255, 220));
        }
    }

    if (results.status == SponsorGridState::TrackingStatus::TRACKING
        && results.has_placed_graphic && !results.sponsor_graphics_hidden) {
        ImVec2 quad[4];
        for (int i = 0; i < 4; ++i) {
            quad[i] = toScreen(results.graphic_quad[static_cast<size_t>(i)]);
        }
        draw->AddPolyline(quad, 4, IM_COL32(0, 255, 0, 255), ImDrawFlags_Closed, 2.f);
    }
}

bool PreviewUi::drawAlignmentPanel(SponsorGridState::Config& cfg, float panel_width)
{
    bool transform_changed = false;
    const float w = std::max(120.f, panel_width - 16.f);
    const float half = (w - 8.f) * 0.5f;

    ImGui::Text("Position");
    ImGui::SetNextItemWidth(half);
    if (ImGui::DragFloat("##offset_x", &ui_transform_.offset_x, 1.f, -960.f, 960.f, "X: %.0f")) {
        transform_changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    if (ImGui::DragFloat("##offset_y", &ui_transform_.offset_y, 1.f, -540.f, 540.f, "Y: %.0f")) {
        transform_changed = true;
    }

    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##depth_z", &ui_transform_.depth_z, 0.1f, 2.5f, "Depth: %.2f")) {
        transform_changed = true;
    }

    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##rotation", &ui_transform_.rotation_deg, -180.f, 180.f, "Rotate: %.1f deg")) {
        transform_changed = true;
    }

    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##pitch", &ui_transform_.pitch_deg, -89.f, 89.f, "Pitch: %.1f deg")) {
        transform_changed = true;
    }

    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##roll", &ui_transform_.roll_deg, -180.f, 180.f, "Roll: %.1f deg")) {
        transform_changed = true;
    }

    ImGui::Spacing();
    ImGui::Text("Grid density");
    ImGui::SetNextItemWidth(half);
    if (ImGui::SliderInt("##cols", &ui_transform_.grid_cols, 5, 50, "Cols: %d")) {
        transform_changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    if (ImGui::SliderInt("##rows", &ui_transform_.grid_rows, 4, 40, "Rows: %d")) {
        transform_changed = true;
    }

    if (transform_changed) {
        ui_transform_.grid_width = cfg.transform.grid_width;
        ui_transform_.grid_height = cfg.transform.grid_height;
        applyUiTransformToConfig(cfg, ui_transform_);
    }
    return transform_changed;
}

bool PreviewUi::drawGraphicTransformPanel(SponsorGridState::Config& cfg, float panel_width)
{
    bool graphic_transform_changed = false;
    const float w = std::max(120.f, panel_width - 16.f);

    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##gfx_scale", &graphic_scale_, 0.1f, 5.f, "Scale: %.2fx")) {
        graphic_transform_changed = true;
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##gfx_pitch", &graphic_pitch_deg_, -180.f, 180.f, "Pitch: %.1f deg")) {
        graphic_transform_changed = true;
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##gfx_yaw", &graphic_yaw_deg_, -180.f, 180.f, "Yaw: %.1f deg")) {
        graphic_transform_changed = true;
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##gfx_roll", &graphic_roll_deg_, -180.f, 180.f, "Roll: %.1f deg")) {
        graphic_transform_changed = true;
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##gfx_opacity", &graphic_opacity_, 0.f, 1.f, "Opacity: %.2f")) {
        graphic_transform_changed = true;
    }

    if (ImGui::Button("Reset Transform", ImVec2(w, 0.f))) {
        graphic_scale_ = 1.f;
        graphic_rotation_ = 0.f;
        graphic_yaw_deg_ = 0.f;
        graphic_pitch_deg_ = 0.f;
        graphic_roll_deg_ = 0.f;
        graphic_opacity_ = 1.f;
        graphic_transform_changed = true;
    }

    if (graphic_transform_changed) {
        cfg.graphic_scale = graphic_scale_;
        cfg.graphic_rotation = graphic_rotation_;
        cfg.graphic_yaw_deg = graphic_yaw_deg_;
        cfg.graphic_pitch_deg = graphic_pitch_deg_;
        cfg.graphic_roll_deg = graphic_roll_deg_;
        cfg.graphic_opacity = graphic_opacity_;
        cfg.graphic_transform_changed = true;
    }

    return graphic_transform_changed;
}

bool PreviewUi::drawGraphicPanel(SponsorGridState::Config& cfg, float panel_width)
{
    bool graphic_changed = false;
    const float w = std::max(120.f, panel_width - 16.f);

    if (available_graphics_.empty()) {
        refreshAvailableGraphics();
    }

    std::string preview = "Select image...";
    if (selected_graphic_index_ >= 0
        && selected_graphic_index_ < static_cast<int>(available_graphics_.size())) {
        preview = std::filesystem::path(available_graphics_[static_cast<size_t>(selected_graphic_index_)])
                      .filename()
                      .string();
    } else if (!cfg.graphic_path.empty()) {
        preview = std::filesystem::path(cfg.graphic_path).filename().string();
    }

    ImGui::SetNextItemWidth(w - 36.f);
    if (ImGui::BeginCombo("##img_select", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(available_graphics_.size()); ++i) {
            const std::string filename =
                std::filesystem::path(available_graphics_[static_cast<size_t>(i)]).filename().string();
            if (ImGui::Selectable(filename.c_str(), selected_graphic_index_ == i)) {
                selected_graphic_index_ = i;
                cfg.graphic_path = available_graphics_[static_cast<size_t>(i)];
                cfg.graphic_changed = true;
                graphic_changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("R##gfx_refresh", ImVec2(28.f, 0.f))) {
        refreshAvailableGraphics();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh image list");
    }

    if (available_graphics_.empty()) {
        ImGui::TextWrapped("Put PNG/JPG images in data/");
    }

    return graphic_changed;
}

PreviewUiActions PreviewUi::render(const cv::Mat& display_bgr,
                                   const SponsorGridState::Results& results,
                                   SponsorGridState::Config& cfg,
                                   int frame_id,
                                   bool paused)
{
    PreviewUiActions actions;
    glfwMakeContextCurrent(window_);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    constexpr float kSidePanelWidth = 300.f;
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(kSidePanelWidth, viewport.y));
    ImGui::Begin("Controls", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Frame %d", frame_id);
    ImGui::Text("Status: %s", statusLabel(results.status));
    ImGui::Text("Conf: %.2f  pts: %d/%d", results.tracking_confidence,
                results.active_point_count, results.total_point_count);
    if (paused) {
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "[PAUSED]");
    }

    ImGui::Spacing();
    if (ImGui::Button(paused ? "Resume" : "Pause", ImVec2(-1, 0))) {
        actions.toggle_pause = true;
    }

    if (aligning_ui_) {
        if (ImGui::Button("Apply Alignment", ImVec2(-1, 0))) {
            actions.apply_align = true;
            aligning_ui_ = false;
        }
    } else {
        if (ImGui::Button("Align Grid", ImVec2(-1, 0))) {
            actions.start_align = true;
            aligning_ui_ = true;
            syncTransformFromConfig(cfg.transform);
        }
    }

    if (ImGui::Button("Place Graphic", ImVec2(-1, 0))) {
        placement_armed_ = true;
    }
    if (placement_armed_) {
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.5f, 1.f),
                           "Right-click on video to place graphic");
    }
    if (ImGui::Button("Reinit", ImVec2(-1, 0))) {
        actions.request_reinit = true;
    }

    ImGui::Spacing();
    if (aligning_ui_ || results.status == SponsorGridState::TrackingStatus::ALIGNING) {
        if (ImGui::CollapsingHeader("Grid Alignment", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (drawAlignmentPanel(cfg, kSidePanelWidth)) {
                actions.transform_changed = true;
            }
        }
    }

    const bool has_graphic = selected_graphic_index_ >= 0 || !cfg.graphic_path.empty();
    const bool is_tracking = results.status == SponsorGridState::TrackingStatus::TRACKING;
    if (is_tracking || has_graphic || placement_armed_) {
        if (ImGui::CollapsingHeader("Graphic Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (drawGraphicTransformPanel(cfg, kSidePanelWidth)) {
                actions.graphic_transform_changed = true;
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Sponsor Image", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (drawGraphicPanel(cfg, kSidePanelWidth)) {
            actions.graphic_changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Keys: Space=pause, a=align, Enter=apply, p=arm place, "
                       "right-click=set position, r=reinit, q=quit");
    if (ImGui::Button("Quit", ImVec2(-1, 0))) {
        actions.quit = true;
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(kSidePanelWidth, 0));
    ImGui::SetNextWindowSize(ImVec2(viewport.x - kSidePanelWidth, viewport.y));
    ImGui::Begin("Video", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoScrollbar);

    uploadFrameTexture(display_bgr);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = 1.f;
    if (tex_w_ > 0 && tex_h_ > 0) {
        scale = std::min(avail.x / static_cast<float>(tex_w_),
                         avail.y / static_cast<float>(tex_h_));
    }
    const ImVec2 image_size(tex_w_ > 0 ? tex_w_ * scale : avail.x,
                            tex_h_ > 0 ? tex_h_ * scale : avail.y);
    const ImVec2 image_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(texture_id_)),
                 image_size, ImVec2(0, 0), ImVec2(1, 1));

    const float scale_x = (tex_w_ > 0) ? (image_size.x / static_cast<float>(tex_w_)) : 1.f;
    const float scale_y = (tex_h_ > 0) ? (image_size.y / static_cast<float>(tex_h_)) : 1.f;
    drawVideoOverlays(results, image_pos, scale_x, scale_y);

    if (placement_armed_ && ImGui::IsWindowHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float ui_x = mouse.x - image_pos.x;
        const float ui_y = mouse.y - image_pos.y;
        if (ui_x >= 0.f && ui_x <= image_size.x && ui_y >= 0.f && ui_y <= image_size.y
            && tex_w_ > 0 && tex_h_ > 0) {
            actions.place_graphic_at = true;
            actions.placement_point = cv::Point2f(
                ui_x * static_cast<float>(tex_w_) / image_size.x,
                ui_y * static_cast<float>(tex_h_) / image_size.y);
            placement_armed_ = false;
        }
    }

    ImGui::End();

    ImGui::Render();
    const int fb_w = static_cast<int>(viewport.x);
    const int fb_h = static_cast<int>(viewport.y);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.1f, 0.1f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);

    return actions;
}
