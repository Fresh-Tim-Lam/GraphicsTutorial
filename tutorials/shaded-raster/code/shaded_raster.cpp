// shaded_raster.cpp —— 光栅化着色管线演示（离线出图版）：带纹理 + 法线贴图 + 阴影贴图的立方体 / 球 / 地面
//
// 用途：演示 `gfx/render/shading.hpp` 的能力 —— 顶点属性（位置/法向/切线/uv）经
//       **投影 + 近平面裁剪 + 透视校正插值**后，逐像素调用片元着色器：
//         采样法线贴图 → 切线空间扰动法向 → 喂给 shadeLocal（Blinn-Phong）→ 逐像素颜色；
//       阴影则由 `gfx/render/shadowmap.hpp` 提供（光栅化路径没有场景求交，用不了阴影射线）。
//
// 与窗口交互版（shaded_raster_interactive.cpp）**共用** shaded_raster_scene.hpp 的场景与着色代码；
// 本文件只负责"参数解析 + 渲一张 PNG（含 A/B 对照统计）"。
//
// 用法：build/tutorials/ShadedRaster [--output <out.png>] [--size WxH] [--strength s]
//                                     [--no-shadow] [--compare] [--help]
//   --strength s  凹凸强度（默认 1.0；0 = 贴图退化成平整法向）
//   --no-shadow   关闭阴影贴图（A/B 对照）
//   --compare     额外渲染一张"关闭阴影"的图并统计差异像素与椒盐像素（默认关闭）
//
// 关联：tutorials/shaded-raster/code/shaded_raster_scene.hpp、src/include/gfx/render/shading.hpp、shadowmap.hpp

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/shadowmap.hpp>
#include <gfx/render/view.hpp>

#include "shaded_raster_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台 UTF-8，中文不乱码）
#endif

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::View;
using gfx::math::Vec3;

// 场景在光源视角下的范围实测 x∈[−8.5, 8.5]、y∈[−6.2, 5.5]、深度∈[1.5, 13.8]，故正交半宽取 10、深度取 [0.1, 24] 留余量
constexpr float kShadowExtent = 10.0f;
constexpr float kShadowNearZ = 0.1f;
constexpr float kShadowFarZ = 24.0f;

struct Options {
    std::string outputPath = "output/shaded-raster.png";
    std::string comparePath = "output/shaded-raster-no-shadow.png";
    int width = 800;
    int height = 600;
    float strength = 1.0f;
    int shadowSize = 1024;
    bool shadow = true;
    bool compare = false;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  ShadedRaster [--output <out.png>] [--size WxH] [--strength s] [--shadow-size n]\n"
        "               [--no-shadow] [--compare] [--help]\n"
        "\n"
        "  --output <file>  结果 PNG 路径（默认 output/shaded-raster.png，父目录自动创建）\n"
        "  --size WxH       画布尺寸（默认 800x600）\n"
        "  --strength s     凹凸强度（默认 1.0；0 = 贴图退化为平整法向）\n"
        "  --shadow-size n  阴影贴图分辨率（默认 1024）\n"
        "  --no-shadow      关闭阴影贴图（A/B 对照）\n"
        "  --compare        额外渲染一张关闭阴影的图并统计差异像素（默认关闭）\n"
        "  --help           显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--no-shadow") { opt.shadow = false; }
        else if (arg == "--compare") { opt.compare = true; }
        else if (arg == "--strength" && i + 1 < argc) { opt.strength = static_cast<float>(std::atof(argv[++i])); }
        else if (arg == "--shadow-size" && i + 1 < argc) { opt.shadowSize = std::atoi(argv[++i]); }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help）\n";
            exitCode = 2;
            return false;
        }
    }
    if (opt.width < 32) opt.width = 32;
    if (opt.height < 32) opt.height = 32;
    if (opt.strength < 0.0f) opt.strength = 0.0f;
    if (opt.shadowSize < 16) opt.shadowSize = 16;
    return true;
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);
#endif
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    // ---- 场景（与窗口交互版共用同一份构造代码）----
    const sr_demo::Scene scene = sr_demo::makeScene(256, opt.strength);

    // ---- 相机（与共享头 CameraRig 的默认取景一致；这里显式写 eye/target 便于对照）----
    const Vec3 eye(0.0f, 1.30f, 4.60f);
    View view;
    view.setViewport(opt.width, opt.height);
    view.lookAt(eye, Vec3(0.0f, -0.25f, -0.10f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(42.0f * sr_demo::kPi / 180.0f,
                        static_cast<float>(opt.width) / static_cast<float>(opt.height), 0.1f, 100.0f);

    // ---- 阴影贴图（一个光源一张；只依赖光源与场景，与相机无关）----
    sr_demo::ShadowConfig shadowConfig;
    shadowConfig.size = opt.shadowSize;
    shadowConfig.extent = kShadowExtent;
    shadowConfig.nearZ = kShadowNearZ;
    shadowConfig.farZ = kShadowFarZ;

    std::vector<gfx::ShadowMap> shadowMaps;
    if (opt.shadow) shadowMaps = sr_demo::buildShadowMaps(scene, shadowConfig);

    Canvas canvas(opt.width, opt.height);
    canvas.clear(Color(24, 28, 38));

    std::cout << "光栅化着色管线演示（顶点属性 → 透视校正插值 → 逐像素着色）\n"
                 "  分辨率        : " << opt.width << "x" << opt.height << "\n"
              << "  凹凸强度      : " << opt.strength << "\n"
              << "  场景          : 地面(棋格) + 立方体(砖缝) + 球(波纹)，全部由三角形拼出\n"
              << "  片元着色      : 采样法线贴图 → TBN 扰动 → shadeLocal(Blinn-Phong) + gamma\n"
              << "  阴影          : " << (opt.shadow
                     ? "逐光源一张 " + std::to_string(shadowConfig.size) + "² 阴影贴图（正交光源视角 + 法向偏移 + 斜率缩放容差 + PCF 3×3）"
                     : std::string("已用 --no-shadow 关闭"))
              << "\n";

    sr_demo::RenderOptions render;
    render.normalMaps = true;
    render.shadow = opt.shadow;

    // ---- 主 pass：光栅化 + 深度测试 + 片元着色（阴影在片元回调里查表）----
    const long long fragments = sr_demo::renderScene(canvas, view, eye, scene, shadowMaps, render);

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 4;
    }
    std::cout << "  三角形数      : " << scene.triangles.size() << "\n"
                 "  着色片元数    : " << fragments << "\n"
                 "  输出          : " << opt.outputPath << "\n";

    // A/B 对照：再渲一张"关掉阴影"的，统计差异像素（阴影只影响受遮挡处，差异应集中在影子区域）
    if (opt.compare) {
        Canvas plainCanvas(opt.width, opt.height);
        plainCanvas.clear(Color(24, 28, 38));
        sr_demo::RenderOptions plainRender;
        plainRender.normalMaps = true;
        plainRender.shadow = false;
        sr_demo::renderScene(plainCanvas, view, eye, scene, {}, plainRender);

        long long diff = 0;
        long long maxDiff = 0;
        long long sumDiff = 0;
        const long long pixels = static_cast<long long>(opt.width) * opt.height;
        const auto indexAt = [&](int x, int y) { return static_cast<std::size_t>(y) * opt.width + x; };
        std::vector<unsigned char> mask(static_cast<std::size_t>(pixels), 0);
        for (int y = 0; y < opt.height; ++y) {
            for (int x = 0; x < opt.width; ++x) {
                const Color a = canvas.pixelAt(x, y);
                const Color b = plainCanvas.pixelAt(x, y);
                const int d = std::max({std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)),
                                        std::abs(int(a.b) - int(b.b))});
                if (d > 0) { mask[indexAt(x, y)] = 1; ++diff; }
                if (d > maxDiff) maxDiff = d;
                sumDiff += d;
            }
        }

        // "椒盐像素"：差异像素中 4 邻域都无差异的那些 —— 自阴影 acne 的典型形态。
        // 实体阴影区里每个差异像素必然与邻居同属差异区，故这个数字直接反映自阴影噪点的多少。
        long long speckle = 0;
        for (int y = 0; y < opt.height; ++y) {
            for (int x = 0; x < opt.width; ++x) {
                if (mask[indexAt(x, y)] == 0) continue;
                const bool adjacent =
                    (x > 0 && mask[indexAt(x - 1, y)] != 0) ||
                    (x + 1 < opt.width && mask[indexAt(x + 1, y)] != 0) ||
                    (y > 0 && mask[indexAt(x, y - 1)] != 0) ||
                    (y + 1 < opt.height && mask[indexAt(x, y + 1)] != 0);
                if (!adjacent) ++speckle;
            }
        }

        std::cout << "  对照（关闭阴影）: "
                  << diff << " / " << pixels << " 个像素不同（占 "
                  << (100.0 * static_cast<double>(diff) / static_cast<double>(pixels)) << "%）"
                     "，最大通道差 " << maxDiff
                  << "，平均差 " << (static_cast<double>(sumDiff) / static_cast<double>(pixels)) << "\n"
                  << "  自阴影椒盐像素: " << speckle << "（占画面 "
                  << (100.0 * static_cast<double>(speckle) / static_cast<double>(pixels)) << "%）\n";
        std::string plainError;
        gfx::savePng(plainCanvas, opt.comparePath.c_str(), &plainError);
        std::cout << "  对照图输出    : " << opt.comparePath << "\n";
    }

    return 0;
}
