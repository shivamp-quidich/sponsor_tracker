#pragma once

#include "shared_state.h"
#include <string>

struct AppConfig {
    SponsorGridState::Config sponsor;
    std::string camera_id = "1";
    bool auto_align = false;
    bool auto_place_graphic = false;
    float boundary_margin = 0.0f;
};

/** Load sponsor_grid_tracker settings from a StiQy-style YAML file. */
AppConfig loadAppConfig(const std::string& yaml_path);

/** Derive grid transform offsets/size from a 4-point boundary (TL,TR,BR,BL). */
SponsorGridState::Transform transformFromBoundary(
    const std::array<cv::Point2f, 4>& boundary,
    int video_width,
    int video_height);

/** Read boundary file (one x y pair per line, 4 lines). */
bool loadBoundaryFile(const std::string& path, std::array<cv::Point2f, 4>& out);

/** Preview quad for the alignment plane (matches SponsorGridTracker::applyAlignment). */
std::array<cv::Point2f, 4> alignmentQuadFromTransform(
    const SponsorGridState::Transform& transform,
    int video_width,
    int video_height);
