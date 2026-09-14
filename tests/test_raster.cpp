// test_raster.cpp —— gfx 光栅化测试：Bresenham 画线 + 扫描线多边形填充
//
// 运行：ctest --test-dir build -C Release -R test_raster
//
// 测试策略：
//   1. 画线：逐像素集合与"期望点集"精确比对（含八个卦限方向）。
//   2. 填充：用**独立的实现**（射线法判点在多边形内，逐像素中心采样）作为参考，
//      与被测填充结果逐像素比对 —— 这直接验证了"半开区间规则 + 成对填充"是否正确。
//   3. 边界情形：水平边、共享顶点/极值点、凹多边形、顶点落在扫描线上、退化输入。

#include "gfx_test.hpp"

#include <gfx/raster.hpp>

#include <algorithm>
#include <cmath>
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
