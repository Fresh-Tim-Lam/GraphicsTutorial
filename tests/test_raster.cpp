// test_raster.cpp —— gfx 光栅化测试：Bresenham 画线 + 扫描线多边形填充
//
// 运行：ctest --test-dir build -C Release -R test_raster
//
// 测试策略：
//   1. 画线：逐像素集合与"期望点集"精确比对（含八个卦限方向）。
//   1b. 画线算法对照：Naive / DDA / Bresenham 三种实现在整数端点下逐像素一致（代表性线段），
//       并校验非空、含端点、落在包围盒内，以及"默认参数即 Bresenham"。
//   2. 填充：用**独立的实现**（射线法判点在多边形内，逐像素中心采样）作为参考，
//      与被测填充结果逐像素比对 —— 这直接验证了"半开区间规则 + 成对填充"是否正确。
//   3. 边界情形：水平边、共享顶点/极值点、凹多边形、顶点落在扫描线上、退化输入。
//   4. 浮点端点画线：整数端点下与整数 Bresenham 逐像素一致；亚像素端点下保留精度（含 Point2D 直传）。

#include "gfx_test.hpp"

#include <gfx/geom/point.hpp>
#include <gfx/render/raster.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using gfx::Canvas;
using gfx::Color;
using gfx::math::Vec2;
using gfx::rgb;

namespace {

const Color kBg = rgb(0, 0, 0);
const Color kFill = rgb(200, 60, 60);

// 参考实现：射线法判断"像素中心 (px+0.5, py+0.5)"是否在多边形内部。
// 采用与生产实现一致的 y 方向半开区间（比较用 >），x 方向用严格小于。
bool centerInside(const std::vector<Vec2>& poly, int px, int py) {
    const float x = static_cast<float>(px) + 0.5f;
    const float y = static_cast<float>(py) + 0.5f;
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[(i + 1) % n];
        if ((a.y > y) != (b.y > y)) {
            const float xCross = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x < xCross) inside = !inside;
        }
    }
    return inside;
}

// 统计"参考说在内、填充却没填"与"参考说在外、填充却填了"的像素数
std::pair<int, int> compareFill(const Canvas& c, const std::vector<Vec2>& poly) {
    int missed = 0, extra = 0;
    for (int y = 0; y < c.height(); ++y) {
        for (int x = 0; x < c.width(); ++x) {
            const bool filled = (c.pixelAt(x, y) == kFill);
            const bool expect = centerInside(poly, x, y);
            if (expect && !filled) ++missed;
            if (!expect && filled) ++extra;
        }
    }
    return {missed, extra};
}

int countFilled(const Canvas& c) {
    int n = 0;
    for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x)
            if (c.pixelAt(x, y) == kFill) ++n;
    return n;
}

// 收集画布上所有"填充色"像素坐标（按 y 再 x 升序）——用于逐像素比对
std::vector<std::pair<int, int>> filledPixels(const Canvas& c) {
    std::vector<std::pair<int, int>> v;
    for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x)
            if (c.pixelAt(x, y) == kFill) v.emplace_back(x, y);
    return v;
}

// 打印某算法的像素列表（仅在不一致时用于回传原始证据）
void printPixels(const char* name, const std::vector<std::pair<int, int>>& px) {
    std::fprintf(stderr, "    %s(%zu):", name, px.size());
    for (const std::pair<int, int>& p : px) std::fprintf(stderr, " (%d,%d)", p.first, p.second);
    std::fprintf(stderr, "\n");
}

// 两个（已按 y 再 x 升序的）像素列表的**对称差**大小 —— 即两算法"不一致"的像素数
int pixelSetDiff(const std::vector<std::pair<int, int>>& a, const std::vector<std::pair<int, int>>& b) {
    std::vector<std::pair<int, int>> d;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
    return static_cast<int>(d.size());
}

// 生成简单凹多边形：10 顶点星形（外/内半径交替），中心坐标带非半整数偏移以避免"中心恰落在边上"
std::vector<Vec2> makeStar(float cx, float cy, float rOuter, float rInner, int points) {
    std::vector<Vec2> poly;
    const float step = 3.14159265358979f / static_cast<float>(points);
    for (int k = 0; k < points * 2; ++k) {
        const float r = (k % 2 == 0) ? rOuter : rInner;
        const float a = step * static_cast<float>(k) - 1.5707963f;   // 从正上方开始
        poly.push_back(Vec2(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    return poly;
}

} // namespace

int main() {
    // ---------------- 画线 ----------------
    {
        Canvas c(16, 16);

        // 水平线：含两端点，共 6 个像素
        gfx::drawLine(c, 2, 3, 7, 3, kFill);
        int n = 0;
        for (int x = 2; x <= 7; ++x) if (c.pixelAt(x, 3) == kFill) ++n;
        TEST_CHECK(n == 6);
        TEST_CHECK(c.pixelAt(1, 3) == kBg && c.pixelAt(8, 3) == kBg);

        // 垂直线
        Canvas v(16, 16);
        gfx::drawLine(v, 5, 1, 5, 9, kFill);
        int m = 0;
        for (int y = 1; y <= 9; ++y) if (v.pixelAt(5, y) == kFill) ++m;
        TEST_CHECK(m == 9);

        // 45° 对角线：像素数 = max(dx,dy)+1
        Canvas d(16, 16);
        gfx::drawLine(d, 0, 0, 9, 9, kFill);
        TEST_CHECK(countFilled(d) == 10);
        TEST_CHECK(d.pixelAt(0, 0) == kFill && d.pixelAt(9, 9) == kFill);

        // 陡斜率（每行必进一格）：像素数 = dy+1
        Canvas s(16, 16);
        gfx::drawLine(s, 3, 0, 5, 11, kFill);
        TEST_CHECK(countFilled(s) == 12);
        TEST_CHECK(s.pixelAt(3, 0) == kFill && s.pixelAt(5, 11) == kFill);

        // 反向画同一条线：点集一致（对称性）
        Canvas f1(16, 16), f2(16, 16);
        gfx::drawLine(f1, 1, 2, 12, 7, kFill);
        gfx::drawLine(f2, 12, 7, 1, 2, kFill);
        int diff = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (f1.pixelAt(x, y) != f2.pixelAt(x, y)) ++diff;
        TEST_CHECK(diff == 0);
    }

    // ---------------- 画直线算法对照：Naive / DDA / Bresenham ----------------
    {
        struct Seg { int x0, y0, x1, y1; const char* name; };
        // 代表性线段（整数端点）。刻意避开"直线恰穿过像素中心中线"（半整数并列）的斜率：
        // 那种并列情形下 DDA/Naive 的"四舍五入"与 Bresenham 的并列取舍可能相差 1 像素；
        // 本测试锁定三者**应当完全一致**的一般情形（并列情形见 line-algorithms 演示与 README）。
        const Seg segs[] = {
            {2, 3, 20, 3, "水平"},
            {7, 1, 7, 25, "垂直"},
            {3, 3, 30, 30, "45°"},
            {30, 30, 3, 3, "45°反向"},
            {0, 0, 21, 7, "缓斜 1/3"},
            {0, 0, 21, 9, "缓斜 3/7"},
            {0, 0, 7, 21, "陡斜 3/1"},
            {0, 0, 9, 21, "陡斜 7/3"},
            {30, 5, 5, 20, "负缓斜 -3/5"},
            {5, 20, 30, 5, "负缓斜反向"},
            {5, 5, 20, 30, "负陡斜 5/3"},
            {20, 30, 5, 5, "负陡斜反向"},
            {10, 10, 11, 10, "长度 1"},
            {10, 10, 10, 10, "退化点"},
            {0, 0, 199, 51, "极长缓斜"},
            {0, 0, 51, 199, "极长陡斜"},
        };
        const gfx::LineAlgorithm algos[3] = {gfx::LineAlgorithm::Naive, gfx::LineAlgorithm::DDA,
                                             gfx::LineAlgorithm::Bresenham};
        const char* algoNames[3] = {"Naive", "DDA", "Bresenham"};
        constexpr int kSize = 210;

        for (const Seg& s : segs) {
            Canvas cs[3] = {Canvas(kSize, kSize), Canvas(kSize, kSize), Canvas(kSize, kSize)};
            for (int a = 0; a < 3; ++a)
                gfx::drawLine(cs[a], s.x0, s.y0, s.x1, s.y1, kFill, algos[a]);
            std::vector<std::pair<int, int>> px[3];
            for (int a = 0; a < 3; ++a) px[a] = filledPixels(cs[a]);

            // ① 三种算法逐像素完全一致（若不一致，打印各算法像素列表作为原始证据）
            const bool same = (px[0] == px[1]) && (px[1] == px[2]);
            TEST_CHECK(same);
            if (!same) {
                std::fprintf(stderr, "  [不一致] 线段 %s (%d,%d)-(%d,%d):\n",
                             s.name, s.x0, s.y0, s.x1, s.y1);
                for (int a = 0; a < 3; ++a) printPixels(algoNames[a], px[a]);
            }

            // ② 非空，且含两个端点
            TEST_CHECK(!px[2].empty());
            TEST_CHECK(cs[2].pixelAt(s.x0, s.y0) == kFill);
            TEST_CHECK(cs[2].pixelAt(s.x1, s.y1) == kFill);

            // ③ 所有像素落在线段的包围盒内（不越界、不越写）
            const int bx0 = std::min(s.x0, s.x1), bx1 = std::max(s.x0, s.x1);
            const int by0 = std::min(s.y0, s.y1), by1 = std::max(s.y0, s.y1);
            bool inBox = true;
            for (int y = 0; y < kSize && inBox; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    if (cs[2].pixelAt(x, y) == kFill && (x < bx0 || x > bx1 || y < by0 || y > by1)) {
                        inBox = false;
                        break;
                    }
                }
            }
            TEST_CHECK(inBox);
        }

        // ④ 默认参数即最优：不传算法 == 显式传 Bresenham
        {
            Canvas cDef(kSize, kSize), cBres(kSize, kSize);
            gfx::drawLine(cDef, 5, 7, 40, 33, kFill);                                 // 默认（应为 Bresenham）
            gfx::drawLine(cBres, 5, 7, 40, 33, kFill, gfx::LineAlgorithm::Bresenham); // 显式 Bresenham
            TEST_CHECK(filledPixels(cDef) == filledPixels(cBres));

            Canvas cDef2(kSize, kSize), cBres2(kSize, kSize);
            gfx::drawLine(cDef2, 60, 5, 8, 44, kFill);
            gfx::drawLine(cBres2, 60, 5, 8, 44, kFill, gfx::LineAlgorithm::Bresenham);
            TEST_CHECK(filledPixels(cDef2) == filledPixels(cBres2));
        }
    }

    // ---------------- 浮点端点画线（保留亚像素 + 与整数版逐像素一致） ----------------
    {
        // ① 整数端点：经**唯一入口**（Vec2 形式）自动选中纯整数 Bresenham，与直接调用内部整数版**逐像素一致**
        //    （保证"收敛为单一 API 且不丢整数性能"的替换安全；内部整数版仍在 detail:: 中）
        const float intEnds[][4] = {
            {0, 0, 10, 3}, {0, 0, 9, 9}, {3, 0, 5, 11}, {1, 2, 12, 7},
            {12, 7, 1, 2}, {0, 0, 15, 1}, {2, 14, 13, 3}, {7, 7, 7, 1},
        };
        for (const float* e : intEnds) {
            Canvas ci(16, 16), cf(16, 16);
            gfx::detail::drawLineBresenham(ci, static_cast<int>(e[0]), static_cast<int>(e[1]),
                                           static_cast<int>(e[2]), static_cast<int>(e[3]), kFill);
            gfx::drawLine(cf, e[0], e[1], e[2], e[3], kFill);
            int diff = 0;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    if (ci.pixelAt(x, y) != cf.pixelAt(x, y)) ++diff;
            TEST_CHECK(diff == 0);
        }

        // ② 亚像素端点·短线：steps = lround(max(2.2, 1.4)) = 2
        //    t = 0 / 0.5 / 1 ⇒ 采样点 (0.4,0.3) / (1.49999995, 1.0) / (2.6,1.7) ⇒ 像素 (0,0) (1,1) (3,2)
        //    注意 x 是 1.49999995（float 的 0.4/2.6 本身就略偏小）⇒ 落像素 1，不是 2。
        Canvas shortLine(8, 8);
        gfx::drawLine(shortLine, 0.4f, 0.3f, 2.6f, 1.7f, kFill);
        TEST_CHECK(countFilled(shortLine) == 3);
        TEST_CHECK(shortLine.pixelAt(0, 0) == kFill);
        TEST_CHECK(shortLine.pixelAt(1, 1) == kFill);
        TEST_CHECK(shortLine.pixelAt(3, 2) == kFill);
        TEST_CHECK(shortLine.pixelAt(2, 1) == kBg);   // 整数版（端点取整成 (0,0)→(3,2)）会多画这一格

        // ③ 亚像素端点·长缓斜线：端点取整会让整数版的阶梯**整体偏一行**
        //    (0.0,0.4) → (100.0,10.4)：i=1 时 y = 0.4 + 0.104 = 0.504 ⇒ 行 1
        Canvas longLine(104, 16);
        gfx::drawLine(longLine, 0.0f, 0.4f, 100.0f, 10.4f, kFill);
        TEST_CHECK(longLine.pixelAt(0, 0) == kFill);      // 起点 lround(0.4) = 0
        TEST_CHECK(longLine.pixelAt(1, 1) == kFill);      // 浮点版：落在行 1
        TEST_CHECK(longLine.pixelAt(1, 0) == kBg);        // 整数版会落在行 0
        TEST_CHECK(longLine.pixelAt(100, 10) == kFill);   // 终点 lround(10.4) = 10

        // ④ 两点落在同一像素（steps = 0）：退化成一个点
        Canvas dot(4, 4);
        gfx::drawLine(dot, 1.2f, 1.3f, 1.4f, 1.1f, kFill);
        TEST_CHECK(countFilled(dot) == 1);
        TEST_CHECK(dot.pixelAt(1, 1) == kFill);

        // ⑤ 几何层 Point2D：经隐式转换取**变换后**的坐标，再以平铺坐标传入唯一入口
        Canvas fromPoint(16, 16);
        gfx::Point2D p(2.0f, 3.0f);
        p.translate(4.0f, 0.0f);                    // 变换后 = (6, 3)
        const gfx::Point2D q(6.0f, 3.0f);           // 与 p 的变换后坐标重合
        const Vec2 pv = p;                          // 隐式转换取变换后的坐标
        const Vec2 qv = q;
        gfx::drawLine(fromPoint, pv.x, pv.y, qv.x, qv.y, kFill);
        TEST_CHECK(countFilled(fromPoint) == 1);
        TEST_CHECK(fromPoint.pixelAt(6, 3) == kFill);
    }

    // ---------------- 浮点端点的算法选择（统一接口：同样可选算法、同样默认 Bresenham） ----------------
    {
        struct FSeg { float x0, y0, x1, y1; const char* name; };
        const FSeg fsegs[] = {
            {0.4f, 0.3f, 2.6f, 1.7f, "短线"},
            {0.0f, 0.4f, 100.0f, 10.4f, "长缓斜"},
            {3.3f, 2.7f, 40.6f, 33.1f, "一般斜"},
            {5.2f, 2.1f, 12.6f, 40.7f, "陡斜"},
            {1.2f, 1.3f, 1.4f, 1.1f, "退化点"},
        };
        constexpr int kSize = 112;

        // ① 浮点版"不传算法" == 显式传 Bresenham（默认统一为 Bresenham）
        for (const FSeg& s : fsegs) {
            Canvas cd(kSize, kSize), cb(kSize, kSize);
            gfx::drawLine(cd, s.x0, s.y0, s.x1, s.y1, kFill);   // 默认
            gfx::drawLine(cb, s.x0, s.y0, s.x1, s.y1, kFill, gfx::LineAlgorithm::Bresenham);
            TEST_CHECK(filledPixels(cd) == filledPixels(cb));
        }

        // ② 浮点版传 DDA / Naive：能编译、非空、含两端点、像素落在包围盒内；
        //    与 Bresenham 的差异**如实打印**，并断言"受限"（不超过该线段像素总数的一个小比例）。
        const gfx::LineAlgorithm algos[3] = {gfx::LineAlgorithm::Bresenham, gfx::LineAlgorithm::DDA,
                                             gfx::LineAlgorithm::Naive};
        const char* names[3] = {"Bresenham", "DDA", "Naive"};
        for (const FSeg& s : fsegs) {
            Canvas cs[3] = {Canvas(kSize, kSize), Canvas(kSize, kSize), Canvas(kSize, kSize)};
            for (int a = 0; a < 3; ++a)
                gfx::drawLine(cs[a], s.x0, s.y0, s.x1, s.y1, kFill, algos[a]);
            std::vector<std::pair<int, int>> px[3];
            for (int a = 0; a < 3; ++a) px[a] = filledPixels(cs[a]);

            const int ex0 = static_cast<int>(std::lround(s.x0));
            const int ey0 = static_cast<int>(std::lround(s.y0));
            const int ex1 = static_cast<int>(std::lround(s.x1));
            const int ey1 = static_cast<int>(std::lround(s.y1));
            const int bx0 = std::min(ex0, ex1), bx1 = std::max(ex0, ex1);
            const int by0 = std::min(ey0, ey1), by1 = std::max(ey0, ey1);

            for (int a = 0; a < 3; ++a) {
                TEST_CHECK(!px[a].empty());                     // 非空
                TEST_CHECK(cs[a].pixelAt(ex0, ey0) == kFill);   // 含起点
                TEST_CHECK(cs[a].pixelAt(ex1, ey1) == kFill);   // 含终点
                bool inBox = true;                              // 落在端点包围盒内
                for (const std::pair<int, int>& p : px[a])
                    if (p.first < bx0 || p.first > bx1 || p.second < by0 || p.second > by1) inBox = false;
                TEST_CHECK(inBox);
            }

            // 与 Bresenham 的对称差像素数：打印实际值，并断言"受限"（≤ max(2, 总数/4)，不假设必然一致）。
            // 实测：只有"直线**恰穿过像素中心的中线（严格 .5 并列）**"的长缓斜线（dy=10.0）出现差异
            //       （Naive=2、DDA=16，源于浮点取舍方向不同）；其余线段三者逐像素一致（均为 0）。
            const int dDDA = pixelSetDiff(px[1], px[0]);
            const int dNaive = pixelSetDiff(px[2], px[0]);
            const int bound = std::max(2, static_cast<int>(px[0].size()) / 4);
            std::fprintf(stderr,
                         "  [浮点算法差异] %-6s 像素数 Bren=%zu DDA=%zu Naive=%zu | DDA<->Bren=%d Naive<->Bren=%d (界=%d)\n",
                         s.name, px[0].size(), px[1].size(), px[2].size(), dDDA, dNaive, bound);
            TEST_CHECK(dDDA <= bound);
            TEST_CHECK(dNaive <= bound);
            if (dDDA > bound || dNaive > bound) {               // 超界时回传三算法原始像素列表
                std::fprintf(stderr, "  [超界] 线段 %s (%.2f,%.2f)-(%.2f,%.2f):\n", s.name, s.x0, s.y0, s.x1, s.y1);
                for (int a = 0; a < 3; ++a) printPixels(names[a], px[a]);
            }
        }
    }

    // ---------------- 轴对齐矩形：精确区间 ----------------
    // 顶点 (10,10)-(20,10)-(20,20)-(10,20)：行 10..19、列 10..19，共 100 像素
    {
        const std::vector<Vec2> rect{Vec2(10, 10), Vec2(20, 10), Vec2(20, 20), Vec2(10, 20)};
        Canvas c(32, 32);
        gfx::PolygonError err = gfx::PolygonError::SelfIntersecting;
        TEST_CHECK(gfx::fillPolygon(c, rect, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::None);
        TEST_CHECK(countFilled(c) == 100);
        TEST_CHECK(c.pixelAt(10, 10) == kFill && c.pixelAt(19, 19) == kFill);
        TEST_CHECK(c.pixelAt(9, 10) == kBg && c.pixelAt(20, 19) == kBg);
        TEST_CHECK(c.pixelAt(19, 20) == kBg && c.pixelAt(10, 9) == kBg);
    }

    // ---------------- 水平边 + 顶点落在扫描线上（矩形退化高度） ----------------
    // 高度为 1 像素的矩形：行范围 [10, 11) ⇒ 只填一行
    {
        const std::vector<Vec2> thin{Vec2(4, 10), Vec2(14, 10), Vec2(14, 11), Vec2(4, 11)};
        Canvas c(24, 24);
        TEST_CHECK(gfx::fillPolygon(c, thin, kFill));
        TEST_CHECK(countFilled(c) == 10);
        const std::pair<int, int> r = compareFill(c, thin);
        TEST_CHECK(r.first == 0 && r.second == 0);
    }

    // ---------------- 凸多边形 / 凹多边形 / 星形：与参考实现逐像素一致 ----------------
    {
        // 凸五边形
        const std::vector<Vec2> convex{Vec2(10.3f, 5.7f), Vec2(40.7f, 12.1f), Vec2(35.3f, 40.9f),
                                       Vec2(12.9f, 45.3f), Vec2(4.1f, 22.7f)};
        Canvas c(52, 52);
        TEST_CHECK(gfx::fillPolygon(c, convex, kFill));
        std::pair<int, int> r = compareFill(c, convex);
        TEST_CHECK(r.first == 0);
        TEST_CHECK(r.second == 0);
        TEST_CHECK(countFilled(c) > 0);

        // 凹多边形（L 形：缺口必须不被填充）
        const std::vector<Vec2> concave{Vec2(5.1f, 5.3f), Vec2(35.7f, 5.9f), Vec2(35.3f, 15.1f),
                                        Vec2(15.7f, 15.9f), Vec2(15.3f, 35.7f), Vec2(5.9f, 35.1f)};
        Canvas l(44, 44);
        TEST_CHECK(gfx::fillPolygon(l, concave, kFill));
        r = compareFill(l, concave);
        TEST_CHECK(r.first == 0);
        TEST_CHECK(r.second == 0);
        TEST_CHECK(l.pixelAt(30, 30) == kBg);   // (30,30) 在缺口内 ⇒ 未填

        // 10 顶点星形（含 5 个凹顶点）
        const std::vector<Vec2> star = makeStar(25.87f, 25.63f, 20.0f, 8.0f, 5);
        Canvas s(56, 56);
        gfx::PolygonError err = gfx::PolygonError::None;
        TEST_CHECK(gfx::fillPolygon(s, star, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::None);
        r = compareFill(s, star);
        TEST_CHECK(r.first == 0);
        TEST_CHECK(r.second == 0);
    }

    // ---------------- 共享顶点 / 极值点：不重复计数（菱形的上下极值点） ----------------
    {
        const std::vector<Vec2> diamond{Vec2(20.7f, 3.1f), Vec2(37.3f, 21.9f), Vec2(19.1f, 39.7f), Vec2(2.9f, 20.3f)};
        Canvas c(44, 44);
        TEST_CHECK(gfx::fillPolygon(c, diamond, kFill));
        const std::pair<int, int> r = compareFill(c, diamond);
        TEST_CHECK(r.first == 0 && r.second == 0);

        // 每行填充像素数应是"先增后减"的单峰序列（共享顶点若被重复计数会破坏该形状）
        std::vector<int> rowCounts;
        for (int y = 0; y < c.height(); ++y) {
            int n = 0;
            for (int x = 0; x < c.width(); ++x) if (c.pixelAt(x, y) == kFill) ++n;
            if (n > 0) rowCounts.push_back(n);
        }
        bool unimodal = rowCounts.size() >= 3;
        for (std::size_t i = 1; i + 1 < rowCounts.size(); ++i)
            if (rowCounts[i] < rowCounts[i - 1] && rowCounts[i] < rowCounts[i + 1]) unimodal = false;
        TEST_CHECK(unimodal);
    }

    // ---------------- 顶点顺序无关 / 末尾重复首点 / 相邻重合点 ----------------
    {
        std::vector<Vec2> closed{Vec2(4, 4), Vec2(14, 4), Vec2(14, 14), Vec2(4, 14), Vec2(4, 4)};   // 末尾重复首点
        std::vector<Vec2> cleaned = gfx::normalizePolygon(closed);
        TEST_CHECK(cleaned.size() == 4);

        std::vector<Vec2> dup{Vec2(4, 4), Vec2(14, 4), Vec2(14, 4), Vec2(14, 14), Vec2(4, 14)};    // 相邻重合
        TEST_CHECK(gfx::normalizePolygon(dup).size() == 4);

        // 闭合点重复不影响填充结果
        Canvas c1(24, 24), c2(24, 24);
        TEST_CHECK(gfx::fillPolygon(c1, closed, kFill));
        std::vector<Vec2> open{std::begin(closed), std::end(closed) - 1};
        TEST_CHECK(gfx::fillPolygon(c2, open, kFill));
        TEST_CHECK(countFilled(c1) == countFilled(c2));

        // 顺时针输入同样正确（填充与环绕方向无关）；有向面积符号相反
        std::vector<Vec2> ccw{Vec2(4, 4), Vec2(14, 4), Vec2(14, 14), Vec2(4, 14)};
        std::vector<Vec2> cw{std::begin(ccw), std::end(ccw)};
        std::reverse(cw.begin(), cw.end());
        TEST_CHECK(gfx::polygonSignedArea(ccw) * gfx::polygonSignedArea(cw) < 0.0f);
        Canvas c3(24, 24);
        TEST_CHECK(gfx::fillPolygon(c3, cw, kFill));
        TEST_CHECK(countFilled(c3) == countFilled(c1));
    }

    // ---------------- 逐扫描线推进接口（动画用） ----------------
    {
        const std::vector<Vec2> poly{Vec2(5.3f, 4.1f), Vec2(30.7f, 9.3f), Vec2(20.1f, 28.9f), Vec2(3.7f, 17.5f)};
        gfx::ScanlineFiller filler;
        TEST_CHECK(filler.begin(poly));
        Canvas stepwise(36, 36);
        int rows = 0, pixels = 0;
        while (filler.step()) {
            ++rows;
            for (const gfx::Span& s : filler.spans()) pixels += s.length();
            filler.fillCurrentRow(stepwise, kFill);
        }
        TEST_CHECK(rows == filler.lastScanline() - filler.firstScanline() + 1);
        TEST_CHECK(!filler.running());

        Canvas oneshot(36, 36);
        TEST_CHECK(gfx::fillPolygon(oneshot, poly, kFill));
        TEST_CHECK(pixels == countFilled(oneshot));      // 逐行区间总数 == 一次性填充像素数
        int diff = 0;
        for (int y = 0; y < 36; ++y)
            for (int x = 0; x < 36; ++x)
                if (stepwise.pixelAt(x, y) != oneshot.pixelAt(x, y)) ++diff;
        TEST_CHECK(diff == 0);
    }

    // ---------------- 非法输入：给出明确错误码而不是崩溃 ----------------
    {
        Canvas c(32, 32);

        const std::vector<Vec2> two{Vec2(1, 1), Vec2(5, 5)};
        gfx::PolygonError err = gfx::PolygonError::None;
        TEST_CHECK(!gfx::fillPolygon(c, two, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::TooFewVertices);

        TEST_CHECK(!gfx::fillPolygon(c, {}, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::TooFewVertices);

        const std::vector<Vec2> collinear{Vec2(0, 0), Vec2(5, 5), Vec2(10, 10)};
        TEST_CHECK(!gfx::fillPolygon(c, collinear, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::DegenerateArea);

        // 蝴蝶结（自交）
        const std::vector<Vec2> bowtie{Vec2(0, 0), Vec2(10, 10), Vec2(10, 0), Vec2(0, 10)};
        TEST_CHECK(!gfx::fillPolygon(c, bowtie, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::SelfIntersecting);

        // 非相邻边接触（顶点落在另一条边上）也算非简单多边形
        const std::vector<Vec2> touching{Vec2(0, 0), Vec2(10, 0), Vec2(10, 10), Vec2(5, 0), Vec2(0, 10)};
        TEST_CHECK(!gfx::fillPolygon(c, touching, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::SelfIntersecting);

        // 面积过小：整条边不足一个像素高 ⇒ 合法但无可填扫描线（不写入任何像素）
        const std::vector<Vec2> hairline{Vec2(2, 5.0f), Vec2(12, 5.0f), Vec2(12, 5.2f), Vec2(2, 5.2f)};
        Canvas h(16, 16);
        TEST_CHECK(gfx::fillPolygon(h, hairline, kFill, &err));
        TEST_CHECK(err == gfx::PolygonError::None);
        TEST_CHECK(countFilled(h) == 0);

        TEST_CHECK(std::string(gfx::polygonErrorText(gfx::PolygonError::SelfIntersecting)).size() > 0);
    }

    // ---------------- 边界不越出：填充不触碰画布外与包围盒外 ----------------
    {
        const std::vector<Vec2> poly{Vec2(8.4f, 6.2f), Vec2(20.6f, 14.7f), Vec2(11.3f, 22.9f)};
        Canvas c(32, 32);
        TEST_CHECK(gfx::fillPolygon(c, poly, kFill));
        TEST_CHECK(c.pixelAt(0, 0) == kBg);
        TEST_CHECK(c.pixelAt(31, 31) == kBg);
        TEST_CHECK(c.pixelAt(7, 6) == kBg);
        TEST_CHECK(c.pixelAt(22, 23) == kBg);
    }

    return gfx::test::summary();
}
