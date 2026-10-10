// line_algorithms.cpp —— 教学示例：三种画直线算法对照（Naive / DDA / Bresenham）
//
// 用途：把 gfx::LineAlgorithm 提供的三种画直线算法（见 gfx/render/raster.hpp）画成一张**三幅并排**的对照图：
//       同一组线段，分别用 Naive / DDA / Bresenham 渲染，左/中/右各一幅，便于逐像素对照。
//       另在控制台打印**真实统计**：每条线段三种算法各自的像素数，以及任意两种算法之间是否存在像素差异；
//       最后打印总计（差异线段数）。
//
// 权威实现：gfx::drawLine（raster.hpp 的唯一入口，坐标平铺为 float，末位参数 LineAlgorithm，默认 Bresenham）。
// 构建：cmake --build build --target tutorial_line_algorithms
// 运行：build/tutorials/LineAlgorithms --output tutorials/line-algorithms/output/line-algorithms.png
//
// 参数：--output <png>（默认 output/line-algorithms.png，目录不存在会自动创建）/ --help

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/png.hpp>
#include <gfx/render/raster.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using gfx::Canvas;
using gfx::Color;
using gfx::LineAlgorithm;
using gfx::rgb;

// ---- 配色 ----
const Color kBackground = rgb(16, 18, 26);
const Color kDivider    = rgb(60, 66, 80);
const Color kBase       = rgb(90, 170, 220);   // 线段（三幅图用同一基础色，便于看出"三者一致"）
const Color kStat       = rgb(255, 255, 255);  // 统计用（离屏单独渲染，不参与成图）
const Color kExtra      = rgb(240, 90, 200);   // 标注：本幅算法画出、而 Bresenham 没有的像素
const Color kMissing    = rgb(250, 205, 80);   // 标注：Bresenham 画出、而本幅算法没有的像素

// ---- 画布与面板布局 ----
constexpr int kCanvasW = 960;
constexpr int kCanvasH = 360;
constexpr int kPanelCount = 3;                 // Naive / DDA / Bresenham

struct Segment { int x0, y0, x1, y1; const char* name; };

// 同一组线段（面板内局部坐标，像素、y 向下）。刻意包含若干"直线恰穿过像素中心中线（严格半整数并列）"
// 的斜率 —— 这些是三种算法在整数端点下**唯一**可能出现 ±1 像素差别的情形（见 README 说明）。
const Segment kSegments[] = {
    { 30,  40, 290,  40, "水平"},
    { 60,  30,  60, 330, "垂直"},
    { 40,  50, 280, 290, "45 度"},
    { 30,  80, 270, 160, "缓斜 1/3"},
    {150,  30, 210, 210, "陡斜 3/1"},
    { 30, 120, 270,  60, "缓斜 -1/4 并列"},
    { 30, 300, 270, 220, "缓斜 -1/3"},
    {270,  40, 210, 200, "陡斜 -8/3 并列"},
    { 20,  20, 300, 320, "近 45 度 15/14 并列"},
    {160, 335, 161, 335, "长度 1"},
    {200, 340, 200, 340, "退化点"},
};
constexpr int kSegmentCount = static_cast<int>(sizeof(kSegments) / sizeof(kSegments[0]));

const LineAlgorithm kAlgos[kPanelCount] = {LineAlgorithm::Naive, LineAlgorithm::DDA, LineAlgorithm::Bresenham};

using PixelSet = std::vector<std::pair<int, int>>;   // 排序后按 (x, y) 升序

// 收集画布上等于指定颜色的所有像素（排序为 (x, y) 升序，供集合比对）
PixelSet collectPixels(const Canvas& c, const Color& color) {
    PixelSet v;
    for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x)
            if (c.pixelAt(x, y) == color) v.emplace_back(x, y);
    std::sort(v.begin(), v.end());
    return v;
}

// 两个已排序像素集合的对称差大小（= 两种算法不一致的像素数）
int symmetricDiffCount(const PixelSet& a, const PixelSet& b) {
    PixelSet d;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
    return static_cast<int>(d.size());
}

struct LineStat {
    const char* name;
    int counts[kPanelCount];
    int diffNaiveDda;
    int diffDdaBres;
    int diffNaiveBres;
    bool consistent;
};

void printUsage() {
    std::cout <<
        "用法：\n"
        "  LineAlgorithms [--output <out.png>]\n"
        "  LineAlgorithms --help\n"
        "\n"
        "  --output <file>   结果 PNG 路径（默认 output/line-algorithms.png；目录会自动创建）\n"
        "  --help            显示本帮助\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string outputPath = "output/line-algorithms.png";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        if (arg == "--output" && i + 1 < argc) { outputPath = argv[++i]; }
        else { std::cerr << "错误：无法识别的参数 " << arg << "（用 --help 查看用法）\n"; return 2; }
    }

    const int panelW = kCanvasW / kPanelCount;   // 每幅宽度（局部坐标 x ∈ [0, panelW)）

    // ---- ① 逐线段统计：三种算法各自的像素集合（在离屏画布上单独渲染，互不干扰）----
    std::vector<LineStat> stats;
    stats.reserve(kSegmentCount);
    for (int s = 0; s < kSegmentCount; ++s) {
        const Segment& seg = kSegments[s];
        PixelSet px[kPanelCount];
        Canvas scratch[kPanelCount] = {Canvas(panelW, kCanvasH), Canvas(panelW, kCanvasH), Canvas(panelW, kCanvasH)};
        for (int a = 0; a < kPanelCount; ++a) {
            gfx::drawLine(scratch[a], static_cast<float>(seg.x0), static_cast<float>(seg.y0),
                          static_cast<float>(seg.x1), static_cast<float>(seg.y1), kStat, kAlgos[a]);
            px[a] = collectPixels(scratch[a], kStat);
        }
        LineStat st;
        st.name = seg.name;
        for (int a = 0; a < kPanelCount; ++a) st.counts[a] = static_cast<int>(px[a].size());
        st.diffNaiveDda = symmetricDiffCount(px[0], px[1]);
        st.diffDdaBres = symmetricDiffCount(px[1], px[2]);
        st.diffNaiveBres = symmetricDiffCount(px[0], px[2]);
        st.consistent = (st.diffNaiveDda == 0 && st.diffDdaBres == 0 && st.diffNaiveBres == 0);
        stats.push_back(st);
    }

    // ---- ② 成图：三幅并排，各用其算法渲染同一组线段 ----
    Canvas canvas(kCanvasW, kCanvasH);
    canvas.clear(kBackground);
    for (int i = 1; i < kPanelCount; ++i)                       // 面板分隔线
        gfx::drawLine(canvas, static_cast<float>(i * panelW), 0.0f,
                      static_cast<float>(i * panelW), static_cast<float>(kCanvasH - 1), kDivider);

    for (int a = 0; a < kPanelCount; ++a) {
        const int ox = a * panelW;
        for (int s = 0; s < kSegmentCount; ++s) {               // ① 基础渲染（真实结果）
            const Segment& seg = kSegments[s];
            gfx::drawLine(canvas, static_cast<float>(seg.x0 + ox), static_cast<float>(seg.y0),
                          static_cast<float>(seg.x1 + ox), static_cast<float>(seg.y1), kBase, kAlgos[a]);
        }
        if (a == 2) continue;                                  // 以 Bresenham 为参照，只标注另两幅
        for (int s = 0; s < kSegmentCount; ++s) {               // ② 差异标注（相对 Bresenham）
            const Segment& seg = kSegments[s];
            Canvas mine(panelW, kCanvasH);
            gfx::drawLine(mine, static_cast<float>(seg.x0), static_cast<float>(seg.y0),
                          static_cast<float>(seg.x1), static_cast<float>(seg.y1), kStat, kAlgos[a]);
            Canvas ref(panelW, kCanvasH);
            gfx::drawLine(ref, static_cast<float>(seg.x0), static_cast<float>(seg.y0),
                          static_cast<float>(seg.x1), static_cast<float>(seg.y1), kStat, LineAlgorithm::Bresenham);
            const PixelSet minePx = collectPixels(mine, kStat);
            const PixelSet refPx = collectPixels(ref, kStat);
            for (const std::pair<int, int>& p : minePx)         // 本算法多出的像素 → 品红
                if (!std::binary_search(refPx.begin(), refPx.end(), p))
                    canvas.putPixel(p.first + ox, p.second, kExtra);
            for (const std::pair<int, int>& p : refPx)          // 本算法缺少的像素 → 琥珀
                if (!std::binary_search(minePx.begin(), minePx.end(), p))
                    canvas.putPixel(p.first + ox, p.second, kMissing);
        }
    }

    // ---- ③ 保存 PNG ----
    std::string error;
    if (!gfx::savePng(canvas, outputPath.c_str(), &error)) {
        std::cerr << "错误：保存 PNG 失败 —— " << error << "\n";
        return 5;
    }

    // ---- ④ 控制台统计（真实计算，非写死）----
    std::printf("三种画直线算法对照（同一组线段，面板局部坐标，y 向下）\n");
    std::printf("面板：左=Naive  中=DDA  右=Bresenham（Bresenham 为默认）\n\n");
    std::printf("%-24s %6s %5s %10s  %9s %9s %10s  %s\n",
                "线段", "Naive", "DDA", "Bresenham", "N<->D", "D<->B", "N<->B", "一致?");
    int diffLines = 0;
    for (const LineStat& st : stats) {
        std::printf("%-24s %6d %5d %10d  %9d %9d %10d  %s\n",
                    st.name, st.counts[0], st.counts[1], st.counts[2],
                    st.diffNaiveDda, st.diffDdaBres, st.diffNaiveBres,
                    st.consistent ? "一致" : "不一致");
        if (!st.consistent) ++diffLines;
    }
    std::printf("\n共 %d 条线段，差异线段数 = %d\n", kSegmentCount, diffLines);
    std::printf("标注：品红=%该算法多出的像素，琥珀=Bresenham 有而该算法缺的像素（仅整数端点下的并列取整情形）。\n");
    std::printf("已保存结果图：%s\n", outputPath.c_str());
    return 0;
}
