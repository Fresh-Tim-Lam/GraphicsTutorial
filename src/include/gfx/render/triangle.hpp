// triangle.hpp —— gfx 三角形光栅化：采样判定、重心坐标、包围盒/增量填充、抗锯齿
//
// @file 在 Canvas 上做**三角形**光栅化（讲义《第五讲 光栅化、采样与抗锯齿》的路线）：
//       - 采样判定：像素中心 (x+0.5, y+0.5) 是否落在三角形内（三个边函数/叉积），
//         **边界按 top-left 规则唯一归属** —— 与"两个三角形共享一条边"不重不漏；
//       - 重心坐标：为顶点属性插值（颜色/深度）提供权重；
//       - 填充：**唯一入口** fillTriangle(canvas, a, b, c, color[, mode[, samplesPerAxis[, background]]])，
//         由模式枚举 TriangleFillMode 分派——SolidScanline 增量式逐扫描线 + 点采样（默认，最快）、
//         SolidBBox 包围盒 + 逐像素测试 + 点采样（最直白，对照）、CoverageAA 覆盖率抗锯齿、SSAA 超采样抗锯齿；
//         对当前模式**无意义的参数会被警告并忽略**（见下）。
//
// @note 与 gfx/render/raster.hpp 的关系：raster 的 ScanlineFiller 填充**任意多边形**（边表 ET + 活性边表 AET）；
//       本文件填充**三角形**（凸 ⇒ 每行一个连续区间，可用更省的方式）。两者都遵循
//       "像素中心采样 + 一致的边界规则"，但实现路线不同（教科书经典 vs GPU 式采样）。
// @note 坐标：像素坐标（浮点，y 向下），与世界坐标的换算由 View 负责（gfx/render/view.hpp）。
//
// 用途：三角形填充（含抗锯齿）、为 3D 线框/面片打基础、演示"点采样 vs 超采样"。
// 目标读者：gfx 光栅化模块使用者与实现者（#include <gfx/render/triangle.hpp>）。
// 关联文档：docs/design/adr-0008-triangle-raster.md；gfx/render/raster.hpp（多边形扫描线）；gfx/render/depth.hpp（深度测试）
//
// 状态：新增（2026-09-16）。对应讲义第五讲。

#ifndef GFX_TRIANGLE_HPP
#define GFX_TRIANGLE_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/core/color.hpp"
#include "gfx/math/vec.hpp"
#include "gfx/render/raster.hpp"   // 复用 Span（一条扫描线上的闭区间）

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace gfx {

// ============================ 边函数与内部判定 ============================

// 边函数（二维叉积的 z 分量）：(b − a) × (p − a)。
// @note 符号表示 p 在**有向边** a→b 的哪一侧；三条边的边函数**同号** ⇒ 点在三角形内
//       （讲义 p40 的"三个叉积"判定）。
inline float edgeFunction(const math::Vec2& a, const math::Vec2& b, const math::Vec2& p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

namespace detail {

// top-left 规则（讲义 p41"边界情况"）：采样点恰落在边上时，只有 **top 边**或**left 边**接受它。
// @note 屏幕坐标 y 向下 ⇒ 判据为：向下走的边（dy > 0）算 left；水平且向左走的边（dy == 0 且 dx < 0）算 top。
//       于是相邻三角形在共享边上**恰好一方接受**，填充既不重不漏（测试锁定）。
// 当边（射线）方向是>0 <=pi时，返回true
inline bool isTopLeftEdge(const math::Vec2& a, const math::Vec2& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

} // namespace detail

// 点是否在三角形内（**含边界**，边界按 top-left 规则唯一归属）。
// @param p 待测点（像素坐标，浮点；通常取像素中心）
// @note 顶点顺序任意：内部先按有向面积把三角形归一化成"边的内侧为正"，故不要求 CW/CCW。
inline bool insideTriangle(const math::Vec2& p, math::Vec2 a, math::Vec2 b, math::Vec2 c) {
    if (edgeFunction(a, b, c) == 0.0f) return false;      // 退化三角形
    if (edgeFunction(a, b, c) < 0.0f) std::swap(b, c);    //（b-a) x (c-a)<0说明向量ab在ac的左侧,
    //要交换b、c位置保证a->b->c是逆时针

    const float e0 = edgeFunction(a, b, p);//（b-a) x (p-a)>0说明p在ab的右侧
    const float e1 = edgeFunction(b, c, p);
    const float e2 = edgeFunction(c, a, p);

    if (e0 > 0.0f && e1 > 0.0f && e2 > 0.0f) return true;         // 严格内部（逆时针的右侧）
    if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) return false;        // 至少一侧在外

    // 落在边界上：只接受 top-left 边
    return (e0 == 0.0f && detail::isTopLeftEdge(a, b))
        || (e1 == 0.0f && detail::isTopLeftEdge(b, c))
        || (e2 == 0.0f && detail::isTopLeftEdge(c, a));
}

// ============================ 重心坐标 ============================

// 重心坐标：p = wa·a + wb·b + wc·c，且 wa + wb + wc = 1。
// @return 三角形未退化时写入 out 并返回 true；退化（面积为 0）返回 false
// @note 用途：按权重**插值顶点属性**（颜色、深度、法线…）；权重全正 ⇒ 点在内部，
//       某个为 0 ⇒ 点在该边所在直线上。
inline bool barycentric(const math::Vec2& p, const math::Vec2& a, const math::Vec2& b,
                        const math::Vec2& c, math::Vec3& out) {
    const float area = edgeFunction(a, b, c);
    if (area == 0.0f) return false;
    const float inv = 1.0f / area;
    out = math::Vec3(edgeFunction(b, c, p) * inv,
                     edgeFunction(c, a, p) * inv,
                     edgeFunction(a, b, p) * inv);
    return true;
}

// ============================ 填充：模式枚举与唯一入口 ============================

// 三角形填充方式：同一件事（把三角形填到画布）的不同做法。
// 前两者只影响"哪些像素被覆盖"，后两者在此基础上做抗锯齿（更高代价换更平滑的边界）。
enum class TriangleFillMode {
    SolidScanline,   // 增量式逐扫描线 + 点采样 —— 默认，最快
    SolidBBox,       // 包围盒 + 逐像素 inside test + 点采样 —— 最直白，用于对照
    CoverageAA,      // 覆盖率抗锯齿：按像素内覆盖面积 alpha 叠色
    SSAA,            // 超采样：渲染 factor 倍大图后下采样平均
};

// 填充诊断开关：默认开启；批量渲染/测试时可关掉以免刷屏。
// @note 仅当为 true 时，fillTriangle 才会对"对当前模式无意义的参数"打印警告。
inline bool& triangleFillWarningsEnabled() { static bool enabled = true; return enabled; }

namespace detail {

// 填充模式名（用于警告文案，便于定位）。
inline const char* triangleFillModeName(TriangleFillMode mode) {
    switch (mode) {
        case TriangleFillMode::SolidScanline: return "SolidScanline";
        case TriangleFillMode::SolidBBox:     return "SolidBBox";
        case TriangleFillMode::CoverageAA:    return "CoverageAA";
        case TriangleFillMode::SSAA:          return "SSAA";
    }
    return "Unknown";
}

// 参数对当前模式无意义时打印**一行**中文警告；受 triangleFillWarningsEnabled 开关控制。
inline void warnParameterIgnored(const char* param, TriangleFillMode mode) {
    if (triangleFillWarningsEnabled())
        std::fprintf(stderr, "[gfx] fillTriangle: 参数 %s 对填充方式 %s 无意义，已忽略\n",
                     param, triangleFillModeName(mode));
}

// ① 包围盒 + 逐像素 inside test（讲义 p42 的做法）：对包围盒内每个像素做点内测试。
// @param a/b/c 三个顶点（像素坐标，浮点；顺序任意）
// @return 三角形未退化返回 true；退化（面积为 0）返回 false（不绘制）
// @note 采样点 = 像素中心；边界按 top-left 规则唯一归属。本质是逐像素测试，**不提供** step 版本。
inline bool fillTriangleBBox(Canvas& canvas, const math::Vec2& a, const math::Vec2& b,
                             const math::Vec2& c, const Color& color) {
    if (edgeFunction(a, b, c) == 0.0f) return false;

    const float minX = std::min({a.x, b.x, c.x});
    const float maxX = std::max({a.x, b.x, c.x});
    const float minY = std::min({a.y, b.y, c.y});
    const float maxY = std::max({a.y, b.y, c.y});

    const int xBegin = static_cast<int>(std::floor(minX));
    const int xEnd = static_cast<int>(std::ceil(maxX));
    const int yBegin = static_cast<int>(std::floor(minY));
    const int yEnd = static_cast<int>(std::ceil(maxY));

    for (int y = yBegin; y < yEnd; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = xBegin; x < xEnd; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            if (insideTriangle(math::Vec2(px, py), a, b, c)) canvas.putPixel(x, y, color);
        }
    }
    return true;
}

} // namespace detail

// 三角形填充器：**增量式**逐扫描线推进（讲义 p43），与 raster 的 ScanlineFiller 对称、可 step 演示。
// @note 为什么能更快：三角形是凸的 ⇒ 每条扫描线上被覆盖的像素必是**一个连续区间**，
//       于是只需算出该行的左右边界，不必逐像素做 inside test。
// @note 包围盒法本质是逐像素测试，**不提供** step 版本；需要逐行推进请用本类。
// @note 边界口径与 ScanlineFiller 一致：y 方向半开 [yMin, yMax)、x 方向左闭右开 [xl, xr)。
class TriangleFiller {
private:
    struct Edge {
        float yMin = 0.0f;
        float yMax = 0.0f;
        float xAtYMin = 0.0f;
        float invSlope = 0.0f;
    };

    std::vector<Edge> edges_;
    std::vector<Span> spans_;
    int firstScanline_ = 0;
    int lastScanline_ = -1;
    int scanline_ = 0;
    bool running_ = false;

public:
    // 用三个顶点（像素坐标，浮点；顺序任意）重建；退化三角形返回 false。
    bool begin(const math::Vec2& a, const math::Vec2& b, const math::Vec2& c) {
        edges_.clear();
        spans_.clear();
        running_ = false;
        if (edgeFunction(a, b, c) == 0.0f) return false;

        const math::Vec2 tri[3] = {a, b, c};
        float minY = tri[0].y, maxY = tri[0].y;
        for (const math::Vec2& v : tri) {
            minY = std::min(minY, v.y);
            maxY = std::max(maxY, v.y);
        }
        firstScanline_ = static_cast<int>(std::ceil(minY - 0.5f));
        lastScanline_ = static_cast<int>(std::ceil(maxY - 0.5f)) - 1;
        scanline_ = firstScanline_ - 1;
        if (lastScanline_ < firstScanline_) return true;   // 薄于一个像素：无扫描线可填

        // 三条边（跳过水平边），记录 yMin/yMax/xAtYMin/斜率倒数（与 ScanlineFiller 同口径）
        for (int i = 0; i < 3; ++i) {
            const math::Vec2& p = tri[i];
            const math::Vec2& q = tri[(i + 1) % 3];
            if (q.y == p.y) continue;
            const math::Vec2 lo = (p.y < q.y) ? p : q;
            const math::Vec2 hi = (p.y < q.y) ? q : p;
            Edge e;
            e.yMin = lo.y;
            e.yMax = hi.y;
            e.xAtYMin = lo.x;
            e.invSlope = (hi.x - lo.x) / (hi.y - lo.y);
            edges_.push_back(e);
        }
        running_ = true;
        return true;
    }

    int firstScanline() const { return firstScanline_; }
    int lastScanline() const { return lastScanline_; }
    int scanline() const { return scanline_; }
    const std::vector<Span>& spans() const { return spans_; }
    bool running() const { return running_; }

    // 推进到下一条扫描线并算出该行区间；返回 false 表示结束。
    bool step() {
        if (!running_) return false;
        ++scanline_;
        if (scanline_ > lastScanline_) {
            running_ = false;
            spans_.clear();
            return false;
        }

        const float yc = static_cast<float>(scanline_) + 0.5f;   // 采样点 = 像素中心
        float lo = 0.0f, hi = 0.0f;
        bool any = false;
        for (const Edge& e : edges_) {
            if (yc < e.yMin || yc >= e.yMax) continue;           // y 方向半开区间
            const float x = e.xAtYMin + e.invSlope * (yc - e.yMin);
            if (!any) { lo = hi = x; any = true; }
            else { lo = std::min(lo, x); hi = std::max(hi, x); }
        }

        spans_.clear();
        if (any) {
            const int xs = static_cast<int>(std::ceil(lo - 0.5f));        // 左闭
            const int xe = static_cast<int>(std::ceil(hi - 0.5f)) - 1;    // 右开
            if (xs <= xe) spans_.push_back(Span{xs, xe});
        }
        return true;
    }

    // 把当前扫描线的区间写入画布（越界忽略）
    void fillCurrentRow(Canvas& canvas, const Color& color) const {
        if (!running_) return;
        for (const Span& s : spans_)
            for (int x = s.xStart; x <= s.xEnd; ++x) canvas.putPixel(x, scanline_, color);
    }
};

namespace detail {

// ② 增量式逐扫描线（讲义 p43）：内部即 TriangleFiller 逐行推进 —— 与 ScanlineFiller 同样的"单一实现"约定。
inline bool fillTriangleScanline(Canvas& canvas, const math::Vec2& a, const math::Vec2& b,
                                 const math::Vec2& c, const Color& color) {
    TriangleFiller filler;
    if (!filler.begin(a, b, c)) return false;
    while (filler.step()) filler.fillCurrentRow(canvas, color);
    return true;
}

} // namespace detail

// ============================ 抗锯齿 ============================

// 像素 (pixelX, pixelY) 被三角形覆盖的**面积比例**（0..1）。
// @param samplesPerAxis 每个方向上的子采样数（共 N×N 个子样本；N 越大越接近真实面积）
// @note 这是"预滤波"的离散近似（讲义 p107-109）：覆盖率 = 落在三角形内的子样本数 / 总子样本数。
//       子样本本身也走 top-left 规则 ⇒ 共享边上两个三角形的覆盖率之和恰为 1。
inline float coverage(float pixelX, float pixelY, const math::Vec2& a, const math::Vec2& b,
                      const math::Vec2& c, int samplesPerAxis = 4) {
    if (samplesPerAxis <= 0 || edgeFunction(a, b, c) == 0.0f) return 0.0f;

    const float step = 1.0f / static_cast<float>(samplesPerAxis);
    int inside = 0;
    for (int sy = 0; sy < samplesPerAxis; ++sy) {
        for (int sx = 0; sx < samplesPerAxis; ++sx) {
            const float ox = (static_cast<float>(sx) + 0.5f) * step;
            const float oy = (static_cast<float>(sy) + 0.5f) * step;
            if (insideTriangle(math::Vec2(pixelX + ox, pixelY + oy), a, b, c)) ++inside;
        }
    }
    const int total = samplesPerAxis * samplesPerAxis;
    return static_cast<float>(inside) / static_cast<float>(total);
}

namespace detail {

// ③ 覆盖率抗锯齿填充（**覆盖面积着色**）：对包围盒内每个像素按覆盖率**叠色**。
// @note 与点采样的差别：像素可以取"中间值"（讲义 p73/p106），边界不再呈阶梯状。
//       覆盖率 0 的像素完全不碰，覆盖率 1 的像素等价于 putPixel（blend 内部走整数分支）。
//
// @note 性能优化（**输出逐像素不变**）：包围盒内绝大多数像素要么**完全在三角形内**（覆盖率恒为 1）、
//       要么**完全在三角形外**（覆盖率恒为 0），只有"可能跨界"的像素才需要跑满 N² 次子采样。
//       本实现先用三条**有向边函数**对每个像素做两次快速判定，只有判定落空时才调用 coverage()：
//         - 完全在内：三条边函数在像素**闭包** [x,x+1]×[y,y+1] 上的**最小值**均 > 0 ⇒ 闭包严格落在三角形内
//           ⇒ 覆盖用的子样本（都落在闭包内）全部在内 ⇒ 覆盖率恒为 1（凸集性质）。
//         - 完全在外：存在一条边函数在闭包上的**最大值** < 0 ⇒ 整个闭包在该边外侧 ⇒ 所有子样本都在外
//           ⇒ 覆盖率恒为 0 ⇒ 直接跳过（不调用 coverage()）。
//       边函数是仿射函数，其在轴对齐闭包上的 min/max 取在**四个角点**，且恰为"中心值 ∓ 半跨度"，
//       半跨度 = 0.5·(|∂f/∂x| + |∂f/∂y|)。为免浮点舍入把"擦边"像素误判（覆盖判定本身也用 float 的
//       edgeFunction），比较时留出随坐标平方增长的裕度 eps；裕度只会让更多像素回退到 coverage()，
//       不影响正确性，只略降速度。
inline bool fillTriangleCoverageAA(Canvas& canvas, const math::Vec2& a, const math::Vec2& b,
                                   const math::Vec2& c, const Color& color, int samplesPerAxis = 4) {
    if (edgeFunction(a, b, c) == 0.0f) return false;
    if (samplesPerAxis <= 0) return true;   // 与原实现一致：coverage 恒为 0，一个像素也不写

    const int xBegin = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
    const int xEnd = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
    const int yBegin = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
    const int yEnd = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));

    // 归一化顶点方向（与 insideTriangle 内部一致）：使三角形内部对三条有向边均为正。
    math::Vec2 v[3] = {a, b, c};
    if (edgeFunction(a, b, c) < 0.0f) std::swap(v[1], v[2]);

    // 每条有向边的偏导（由中心值推闭包上的 min/max）与半个跨度。
    float halfSpan[3];
    for (int i = 0; i < 3; ++i) {
        const math::Vec2& p = v[i];
        const math::Vec2& q = v[(i + 1) % 3];
        const float gx = p.y - q.y;   // ∂edgeFunction(p,q,·)/∂x
        const float gy = q.x - p.x;   // ∂edgeFunction(p,q,·)/∂y
        halfSpan[i] = 0.5f * (std::fabs(gx) + std::fabs(gy));
    }
    // 浮点安全裕度：子样本的边函数值同样用 float 计算，舍入误差与坐标平方同阶。
    const float maxCoord = std::max({std::fabs(a.x), std::fabs(a.y), std::fabs(b.x), std::fabs(b.y),
                                     std::fabs(c.x), std::fabs(c.y), 1.0f});
    const float eps = 1e-4f + 1e-5f * maxCoord * maxCoord;

    for (int y = yBegin; y < yEnd; ++y) {
        const float cy = static_cast<float>(y) + 0.5f;
        for (int x = xBegin; x < xEnd; ++x) {
            const float cx = static_cast<float>(x) + 0.5f;
            bool fullyInside = true;
            bool fullyOutside = false;
            for (int i = 0; i < 3; ++i) {
                const float f = edgeFunction(v[i], v[(i + 1) % 3], math::Vec2(cx, cy));
                if (f - halfSpan[i] <= eps) fullyInside = false;   // 闭包未完全进入本边内侧
                if (f + halfSpan[i] < -eps) { fullyOutside = true; break; }
            }
            if (fullyOutside) continue;                            // 覆盖率恒为 0：跳过
            if (fullyInside) {                                     // 覆盖率恒为 1
                canvas.blendPixel(x, y, Color(color.r, color.g, color.b, 255));
                continue;
            }
            const float cov = coverage(static_cast<float>(x), static_cast<float>(y), a, b, c, samplesPerAxis);
            if (cov <= 0.0f) continue;
            const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround(cov * 255.0f));
            canvas.blendPixel(x, y, Color(color.r, color.g, color.b, alpha));
        }
    }
    return true;
}

// ④ 超采样填充（SSAA）：把三角形渲染到 factor 倍分辨率的离屏画布，再**下采样平均**（讲义 p110-119）。
// @param factor 超采样倍数（每像素 factor×factor 个子样本）
// @param background 离屏画布的背景色（平均时会被计入，应与目标画布的底色一致）
// @note 与覆盖率抗锯齿（fillTriangleCoverageAA）的关系：效果近似（都逼近"像素内覆盖面积"），但 SSAA 走的是
//       "真的渲染大图 → 逐块平均"的两步流程，更贴合讲义的描述；代价是 factor² 倍内存与时间。
// @note 本函数**直接写入**目标像素（不做 alpha 合成）：语义是"三角形叠在 background 上的平均结果"。
inline bool fillTriangleSSAA(Canvas& canvas, const math::Vec2& a, const math::Vec2& b,
                             const math::Vec2& c, const Color& color, int factor = 4,
                             const Color& background = Color(0, 0, 0, 255)) {
    if (factor <= 1 || edgeFunction(a, b, c) == 0.0f)
        return fillTriangleScanline(canvas, a, b, c, color);

    const int hiW = canvas.width() * factor;
    const int hiH = canvas.height() * factor;
    Canvas hi(hiW, hiH);
    hi.clear(background);

    const float f = static_cast<float>(factor);
    fillTriangleScanline(hi,
                         math::Vec2(a.x * f, a.y * f),
                         math::Vec2(b.x * f, b.y * f),
                         math::Vec2(c.x * f, c.y * f),
                         color);

    const unsigned n = static_cast<unsigned>(factor) * static_cast<unsigned>(factor);
    // 只更新三角形**包围盒**覆盖的像素：若遍历整张画布，会把画布上其它内容一并抹掉（且大量无用功）
    const int xBegin = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int xEnd = std::min(canvas.width(), static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int yBegin = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int yEnd = std::min(canvas.height(), static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            unsigned sr = 0, sg = 0, sb = 0;
            for (int sy = 0; sy < factor; ++sy)
                for (int sx = 0; sx < factor; ++sx) {
                    const Color p = hi.pixelAt(x * factor + sx, y * factor + sy);
                    sr += p.r; sg += p.g; sb += p.b;
                }
            canvas.putPixel(x, y, Color(static_cast<std::uint8_t>(sr / n),
                                        static_cast<std::uint8_t>(sg / n),
                                        static_cast<std::uint8_t>(sb / n), 255));
        }
    }
    return true;
}

} // namespace detail

// 填充三角形（唯一对外入口）。
// @param a/b/c 三个顶点（像素坐标，浮点；顺序任意）
// @param mode          填充方式，默认 SolidScanline（增量式 + 点采样，最快）
// @param samplesPerAxis 每轴子样本数（CoverageAA / SSAA 用）；**0 表示未指定**，此时按该模式默认值（4）
// @param background    离屏背景色（仅 SSAA 用）；**nullptr 表示未指定**，此时用不透明黑 (0,0,0,255)
// @return 三角形未退化返回 true；退化（面积为 0）返回 false
// @note 对当前 mode 无意义的参数会被**忽略并给出警告**（见下）。
//       - SolidScanline / SolidBBox：只用 color + 顶点；samplesPerAxis、background 均无意义（若显式指定 ⇒ 警告并忽略）；
//       - CoverageAA：用 color + samplesPerAxis（background 无意义 ⇒ 警告并忽略）；
//       - SSAA：用 color + samplesPerAxis（= factor）+ background（两者都可选）。
// @note 为什么用哨兵值：C++ 默认参数无法区分"调用者没传"和"传了默认值"，故用 0 / nullptr 表示**未指定**；
//       只有显式传入了"非法值"才判定为"调用者确实想指定它"，再据 mode 决定采纳或警告。
// @note 采样点 = 像素中心。两种点采样覆盖判定（SolidScanline 半开区间 vs SolidBBox top-left）
//       对"采样点恰落在边上"的三角形边界像素归属可能不同（见 docs/design/adr-0008）。
//       需要**可 step 的逐行推进**（动画/调试演示）请直接用 TriangleFiller（增量式专属）。
// @note 警告开关：triangleFillWarningsEnabled()，默认开启；批量渲染/测试时可关掉以免刷屏。
inline bool fillTriangle(Canvas& canvas, const math::Vec2& a, const math::Vec2& b,
                         const math::Vec2& c, const Color& color,
                         TriangleFillMode mode = TriangleFillMode::SolidScanline,
                         int samplesPerAxis = 0,
                         const Color* background = nullptr) {
    switch (mode) {
        case TriangleFillMode::SolidBBox:
            if (samplesPerAxis > 0) detail::warnParameterIgnored("samplesPerAxis", mode);
            if (background != nullptr) detail::warnParameterIgnored("background", mode);
            return detail::fillTriangleBBox(canvas, a, b, c, color);
        case TriangleFillMode::CoverageAA:
            if (background != nullptr) detail::warnParameterIgnored("background", mode);
            return detail::fillTriangleCoverageAA(canvas, a, b, c, color,
                                                  samplesPerAxis > 0 ? samplesPerAxis : 4);
        case TriangleFillMode::SSAA:
            return detail::fillTriangleSSAA(canvas, a, b, c, color,
                                            samplesPerAxis > 0 ? samplesPerAxis : 4,
                                            background != nullptr ? *background : Color(0, 0, 0, 255));
        case TriangleFillMode::SolidScanline:
        default:
            if (samplesPerAxis > 0) detail::warnParameterIgnored("samplesPerAxis", mode);
            if (background != nullptr) detail::warnParameterIgnored("background", mode);
            return detail::fillTriangleScanline(canvas, a, b, c, color);
    }
}

} // namespace gfx

#endif // GFX_TRIANGLE_HPP
