#include "config_loader.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cmath>

AppConfig loadAppConfig(const std::string& yaml_path)
{
    AppConfig cfg;
    YAML::Node root = YAML::LoadFile(yaml_path);
    if (!root["sponsor_grid_tracker"]) {
        return cfg;
    }
    const YAML::Node node = root["sponsor_grid_tracker"];
    auto& s = cfg.sponsor;

    s.enable_async_loftr = node["enable_async_loftr"].as<bool>(false);
    s.enable_reinit_sidecar = node["enable_reinit_sidecar"].as<bool>(true);
    s.reinit_sidecar_host = node["reinit_sidecar_host"].as<std::string>("127.0.0.1");
    s.reinit_sidecar_port = node["reinit_sidecar_port"].as<int>(5557);
    s.reinit_sidecar_slots = node["reinit_sidecar_slots"].as<int>(2);
    s.reinit_sidecar_timeout_ms = node["reinit_sidecar_timeout_ms"].as<int>(1200);
    s.reinit_sidecar_only = node["reinit_sidecar_only"].as<bool>(true);
    s.save_debug_frames = node["save_debug_frames"].as<bool>(false);
    s.reinit_retry_cooldown_ms = node["reinit_retry_cooldown_ms"].as<int>(300);
    s.auto_reinit_on_camera_return = node["auto_reinit_on_camera_return"].as<bool>(false);
    s.enable_grass_mask = node["enable_grass_mask"].as<bool>(false);
    s.grass_hue_min = node["grass_hue_min"].as<int>(30);
    s.grass_hue_max = node["grass_hue_max"].as<int>(80);
    s.grass_sat_min = node["grass_sat_min"].as<int>(40);
    s.grass_sat_max = node["grass_sat_max"].as<int>(255);
    s.grass_val_min = node["grass_val_min"].as<int>(40);
    s.grass_val_max = node["grass_val_max"].as<int>(255);
    s.show_grass_mask_preview = node["show_grass_mask_preview"].as<bool>(false);
    s.use_chroma_mask_for_tracking = node["use_chroma_mask_for_tracking"].as<bool>(false);
    s.invert_chroma_mask = node["invert_chroma_mask"].as<bool>(true);
    s.exp_tracking_mode = node["exp_tracking_mode"].as<int>(1);

    if (root["standalone"]) {
        const YAML::Node standalone = root["standalone"];
        cfg.camera_id = standalone["camera_id"].as<std::string>("1");
        cfg.auto_align = standalone["auto_align"].as<bool>(false);
        cfg.auto_place_graphic = standalone["auto_place_graphic"].as<bool>(false);
        cfg.boundary_margin = standalone["boundary_margin"].as<float>(0.0f);
    }

    return cfg;
}

SponsorGridState::Transform transformFromBoundary(
    const std::array<cv::Point2f, 4>& boundary,
    int video_width,
    int video_height)
{
    SponsorGridState::Transform t;
    t.grid_width = static_cast<float>(video_width);
    t.grid_height = static_cast<float>(video_height);
    t.grid_cols = 20;
    t.grid_rows = 15;
    t.depth_z = 1.0f;

    cv::Point2f center{0.f, 0.f};
    for (const auto& p : boundary) {
        center += p;
    }
    center *= 0.25f;

    const float top_w = cv::norm(boundary[1] - boundary[0]);
    const float bot_w = cv::norm(boundary[2] - boundary[3]);
    const float left_h = cv::norm(boundary[3] - boundary[0]);
    const float right_h = cv::norm(boundary[2] - boundary[1]);

    t.grid_width = std::max(100.f, 0.5f * (top_w + bot_w));
    t.grid_height = std::max(75.f, 0.5f * (left_h + right_h));
    t.offset_x = center.x - static_cast<float>(video_width) * 0.5f;
    t.offset_y = center.y - static_cast<float>(video_height) * 0.5f;

    const cv::Point2f top = boundary[1] - boundary[0];
    t.rotation_deg = static_cast<float>(std::atan2(top.y, top.x) * 180.0 / CV_PI);

    return t;
}

namespace {

cv::Point2f applyPlaneTransform(const cv::Point2f& local, const SponsorGridState::Transform& t)
{
    const float rad = t.rotation_deg * static_cast<float>(CV_PI) / 180.f;
    const float cos_r = std::cos(rad);
    const float sin_r = std::sin(rad);
    float rx = local.x * cos_r - local.y * sin_r;
    float ry = local.x * sin_r + local.y * cos_r;
    rx *= t.depth_z;
    ry *= t.depth_z;
    if (std::abs(t.pitch_deg) > 0.01f) {
        const float pitch_rad = t.pitch_deg * static_cast<float>(CV_PI) / 180.f;
        const float cos_p = std::cos(pitch_rad);
        const float sin_p = std::sin(pitch_rad);
        constexpr float virtual_camera_dist = 1000.f;
        const float z_offset = ry * sin_p;
        const float perspective_scale = virtual_camera_dist / (virtual_camera_dist - z_offset);
        rx *= perspective_scale;
        ry = (ry * cos_p) * perspective_scale;
    }
    return {rx, ry};
}

} // namespace

std::array<cv::Point2f, 4> alignmentQuadFromTransform(
    const SponsorGridState::Transform& transform,
    int video_width,
    int video_height)
{
    const std::vector<cv::Point2f> plane = {
        {-transform.grid_width * 0.5f, -transform.grid_height * 0.5f},
        { transform.grid_width * 0.5f, -transform.grid_height * 0.5f},
        { transform.grid_width * 0.5f,  transform.grid_height * 0.5f},
        {-transform.grid_width * 0.5f,  transform.grid_height * 0.5f},
    };
    std::array<cv::Point2f, 4> quad{};
    const float cx = static_cast<float>(video_width) * 0.5f + transform.offset_x;
    const float cy = static_cast<float>(video_height) * 0.5f + transform.offset_y;
    for (size_t i = 0; i < 4; ++i) {
        const cv::Point2f rel = applyPlaneTransform(plane[i], transform);
        quad[i] = {rel.x + cx, rel.y + cy};
    }
    return quad;
}

bool loadBoundaryFile(const std::string& path, std::array<cv::Point2f, 4>& out)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        float x = 0.f, y = 0.f;
        if (!(in >> x >> y)) {
            return false;
        }
        out[static_cast<size_t>(i)] = {x, y};
    }
    return true;
}
