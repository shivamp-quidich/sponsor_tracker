#include "SponsorGridTracker.h"
#include "config_loader.h"
#include "frame_utils.h"
#include "logger.h"
#include "opengl_context_manager.hpp"
#include "preview_ui.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct CliOptions {
    std::string video_path;
    std::string config_path = "config/config.yaml";
    std::string boundary_path;
    std::string output_dir = "output";
    std::string camera_id;
    bool headless = false;
    bool preview = true;
    bool auto_align = false;
    bool auto_place = false;
    int max_frames = -1;
};

static void printUsage(const char* prog)
{
    std::cerr
        << "Usage: " << prog << " --video <path> [options]\n"
        << "Options:\n"
        << "  --config <yaml>       Config file (default: config/config.yaml)\n"
        << "  --boundary <file>     4-point boundary (x y per line, TL TR BR BL)\n"
        << "  --camera-id <id>      Camera id for ref bank (default from config)\n"
        << "  --output <dir>        Output directory for export (default: output)\n"
        << "  --headless            No preview window; write export files only\n"
        << "  --preview             Show live preview window (default unless --headless)\n"
        << "  --auto-align          Start alignment from --boundary on first frame\n"
        << "  --auto-place          Place graphic at boundary center after align\n"
        << "  --max-frames <n>      Stop after N frames (-1 = all)\n"
        << "\nPreview keys:\n"
        << "  a = start manual align   Enter = apply align   i/j/k/l = nudge grid\n"
        << "  u/o = depth   ,/. = rotate   p = arm place   right-click = position   r = reinit   q = quit\n";
}

static bool parseArgs(int argc, char** argv, CliOptions& opts)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                return {};
            }
            return argv[++i];
        };
        if (arg == "--video") {
            opts.video_path = need("--video");
        } else if (arg == "--config") {
            opts.config_path = need("--config");
        } else if (arg == "--boundary") {
            opts.boundary_path = need("--boundary");
        } else if (arg == "--camera-id") {
            opts.camera_id = need("--camera-id");
        } else if (arg == "--output") {
            opts.output_dir = need("--output");
        } else if (arg == "--headless") {
            opts.headless = true;
            opts.preview = false;
        } else if (arg == "--preview") {
            opts.preview = true;
            opts.headless = false;
        } else if (arg == "--auto-align") {
            opts.auto_align = true;
        } else if (arg == "--auto-place") {
            opts.auto_place = true;
        } else if (arg == "--max-frames") {
            opts.max_frames = std::stoi(need("--max-frames"));
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return false;
        }
    }
    if (opts.video_path.empty()) {
        printUsage(argv[0]);
        return false;
    }
    if (opts.auto_align && opts.boundary_path.empty()) {
        std::cerr << "--auto-align requires --boundary\n";
        return false;
    }
    return true;
}

static void applySmartDefaults(CliOptions& opts)
{
    if (!opts.boundary_path.empty() && !opts.auto_align) {
        opts.auto_align = true;
        std::cout << "Note: --boundary provided → enabling --auto-align\n";
    }
}

static void updateGraphicPreview(const std::string& path,
                                 const std::array<cv::Point2f, 4>* boundary,
                                 cv::Mat& graphic_preview,
                                 float& graphic_place_w,
                                 float& graphic_place_h)
{
    graphic_preview.release();
    if (path.empty()) {
        return;
    }
    graphic_preview = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (graphic_preview.empty()) {
        std::cerr << "Warning: could not load graphic " << path << "\n";
        return;
    }
    if (boundary) {
        graphic_place_w = std::max(50.f, static_cast<float>(cv::norm((*boundary)[1] - (*boundary)[0])) * 0.85f);
        const float aspect = static_cast<float>(graphic_preview.rows) /
                             std::max(1, graphic_preview.cols);
        graphic_place_h = std::max(30.f, graphic_place_w * aspect);
    }
}

static const char* statusLabel(SponsorGridState::TrackingStatus s)
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

static void compositeWarpedGraphic(cv::Mat& frame, const cv::Mat& graphic,
                                  const std::array<cv::Point2f, 4>& quad)
{
    if (graphic.empty()) {
        return;
    }
    cv::Mat bgr = graphic;
    cv::Mat alpha;
    if (graphic.channels() == 4) {
        std::vector<cv::Mat> ch;
        cv::split(graphic, ch);
        cv::merge(std::vector<cv::Mat>{ch[0], ch[1], ch[2]}, bgr);
        alpha = ch[3];
    }
    const std::vector<cv::Point2f> src = {
        {0.f, 0.f},
        {static_cast<float>(bgr.cols), 0.f},
        {static_cast<float>(bgr.cols), static_cast<float>(bgr.rows)},
        {0.f, static_cast<float>(bgr.rows)},
    };
    const std::vector<cv::Point2f> dst(quad.begin(), quad.end());
    const cv::Mat H = cv::getPerspectiveTransform(src, dst);
    cv::Mat warped;
    // BORDER_CONSTANT (not BORDER_TRANSPARENT): fully initialize the output so
    // pixels outside the warped quad are zero. BORDER_TRANSPARENT leaves them
    // uninitialized, and the mask below would then key in random memory.
    cv::warpPerspective(bgr, warped, H, frame.size(), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    cv::Mat mask;
    if (!alpha.empty()) {
        // Derive coverage from the warped alpha channel, clamped to the quad.
        cv::warpPerspective(alpha, mask, H, frame.size(), cv::INTER_NEAREST,
                            cv::BORDER_CONSTANT, cv::Scalar(0));
        cv::threshold(mask, mask, 0, 255, cv::THRESH_BINARY);
    } else {
        cv::cvtColor(warped, mask, cv::COLOR_BGR2GRAY);
        cv::threshold(mask, mask, 1, 255, cv::THRESH_BINARY);
    }
    warped.copyTo(frame, mask);
}

static cv::Point2f quadCenter(const std::array<cv::Point2f, 4>& quad)
{
    cv::Point2f center{0.f, 0.f};
    for (const auto& p : quad) {
        center += p;
    }
    return center * 0.25f;
}

static std::array<cv::Point2f, 4> scaleQuadAboutCenter(
    const std::array<cv::Point2f, 4>& quad, float scale)
{
    const cv::Point2f center = quadCenter(quad);
    std::array<cv::Point2f, 4> out{};
    for (size_t i = 0; i < 4; ++i) {
        out[i] = center + (quad[static_cast<size_t>(i)] - center) * scale;
    }
    return out;
}

static void drawQuadOutline(cv::Mat& out, const std::array<cv::Point2f, 4>& quad,
                            cv::Scalar color, int thickness)
{
    std::vector<cv::Point> poly(4);
    for (int i = 0; i < 4; ++i) {
        poly[i] = cv::Point(static_cast<int>(std::round(quad[i].x)),
                            static_cast<int>(std::round(quad[i].y)));
    }
    cv::polylines(out, poly, true, color, thickness, cv::LINE_AA);
}

static cv::Mat drawOverlay(const cv::Mat& frame,
                           const SponsorGridState::Results& results,
                           const std::array<cv::Point2f, 4>* graphic_quad,
                           const cv::Mat* graphic_image,
                           const std::array<cv::Point2f, 4>* align_quad,
                           const std::array<cv::Point2f, 4>* reference_boundary,
                           int frame_id,
                           bool paused)
{
    cv::Mat out = frame.clone();
    const bool aligning = results.status == SponsorGridState::TrackingStatus::ALIGNING;
    const bool tracking = results.status == SponsorGridState::TrackingStatus::TRACKING;

    if (reference_boundary && aligning) {
        drawQuadOutline(out, *reference_boundary, cv::Scalar(0, 220, 255), 1);
    }
    if (align_quad && aligning) {
        drawQuadOutline(out, *align_quad, cv::Scalar(255, 120, 0), 2);
    }

    if (tracking && graphic_quad && graphic_image && !graphic_image->empty()
        && results.has_placed_graphic && !results.sponsor_graphics_hidden) {
        compositeWarpedGraphic(out, *graphic_image, *graphic_quad);
        drawQuadOutline(out, *graphic_quad, cv::Scalar(0, 255, 0), 2);
    }

    if (aligning && !results.grid_points.empty()) {
        for (size_t i = 0; i < results.grid_points.size(); i += 4) {
            const auto& p = results.grid_points[i];
            cv::circle(out, p, 2, cv::Scalar(100, 180, 255), -1, cv::LINE_AA);
        }
    }

    std::ostringstream hud;
    hud << "frame " << frame_id
        << "  |  " << statusLabel(results.status)
        << "  conf=" << std::fixed << std::setprecision(2) << results.tracking_confidence
        << "  pts=" << results.active_point_count << "/" << results.total_point_count;
    if (paused) {
        hud << "  [PAUSED]";
    }
    cv::putText(out, hud.str(), {12, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
    cv::putText(out, hud.str(), {12, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

    const char* help = aligning
        ? "ALIGN: i/j/k/l=move  u/o=depth  ,/.=rotate  Enter=apply  q=quit"
        : "Space=pause  a=align  Enter=apply  p=arm place  right-click=position  r=reinit  q=quit";
    cv::putText(out, help, {12, out.rows - 12}, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
    cv::putText(out, help, {12, out.rows - 12}, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
    return out;
}

static bool initGlfwWindow(GLFWwindow*& window, int width, int height, bool visible)
{
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (!visible) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    const int win_w = visible ? std::max(1280, std::min(width, 1600)) : std::max(64, width);
    const int win_h = visible ? std::max(720, std::min(height, 900)) : std::max(64, height);
    window = glfwCreateWindow(win_w, win_h, "Sponsor Tracker", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return false;
    }
    OpenGLContextManager::getInstance().initializeMainContext(window);
    return true;
}

static void writeCsvHeader(std::ofstream& csv)
{
    csv << "frame_id,vid_tl_x,vid_tl_y,vid_tr_x,vid_tr_y,"
        << "vid_br_x,vid_br_y,vid_bl_x,vid_bl_y\n";
}

static void writeCsvRow(std::ofstream& csv, int frame_id, const std::array<cv::Point2f, 4>& quad)
{
    csv << frame_id << ','
        << quad[0].x << ',' << quad[0].y << ','
        << quad[1].x << ',' << quad[1].y << ','
        << quad[2].x << ',' << quad[2].y << ','
        << quad[3].x << ',' << quad[3].y << '\n';
}

int main(int argc, char** argv)
{
    CliOptions opts;
    if (!parseArgs(argc, argv, opts)) {
        return 1;
    }
    applySmartDefaults(opts);

    fs::create_directories("logs");
    fs::create_directories(opts.output_dir);
    fs::create_directories("assets/sponsor_refs");
    initLogger("logs/sponsor_tracker.log");
    setGlobalLogLevel(spdlog::level::info);

    AppConfig app_cfg = loadAppConfig(opts.config_path);
    if (!opts.camera_id.empty()) {
        app_cfg.camera_id = opts.camera_id;
    }
    if (opts.auto_align) {
        app_cfg.auto_align = true;
    }
    if (opts.auto_place) {
        app_cfg.auto_place_graphic = true;
    }

    app_cfg.sponsor.active_camera_id = app_cfg.camera_id;

    std::array<cv::Point2f, 4> boundary{};
    const bool have_boundary = !opts.boundary_path.empty() &&
                               loadBoundaryFile(opts.boundary_path, boundary);

    cv::Mat graphic_preview;
    float graphic_place_w = 300.f;
    float graphic_place_h = 200.f;

    cv::VideoCapture cap(opts.video_path);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open video: " << opts.video_path << "\n";
        return 1;
    }

    const int frame_w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int frame_h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double fps = cap.get(cv::CAP_PROP_FPS);
    const double out_fps = (fps > 1.0) ? fps : 25.0;

    const bool show_preview = opts.preview && !opts.headless;

    GLFWwindow* window = nullptr;
    if (!initGlfwWindow(window, frame_w, frame_h, show_preview)) {
        std::cerr << "Failed to create OpenGL context."
                  << (opts.headless ? " Try: xvfb-run -a ./scripts/run.sh ..." : "")
                  << "\n";
        return 1;
    }

    PreviewUi preview_ui;
    if (show_preview && !preview_ui.init(window)) {
        std::cerr << "Failed to initialize ImGui preview\n";
        return 1;
    }
    if (show_preview) {
        std::cout << "ImGui preview open. Pick a sponsor image in the side panel (data/ folder).\n";
        preview_ui.syncGraphicSettingsFromConfig(app_cfg.sponsor);
    }

    SharedState shared_state;
    shared_state.setData(app_cfg.sponsor);

    SponsorGridTracker tracker;

    const auto stamp = []() {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return oss.str();
    };

    const std::string export_base = opts.output_dir + "/sponsor_export_" + stamp();
    cv::VideoWriter writer;
    std::ofstream csv(export_base + ".csv");
    writeCsvHeader(csv);

    enum class AlignPhase { Pending, Aligning, Applied };
    AlignPhase align_phase = app_cfg.auto_align ? AlignPhase::Pending : AlignPhase::Applied;
    bool auto_apply_once = app_cfg.auto_align;
    bool placed_graphic = false;
    bool pending_apply = false;
    bool pending_place = false;
    cv::Point2f place_point{frame_w * 0.5f, frame_h * 0.5f};
    std::array<cv::Point2f, 4> preview_graphic_quad_{};
    bool have_preview_graphic_quad_ = false;
    float preview_graphic_scale_ = 1.f;
    cv::Point2f last_tracker_quad_center_{0.f, 0.f};

    if (have_boundary && app_cfg.auto_align) {
        app_cfg.sponsor.transform = transformFromBoundary(boundary, frame_w, frame_h);
        app_cfg.sponsor.transform_changed = true;
        place_point = cv::Point2f(
            0.25f * (boundary[0].x + boundary[1].x + boundary[2].x + boundary[3].x),
            0.25f * (boundary[0].y + boundary[1].y + boundary[2].y + boundary[3].y));
    }

    int frame_id = 0;
    bool paused = false;
    const int frame_delay_ms = std::max(1, static_cast<int>(1000.0 / out_fps));
    cv::Mat bgr;
    while (true) {
        if (!paused) {
            if (!cap.read(bgr) || bgr.empty()) {
                break;
            }
            if (opts.max_frames >= 0 && frame_id >= opts.max_frames) {
                break;
            }
        } else if (bgr.empty()) {
            break;
        }

        auto frame = makeFrameFromBgr(bgr, frame_id);
        if (!frame) {
            std::cerr << "Failed to upload frame " << frame_id << " to GPU\n";
            break;
        }

        auto cfg_opt = shared_state.getData<SponsorGridState::Config>();
        SponsorGridState::Config cfg = cfg_opt.value_or(app_cfg.sponsor);

        if (align_phase == AlignPhase::Pending) {
            cfg.start_alignment = true;
            align_phase = AlignPhase::Aligning;
        } else if (align_phase == AlignPhase::Aligning && pending_apply) {
            cfg.apply_alignment = true;
            align_phase = AlignPhase::Applied;
            pending_apply = false;
        } else if (align_phase == AlignPhase::Aligning && auto_apply_once) {
            cfg.apply_alignment = true;
            align_phase = AlignPhase::Applied;
            auto_apply_once = false;
        }

        if (pending_place || (app_cfg.auto_place_graphic && align_phase == AlignPhase::Applied && !placed_graphic)) {
            cfg.graphic_transform_changed = true;
            cfg.place_graphic = true;
            cfg.placement_point = place_point;
            cfg.placement_request_id++;
            cfg.graphic_width = graphic_place_w * cfg.graphic_scale;
            cfg.graphic_height = graphic_place_h * cfg.graphic_scale;
            placed_graphic = true;
            pending_place = false;
        }

        shared_state.setData(cfg);
        tracker.process(frame, &shared_state);

        auto results_opt = shared_state.getData<SponsorGridState::Results>();
        SponsorGridState::Results results = results_opt.value_or(SponsorGridState::Results{});

        std::array<cv::Point2f, 4> quad{};
        bool have_quad = false;
        auto sponsor_data = IModule::getModuleData<SponsorGraphicData>(frame);
        if (sponsor_data && sponsor_data->is_valid) {
            quad = sponsor_data->quad;
            have_quad = true;
            writeCsvRow(csv, frame_id, quad);
        } else if (results.graphic_quad[0].x >= 0.f) {
            quad = results.graphic_quad;
            have_quad = true;
            writeCsvRow(csv, frame_id, quad);
        }

        std::array<cv::Point2f, 4> draw_quad = quad;
        if (have_quad) {
            const cv::Point2f tracker_center = quadCenter(quad);
            if (!have_preview_graphic_quad_) {
                preview_graphic_quad_ = quad;
                preview_graphic_scale_ = cfg.graphic_scale;
                last_tracker_quad_center_ = tracker_center;
                have_preview_graphic_quad_ = true;
            } else if (paused) {
                draw_quad = preview_graphic_quad_;
            } else {
                const cv::Point2f delta = tracker_center - last_tracker_quad_center_;
                for (auto& p : preview_graphic_quad_) {
                    p += delta;
                }
                last_tracker_quad_center_ = tracker_center;
                draw_quad = preview_graphic_quad_;
            }
        }

        const std::array<cv::Point2f, 4> align_quad =
            alignmentQuadFromTransform(cfg.transform, frame_w, frame_h);
        const cv::Mat* gfx_for_draw =
            (results.status == SponsorGridState::TrackingStatus::TRACKING && have_quad
             && !graphic_preview.empty())
                ? &graphic_preview
                : nullptr;
        const std::array<cv::Point2f, 4>* ref_boundary_ptr =
            (have_boundary && results.status == SponsorGridState::TrackingStatus::ALIGNING)
                ? &boundary
                : nullptr;
        cv::Mat display = drawOverlay(
            bgr, results,
            (gfx_for_draw && have_quad) ? &draw_quad : nullptr,
            gfx_for_draw,
            (results.status == SponsorGridState::TrackingStatus::ALIGNING) ? &align_quad : nullptr,
            ref_boundary_ptr,
            frame_id, paused);

        if (!writer.isOpened()) {
            writer.open(export_base + ".mp4",
                        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                        out_fps,
                        cv::Size(frame_w, frame_h));
        }
        if (writer.isOpened()) {
            writer.write(display);
        }

        if (show_preview) {
            preview_ui.setAligningUi(align_phase == AlignPhase::Aligning
                                     || results.status == SponsorGridState::TrackingStatus::ALIGNING);
            PreviewUiActions ui = preview_ui.render(display, results, cfg, frame_id, paused);
            bool redo_preview = false;

            if (ui.quit) {
                break;
            }
            if (ui.toggle_pause) {
                const bool was_paused = paused;
                paused = !paused;
                if (was_paused && !paused) {
                    cfg = shared_state.getData<SponsorGridState::Config>().value_or(cfg);
                    cfg.graphic_transform_changed = true;
                    shared_state.setData(cfg);
                }
            }
            if (ui.start_align) {
                cfg = shared_state.getData<SponsorGridState::Config>().value_or(cfg);
                cfg.start_alignment = true;
                if (have_boundary) {
                    cfg.transform = transformFromBoundary(boundary, frame_w, frame_h);
                }
                cfg.transform_changed = true;
                preview_ui.syncTransformFromConfig(cfg.transform);
                shared_state.setData(cfg);
                align_phase = AlignPhase::Aligning;
                auto_apply_once = false;
                placed_graphic = false;
                pending_place = false;
                have_preview_graphic_quad_ = false;
            }
            if (ui.apply_align) {
                pending_apply = true;
            }
            if (ui.place_graphic_at) {
                place_point = ui.placement_point;
                pending_place = true;
                have_preview_graphic_quad_ = false;
            }
            if (ui.graphic_changed) {
                shared_state.setData(cfg);
                updateGraphicPreview(cfg.graphic_path,
                                     have_boundary ? &boundary : nullptr,
                                     graphic_preview,
                                     graphic_place_w,
                                     graphic_place_h);
                placed_graphic = false;
                pending_place = false;
                have_preview_graphic_quad_ = false;
            }
            if (ui.graphic_transform_changed) {
                shared_state.setData(cfg);
                if (have_preview_graphic_quad_) {
                    const float ratio =
                        cfg.graphic_scale / std::max(preview_graphic_scale_, 0.01f);
                    if (std::abs(ratio - 1.f) > 1e-4f) {
                        preview_graphic_quad_ =
                            scaleQuadAboutCenter(preview_graphic_quad_, ratio);
                        preview_graphic_scale_ = cfg.graphic_scale;
                        redo_preview = true;
                    }
                }
            }
            if (ui.request_reinit) {
                cfg = shared_state.getData<SponsorGridState::Config>().value_or(cfg);
                cfg.request_reinit = true;
                shared_state.setData(cfg);
            }
            if (ui.transform_changed) {
                shared_state.setData(cfg);
            }

            if (redo_preview && have_preview_graphic_quad_) {
                display = drawOverlay(
                    bgr, results,
                    (gfx_for_draw && have_quad) ? &preview_graphic_quad_ : nullptr,
                    gfx_for_draw,
                    (results.status == SponsorGridState::TrackingStatus::ALIGNING) ? &align_quad
                                                                                     : nullptr,
                    ref_boundary_ptr,
                    frame_id, paused);
                preview_ui.render(display, results, cfg, frame_id, paused);
            }

            bool request_quit = false;
            const int wait_ms = paused ? 50 : frame_delay_ms;
            const double deadline = glfwGetTime() + wait_ms / 1000.0;
            while (glfwGetTime() < deadline && !glfwWindowShouldClose(window)) {
                glfwPollEvents();
                if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS
                    || glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                    request_quit = true;
                    break;
                }
                if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
                    paused = !paused;
                    break;
                }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                    cfg = shared_state.getData<SponsorGridState::Config>().value_or(cfg);
                    cfg.start_alignment = true;
                    if (have_boundary) {
                        cfg.transform = transformFromBoundary(boundary, frame_w, frame_h);
                    }
                    cfg.transform_changed = true;
                    preview_ui.syncTransformFromConfig(cfg.transform);
                    preview_ui.setAligningUi(true);
                    shared_state.setData(cfg);
                    align_phase = AlignPhase::Aligning;
                    auto_apply_once = false;
                    placed_graphic = false;
                    pending_place = false;
                    have_preview_graphic_quad_ = false;
                    break;
                }
                if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS
                    || glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS) {
                    pending_apply = true;
                    break;
                }
                if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
                    preview_ui.armGraphicPlacement();
                    break;
                }
                if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
                    cfg = shared_state.getData<SponsorGridState::Config>().value_or(cfg);
                    cfg.request_reinit = true;
                    shared_state.setData(cfg);
                    break;
                }
                if (align_phase == AlignPhase::Aligning) {
                    SponsorGridState::Transform t = cfg.transform;
                    bool nudged = false;
                    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) { t.offset_y -= 8.f; nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) { t.offset_y += 8.f; nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) { t.offset_x -= 8.f; nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) { t.offset_x += 8.f; nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) { t.depth_z = std::max(0.1f, t.depth_z - 0.05f); nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) { t.depth_z = std::min(2.5f, t.depth_z + 0.05f); nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_COMMA) == GLFW_PRESS) { t.rotation_deg -= 1.f; nudged = true; }
                    if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS) { t.rotation_deg += 1.f; nudged = true; }
                    if (nudged) {
                        cfg.transform = t;
                        cfg.transform_changed = true;
                        preview_ui.syncTransformFromConfig(t);
                        shared_state.setData(cfg);
                        break;
                    }
                }
            }
            if (request_quit || glfwWindowShouldClose(window)) {
                break;
            }
        }

        if (!paused) {
            ++frame_id;
        }
        if (!show_preview) {
            glfwPollEvents();
        }
    }

    if (show_preview) {
        preview_ui.shutdown();
    }

    SponsorGridTracker::requestSponsorGridLoFTRShutdown();
    csv.close();
    if (writer.isOpened()) {
        writer.release();
    }
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();

    std::cout << "Processed " << frame_id << " frames\n";
    std::cout << "Export: " << export_base << ".mp4\n";
    std::cout << "CSV:    " << export_base << ".csv\n";
    return 0;
}
