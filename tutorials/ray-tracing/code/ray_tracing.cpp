// ray_tracing.cpp —— 教学示例：用 gfx **自己**渲染一张光线跟踪图（脱离老师引擎）
//
// 用途：把本阶段新增的 gfx 能力端到端用一遍，渲染出一张"类似作业三"的光线跟踪图：
//       - 光照：gfx::shadeLocal（环境光 + 自发光 + Lambert 漫反射 + Blinn-Phong 高光 + 阴影）；
//       - 递归：gfx::RayTracer（镜面反射 + 规则透射 + 全反射 + 递归深度 + 超采样 + 可选伽马）；
//       - 场景：地面平面 + 高反射镜面球 + 透明玻璃球 + 红球 + 立方体 + 圆柱 +
//               不规则晶体（三角网格：一半透明一半实心）+ 悬浮在前景的透明球 + 5 个彩色点光源；
//       - 相机：gfx::View（lookAt + setPerspective + **pixelToRay**）生成主光线；
//       - 加速：gfx::Bvh（只吃 AABB；visitor 里按索引做精确求交并回传材质）；
//       - 输出：gfx::savePng。
//
// 权威实现：gfx/render/lighting.hpp（shadeLocal）、gfx/render/raytracer.hpp（RayTracer）、
//           gfx/render/view.hpp（View）、gfx/geom/ray.hpp（求交）、gfx/geom/bvh.hpp（Bvh）、gfx/render/png.hpp。
// 构建：cmake --build build --target tutorial_ray_tracing
// 运行：build/tutorials/RayTracing --output tutorials/ray-tracing/output/ray-tracing.png
//
// 参数：--output <png>（默认 output/ray-tracing.png，父目录自动创建）/ --size WxH / --samples n（超采样，默认 3）
//       / --depth d（递归深度，默认 6）/ --brute（额外跑暴力法做一致性校验，默认关）/ --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/geom.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/view.hpp>

#include "ray_tracing_scene.hpp"   // 场景 / BVH / 求交（与窗口交互 demo 共用）

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>   // _isatty / _fileno（双击运行时判断 stdin 是否为终端）
#ifndef NOMINMAX
#define NOMINMAX      // 避免 windows.h 的 min/max 宏破坏 std::min / std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // SetConsoleOutputCP（控制台按 UTF-8 输出，避免中文乱码）
#endif

namespace {

using gfx::Aabb;
using gfx::Bvh;
using gfx::Canvas;
using gfx::HitRecord;
using gfx::Light;
using gfx::Material;
using gfx::Ray;
using gfx::RayTracer;
using gfx::SurfaceHit;
using gfx::View;
using gfx::math::Vec3;

// 双击运行（命令行无参数）时渲染完暂停一下，避免控制台一闪而过、看不到输出路径。
// 仅当 stdin 是终端时才暂停，脚本 / 管道调用不受影响。
void pauseIfInteractive() {
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        std::cout << "\n按回车键退出 ..." << std::flush;
        std::cin.get();
    }
#endif
}

constexpr float kPi = 3.14159265358979323846f;

struct Options {
    std::string outputPath = "output/ray-tracing.png";
    int width = 800;
    int height = 600;
    int samplesPerAxis = 3;   // 3×3 超采样
    int maxDepth = 6;         // 递归深度
    bool compareBrute = false; // 是否额外跑一遍"关掉 BVH 的暴力法"做一致性校验（默认关，用 --brute 打开）
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  RayTracing [--output <out.png>] [--size WxH] [--samples n] [--depth d] [--brute]\n"
        "  RayTracing --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/ray-tracing.png；父目录会自动创建）\n"
        "  --size WxH        画布尺寸（默认 800x600）\n"
        "  --samples n       每像素 n×n 超采样（默认 3，n=1 关闭）\n"
        "  --depth d         递归深度（默认 6）\n"
        "  --brute           额外跑一遍\"关掉 BVH 的暴力法\"做逐像素一致性校验（默认关闭，约多花一倍时间）\n"
        "  --help            显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--brute") { opt.compareBrute = true; }
        else if (arg == "--samples" && i + 1 < argc) { opt.samplesPerAxis = std::atoi(argv[++i]); }
        else if (arg == "--depth" && i + 1 < argc) { opt.maxDepth = std::atoi(argv[++i]); }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 800x600\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
            if (opt.width < 64 || opt.height < 64) {
                std::cerr << "错误：画布尺寸过小（至少 64x64）\n";
                exitCode = 2;
                return false;
            }
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
            exitCode = 2;
            return false;
        }
    }
    if (opt.samplesPerAxis < 1) opt.samplesPerAxis = 1;
    if (opt.maxDepth < 1) opt.maxDepth = 1;
    return true;
}

// 场景与求交：均在共享头 ray_tracing_scene.hpp 中定义（离线渲染与窗口交互 demo 共用同一份）。

// 统计 + 求交（hitSceneBvh / hitSceneBrute）也来自共享头。

double secondsSince(const std::chrono::high_resolution_clock::time_point& t0) {
    const auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(65001);   // 控制台按 UTF-8 解释输出 ⇒ 中文不乱码（双击运行同样是 UTF-8 代码页）
#endif
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    // ---- 场景：与窗口交互 demo 共用同一份定义（ray_tracing_scene.hpp）----
    rt_demo::Scene scene = rt_demo::makeScene();

    // ---- 相机：gfx::View（lookAt + setPerspective + pixelToRay）----
    const float aspect = static_cast<float>(opt.width) / static_cast<float>(opt.height);
    View view;
    view.setViewport(opt.width, opt.height);
    view.lookAt(Vec3(0.0f, 1.35f, 4.60f), Vec3(0.0f, -0.15f, -1.00f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(42.0f * kPi / 180.0f, aspect, 0.1f, 100.0f);

    auto rayForPixel = [&](float px, float py) {
        Vec3 origin, direction;
        view.pixelToRay(px, py, origin, direction);   // 复用第一阶段的相机反投影
        return Ray(origin, direction);
    };

    // ---- 两个跟踪器：BVH 版 / 暴力版（只换求交函数，其余参数一致）----
    rt_demo::Stats bvhStats, bruteStats;
    RayTracer::HitFunction bvhHit = [&](const Ray& ray, SurfaceHit& out) {
        return rt_demo::hitSceneBvh(ray, out, scene, bvhStats);
    };
    RayTracer::HitFunction bruteHit = [&](const Ray& ray, SurfaceHit& out) {
        return rt_demo::hitSceneBrute(ray, out, scene, bruteStats);
    };

    RayTracer tracerBvh(bvhHit, scene.lights);
    tracerBvh.setBackground(scene.background);
    tracerBvh.setSamplesPerAxis(opt.samplesPerAxis);

    // ---- 主光线命中统计（1 采样/像素，仅统计）----
    long long primaryHits = 0;
    for (int y = 0; y < opt.height; ++y) {
        for (int x = 0; x < opt.width; ++x) {
            Vec3 origin, direction;
            view.pixelToRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, origin, direction);
            SurfaceHit sh;
            if (bvhHit(Ray(origin, direction), sh)) ++primaryHits;
        }
    }

    // ---- 渲染（BVH 版）----
    // 渲染进度：与课程框架 render_scene 的做法一致，每 50 行打印一次百分比（末行必打 100%）。
    auto progressPrinter = [](const char* label) {
        return [label](int done, int total) {
            if (done % 50 == 0 || done == total) {
                std::cout << label << (done * 100 / total) << "%" << std::endl;
            }
        };
    };

    Canvas canvas(opt.width, opt.height);
    bvhStats = rt_demo::Stats{};
    const auto t0 = std::chrono::high_resolution_clock::now();
    tracerBvh.render(canvas, rayForPixel, opt.maxDepth, progressPrinter("渲染进度: "));
    const double bvhSeconds = secondsSince(t0);

    // ---- 暴力法对比（同一场景、同一参数）----
    double bruteSeconds = 0.0;
    long long bruteMismatchPixels = -1;
    if (opt.compareBrute) {
        RayTracer tracerBrute(bruteHit, scene.lights);
        tracerBrute.setBackground(scene.background);
        tracerBrute.setSamplesPerAxis(opt.samplesPerAxis);
        Canvas canvasBrute(opt.width, opt.height);
        bruteStats = rt_demo::Stats{};
        const auto t1 = std::chrono::high_resolution_clock::now();
        tracerBrute.render(canvasBrute, rayForPixel, opt.maxDepth, progressPrinter("暴力法进度: "));
        bruteSeconds = secondsSince(t1);

        long long mism = 0;
        for (int y = 0; y < opt.height; ++y)
            for (int x = 0; x < opt.width; ++x)
                if (canvas.pixelAt(x, y) != canvasBrute.pixelAt(x, y)) ++mism;
        bruteMismatchPixels = mism;
    }

    // ---- 保存 PNG ----
    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }

    // ---- 控制台统计 ----
    const double pixels = static_cast<double>(opt.width) * static_cast<double>(opt.height);
    const double primaryRays = pixels * static_cast<double>(opt.samplesPerAxis) * static_cast<double>(opt.samplesPerAxis);

    std::cout << "光线跟踪（用 gfx 自己渲染：光照 + 反射/折射 + 阴影 + 超采样）\n";
    std::cout << "  分辨率        : " << opt.width << "x" << opt.height
              << "（每像素 " << opt.samplesPerAxis << "x" << opt.samplesPerAxis << " = "
              << opt.samplesPerAxis * opt.samplesPerAxis << " 条主光线，共 "
              << static_cast<long long>(primaryRays) << " 条）\n";
    std::cout << "  递归深度      : " << opt.maxDepth << "\n";
    std::cout << "  场景图元      : 球 x4（镜面 ks=0.85 / 玻璃 kt=0.9,n=1.5 / 漫反射 / 悬浮透明 kt=0.92,n=1.45）"
                 " + 立方体 x1 + 圆柱 x1 + 不规则晶体（8 三角面：一半透明 kt=0.9,n=1.55、一半实心）"
                 " + 凸透镜 x3（双凸 = 两球交集，n=1.5；物距 u = 0.7f / 1.5f / 3f）（以上入 BVH）+ 无限地面 x1\n";
    std::cout << "  光源          : 5 个彩色点光源\n";
    std::cout << "  BVH           : 节点 " << scene.bvh.nodeCount() << " / 叶子 " << scene.bvh.leafCount() << "\n";
    std::cout << "  主光线命中像素: " << primaryHits << "（占 "
              << static_cast<int>(100.0 * static_cast<double>(primaryHits) / pixels) << "%）\n";
    std::cout << "  平均每像素    : BVH 精确求交 " << (static_cast<double>(bvhStats.intersectCalls) / pixels)
              << " 次 / 节点访问 " << (static_cast<double>(bvhStats.nodeVisits) / pixels) << " 次"
              << "（含超采样与递归）\n";
    std::cout << "  渲染耗时      : BVH " << bvhSeconds * 1000.0 << " ms";
    if (opt.compareBrute) {
        std::cout << "   暴力法 " << bruteSeconds * 1000.0 << " ms"
                  << "   （BVH/暴力 = " << (bruteSeconds > 0.0 ? bvhSeconds / bruteSeconds : 0.0) << "）\n";
        std::cout << "  暴力法求交    : 平均每像素 " << (static_cast<double>(bruteStats.intersectCalls) / pixels)
                  << " 次（= 图元数 " << scene.prims.size() << " 次/查询）\n";
        std::cout << "  两图一致性    : ";
        if (bruteMismatchPixels == 0) std::cout << "完全一致（0 不同像素）\n";
        else std::cout << "不一致（" << bruteMismatchPixels << " / "
                       << static_cast<long long>(pixels) << " 个像素不同，占 "
                       << (100.0 * static_cast<double>(bruteMismatchPixels) / pixels) << "%）\n";
    } else {
        std::cout << "（已跳过暴力法对比）\n";
    }
    std::cout << "  输出          : " << opt.outputPath << "\n";
    if (argc == 1) {
        std::cout << "（未指定参数：已用默认设置渲染，路径相对当前目录 —— 双击运行时即 exe 所在目录；"
                     "想校验 BVH 正确性可加 --brute）\n";
        pauseIfInteractive();
    }
    return 0;
}
