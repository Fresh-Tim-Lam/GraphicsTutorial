// raster.hpp —— gfx 光栅化：Bresenham 画线与扫描线多边形填充（ET/AET）
//
// @file 在 Canvas 上做基础图元光栅化：
//       - 画线：整数 Bresenham（中点法），只用整数加/减与比较，无浮点、无除法；
//       - 多边形填充：扫描线算法，**显式**建模"边表（ET）+ 活性边表（AET）"，
//         逐条扫描线求交点、排序、成对填充；并支持逐扫描线推进（供动画/调试演示）。
//
// @note 采样规则（本文件统一，用以避免漏填/重填；对应作业 1 的"一致的半开区间规则"）：
//       ① 像素 i 覆盖区间 [i, i+1)，取其**中心** i+0.5 作为采样点；
//       ② 边在 y 方向按**半开区间** [yMin, yMax) 参与求交 ⇒ 共享顶点、局部极值点只被计一次；
//       ③ 水平边不产生交点，直接跳过（闭合性由 ② 保证：极值点处两条边同时进/同时出）；
//       ④ 一条扫描线内，交点区间按**左闭右开** [xl, xr) 取像素 ⇒ 相邻多边形共享边时无缝且不重叠。
// @note 坐标：本文件一律使用**像素坐标**（浮点，y 向下，与 Canvas 一致）；
//       "世界坐标 → 像素坐标"的映射（缩放/平移/y 翻转）属调用方职责。
//
// 用途：填充多边形（凸/凹）、画多边形边界；作为 putPixel 之上的首批绘制原语。
// 目标读者：gfx 光栅化模块使用者与实现者（#include <gfx/raster.hpp>）。
// 关联文档：docs/design/adr-0005-raster-polygon-fill.md；canvas.hpp；color.hpp
//
// 状态：新增（2026-09-14）。对应作业 1：多边形扫描转换。

#ifndef GFX_RASTER_HPP
#define GFX_RASTER_HPP

#include "canvas.hpp"
#include "color.hpp"
#include "math/vec.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace gfx {

// 几何判定的绝对容差（像素坐标下 1e-4 px 远小于半像素，不会影响正常图形）
inline constexpr float POLYGON_POINT_EPS = 1e-4f;   // 顶点重合判定
inline constexpr float POLYGON_CROSS_EPS = 1e-4f;   // 共线（叉积）判定
inline constexpr float POLYGON_AREA_EPS = 1e-3f;    // 面积退化判定

// ============================ 画线 ============================

// Bresenham 画线（整数中点法）：画 (x0,y0) → (x1,y1)，含两端点。
// @note 全程整数运算（误差项 + 一阶增量），无浮点与除法；越界像素由 Canvas::putPixel 直接忽略。
//       该形式同时覆盖八个卦限（由 e2 的两个判断决定走 x 步、y 步还是同时走），无需分支分情形。
inline void drawLine(Canvas& canvas, int x0, int y0, int x1, int y1, const Color& color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);   // 取负，便于与 e2 统一比较
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;                   // 误差项（标准 Bresenham 统一形式）
    for (;;) {
        canvas.putPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }   // 水平推进
        if (e2 <= dx) { err += dx; y0 += sy; }   // 垂直推进
    }
}

// 画多边形边界（自动闭合：最后一个顶点连回第一个顶点）。
// @note 顶点为像素坐标（浮点）；轮廓按**最近像素**（std::lround）落点，与填充的"像素中心采样"
//       最多相差 1 像素 —— 故调用方应**先填充、后画轮廓**，让边界覆盖在填充层之上。
inline void drawPolygonOutline(Canvas& canvas, const std::vector<math::Vec2>& polygon, const Color& color) {
    if (polygon.size() < 2) return;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const math::Vec2& a = polygon[i];
        const math::Vec2& b = polygon[(i + 1) % polygon.size()];
        drawLine(canvas,
                 static_cast<int>(std::lround(a.x)), static_cast<int>(std::lround(a.y)),
                 static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)),
                 color);
    }
}

// ============================ 多边形：区间、错误、校验 ============================

// 一条扫描线上的填充区间（**闭区间**，单位像素，含两端）
struct Span {
    int xStart = 0;
    int xEnd = 0;
    int length() const { return xEnd - xStart + 1; }
};

// 多边形输入校验结果
enum class PolygonError {
    None,              // 合法
    TooFewVertices,    // 规范化后顶点数不足 3
    DegenerateArea,    // 顶点共线：有向面积 ≈ 0
    SelfIntersecting,  // 自交：不是简单多边形
};

inline const char* polygonErrorText(PolygonError e) {
    switch (e) {
        case PolygonError::None:             return "多边形合法";
        case PolygonError::TooFewVertices:   return "顶点数不足 3（已剔除重复的闭合点与相邻重合点）";
        case PolygonError::DegenerateArea:   return "顶点共线：多边形面积为 0，无法填充";
        case PolygonError::SelfIntersecting: return "多边形自交：不是简单多边形";
    }
    return "未知错误";
}

namespace detail {

inline bool samePoint(const math::Vec2& a, const math::Vec2& b) {
    return std::fabs(a.x - b.x) <= POLYGON_POINT_EPS && std::fabs(a.y - b.y) <= POLYGON_POINT_EPS;
}

// 二维叉积（z 分量）
inline float cross2(const math::Vec2& a, const math::Vec2& b) { return a.x * b.y - a.y * b.x; }

// 点 c 相对有向直线 a→b 的位置：+1 左、-1 右、0 共线
inline int orient(const math::Vec2& a, const math::Vec2& b, const math::Vec2& c) {
    const float v = cross2(math::Vec2(b.x - a.x, b.y - a.y), math::Vec2(c.x - a.x, c.y - a.y));
    if (v > POLYGON_CROSS_EPS) return 1;
    if (v < -POLYGON_CROSS_EPS) return -1;
    return 0;
}

// 共线时判断点 p 是否落在线段 ab 的包围盒内
inline bool onSegment(const math::Vec2& a, const math::Vec2& b, const math::Vec2& p) {
    return p.x >= std::min(a.x, b.x) - POLYGON_POINT_EPS && p.x <= std::max(a.x, b.x) + POLYGON_POINT_EPS &&
           p.y >= std::min(a.y, b.y) - POLYGON_POINT_EPS && p.y <= std::max(a.y, b.y) + POLYGON_POINT_EPS;
}

// 严格跨越：两条线段真正交叉（交点严格落在线段内部，不含共线与端点接触）
inline bool properCross(const math::Vec2& p1, const math::Vec2& p2,
                        const math::Vec2& p3, const math::Vec2& p4) {
    const int d1 = orient(p3, p4, p1);
    const int d2 = orient(p3, p4, p2);
    const int d3 = orient(p1, p2, p3);
    const int d4 = orient(p1, p2, p4);
    return (d1 * d2 < 0) && (d3 * d4 < 0);
}

// 退化接触：共线重叠，或某端点落在另一条线段上
inline bool degenerateTouch(const math::Vec2& p1, const math::Vec2& p2,
                            const math::Vec2& p3, const math::Vec2& p4) {
    return (orient(p3, p4, p1) == 0 && onSegment(p3, p4, p1)) ||
           (orient(p3, p4, p2) == 0 && onSegment(p3, p4, p2)) ||
           (orient(p1, p2, p3) == 0 && onSegment(p1, p2, p3)) ||
           (orient(p1, p2, p4) == 0 && onSegment(p1, p2, p4));
}

// 线段相交（含共线重叠与端点接触）——用于自交检测
inline bool segmentsIntersect(const math::Vec2& p1, const math::Vec2& p2,
                              const math::Vec2& p3, const math::Vec2& p4) {
    return properCross(p1, p2, p3, p4) || degenerateTouch(p1, p2, p3, p4);
}

// 对**已规范化**的顶点序列做几何校验（顶点数/面积/自交）
inline PolygonError validateNormalized(const std::vector<math::Vec2>& poly);

} // namespace detail

// 规范化顶点序列：① 去掉相邻重合点（零长度边）；② 去掉末尾与首点重合的闭合点。
// @note 允许输入"末尾重复首点"（作业说明允许的两种闭合写法之一）；本函数是各入口的统一前置处理。
inline std::vector<math::Vec2> normalizePolygon(const std::vector<math::Vec2>& raw) {
    std::vector<math::Vec2> out;
    out.reserve(raw.size());
    for (const math::Vec2& v : raw) {
        if (!out.empty() && detail::samePoint(v, out.back())) continue;   // 零长度边
        out.push_back(v);
    }
    while (out.size() >= 2 && detail::samePoint(out.front(), out.back())) out.pop_back();   // 闭合点
    return out;
}

// 有向面积（鞋带公式）：
// @note 在 **y 轴向上**的数学坐标系里 > 0 表示逆时针（CCW），< 0 表示顺时针；
//       若顶点取自屏幕坐标（y 向下），符号含义与之相反。
inline float polygonSignedArea(const std::vector<math::Vec2>& polygon) {
    float sum = 0.0f;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const math::Vec2& a = polygon[i];
        const math::Vec2& b = polygon[(i + 1) % polygon.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return 0.5f * sum;
}

// 校验多边形（接受原始顶点序列，内部先规范化）：
//   顶点数 ≥ 3、有向面积非 0（至少三个不共线顶点）、非自交（简单多边形）。
inline PolygonError validatePolygon(const std::vector<math::Vec2>& polygon) {
    return detail::validateNormalized(normalizePolygon(polygon));
}

namespace detail {

inline PolygonError validateNormalized(const std::vector<math::Vec2>& poly) {
    if (poly.size() < 3) return PolygonError::TooFewVertices;

    // 自交检测：只检查**不相邻**的边（相邻边共享顶点，不算相交）。
    // 分两类结果，是为了给出最贴切的错误信息：
    //   - 严格跨越 ⇒ 真自交（如蝴蝶结），优先报"自交"；
    //   - 仅在退化接触（顶点落在非相邻边上、共线重叠）⇒ 先看面积是否退化，再报"自交"。
    const std::size_t n = poly.size();
    bool cross = false;
    bool touch = false;
    for (std::size_t i = 0; i < n && !cross; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i + 1) continue;                 // 相邻边
            if (i == 0 && j == n - 1) continue;       // 首尾相邻
            const math::Vec2& a1 = poly[i];
            const math::Vec2& a2 = poly[(i + 1) % n];
            const math::Vec2& b1 = poly[j];
            const math::Vec2& b2 = poly[(j + 1) % n];
            if (properCross(a1, a2, b1, b2)) { cross = true; break; }
            if (degenerateTouch(a1, a2, b1, b2)) touch = true;
        }
    }

    if (cross) return PolygonError::SelfIntersecting;
    if (std::fabs(polygonSignedArea(poly)) <= POLYGON_AREA_EPS) return PolygonError::DegenerateArea;
    if (touch) return PolygonError::SelfIntersecting;
    return PolygonError::None;
}

} // namespace detail

// ============================ 扫描线填充（ET + AET） ============================

// 扫描线填充器：显式建模"边表（ET）+ 活性边表（AET）"，可逐条扫描线推进（动画/调试）。
//
// 算法步骤（与作业 1 的流程一一对应）：
//   1. 规范化顶点并校验；自动闭合（最后一点连回第一点）。
//   2. 构造边表 ET：跳过水平边；其余每条边按"进入扫描线"分桶。
//   3. 逐条扫描线推进 y：把 ET[y] 的新边插入 AET，把已越过 yMax 的边从 AET 移除。
//   4. 计算 AET 中各边与本扫描线的交点 x，按 x 升序排序。
//   5. 交点**成对**取出 ⇒ 若干填充区间（左闭右闭），交由调用方逐像素写入。
//
// @note 交点 x 由边参数**直接计算**（x = xAtYMin + invSlope·(yc − yMin)，yc = y + 0.5），
//       不做"每行累加 invSlope"的增量更新 ⇒ 无累积误差。
class ScanlineFiller {
public:
    // 边表条目（ET）
    struct Edge {
        float yMin = 0.0f;        // 下端 y
        float yMax = 0.0f;        // 上端 y（半开区间的开端）
        float xAtYMin = 0.0f;     // 在 y = yMin 处的 x
        float invSlope = 0.0f;    // dx/dy
        int enterScanline = 0;    // 首个满足 yMin <= y+0.5 的整数扫描线
        int leaveScanline = 0;    // 首个不再满足 y+0.5 < yMax 的整数扫描线
    };

    // 用原始顶点序列（像素坐标，浮点）重建 ET；成功返回 true。
    // @param error 非空时写入校验结果（成功为 PolygonError::None）
    bool begin(const std::vector<math::Vec2>& polygon, PolygonError* error = nullptr) {
        edges_.clear();
        buckets_.clear();
        active_.clear();
        spans_.clear();
        xBuf_.clear();
        firstScanline_ = 0;
        lastScanline_ = -1;
        scanline_ = 0;
        running_ = false;

        const std::vector<math::Vec2> poly = normalizePolygon(polygon);
        const PolygonError err = detail::validateNormalized(poly);
        if (error) *error = err;
        if (err != PolygonError::None) return false;

        // 构造 ET：跳过水平边；其余边按"进入扫描线"分桶
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const math::Vec2& a = poly[i];
            const math::Vec2& b = poly[(i + 1) % n];
            if (std::fabs(b.y - a.y) <= POLYGON_POINT_EPS) continue;   // 水平边不产生交点

            const bool aIsLower = (a.y < b.y);
            const math::Vec2& lower = aIsLower ? a : b;
            const math::Vec2& upper = aIsLower ? b : a;

            Edge e;
            e.yMin = lower.y;
            e.yMax = upper.y;
            e.xAtYMin = lower.x;
            e.invSlope = (upper.x - lower.x) / (upper.y - lower.y);
            e.enterScanline = static_cast<int>(std::ceil(e.yMin - 0.5f));
            e.leaveScanline = static_cast<int>(std::ceil(e.yMax - 0.5f));
            edges_.push_back(e);
        }
        if (edges_.empty()) {                 // 全为水平边 ⇒ 面积必然为 0，正常流程不会走到这里
            if (error) *error = PolygonError::DegenerateArea;
            return false;
        }

        firstScanline_ = edges_.front().enterScanline;
        lastScanline_ = edges_.front().leaveScanline - 1;
        for (const Edge& e : edges_) {
            firstScanline_ = std::min(firstScanline_, e.enterScanline);
            lastScanline_ = std::max(lastScanline_, e.leaveScanline - 1);
        }
        if (lastScanline_ < firstScanline_) return true;   // 多边形整体薄于一个像素：无扫描线可填

        buckets_.assign(static_cast<std::size_t>(lastScanline_ - firstScanline_ + 1), {});
        for (std::size_t i = 0; i < edges_.size(); ++i)
            buckets_[static_cast<std::size_t>(edges_[i].enterScanline - firstScanline_)].push_back(static_cast<int>(i));

        scanline_ = firstScanline_ - 1;   // 尚未推进
        running_ = true;
        return true;
    }

    int firstScanline() const { return firstScanline_; }
    int lastScanline() const { return lastScanline_; }
    int scanline() const { return scanline_; }                 // 当前扫描线；未推进时 = firstScanline - 1
    const std::vector<Span>& spans() const { return spans_; }  // 当前扫描线的填充区间
    bool running() const { return running_; }

    // 推进到下一条扫描线并计算该行的填充区间；返回 false 表示已无扫描线（结束）。
    bool step() {
        if (!running_) return false;
        ++scanline_;
        if (scanline_ > lastScanline_) {
            running_ = false;
            spans_.clear();
            return false;
        }

        // ① 新边进入：ET[scanline] 的边插入 AET
        for (int ei : buckets_[static_cast<std::size_t>(scanline_ - firstScanline_)])
            active_.push_back(ei);
        // ② 旧边离开：已越过 yMax 的边从 AET 移除（半开区间 ⇒ y == leaveScanline 即不再参与）
        active_.erase(std::remove_if(active_.begin(), active_.end(),
                                     [this](int ei) { return scanline_ >= edges_[static_cast<std::size_t>(ei)].leaveScanline; }),
                      active_.end());

        // ③ 求交：x 直接由边参数算出（yc = 采样点 = 像素中心 y + 0.5）
        const float yc = static_cast<float>(scanline_) + 0.5f;
        xBuf_.clear();
        for (int ei : active_) {
            const Edge& e = edges_[static_cast<std::size_t>(ei)];
            xBuf_.emplace_back(e.xAtYMin + e.invSlope * (yc - e.yMin), ei);
        }
        // ④ 按 x 升序排序
        std::sort(xBuf_.begin(), xBuf_.end(),
                  [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first < b.first; });

        // ⑤ 成对生成区间：像素 x 覆盖 [x, x+1)、取中心采样，且区间按**左闭右开** [xl, xr)：
        //    xl <= x+0.5  ⇒ x >= xl-0.5  ⇒ xStart = ceil(xl-0.5)
        //    x+0.5 <  xr  ⇒ x <  xr-0.5  ⇒ xEnd   = ceil(xr-0.5) - 1
        spans_.clear();
        for (std::size_t i = 0; i + 1 < xBuf_.size(); i += 2) {
            const int xStart = static_cast<int>(std::ceil(xBuf_[i].first - 0.5f));
            const int xEnd = static_cast<int>(std::ceil(xBuf_[i + 1].first - 0.5f)) - 1;
            if (xStart <= xEnd) spans_.push_back(Span{xStart, xEnd});
        }
        return true;
    }

    // 把当前扫描线的区间写入画布（越界由 Canvas::putPixel 忽略）
    void fillCurrentRow(Canvas& canvas, const Color& color) const {
        if (!running_) return;
        for (const Span& s : spans_)
            for (int x = s.xStart; x <= s.xEnd; ++x) canvas.putPixel(x, scanline_, color);
    }

private:
    std::vector<Edge> edges_;                    // 边表 ET：全部非水平边
    std::vector<std::vector<int>> buckets_;      // ET 分桶：下标 = 扫描线 − firstScanline_
    std::vector<int> active_;                    // 活性边表 AET：存 edges_ 下标
    std::vector<Span> spans_;                    // 当前扫描线的填充区间
    std::vector<std::pair<float, int>> xBuf_;    // 交点排序缓冲（x, 边下标）
    int firstScanline_ = 0;
    int lastScanline_ = -1;
    int scanline_ = 0;
    bool running_ = false;
};

// 一次性填充多边形（内部即 ScanlineFiller 逐行推进 —— 单一实现，见 performance.md P2）。
// @param polygon 原始顶点序列（像素坐标、浮点；允许末尾重复首点、允许相邻重合点）
// @param error 非空时写入校验结果
// @return 校验通过并完成填充返回 true
inline bool fillPolygon(Canvas& canvas, const std::vector<math::Vec2>& polygon,
                        const Color& color, PolygonError* error = nullptr) {
    ScanlineFiller filler;
    if (!filler.begin(polygon, error)) return false;
    while (filler.step()) filler.fillCurrentRow(canvas, color);
    return true;
}

} // namespace gfx

#endif // GFX_RASTER_HPP
