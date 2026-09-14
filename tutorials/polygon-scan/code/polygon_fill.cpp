// polygon_fill.cpp —— 作业 1：多边形扫描转换（命令行 + 窗口演示）
//
// 用途：读取多边形顶点（文本文件或交互输入），做扫描线填充与边界绘制，
//       可保存 PNG、也可在窗口中逐条扫描线动画演示。
// 权威实现：gfx::fillPolygon / gfx::ScanlineFiller / gfx::drawLine（src/include/gfx/raster.hpp）；
//           本文件只负责"输入解析 + 世界坐标→像素坐标映射 + 展示"。
// 构建：cmake -S . -B build ; cmake --build build --target tutorial_polygon_fill
// 运行：build/tutorials/PolygonFill.exe --input data/convex.txt --output out.png
//       build/tutorials/PolygonFill.exe --input data/concave.txt --window --animate
//       build/tutorials/PolygonFill.exe --interactive --window
//
// 输入文件格式（第一行顶点数 n，随后 n 行 "x y"）：
//   5
//   100 80
//   220 80
//   ...
// 说明：空行与以 '#' 开头的行会被忽略；末尾允许重复写一次首点（程序自动识别为闭合点）。

#include <gfx/canvas.hpp>
#include <gfx/color.hpp>
#include <gfx/png.hpp>
#include <gfx/raster.hpp>
#include <gfx/window.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::math::Vec2;
using gfx::rgb;

// ---- 配色与默认参数 ----
const Color kBackground = rgb(16, 24, 48);    // 背景：深蓝
const Color kFillColor = rgb(70, 130, 200);   // 填充：钢蓝
const Color kOutlineColor = rgb(250, 220, 120);  // 边界：暖黄
const Color kSweepColor = rgb(64, 72, 96);    // 当前扫描线高亮：暗灰蓝
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;
constexpr float kMargin = 24.0f;              // 多边形到画布边缘的留白（像素）
constexpr int kFrameDelayMs = 12;             // 动画每帧延时

// ---- 退出码（便于脚本判断失败原因）----
enum ExitCode {
    kOk = 0,
    kBadArgs = 2,
    kBadInput = 3,
    kBadPolygon = 4,
    kBadOutput = 5,
};

struct Options {
    bool interactive = false;
    bool useWindow = false;
    bool animate = false;
    std::string inputPath;
    std::string outputPath;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  PolygonFill --input <file>      [--output <out.png>] [--window [--animate]] [--size WxH]\n"
        "  PolygonFill --interactive       [--output <out.png>] [--window [--animate]] [--size WxH]\n"
        "  PolygonFill --help\n"
        "\n"
        "参数：\n"
        "  --input <file>   从文本文件读顶点（首行 n，随后 n 行 \"x y\"）\n"
        "  --interactive    从标准输入交互式输入顶点（先输数量，再逐个输坐标）\n"
        "  --output <file>  把结果写成 PNG（可省略；省略且未开窗口时只在终端打印摘要）\n"
        "  --window         打开窗口展示结果\n"
        "  --animate        配合 --window：逐条扫描线演示填充过程\n"
        "  --size WxH       画布尺寸，默认 800x600\n"
        "\n"
        "顶点坐标：世界坐标（y 轴向上，按逆时针给出）；程序自动缩放并映射到窗口像素。\n";
}

// 解析 "x y" / "x,y" / "(x, y)" 形式的顶点；不允许多余内容
bool parseVertex(const std::string& line, Vec2& out) {
    std::string cleaned;
    cleaned.reserve(line.size());
    for (char ch : line) {
        if (ch == ',' || ch == '(' || ch == ')' || ch == '\t') cleaned += ' ';
        else cleaned += ch;
    }
    std::istringstream in(cleaned);
    float x = 0.0f, y = 0.0f;
    if (!(in >> x >> y)) return false;
    std::string extra;
    if (in >> extra) return false;
    out = Vec2(x, y);
    return true;
}

// 跳过空行与 '#' 注释行
bool nextDataLine(std::istream& in, std::string& line) {
    while (std::getline(in, line)) {
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i < line.size() && line[i] != '#') return true;
    }
    return false;
}

// 从文本文件读顶点；失败时写入中文原因
bool readPolygonFile(const std::string& path, std::vector<Vec2>& out, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "无法打开输入文件：" + path;
        return false;
    }

    std::string line;
    if (!nextDataLine(file, line)) {
        error = "输入文件为空：" + path;
        return false;
    }
    std::istringstream head(line);
    int count = 0;
    std::string extra;
    if (!(head >> count) || (head >> extra) || count < 3) {
        error = "第 1 行应为顶点数（≥3 的整数），实际为：" + line;
        return false;
    }

    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    int lineNo = 1;
    for (int i = 0; i < count; ++i) {
        if (!nextDataLine(file, line)) {
            error = "顶点数声明为 " + std::to_string(count) + "，但只读到 " + std::to_string(i) + " 个顶点";
            return false;
        }
        ++lineNo;
        Vec2 v;
        if (!parseVertex(line, v)) {
            error = "第 " + std::to_string(lineNo) + " 行顶点格式错误（应为 \"x y\"）：" + line;
            return false;
        }
        out.push_back(v);
    }
    return true;
}

// 交互式输入顶点
bool readPolygonInteractive(std::vector<Vec2>& out, std::string& error) {
    std::string line;
    int count = 0;
    for (;;) {
        std::cout << "顶点个数（≥3）：" << std::flush;
        if (!nextDataLine(std::cin, line)) { error = "标准输入已结束"; return false; }
        std::istringstream in(line);
        std::string extra;
        if ((in >> count) && !(in >> extra) && count >= 3) break;
        std::cout << "  输入无效，请输入一个 ≥3 的整数。" << std::endl;
    }

    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        for (;;) {
            std::cout << "顶点 " << (i + 1) << "/" << count << "（x y，建议逆时针）：" << std::flush;
            if (!nextDataLine(std::cin, line)) { error = "标准输入已结束"; return false; }
            Vec2 v;
            if (parseVertex(line, v)) { out.push_back(v); break; }
            std::cout << "  输入无效，示例：100 80" << std::endl;
        }
    }
    std::cout << "（提示：若末尾重复输入了首点，程序会自动识别为闭合点）" << std::endl;
    return true;
}

// 世界坐标 → 像素坐标：按包围盒等比缩放并居中，y 轴翻转（世界 y 向上，屏幕 y 向下）
std::vector<Vec2> toPixelSpace(const std::vector<Vec2>& world, int width, int height, float& scaleOut) {
    float minX = world.front().x, maxX = world.front().x;
    float minY = world.front().y, maxY = world.front().y;
    for (const Vec2& v : world) {
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
    }
    const float spanX = std::max(maxX - minX, 1e-6f);
    const float spanY = std::max(maxY - minY, 1e-6f);
    const float usableW = static_cast<float>(width) - 2.0f * kMargin;
    const float usableH = static_cast<float>(height) - 2.0f * kMargin;
    const float scale = std::min(usableW / spanX, usableH / spanY);
    scaleOut = scale;

    // 居中偏移：缩放后图形的实际占位
    const float drawW = spanX * scale;
    const float drawH = spanY * scale;
    const float offsetX = (static_cast<float>(width) - drawW) * 0.5f;
    const float offsetY = (static_cast<float>(height) - drawH) * 0.5f;

    std::vector<Vec2> pixels;
    pixels.reserve(world.size());
    for (const Vec2& v : world) {
        pixels.push_back(Vec2(offsetX + (v.x - minX) * scale,
                              offsetY + (maxY - v.y) * scale));   // y 翻转
    }
    return pixels;
}

// 画背景 + 边界（动画前后保持一致的外观）
void paintBackground(Canvas& canvas, const std::vector<Vec2>& pixels) {
    canvas.clear(kBackground);
    gfx::drawPolygonOutline(canvas, pixels, kOutlineColor);
}

// 静态渲染：先填充，再把边界覆盖到顶层
void renderStatic(Canvas& canvas, const std::vector<Vec2>& pixels) {
    paintBackground(canvas, pixels);
    gfx::fillPolygon(canvas, pixels, kFillColor);
    gfx::drawPolygonOutline(canvas, pixels, kOutlineColor);
}

// 逐扫描线动画：高亮当前扫描线，已扫过的区域留下填充色
void renderAnimated(gfx::Window& window, const std::vector<Vec2>& pixels) {
    Canvas& canvas = window.canvas();
    paintBackground(canvas, pixels);
    window.present();

    gfx::ScanlineFiller filler;
    if (!filler.begin(pixels)) return;

    int prevY = -1;
    std::vector<gfx::Span> prevSpans;
    while (window.pollEvents() && filler.step()) {
        const int y = filler.scanline();

        // 擦除上一帧的扫描线高亮（整行还原：先背景，再补上该行填充色）
        if (prevY >= 0) {
            for (int x = 0; x < canvas.width(); ++x) canvas.putPixel(x, prevY, kBackground);
            gfx::drawPolygonOutline(canvas, pixels, kOutlineColor);
            for (const gfx::Span& s : prevSpans)
                for (int x = s.xStart; x <= s.xEnd; ++x) canvas.putPixel(x, prevY, kFillColor);
        }
        // 高亮当前扫描线（整行），再把该行区间填上填充色
        for (int x = 0; x < canvas.width(); ++x) canvas.putPixel(x, y, kSweepColor);
        filler.fillCurrentRow(canvas, kFillColor);
        window.present();

        prevY = y;
        prevSpans = filler.spans();
        std::this_thread::sleep_for(std::chrono::milliseconds(kFrameDelayMs));
    }

    // 收尾：擦掉最后一条高亮，恢复"填充 + 边界"的最终画面
    renderStatic(canvas, pixels);
    window.present();

    // 保持窗口，直到用户关闭
    while (window.pollEvents()) std::this_thread::sleep_for(std::chrono::milliseconds(16));
}

bool parseSize(const std::string& text, int& width, int& height) {
    const std::size_t pos = text.find_first_of("xX*");
    if (pos == std::string::npos) return false;
    std::istringstream w(text.substr(0, pos)), h(text.substr(pos + 1));
    std::string extra;
    if (!(w >> width) || !(h >> height)) return false;
    if ((w >> extra) || (h >> extra)) return false;
    return width > 0 && height > 0;
}

bool parseArgs(int argc, char** argv, Options& opt, ExitCode& code) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* name, std::string& slot) {
            if (i + 1 >= argc) {
                std::cerr << "错误：参数 " << name << " 缺少取值。" << std::endl;
                code = kBadArgs;
                return false;
            }
            slot = argv[++i];
            return true;
        };
        if (arg == "--help" || arg == "-h") { printUsage(); code = kOk; return false; }
        else if (arg == "--input") { if (!needValue("--input", opt.inputPath)) return false; }
        else if (arg == "--output") { if (!needValue("--output", opt.outputPath)) return false; }
        else if (arg == "--interactive") { opt.interactive = true; }
        else if (arg == "--window") { opt.useWindow = true; }
        else if (arg == "--animate") { opt.animate = true; }
        else if (arg == "--size") {
            std::string value;
            if (!needValue("--size", value)) return false;
            if (!parseSize(value, opt.width, opt.height)) {
                std::cerr << "错误：--size 取值应为 WxH，例如 800x600，实际为：" << value << std::endl;
                code = kBadArgs;
                return false;
            }
        } else {
            std::cerr << "错误：未知参数 " << arg << "（用 --help 查看用法）" << std::endl;
            code = kBadArgs;
            return false;
        }
    }

    if (opt.interactive && !opt.inputPath.empty()) {
        std::cerr << "错误：只能指定一种输入方式（--input <file> 或 --interactive）。" << std::endl;
        code = kBadArgs;
        return false;
    }
    if (!opt.interactive && opt.inputPath.empty()) {
        std::cerr << "错误：必须指定输入方式：--input <file> 或 --interactive（用 --help 查看用法）。" << std::endl;
        code = kBadArgs;
        return false;
    }
    if (opt.animate && !opt.useWindow) {
        std::cerr << "错误：--animate 需要配合 --window 使用。" << std::endl;
        code = kBadArgs;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    ExitCode code = kOk;
    if (!parseArgs(argc, argv, opt, code)) return code;

    // ---- 1. 输入 ----
    std::vector<Vec2> world;
    std::string error;
    if (opt.interactive) {
        if (!readPolygonInteractive(world, error)) {
            std::cerr << "错误：" << error << std::endl;
            return kBadInput;
        }
    } else {
        if (!readPolygonFile(opt.inputPath, world, error)) {
            std::cerr << "错误：" << error << std::endl;
            return kBadInput;
        }
    }

    // ---- 2. 检查顶点顺序（逆时针为正；填充与方向无关，仅提示）----
    const std::vector<Vec2> cleaned = gfx::normalizePolygon(world);
    const float worldArea = gfx::polygonSignedArea(cleaned);

    // ---- 3. 校验多边形 ----
    const gfx::PolygonError polygonError = gfx::validatePolygon(world);
    if (polygonError != gfx::PolygonError::None) {
        std::cerr << "错误：输入多边形非法 —— " << gfx::polygonErrorText(polygonError) << std::endl;
        return kBadPolygon;
    }

    // ---- 4. 世界坐标 → 像素坐标 ----
    float scale = 1.0f;
    const std::vector<Vec2> pixels = toPixelSpace(cleaned, opt.width, opt.height, scale);

    // ---- 5. 终端摘要 ----
    std::cout << "顶点数：" << world.size() << "（规范化后 " << cleaned.size() << "）\n"
              << "顶点顺序：" << (worldArea > 0.0f ? "逆时针（符合要求）" : "顺时针（填充与方向无关，建议改为逆时针）")
              << "，世界坐标有向面积 " << worldArea << "\n"
              << "画布：" << opt.width << "x" << opt.height << "，缩放系数 " << scale << "\n";

    // ---- 6. 渲染：窗口（可动画）与/或 PNG 输出 ----
    Canvas offscreen(opt.width, opt.height);

    if (opt.useWindow) {
        gfx::Window window("作业1：多边形扫描转换", opt.width, opt.height);
        if (!window.isOpen()) {
            std::cerr << "错误：窗口创建失败。" << std::endl;
            return kBadOutput;
        }
        if (opt.animate) {
            renderAnimated(window, pixels);          // 动画结束后窗口保持到用户关闭
            std::cout << "动画演示结束（窗口可继续查看，关闭窗口即退出）。" << std::endl;
            if (!opt.outputPath.empty()) {
                renderStatic(offscreen, pixels);
                std::string pngError;
                if (!gfx::savePng(offscreen, opt.outputPath.c_str(), &pngError)) {
                    std::cerr << "错误：保存 PNG 失败 —— " << pngError << std::endl;
                    return kBadOutput;
                }
                std::cout << "已保存结果图：" << opt.outputPath << std::endl;
            }
            return kOk;
        }

        // 静态展示：填充 + 边界，保持窗口直到关闭
        renderStatic(window.canvas(), pixels);
        std::cout << "已展示结果（关闭窗口即退出）。" << std::endl;
        while (window.pollEvents()) {
            window.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        renderStatic(offscreen, pixels);
    } else {
        renderStatic(offscreen, pixels);
    }

    // 填充像素统计（可复现的输出说明）
    int filled = 0;
    for (int y = 0; y < offscreen.height(); ++y)
        for (int x = 0; x < offscreen.width(); ++x)
            if (offscreen.pixelAt(x, y) == kFillColor) ++filled;
    std::cout << "填充像素数：" << filled << std::endl;

    if (!opt.outputPath.empty()) {
        std::string pngError;
        if (!gfx::savePng(offscreen, opt.outputPath.c_str(), &pngError)) {
            std::cerr << "错误：保存 PNG 失败 —— " << pngError << std::endl;
            return kBadOutput;
        }
        std::cout << "已保存结果图：" << opt.outputPath << std::endl;
    }
    return kOk;
}
