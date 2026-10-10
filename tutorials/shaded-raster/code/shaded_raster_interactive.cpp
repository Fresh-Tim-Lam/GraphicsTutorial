// shaded_raster_interactive.cpp —— 光栅化着色管线「摄像头交互」演示：窗口内实时看带法线贴图 + 阴影的物体
//
// 用途：与离线版（shaded_raster.cpp）**共用** shaded_raster_scene.hpp 的场景与着色代码，
//       一边拖鼠标/按键调相机，一边实时看低分辨率预览；按空格用高分辨率渲一张存成 PNG。
//
// 为什么能实时：光栅化一条三角形的代价与"屏幕上有多少像素被覆盖"成正比（不是每像素求交），
//       而且**阴影贴图只依赖光源与场景、与相机无关** —— 所以贴图在启动时建一次就够，
//       相机怎么动都不必重建。于是每帧只剩主 pass 的光栅化 + 逐像素着色。
//       本场景四面是墙 ⇒ 屏幕几乎全被覆盖，逐像素代价直接正比于像素数，故再加一招
//       "拖动/按键时降到半分辨率、松手立刻重渲全分辨率"（见 kInteractDiv）。
//       实测 640×480：**拖动中 ≈ 30 fps**（半分辨率）、静止时全分辨率 ≈ 7.7 fps
//       （静止只在改动后渲一帧，所以"截一张清晰的图"永远是全分辨率的）。
//
// 用法：build/tutorials/ShadedRasterInteractive [--size WxH] [--frames N] [--shadow-size n]
//                                               [--strength s] [--shot] [--help]
//   --size WxH       预览窗口尺寸（默认 640x480）
//   --frames N       渲染 N 帧后自动退出（0 = 直到关窗；冒烟测试用）
//   --shadow-size n  阴影贴图分辨率（默认 1024；只建一次，与帧率无关）
//   --strength s     凹凸强度（默认 1.0；0 = 贴图退化成平整法向）
//   --shot           用初始相机渲一张 800x600 存 PNG 后立刻退出（不建窗口；脚本出图/冒烟测试）
//
// 操作（窗口获得焦点时）：
//   左键拖动   转视角（yaw / pitch）      右键拖动   平移相机（沿视线的右 / 上方向）
//   W / S      上升 / 下降（世界竖直）     A / D      左右平移
//   滚轮       前后推拉                    R          相机复位
//   1          切换法线贴图              2          切换阴影          3          切换颜色贴图
//   空格       高分辨率渲染一张并存 PNG（路径打印在控制台）   Esc  退出
//
// 关联文档：tutorials/shaded-raster/README.md；src/include/gfx/render/window.hpp

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/shadowmap.hpp>
#include <gfx/render/view.hpp>
#include <gfx/render/window.hpp>

#include "shaded_raster_scene.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX      // 避免 windows.h 的 min/max 宏破坏 std::min / std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台按 UTF-8 输出，避免中文乱码）
#endif

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::View;
using gfx::math::Vec3;

// 交互速度（每帧 / 每像素）—— 与光线跟踪交互版保持同一套手感
constexpr float kLookSpeed  = 0.004f;   // rad / px
constexpr float kPanSpeed   = 0.006f;   // 世界单位 / px
constexpr float kMoveSpeed  = 0.05f;    // 世界单位 / 帧（按住 WASD）
constexpr float kDollySpeed = 0.06f;    // 世界单位 / 滚轮格（前后推拉）
const Vec3 kWorldUp(0.0f, 1.0f, 0.0f);  // W / S 沿世界竖直方向升降

// 出图规格：固定 800x600 —— 与离线 demo 的成品图同规格
constexpr int kShotWidth = 800;
constexpr int kShotHeight = 600;
constexpr const char* kShotPath = "shaded-raster-interactive.png";

// 程序化贴图分辨率（每套外观两张图：颜色 + 法线）—— 与离线版一致
constexpr int kMapSize = 320;

// 交互中把预览降到 1/kInteractDiv 分辨率（最近邻放大回窗口），松手立刻重渲全分辨率。
// 房间里全是三面墙 ⇒ 屏幕几乎全被三角形覆盖，逐像素着色的代价直接正比于像素数，
// 于是"拖动时降分辨率"是最划算的一招：视觉上只是拖动瞬间略糊，停下就是清晰的。
constexpr int kInteractDiv = 2;

const Color kBackground(24, 28, 38);

void printUsage() {
    std::cout <<
        "用法：\n"
        "  ShadedRasterInteractive [--size WxH] [--frames N] [--shadow-size n] [--strength s]\n"
        "                          [--shot] [--help]\n"
        "\n"
        "  --size WxH       预览窗口尺寸（默认 640x480）\n"
        "  --frames N       渲染 N 帧后自动退出（0 = 直到关窗）\n"
        "  --shadow-size n  阴影贴图分辨率（默认 1024；只建一次，与帧率无关）\n"
        "  --strength s     凹凸强度（默认 1.0；0 = 贴图退化为平整法向）\n"
        "  --shot           用初始相机渲一张 800x600 存 PNG 后立刻退出（不建窗口）\n"
        "  --force-interact 强制按\"拖动中\"的低分辨率预览跑（冒烟测试用）\n"
        "  --help           显示本帮助\n";
}

void printControls() {
    std::cout << "============ 光栅化着色管线 · 摄像头交互 ============\n"
                 "  左键拖动：转视角        右键拖动：平移（视线的右 / 上）\n"
                 "  W / S：上升 / 下降      A / D：左右平移\n"
                 "  滚轮：前后推拉          R：相机复位\n"
                 "  1：切换法线贴图        2：切换阴影        3：切换颜色贴图\n"
                 "  空格：高分辨率渲染并存 PNG（打印路径）   Esc：退出\n\n";
}

// 用当前相机 + 当前开关状态渲一张高分辨率图并存 PNG
void renderShot(const sr_demo::CameraRig& cam, const sr_demo::Scene& scene,
                const std::vector<gfx::ShadowMap>& shadowMaps, const sr_demo::RenderOptions& render) {
    const View view = cam.makeView(kShotWidth, kShotHeight);
    Canvas canvas(kShotWidth, kShotHeight);
    canvas.clear(kBackground);

    const auto start = std::chrono::high_resolution_clock::now();
    const long long fragments = sr_demo::renderScene(canvas, view, cam.position, scene, shadowMaps, render);
    const double seconds = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - start).count();

    std::printf("（出图：%dx%d、%lld 个着色片元、%.0f ms）\n", kShotWidth, kShotHeight, fragments,
                seconds * 1000.0);
    std::string error;
    if (gfx::savePng(canvas, kShotPath, &error)) std::printf("（已保存：%s）\n", kShotPath);
    else std::fprintf(stderr, "错误：保存 PNG 失败 —— %s\n", error.c_str());
}

// 最近邻放大：把小图铺满大画布（**只在交互中**用来撑住帧率，停下立刻重渲全分辨率）
void blitScaled(const Canvas& src, Canvas& dst) {
    if (src.width() <= 0 || src.height() <= 0) return;
    for (int y = 0; y < dst.height(); ++y) {
        const int sy = y * src.height() / dst.height();
        for (int x = 0; x < dst.width(); ++x) {
            dst.putPixel(x, y, src.pixelAt(x * src.width() / dst.width(), sy));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // 控制台按 UTF-8 解释输出 ⇒ 中文不乱码
#endif

    int width = 640;
    int height = 480;
    int frames = 0;          // 0 = 直到关窗
    int shadowSize = 1024;
    float strength = 1.0f;
    bool shotOnly = false;
    bool forceInteract = false;   // 强制走"交互中"的低分辨率路径（冒烟测试用）
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        if (arg == "--shot") { shotOnly = true; continue; }
        if (arg == "--force-interact") { forceInteract = true; continue; }
        if (arg == "--frames" && i + 1 < argc) { frames = std::atoi(argv[++i]); continue; }
        if (arg == "--shadow-size" && i + 1 < argc) { shadowSize = std::atoi(argv[++i]); continue; }
        if (arg == "--strength" && i + 1 < argc) { strength = static_cast<float>(std::atof(argv[++i])); continue; }
        if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 640x480\n";
                return 2;
            }
            width = std::atoi(size.substr(0, sep).c_str());
            height = std::atoi(size.substr(sep + 1).c_str());
            if (width < 64 || height < 64) {
                std::cerr << "错误：预览窗口过小（至少 64x64）\n";
                return 2;
            }
            continue;
        }
        std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
        return 2;
    }
    if (shadowSize < 16) shadowSize = 16;
    if (strength < 0.0f) strength = 0.0f;

    std::cout << std::unitbuf;   // 控制台应用：每次输出立即刷新

    // ---- 场景 + 相机 + 阴影贴图（贴图只建一次：它只依赖光源与场景，与相机无关）----
    const sr_demo::Scene scene = sr_demo::makeScene(kMapSize, strength);
    sr_demo::CameraRig cam;
    sr_demo::RenderOptions render;   // normalMaps / shadow 都默认开

    sr_demo::ShadowConfig shadowConfig;
    shadowConfig.size = shadowSize;
    const auto buildStart = std::chrono::high_resolution_clock::now();
    const std::vector<gfx::ShadowMap> shadowMaps = sr_demo::buildShadowMaps(scene, shadowConfig);
    const double buildSeconds = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - buildStart).count();

    std::cout << "场景：瓷砖地面 + 4 面彩色砖墙 + 碳纤维立方体 + 大理石球 = " << scene.triangles.size()
              << " 个三角形（贴图 " << kMapSize << "² 程序化生成）\n"
              << "阴影贴图：" << shadowMaps.size() << " 张 " << shadowSize << "²（启动时建一次，用时 "
              << buildSeconds * 1000.0 << " ms；相机移动不需要重建）\n";

    if (shotOnly) {   // --shot：不建窗口，直接出一张图
        renderShot(cam, scene, shadowMaps, render);
        return 0;
    }

    printControls();

    gfx::Window window("光栅化着色管线 · 摄像头交互"
                       "（左键转视角 / W·S 升降 / A·D 左右 / 滚轮前后 / 1 法线 / 2 阴影 / 3 颜色 / 空格出图）",
                       width, height);
    if (!window.isOpen()) {
        std::cerr << "错误：窗口创建失败。\n";
        return 5;
    }

    bool dirty = true;               // 首帧与每次改动后重渲
    const bool autoRun = frames > 0; // --frames N：每轮都重渲，跑 N 帧后自动退出（冒烟测试）
    int frame = 0;
    double frameMsSum = 0.0;
    int frameMsCount = 0;
    bool interacting = false;        // 本帧是否处于"交互中"（决定预览分辨率）
    Canvas lowRes(std::max(64, width / kInteractDiv), std::max(64, height / kInteractDiv));

    while (window.pollEvents()) {
        const gfx::MouseState& ms = window.mouse();

        const bool inputActive = forceInteract || ms.leftDown || ms.rightDown || ms.wheel != 0 ||
                                 window.keyDown('W') || window.keyDown('S') ||
                                 window.keyDown('A') || window.keyDown('D');
        if (inputActive != interacting) {
            interacting = inputActive;
            dirty = true;   // 交互状态变了 ⇒ 按新分辨率重渲一张
        }

        // 左键拖动 ⇒ 转视角
        if (ms.leftDown && (ms.dx != 0 || ms.dy != 0)) {
            cam.yaw += static_cast<float>(ms.dx) * kLookSpeed;
            cam.pitch -= static_cast<float>(ms.dy) * kLookSpeed;
            const float lim = 1.48f;   // 夹住俯仰，避免视线与 up 退化
            if (cam.pitch > lim) cam.pitch = lim;
            if (cam.pitch < -lim) cam.pitch = -lim;
            dirty = true;
        }
        // 右键拖动 ⇒ 沿视线的右 / 上方向平移
        if (ms.rightDown && (ms.dx != 0 || ms.dy != 0)) {
            cam.position = cam.position
                         - cam.right() * (static_cast<float>(ms.dx) * kPanSpeed)
                         + cam.up() * (static_cast<float>(ms.dy) * kPanSpeed);
            dirty = true;
        }
        // 滚轮 ⇒ 前后推拉
        if (ms.wheel != 0) {
            cam.position = cam.position + cam.forward() * (static_cast<float>(ms.wheel) * kDollySpeed);
            dirty = true;
        }
        // WASD ⇒ 世界竖直 / 水平移动（按住即持续）；前后用滚轮推拉
        if (window.keyDown('W')) { cam.position = cam.position + kWorldUp * kMoveSpeed; dirty = true; }
        if (window.keyDown('S')) { cam.position = cam.position - kWorldUp * kMoveSpeed; dirty = true; }
        if (window.keyDown('A')) { cam.position = cam.position - cam.right() * kMoveSpeed; dirty = true; }
        if (window.keyDown('D')) { cam.position = cam.position + cam.right() * kMoveSpeed; dirty = true; }

        if (window.keyPressed('R')) {
            cam = sr_demo::CameraRig{};
            dirty = true;
            std::printf("（相机已复位：pos=(%.2f, %.2f, %.2f) yaw=%.1f° pitch=%.1f°）\n",
                        cam.position.x, cam.position.y, cam.position.z,
                        cam.yaw * 180.0f / sr_demo::kPi, cam.pitch * 180.0f / sr_demo::kPi);
        }

        // 1 / 2 / 3 ⇒ 实时开关法线贴图 / 阴影 / 颜色贴图（对照观察它们各自的作用）
        if (window.keyPressed('1')) {
            render.normalMaps = !render.normalMaps;
            dirty = true;
            std::printf("（法线贴图：%s）\n", render.normalMaps ? "开" : "关");
        }
        if (window.keyPressed('2')) {
            render.shadow = !render.shadow;
            dirty = true;
            std::printf("（阴影：%s）\n", render.shadow ? "开" : "关");
        }
        if (window.keyPressed('3')) {
            render.albedo = !render.albedo;
            dirty = true;
            std::printf("（颜色贴图：%s）\n", render.albedo ? "开" : "关");
        }

        if (window.keyPressed(gfx::Key::Space)) {
            std::printf("（开始高分辨率渲染：%dx%d ...）\n", kShotWidth, kShotHeight);
            renderShot(cam, scene, shadowMaps, render);
            dirty = true;   // 出图不改预览，但下一帧照常刷新
        }

        if (window.keyPressed(gfx::Key::Escape)) {
            window.requestClose();
            break;
        }

        if (dirty || autoRun) {
            // 交互中降分辨率，其余时刻全分辨率（松手那一帧 interacting 变 false ⇒ 立即补一张清晰的）
            const int rw = interacting ? lowRes.width() : width;
            const int rh = interacting ? lowRes.height() : height;
            const View view = cam.makeView(rw, rh);
            const auto frameStart = std::chrono::high_resolution_clock::now();

            Canvas& target = interacting ? lowRes : window.canvas();
            target.clear(kBackground);
            sr_demo::renderScene(target, view, cam.position, scene, shadowMaps, render);
            if (interacting) blitScaled(lowRes, window.canvas());
            window.present();

            const double frameMs = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - frameStart).count() * 1000.0;
            frameMsSum += frameMs;
            ++frameMsCount;
            if (frameMsSum >= 1000.0) {   // 约每秒报一次平均帧耗时，避免刷屏
                std::printf("（预览 %dx%d%s：平均 %.1f ms/帧 ≈ %.1f fps）\n", rw, rh,
                            interacting ? "（拖动中半分辨率）" : "", frameMsSum / frameMsCount,
                            1000.0 * frameMsCount / frameMsSum);
                frameMsSum = 0.0;
                frameMsCount = 0;
            }

            dirty = false;
            if (autoRun && ++frame >= frames) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    std::cout << "\n退出。\n";
    return 0;
}
