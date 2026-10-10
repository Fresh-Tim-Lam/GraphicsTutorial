// triangle_demo.cpp —— 教学示例：三角形光栅化（点采样 vs 抗锯齿）+ 深度测试
//
// 用途：把 gfx/render/triangle.hpp 与 gfx/render/depth.hpp 的两件事画成一张对比图：
//       ① 上半：**同一个三角形**分别用 点采样 / 覆盖率抗锯齿 / 超采样 SSAA 渲染 —— 看边缘差别；
//       ② 下半：三个深度不同的重叠三角形，**故意乱序绘制**，靠深度测试得到正确遮挡（与顺序无关）。
// 权威实现：gfx::fillTriangle（唯一入口，TriangleFillMode 分派点采样 / CoverageAA / SSAA）
//           gfx::DepthBuffer / fillTriangleDepth（depth.hpp）
// 构建：cmake --build build --target tutorial_triangle
// 运行：build/tutorials/TriangleRaster --output tutorials/triangle-raster/output/triangle-raster.png
//       build/tutorials/TriangleRaster --window
//
// 参数：--output <png>（默认 output/triangle-raster.png，目录需存在）/ --window / --size WxH / --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/triangle.hpp>
#include <gfx/render/window.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::DepthBuffer;
using gfx::rgb;
using gfx::math::Vec2;
using gfx::math::Vec3;

const Color kBackground = rgb(16, 18, 26);
const Color kPanel = rgb(28, 32, 44);
const Color kTriangle = rgb(90, 200, 220);

struct Options {
    bool useWindow = false;
    std::string outputPath = "output/triangle-raster.png";
    int width = 900;
    int height = 600;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  TriangleRaster [--output <out.png>] [--window] [--size WxH]\n"
        "  TriangleRaster --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/triangle-raster.png；目录需已存在）\n"
        "  --window          打开窗口展示（ESC/关闭退出）\n"
        "  --size WxH        画布尺寸（默认 900x600，上半三块对比 + 下半深度测试）\n"
        "  --help            显示本帮助\n";
}

bool parseArgs(int argc, char** argv, Options& opt, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); exitCode = 0; return false; }
        if (arg == "--window") { opt.useWindow = true; }
        else if (arg == "--output" && i + 1 < argc) { opt.outputPath = argv[++i]; }
        else if (arg == "--size" && i + 1 < argc) {
            const std::string size = argv[++i];
            const std::size_t sep = size.find('x');
            if (sep == std::string::npos) {
                std::cerr << "错误：--size 需要 WxH 形式，例如 900x600\n";
                exitCode = 2;
                return false;
            }
            opt.width = std::atoi(size.substr(0, sep).c_str());
            opt.height = std::atoi(size.substr(sep + 1).c_str());
            if (opt.width < 300 || opt.height < 200) {
                std::cerr << "错误：画布尺寸过小（至少 300x200）\n";
                exitCode = 2;
                return false;
            }
        } else {
            std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n";
            exitCode = 2;
            return false;
        }
    }
    return true;
}

// 统计"既不是背景也不是纯色"的像素数 —— 抗锯齿的中间色
int countIntermediate(const Canvas& canvas, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1 && y < canvas.height(); ++y)
        for (int x = x0; x < x1 && x < canvas.width(); ++x) {
            const Color c = canvas.pixelAt(x, y);
            if (c != kBackground && c != kPanel && c != kTriangle) ++n;
        }
    return n;
}

// 画一帧：上半三块对比（点采样 / 覆盖率 AA / SSAA），下半深度测试
void renderScene(Canvas& canvas, const Options& opt, int& intermediateSharp,
                 int& intermediateAA, int& intermediateSSAA) {
    canvas.clear(kBackground);

    const int panelH = opt.height / 2;
    const int panelW = opt.width / 3;

    // ---- 上半：同一三角形，三种采样方式 ----
    for (int i = 0; i < 3; ++i) {
        const float ox = static_cast<float>(i * panelW);
        // 45° 斜边的直角三角形 —— 斜边正是锯齿最明显的地方
        const Vec2 a(ox + 24.0f, 24.0f);
        const Vec2 b(ox + static_cast<float>(panelW) - 24.0f, static_cast<float>(panelH) - 24.0f);
        const Vec2 c(ox + 24.0f, static_cast<float>(panelH) - 24.0f);

        if (i == 0) {
            gfx::fillTriangle(canvas, a, b, c, kTriangle);
        } else if (i == 1) {
            gfx::fillTriangle(canvas, a, b, c, kTriangle, gfx::TriangleFillMode::CoverageAA, 8);
        } else {
            gfx::fillTriangle(canvas, a, b, c, kTriangle, gfx::TriangleFillMode::SSAA, 4, &kBackground);
        }
    }
    intermediateSharp = countIntermediate(canvas, 0, 0, panelW, panelH);
    intermediateAA = countIntermediate(canvas, panelW, 0, panelW * 2, panelH);
    intermediateSSAA = countIntermediate(canvas, panelW * 2, 0, opt.width, panelH);

    // ---- 下半：深度测试（三块不同深度的重叠三角形，乱序绘制） ----
    DepthBuffer depth(opt.width, opt.height);
    struct Piece {
        Vec2 a, b, c;
        float z;
        Color color;
    };
    const float y0 = static_cast<float>(panelH) + 20.0f;
    const Piece pieces[3] = {
        {Vec2(80.0f, y0), Vec2(520.0f, y0 + 40.0f), Vec2(280.0f, static_cast<float>(opt.height) - 20.0f),
         0.6f, rgb(70, 90, 200)},                                    // 最远：暗蓝
        {Vec2(430.0f, y0 - 10.0f), Vec2(830.0f, y0 + 60.0f), Vec2(600.0f, static_cast<float>(opt.height) - 20.0f),
         0.0f, rgb(80, 190, 130)},                                   // 中间：绿
        {Vec2(240.0f, y0 + 70.0f), Vec2(700.0f, y0 + 140.0f), Vec2(430.0f, static_cast<float>(opt.height) - 20.0f),
         -0.6f, rgb(240, 180, 70)},                                  // 最近：琥珀
    };

    // 故意**乱序**绘制（最近的先画）——深度测试保证遮挡仍然正确
    for (int idx : {2, 0, 1}) {
        const Piece& p = pieces[idx];
        gfx::fillTriangleDepth(canvas, depth, p.a, p.b, p.c,
                               Vec3(p.z, p.z, p.z), p.color);
    }
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    int exitCode = 0;
    if (!parseArgs(argc, argv, opt, exitCode)) return exitCode;

    Canvas canvas(opt.width, opt.height);
    int sharp = 0, aa = 0, ssaa = 0;
    renderScene(canvas, opt, sharp, aa, ssaa);

    if (opt.useWindow) {
        gfx::Window window("三角形光栅化：点采样 / 覆盖率 AA / SSAA + 深度测试", opt.width, opt.height);
        if (!window.isOpen()) {
            std::cerr << "错误：窗口创建失败。\n";
            return 5;
        }
        renderScene(window.canvas(), opt, sharp, aa, ssaa);
        while (window.pollEvents()) {
            window.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    std::string error;
    if (!gfx::savePng(canvas, opt.outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }

    std::cout << "上半（同一三角形，三种采样方式）中间色像素数：\n"
              << "  ① 点采样        ：" << sharp << "（应为 0：只有纯色或背景）\n"
              << "  ② 覆盖率抗锯齿  ：" << aa << "（> 0：边界取中间值）\n"
              << "  ③ 超采样 SSAA   ：" << ssaa << "（> 0）\n"
              << "下半：三个重叠三角形按 近→远→中 乱序绘制，靠深度测试得到正确遮挡。\n"
              << "已保存结果图：" << opt.outputPath << "\n";
    return 0;
}
