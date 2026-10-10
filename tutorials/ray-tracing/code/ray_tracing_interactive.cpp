// ray_tracing_interactive.cpp —— 光线跟踪「摄像头交互」演示：窗口内实时调相机
//
// 用途：同一个场景（共享头 ray_tracing_scene.hpp），一边拖鼠标/按键调相机位置与朝向，
//       一边实时看低分辨率预览；按空格用高质量参数渲一张并存成 PNG。
//
// 用法：build/tutorials/RayTracingInteractive [--size WxH] [--frames N] [--help]
//   --size WxH   预览窗口尺寸（默认 480x360）
//   --frames N   渲染 N 帧后自动退出（0 = 直到关窗；冒烟测试用）
//
// 操作（窗口获得焦点时）：
//   左键拖动   转视角（yaw / pitch）      右键拖动   平移相机（沿视线的右 / 上方向）
//   W / S      上升 / 下降（世界竖直）     A / D      左右平移
//   滚轮       前后推拉                    R          相机复位
//   空格       高质量渲染一张并存 PNG（路径打印在控制台）   Esc  退出
//
// 说明：预览用 1 采样 + 较低递归深度换帧率；空格出图用 3×3 超采样 + 深度 6 + 800x600。
// 关联文档：tutorials/ray-tracing/README.md；src/include/gfx/render/window.hpp

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/view.hpp>
#include <gfx/render/window.hpp>

#include "ray_tracing_scene.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

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

using gfx::Ray;
using gfx::RayTracer;
using gfx::SurfaceHit;
using gfx::View;
using gfx::math::Vec3;

// 交互速度（每帧 / 每像素）
constexpr float kLookSpeed  = 0.004f;   // rad / px
constexpr float kPanSpeed   = 0.006f;   // 世界单位 / px
constexpr float kMoveSpeed  = 0.05f;    // 世界单位 / 帧（按住 WASD）
constexpr float kDollySpeed = 0.06f;    // 世界单位 / 滚轮格（前后推拉）
const Vec3 kWorldUp(0.0f, 1.0f, 0.0f);  // W / S 沿世界竖直方向升降

// 预览质量（换帧率）与出图质量
constexpr int kPreviewDepth = 4;
constexpr int kShotWidth = 800;
constexpr int kShotHeight = 600;
constexpr int kShotSamples = 3;
constexpr int kShotDepth = 6;

void printUsage() {
    std::cout <<
        "用法：\n"
        "  RayTracingInteractive [--size WxH] [--frames N] [--help]\n"
        "\n"
        "  --size WxH   预览窗口尺寸（默认 480x360）\n"
        "  --frames N   渲染 N 帧后自动退出（0 = 直到关窗）\n"
        "  --help       显示本帮助\n";
}

void printControls() {
    std::cout << "================ 光线跟踪 · 摄像头交互 ================\n"
                 "  左键拖动：转视角        右键拖动：平移（视线的右 / 上）\n"
                 "  W / S：上升 / 下降      A / D：左右平移\n"
                 "  滚轮：前后推拉          R：相机复位\n"
                 "  空格：高质量渲染并存 PNG（打印路径）   Esc：退出\n\n";
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // 控制台按 UTF-8 解释输出 ⇒ 中文不乱码
#endif

    int width = 480;
    int height = 360;
    int frames = 0;   // 0 = 直到关窗
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        if (arg == "--frames" && i + 1 < argc) { frames = std::atoi(argv[++i]); continue; }
        if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 480x360\n";
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

    printControls();
    std::cout << std::unitbuf;   // 控制台应用：每次输出立即刷新（重定向到文件时也能看到进度）

    // 场景与相机（与离线 demo 共用 ray_tracing_scene.hpp）
    rt_demo::Scene scene = rt_demo::makeScene();
    rt_demo::CameraRig cam;
    rt_demo::Stats stats;

    gfx::Window window("光线跟踪 · 摄像头交互（左键转视角 / W·S 升降 / A·D 左右 / 滚轮前后 / 空格出图）",
                       width, height);
    if (!window.isOpen()) {
        std::cerr << "错误：窗口创建失败。\n";
        return 5;
    }

    RayTracer::HitFunction hit = [&](const Ray& ray, SurfaceHit& out) {
        return rt_demo::hitSceneBvh(ray, out, scene, stats);
    };
    RayTracer tracer(hit, scene.lights);
    tracer.setBackground(scene.background);
    tracer.setSamplesPerAxis(1);   // 预览：1 采样

    bool dirty = true;              // 首帧与每次改动后重渲
    const bool autoRun = frames > 0; // --frames N：每轮都重渲，跑 N 帧后自动退出（冒烟测试）
    int frame = 0;

    while (window.pollEvents()) {
        const gfx::MouseState& ms = window.mouse();

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
            cam = rt_demo::CameraRig{};
            dirty = true;
            std::printf("（相机已复位：pos=(%.2f, %.2f, %.2f) yaw=%.1f° pitch=%.1f°）\n",
                        cam.position.x, cam.position.y, cam.position.z,
                        cam.yaw * 180.0f / rt_demo::kPi, cam.pitch * 180.0f / rt_demo::kPi);
        }

        // 空格 ⇒ 高质量渲染并存 PNG
        if (window.keyPressed(gfx::Key::Space)) {
            std::printf("（开始高质量渲染：%dx%d、%dx%d 超采样、深度 %d ...）\n",
                        kShotWidth, kShotHeight, kShotSamples, kShotSamples, kShotDepth);
            const View shotView = cam.makeView(kShotWidth, kShotHeight);
            RayTracer shotTracer(hit, scene.lights);
            shotTracer.setBackground(scene.background);
            shotTracer.setSamplesPerAxis(kShotSamples);
            gfx::Canvas shot(kShotWidth, kShotHeight);
            shotTracer.render(shot, [&](float px, float py) {
                Vec3 o, d;
                shotView.pixelToRay(px, py, o, d);
                return Ray(o, d);
            }, kShotDepth, [](int done, int total) {
                if (done % 100 == 0 || done == total) {
                    std::cout << "  出图进度: " << (done * 100 / total) << "%\n";
                }
            });
            const char* path = "ray-tracing-interactive.png";
            std::string error;
            if (gfx::savePng(shot, path, &error)) std::printf("（已保存：%s）\n", path);
            else std::fprintf(stderr, "错误：保存 PNG 失败 —— %s\n", error.c_str());
            dirty = true;   // 出图不改预览，但下一帧照常刷新
        }

        if (window.keyPressed(gfx::Key::Escape)) {
            window.requestClose();
            break;
        }

        if (dirty || autoRun) {
            const View view = cam.makeView(width, height);
            tracer.render(window.canvas(), [&](float px, float py) {
                Vec3 o, d;
                view.pixelToRay(px, py, o, d);
                return Ray(o, d);
            }, kPreviewDepth);
            window.present();
            dirty = false;
            if (autoRun && ++frame >= frames) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    std::cout << "\n退出。\n";
    return 0;
}
