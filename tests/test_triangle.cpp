// test_triangle.cpp —— 三角形光栅化 / 边界规则 / 重心坐标 / 抗锯齿 / 深度测试
//
// 运行：ctest --test-dir build -C Release -R test_triangle
//
// 测试策略：
//   1. 边界规则是**可判定**的：两个共享一条边的三角形，必须"每个采样点恰好覆盖一次"（不重不漏）。
//   2. 两条填充路线互校：包围盒 + inside test  vs  增量式逐扫描线（同一三角形应逐像素一致）。
//   3. 抗锯齿：覆盖率在内部为 1、外部为 0、边界介于两者；共享边上两三角形覆盖率之和为 1。
//   4. 深度缓冲：遮挡正确，且**与绘制顺序无关**（两种顺序结果逐像素相同）。
//   5. 无关参数告警：对当前模式无意义的参数应"警告并忽略"，且可被 triangleFillWarningsEnabled 关闭。

#include "gfx_test.hpp"

#include <gfx/render/depth.hpp>
#include <gfx/render/triangle.hpp>

#include <cmath>
#include <functional>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

using gfx::Canvas;
using gfx::Color;
using gfx::DepthBuffer;
using gfx::rgb;
using gfx::rgba;
using gfx::math::Vec2;
using gfx::math::Vec3;

namespace {

const Color kBg = rgb(0, 0, 0);
const Color kFill = rgb(200, 60, 60);
const Color kNear = rgb(255, 0, 0);
const Color kFar = rgb(0, 0, 255);

int countNot(const Canvas& canvas, const Color& c) {
    int n = 0;
    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
            if (canvas.pixelAt(x, y) != c) ++n;
    return n;
}

int countPixelDiff(const Canvas& a, const Canvas& b) {
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixelAt(x, y) != b.pixelAt(x, y)) ++n;
    return n;
}

// 覆盖率 AA 的**朴素参考实现**：对包围盒内每个像素都调用 coverage()（即"快速判定优化"之前的算法）。
// 用途：锁定"完全在内 ⇒ 1、完全在外 ⇒ 跳过"的优化不改变任何像素。
void referenceCoverageAA(Canvas& canvas, const Vec2& a, const Vec2& b, const Vec2& c,
                         const Color& color, int samplesPerAxis) {
    if (gfx::edgeFunction(a, b, c) == 0.0f) return;
    const int xBegin = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
    const int xEnd = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
    const int yBegin = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
    const int yEnd = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));
    for (int y = yBegin; y < yEnd; ++y)
        for (int x = xBegin; x < xEnd; ++x) {
            const float cov = gfx::coverage(static_cast<float>(x), static_cast<float>(y), a, b, c, samplesPerAxis);
            if (cov <= 0.0f) continue;
            const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround(cov * 255.0f));
            canvas.blendPixel(x, y, Color(color.r, color.g, color.b, alpha));
        }
}

// 捕获一次 stderr 输出（用于验证"无关参数告警"文案）。Windows/MinGW 下用 _dup/_dup2 重定向 fd 2。
std::string captureStderr(const std::function<void()>& fn) {
    std::fflush(stderr);
#if defined(_WIN32)
    const int savedFd = _dup(2);
    const int tmpFd = _open("triangle_warn_capture.txt", _O_CREAT | _O_TRUNC | _O_WRONLY,
                            _S_IREAD | _S_IWRITE);
    if (savedFd < 0 || tmpFd < 0) {   // 捕获失败时至少执行调用，保证测试仍覆盖行为
        fn();
        return std::string();
    }
    _dup2(tmpFd, 2);
    _close(tmpFd);
    fn();
    std::fflush(stderr);
    _dup2(savedFd, 2);
    _close(savedFd);

    std::string text;
    if (std::FILE* f = std::fopen("triangle_warn_capture.txt", "rb")) {
        char buf[512];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    std::remove("triangle_warn_capture.txt");
    return text;
#else
    fn();
    return std::string();
#endif
}

// 文本里的换行符个数（= 打印行数）
int countLines(const std::string& s) {
    int n = 0;
    for (char ch : s) if (ch == '\n') ++n;
    return n;
}

} // namespace

int main() {
    // ---- 边函数与内部判定（基本） ----
    {
        const Vec2 a(0.0f, 0.0f), b(4.0f, 0.0f), c(0.0f, 4.0f);
        TEST_CHECK(gfx::insideTriangle(Vec2(0.5f, 0.5f), a, b, c));      // 内部
        TEST_CHECK(!gfx::insideTriangle(Vec2(3.5f, 3.5f), a, b, c));     // 斜边外侧
        TEST_CHECK(!gfx::insideTriangle(Vec2(-0.5f, 0.5f), a, b, c));    // 左侧外
        // 顶点顺序不影响判定（内部会按有向面积归一化）
        TEST_CHECK(gfx::insideTriangle(Vec2(0.5f, 0.5f), c, b, a));
        // 退化三角形：任何点都不算内部
        TEST_CHECK(!gfx::insideTriangle(Vec2(1.0f, 0.0f), Vec2(0, 0), Vec2(2, 0), Vec2(4, 0)));
    }

    // ---- 边界规则（top-left）：共享边恰好覆盖一次 ----
    {
        // 正方形 [0,4]² 被对角线 x+y=4 分成两个三角形；注意有 4 个像素中心**恰好落在该对角线上**
        const Vec2 a0(0, 0), a1(4, 0), a2(0, 4);
        const Vec2 b0(4, 0), b1(4, 4), b2(0, 4);

        int once = 0, twice = 0, none = 0;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const Vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const int n = (gfx::insideTriangle(p, a0, a1, a2) ? 1 : 0)
                            + (gfx::insideTriangle(p, b0, b1, b2) ? 1 : 0);
                if (n == 1) ++once;
                else if (n == 2) ++twice;
                else ++none;
            }
        }
        TEST_CHECK(once == 16);   // 16 个采样点各被覆盖一次
        TEST_CHECK(twice == 0);   // 不重复
        TEST_CHECK(none == 0);    // 不遗漏
    }

    // ---- 两条填充路线互校：包围盒 + inside  vs  增量式扫描线 ----
    {
        // 顶点取**非整数、非 .5** 的值，避免采样点恰好落在边上 ——
        // 那种情况下两套边界规则（top-left vs 半开区间）本就允许不同归属，互校会变成假失败。
        const Vec2 tris[3][3] = {
            {Vec2(2.3f, 2.7f), Vec2(20.1f, 5.4f), Vec2(6.2f, 21.9f)},
            {Vec2(1.4f, 18.3f), Vec2(22.6f, 20.1f), Vec2(12.3f, 1.7f)},
            {Vec2(3.1f, 3.9f), Vec2(21.2f, 3.3f), Vec2(3.7f, 21.4f)},
        };
        for (const auto& t : tris) {
            Canvas viaBox(24, 24), viaScan(24, 24);
            viaBox.clear(kBg);
            viaScan.clear(kBg);
            gfx::fillTriangle(viaBox, t[0], t[1], t[2], kFill, gfx::TriangleFillMode::SolidBBox);
            gfx::fillTriangle(viaScan, t[0], t[1], t[2], kFill, gfx::TriangleFillMode::SolidScanline);
            TEST_CHECK(countPixelDiff(viaBox, viaScan) == 0);
            TEST_CHECK(countNot(viaBox, kBg) > 20);   // 确实填进去了一块
        }
    }

    // ---- 唯一入口的默认方式 = 增量式（SolidScanline） ----
    {
        const Vec2 a(2.3f, 2.7f), b(20.1f, 5.4f), c(6.2f, 21.9f);
        Canvas viaDefault(24, 24), viaScan(24, 24), viaBox(24, 24);
        viaDefault.clear(kBg);
        viaScan.clear(kBg);
        viaBox.clear(kBg);
        gfx::fillTriangle(viaDefault, a, b, c, kFill);   // 不传 mode：走默认
        gfx::fillTriangle(viaScan, a, b, c, kFill, gfx::TriangleFillMode::SolidScanline);
        gfx::fillTriangle(viaBox, a, b, c, kFill, gfx::TriangleFillMode::SolidBBox);
        TEST_CHECK(countPixelDiff(viaDefault, viaScan) == 0);   // 默认即增量式
        TEST_CHECK(countPixelDiff(viaDefault, viaBox) == 0);    // 该三角形两法一致（无边界歧义）
    }

    // ---- 与多边形扫描线填充（raster.hpp）互校 ----
    {
        const Vec2 t0(2.3f, 2.7f), t1(20.1f, 4.4f), t2(5.6f, 21.3f);
        Canvas viaTriangle(24, 24), viaPolygon(24, 24);
        viaTriangle.clear(kBg);
        viaPolygon.clear(kBg);
        gfx::fillTriangle(viaTriangle, t0, t1, t2, kFill);
        gfx::fillPolygon(viaPolygon, std::vector<Vec2>{t0, t1, t2}, kFill);
        TEST_CHECK(countPixelDiff(viaTriangle, viaPolygon) == 0);
    }

    // ---- 重心坐标 ----
    {
        const Vec2 a(0.0f, 0.0f), b(6.0f, 0.0f), c(0.0f, 6.0f);
        Vec3 w;
        // 顶点处权重为 (1,0,0) 等
        TEST_CHECK(gfx::barycentric(a, a, b, c, w));
        TEST_NEAR(w.x, 1.0f, 1e-5f);
        TEST_NEAR(w.y, 0.0f, 1e-5f);
        TEST_NEAR(w.z, 0.0f, 1e-5f);
        // 重心处三个权重相等
        TEST_CHECK(gfx::barycentric(Vec2(2.0f, 2.0f), a, b, c, w));
        TEST_NEAR(w.x, 1.0f / 3.0f, 1e-5f);
        TEST_NEAR(w.y, 1.0f / 3.0f, 1e-5f);
        TEST_NEAR(w.z, 1.0f / 3.0f, 1e-5f);
        // 权重和恒为 1
        TEST_CHECK(gfx::barycentric(Vec2(1.5f, 2.5f), a, b, c, w));
        TEST_NEAR(w.x + w.y + w.z, 1.0f, 1e-5f);
        // 退化三角形失败
        TEST_CHECK(!gfx::barycentric(Vec2(1.0f, 0.0f), Vec2(0, 0), Vec2(2, 0), Vec2(4, 0), w));
    }

    // ---- 抗锯齿：覆盖率 ----
    {
        const Vec2 a(0.0f, 0.0f), b(20.0f, 0.0f), c(0.0f, 20.0f);
        // 全内 / 全外
        TEST_NEAR(gfx::coverage(4.0f, 4.0f, a, b, c, 8), 1.0f, 1e-6f);
        TEST_NEAR(gfx::coverage(15.0f, 15.0f, a, b, c, 8), 0.0f, 1e-6f);
        // 边界像素：介于 0 与 1 之间（此题解析面积恰为 0.5）
        const float edge = gfx::coverage(3.0f, 3.0f, Vec2(0, 0), Vec2(7, 0), Vec2(0, 7), 16);
        TEST_CHECK(edge > 0.0f && edge < 1.0f);
        TEST_NEAR(edge, 0.5f, 0.05f);

        // 共享边上两三角形的覆盖率之和 = 1（不重不漏）
        const float ca = gfx::coverage(3.0f, 0.0f, Vec2(0, 0), Vec2(4, 0), Vec2(0, 4), 8);
        const float cb = gfx::coverage(3.0f, 0.0f, Vec2(4, 0), Vec2(4, 4), Vec2(0, 4), 8);
        TEST_NEAR(ca + cb, 1.0f, 1e-4f);
    }

    // ---- 抗锯齿：点采样 vs 覆盖率着色 ----
    {
        const Vec2 a(2.0f, 2.0f), b(20.0f, 4.0f), c(5.0f, 21.0f);
        Canvas sharp(24, 24), smooth(24, 24);
        sharp.clear(kBg);
        smooth.clear(kBg);
        gfx::fillTriangle(sharp, a, b, c, kFill);
        gfx::fillTriangle(smooth, a, b, c, kFill, gfx::TriangleFillMode::CoverageAA, 8);

        int sharpIntermediate = 0, smoothIntermediate = 0;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x) {
                const Color s = sharp.pixelAt(x, y);
                const Color m = smooth.pixelAt(x, y);
                if (s != kBg && s != kFill) ++sharpIntermediate;
                if (m != kBg && m != kFill) ++smoothIntermediate;
            }
        TEST_CHECK(sharpIntermediate == 0);    // 点采样：非纯色即背景
        TEST_CHECK(smoothIntermediate > 0);    // 覆盖率着色：存在中间色
    }

    // ---- 覆盖率 AA：快速判定优化不改变输出（与朴素参考逐像素一致） ----
    {
        const struct { Vec2 a, b, c; } tris[] = {
            {Vec2(24.0f, 24.0f), Vec2(276.0f, 276.0f), Vec2(24.0f, 276.0f)},   // 45° 斜边（含整数边界）
            {Vec2(2.3f, 2.7f), Vec2(20.1f, 5.4f), Vec2(6.2f, 21.9f)},         // 小数顶点
            {Vec2(4.0f, 0.0f), Vec2(4.0f, 4.0f), Vec2(0.0f, 4.0f)},           // 共享边一角（贴边界）
            {Vec2(2.0f, 2.0f), Vec2(22.0f, 2.3f), Vec2(12.0f, 2.2f)},         // 极薄（非退化）
        };
        for (const auto& t : tris)
            for (int N : {2, 4, 8}) {
                Canvas fast(300, 300), ref(300, 300);
                fast.clear(kBg);
                ref.clear(kBg);
                gfx::fillTriangle(fast, t.a, t.b, t.c, kFill, gfx::TriangleFillMode::CoverageAA, N);
                referenceCoverageAA(ref, t.a, t.b, t.c, kFill, N);
                TEST_CHECK(countPixelDiff(fast, ref) == 0);
            }
    }

    // ---- 抗锯齿：超采样 SSAA 与覆盖率着色接近（且都远离点采样） ----
    {
        const Vec2 a(2.0f, 2.0f), b(22.0f, 6.0f), c(6.0f, 22.0f);
        Canvas aa(28, 28), ssaa(28, 28), point(28, 28);
        aa.clear(kBg);
        ssaa.clear(kBg);
        point.clear(kBg);
        gfx::fillTriangle(aa, a, b, c, kFill, gfx::TriangleFillMode::CoverageAA, 4);
        gfx::fillTriangle(ssaa, a, b, c, kFill, gfx::TriangleFillMode::SSAA, 4, &kBg);
        gfx::fillTriangle(point, a, b, c, kFill);

        double sumVsAA = 0.0, sumVsPoint = 0.0;
        for (int y = 0; y < 28; ++y)
            for (int x = 0; x < 28; ++x) {
                sumVsAA += std::fabs(static_cast<double>(ssaa.pixelAt(x, y).r) - aa.pixelAt(x, y).r);
                sumVsPoint += std::fabs(static_cast<double>(ssaa.pixelAt(x, y).r) - point.pixelAt(x, y).r);
            }
        TEST_CHECK(sumVsAA < sumVsPoint);      // SSAA 更接近覆盖率着色，而非点采样

        // SSAA 只应影响三角形包围盒内的像素，不能抹掉画布上其它内容
        Canvas canvas(32, 32);
        canvas.clear(kBg);
        canvas.putPixel(1, 1, kNear);          // 包围盒之外的标记像素
        canvas.putPixel(31, 31, kNear);
        const Vec2 ta(10.0f, 10.0f), tb(28.0f, 12.0f), tc(12.0f, 28.0f);
        gfx::fillTriangle(canvas, ta, tb, tc, kFill, gfx::TriangleFillMode::SSAA, 4, &kBg);
        TEST_CHECK(canvas.pixelAt(1, 1) == kNear);
        TEST_CHECK(canvas.pixelAt(31, 31) == kNear);
    }

    // ---- 无关参数：对当前模式无意义的参数"警告并忽略" ----
    {
        const Vec2 a(2.0f, 2.0f), b(20.0f, 4.0f), c(5.0f, 21.0f);
        const Color bg(0, 0, 0, 255);

        TEST_CHECK(gfx::triangleFillWarningsEnabled());   // 默认开启

        // 只传 5 参（SolidScanline，未指定 samplesPerAxis / background）⇒ 不应有任何警告
        const std::string none = captureStderr([&] {
            Canvas cv(24, 24); cv.clear(kBg);
            gfx::fillTriangle(cv, a, b, c, kFill);
        });
        TEST_CHECK(none.empty());

        // SolidScanline + samplesPerAxis=4 ⇒ 一行关于 samplesPerAxis 的警告
        const std::string w1 = captureStderr([&] {
            Canvas cv(24, 24); cv.clear(kBg);
            gfx::fillTriangle(cv, a, b, c, kFill, gfx::TriangleFillMode::SolidScanline, 4);
        });
        TEST_CHECK(countLines(w1) == 1);
        TEST_CHECK(w1.find("samplesPerAxis") != std::string::npos);
        TEST_CHECK(w1.find("SolidScanline") != std::string::npos);

        // CoverageAA + background（samplesPerAxis 用默认 4）⇒ 一行关于 background 的警告
        const std::string w2 = captureStderr([&] {
            Canvas cv(24, 24); cv.clear(kBg);
            gfx::fillTriangle(cv, a, b, c, kFill, gfx::TriangleFillMode::CoverageAA, 4, &bg);
        });
        TEST_CHECK(countLines(w2) == 1);
        TEST_CHECK(w2.find("background") != std::string::npos);
        TEST_CHECK(w2.find("CoverageAA") != std::string::npos);

        // 关闭诊断开关后：上述"无关参数"调用不再有任何警告
        gfx::triangleFillWarningsEnabled() = false;
        TEST_CHECK(captureStderr([&] {
            Canvas cv(24, 24); cv.clear(kBg);
            gfx::fillTriangle(cv, a, b, c, kFill, gfx::TriangleFillMode::SolidScanline, 4, &bg);
        }).empty());
        gfx::triangleFillWarningsEnabled() = true;   // 还原，避免影响后续用例
    }

    // ---- alpha 混合 ----
    {
        TEST_CHECK(gfx::blend(rgb(10, 20, 30), kBg) == rgb(10, 20, 30));   // 不透明：直接取源
        TEST_CHECK(gfx::blend(rgba(255, 0, 0, 0), kBg) == kBg);            // 全透明：保持底色
        const Color half = gfx::blend(rgba(255, 255, 255, 128), kBg);
        TEST_CHECK(half.r >= 127 && half.r <= 128);
        TEST_CHECK(half.a == 255);

        Canvas canvas(2, 2);
        canvas.clear(kBg);
        canvas.blendPixel(0, 0, rgba(255, 255, 255, 128));
        TEST_CHECK(canvas.pixelAt(0, 0).r > 100 && canvas.pixelAt(0, 0).r < 160);
        canvas.putPixel(1, 0, rgba(255, 255, 255, 128));   // putPixel **忽略** a（覆盖式）
        TEST_CHECK(canvas.pixelAt(1, 0) == rgba(255, 255, 255, 128));
    }

    // ---- 深度缓冲：testAndSet 语义 ----
    {
        DepthBuffer depth(4, 4);
        TEST_NEAR(depth.depthAt(0, 0), DepthBuffer::kFar, 1e-6f);
        TEST_CHECK(depth.testAndSet(0, 0, 0.5f));       // 首次写入
        TEST_NEAR(depth.depthAt(0, 0), 0.5f, 1e-6f);
        TEST_CHECK(!depth.testAndSet(0, 0, 0.7f));      // 更远：拒绝
        TEST_NEAR(depth.depthAt(0, 0), 0.5f, 1e-6f);
        TEST_CHECK(depth.testAndSet(0, 0, 0.2f));       // 更近：接受
        TEST_NEAR(depth.depthAt(0, 0), 0.2f, 1e-6f);
        TEST_CHECK(!depth.testAndSet(0, 0, 0.2f));      // 同深度：不覆盖（GL_LESS 语义）
        depth.clear();
        TEST_NEAR(depth.depthAt(0, 0), DepthBuffer::kFar, 1e-6f);
    }

    // ---- 深度测试：遮挡正确 + 与绘制顺序无关 ----
    {
        const Vec2 nearA(0, 0), nearB(10, 0), nearC(0, 10);      // 近三角形：x,y >= 0 且 x+y <= 10
        const Vec2 farA(2, 2), farB(12, 2), farC(2, 12);         // 远三角形：x,y >= 2 且 x+y <= 14
        const Vec3 zNear(-0.5f, -0.5f, -0.5f);
        const Vec3 zFar(0.5f, 0.5f, 0.5f);

        Canvas c1(16, 16), c2(16, 16);
        DepthBuffer d1(16, 16), d2(16, 16);
        c1.clear(kBg); c2.clear(kBg);

        gfx::fillTriangleDepth(c1, d1, nearA, nearB, nearC, zNear, kNear);   // 先近后远
        gfx::fillTriangleDepth(c1, d1, farA, farB, farC, zFar, kFar);

        gfx::fillTriangleDepth(c2, d2, farA, farB, farC, zFar, kFar);        // 先远后近
        gfx::fillTriangleDepth(c2, d2, nearA, nearB, nearC, zNear, kNear);

        TEST_CHECK(countPixelDiff(c1, c2) == 0);              // 顺序无关
        // 重叠区（x,y ≥ 2 且 x+y ≤ 10）：如像素 (3,3) ⇒ 中心 (3.5,3.5)，x+y = 7
        TEST_CHECK(c1.pixelAt(3, 3) == kNear);
        TEST_CHECK(c2.pixelAt(3, 3) == kNear);
        // 只在远三角形内（x+y > 10 且 ≤ 14）：如像素 (5,5) ⇒ 中心 (5.5,5.5)，x+y = 11
        TEST_CHECK(c1.pixelAt(5, 5) == kFar);
        TEST_CHECK(c2.pixelAt(5, 5) == kFar);
        // 只在近三角形内（x 或 y < 2）：如像素 (0,0) ⇒ 中心 (0.5,0.5)
        TEST_CHECK(c1.pixelAt(0, 0) == kNear);
    }

    return gfx::test::summary();
}
