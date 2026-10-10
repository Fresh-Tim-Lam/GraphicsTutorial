// test_shading.cpp —— 光栅化着色管线测试
//
// 覆盖：顶点属性插值（正交 ⇒ 等价仿射；透视 ⇒ **透视校正**）、
//       与既有光栅化器（fillTriangleBBox）的覆盖一致性、
//       深度测试（含乱序绘制结果一致）、近平面裁剪（跨界 + 整块在相机后方）、
//       以及"着色回调解耦"（属性确实透传到片元）。
//
// 关键不变式（用来验证透视校正是否真的生效）：
//   每个被着色像素 (x, y)，把片元的**世界位置**重新投影回去，必须仍落在该像素中心 (x+0.5, y+0.5)。
//   若用了"未校正的仿射插值"，深跨度大的三角形会明显违反它（偏差可达几十像素）。
//
// 运行：ctest --test-dir build -C Release

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/render/shading.hpp>
#include <gfx/render/triangle.hpp>
#include <gfx/render/view.hpp>

#include "gfx_test.hpp"

#include <cmath>
#include <vector>

using namespace gfx;
using math::Vec2;
using math::Vec3;

namespace {

constexpr int kW = 96;
constexpr int kH = 72;

View makeIdentityView() {
    View view;
    view.setViewport(kW, kH);   // 不调 lookAt/setPerspective ⇒ 观察与投影均为单位矩阵
    return view;
}

View makePerspectiveView() {
    View view;
    view.setViewport(kW, kH);
    view.lookAt(Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(60.0f * 3.14159265358979323846f / 180.0f,
                        static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.0f);
    return view;
}

VertexAttributes vertex(float x, float y, float z, float u = 0.0f, float v = 0.0f,
                        float nx = 0.0f, float ny = 0.0f, float nz = 1.0f) {
    VertexAttributes a;
    a.position = Vec3(x, y, z);
    a.normal = Vec3(nx, ny, nz);
    a.tangent = Vec3();
    a.uv = Vec2(u, v);
    return a;
}

// 交叉检查：着色管线（用"返回常量色"的片元着色器）与既有 flat 光栅化器**覆盖的像素应完全一致**
void testCoverageMatchesFlatRasterizer() {
    const View view = makePerspectiveView();
    const ShadingTriangle tri{vertex(-0.9f, -0.7f, 0.6f), vertex(1.0f, -0.5f, -0.8f), vertex(-0.4f, 0.9f, -0.3f)};

    Canvas shaded(kW, kH);
    DepthBuffer depth(kW, kH);
    const bool ok = fillTriangleShaded(shaded, view, tri,
                                       [](const Fragment&) { return Vec3(1.0f, 1.0f, 1.0f); }, &depth);
    TEST_CHECK(ok);

    // 用同一组屏幕坐标喂给既有 flat 光栅化器
    Canvas flat(kW, kH);
    const Vec2 pa = view.projectToPixel(tri.a.position);
    const Vec2 pb = view.projectToPixel(tri.b.position);
    const Vec2 pc = view.projectToPixel(tri.c.position);
    TEST_CHECK(fillTriangle(flat, pa, pb, pc, Color(255, 255, 255), TriangleFillMode::SolidBBox));

    int diff = 0;
    int painted = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool s = shaded.pixelAt(x, y).r > 0;
            const bool f = flat.pixelAt(x, y).r > 0;
            if (s) ++painted;
            if (s != f) ++diff;
        }
    }
    TEST_CHECK(painted > 500);          // 确实画了东西
    TEST_CHECK(diff == 0);              // 覆盖完全一致（同 insideTriangle + 同包围盒）
}

// 收集画布上被着色（白）的像素中心
std::vector<Vec2> paintedPixelCenters(const Canvas& canvas) {
    std::vector<Vec2> out;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            if (canvas.pixelAt(x, y).r > 0) {
                out.push_back(Vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
            }
        }
    }
    return out;
}

// 每个片元的世界位置**重新投影**后，到"最近的被着色像素中心"的距离；取最大值。
// 与着色顺序无关（裁剪会产生多个三角形，回调顺序不等于画布扫描顺序）。
float maxReprojectionError(const View& view, const std::vector<Vec3>& positions,
                           const std::vector<Vec2>& painted) {
    float worst = 0.0f;
    for (const Vec3& p : positions) {
        const Vec2 r = view.projectToPixel(p);
        float best = 1e9f;
        for (const Vec2& q : painted) {
            const float dx = r.x - q.x;
            const float dy = r.y - q.y;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

// 正交（单位投影）：w ≡ 1 ⇒ 透视校正退化为仿射；uv 应等于屏幕空间重心坐标
void testOrthographicInterpolationIsAffine() {
    const View view = makeIdentityView();
    // 世界坐标 → 像素：x_pix = (x*0.5+0.5)*W，y_pix = (1-(y*0.5+0.5))*H
    const ShadingTriangle tri{vertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f),
                              vertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f),
                              vertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f)};

    Canvas canvas(kW, kH);
    const bool ok = fillTriangleShaded(canvas, view, tri, [](const Fragment& f) {
        return Vec3(f.uv.x, f.uv.y, 0.0f);   // 把 uv 写进颜色
    });
    TEST_CHECK(ok);

    // 取几个内部像素，与"像素中心的重心坐标"逐一比对
    const Vec2 pa = view.projectToPixel(tri.a.position);
    const Vec2 pb = view.projectToPixel(tri.b.position);
    const Vec2 pc = view.projectToPixel(tri.c.position);
    int checked = 0;
    for (int y = 4; y < kH - 4 && checked < 20; y += 7) {
        for (int x = 4; x < kW - 4 && checked < 20; x += 7) {
            const Vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            if (!insideTriangle(p, pa, pb, pc)) continue;
            math::Vec3 w;
            if (!barycentric(p, pa, pb, pc, w)) continue;
            // uv(a)=(0,0) uv(b)=(1,0) uv(c)=(0,1) ⇒ u = wb, v = wc
            const Color c = canvas.pixelAt(x, y);
            TEST_NEAR(static_cast<float>(c.r) / 255.0f, w.y, 0.01f);
            TEST_NEAR(static_cast<float>(c.g) / 255.0f, w.z, 0.01f);
            ++checked;
        }
    }
    TEST_CHECK(checked > 0);
}

// 透视校正的核心不变式：片元的世界位置重新投影后必须回到该像素中心
void testPerspectiveCorrectInterpolation() {
    const View view = makePerspectiveView();
    // 深跨度大（w 约 1.5 ~ 4.2）的倾斜三角形：仿射插值会明显违反不变式
    const ShadingTriangle tri{vertex(-1.0f, -0.8f, 1.5f), vertex(1.2f, -0.8f, -1.2f), vertex(-1.0f, 1.0f, -1.2f)};

    std::vector<Vec3> positions;
    Canvas canvas(kW, kH);
    const bool ok = fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) {
        positions.push_back(f.position);
        return Vec3(1.0f, 1.0f, 1.0f);
    });
    TEST_CHECK(ok);
    TEST_CHECK(positions.size() > 200);

    const std::vector<Vec2> painted = paintedPixelCenters(canvas);
    TEST_CHECK(painted.size() == positions.size());   // 一一对应

    const float maxError = maxReprojectionError(view, positions, painted);
    TEST_CHECK(maxError < 0.05f);   // 透视校正生效 ⇒ 误差应在浮点噪声量级
}

// 深度测试：近的赢；乱序绘制结果一致
void testDepthTest() {
    const View view = makeIdentityView();
    const ShadingTriangle nearTri{vertex(-0.6f, -0.6f, -0.5f), vertex(0.6f, -0.6f, -0.5f), vertex(-0.6f, 0.6f, -0.5f)};
    const ShadingTriangle farTri{vertex(-0.4f, -0.4f, 0.5f), vertex(0.8f, -0.4f, 0.5f), vertex(-0.4f, 0.8f, 0.5f)};

    const auto colorOf = [](const Vec3& c) { return [c](const Fragment&) { return c; }; };
    const Vec3 nearColor(1.0f, 0.0f, 0.0f);
    const Vec3 farColor(0.0f, 0.0f, 1.0f);

    // 先远后近
    Canvas a(kW, kH);
    DepthBuffer da(kW, kH);
    fillTriangleShaded(a, view, farTri, colorOf(farColor), &da);
    fillTriangleShaded(a, view, nearTri, colorOf(nearColor), &da);

    // 先近后远
    Canvas b(kW, kH);
    DepthBuffer db(kW, kH);
    fillTriangleShaded(b, view, nearTri, colorOf(nearColor), &db);
    fillTriangleShaded(b, view, farTri, colorOf(farColor), &db);

    int diff = 0;
    int nearPixels = 0;
    int farPixels = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Color ca = a.pixelAt(x, y);
            const Color cb = b.pixelAt(x, y);
            if (ca != cb) ++diff;
            if (ca.r > 0 && ca.b == 0) ++nearPixels;
            if (ca.b > 0 && ca.r == 0) ++farPixels;
        }
    }
    TEST_CHECK(diff == 0);         // 乱序结果一致 ⇒ 深度测试正确
    TEST_CHECK(nearPixels > 100);  // 近三角形可见
    TEST_CHECK(farPixels > 100);   // 远处露出的部分也可见
}

// 近平面裁剪：跨界三角形不产生"镜像鬼影"（仍满足重投影不变式）；整块在后方则不绘制
void testNearPlaneClipping() {
    const View view = makePerspectiveView();

    // a 在相机后方（z 比相机还大），b/c 在前方 ⇒ 必须被裁剪
    const ShadingTriangle straddle{vertex(-1.0f, -1.0f, 8.0f), vertex(1.0f, -1.0f, -1.0f), vertex(-1.0f, 1.0f, -1.0f)};
    std::vector<Vec3> positions;
    Canvas canvas(kW, kH);
    const bool ok = fillTriangleShaded(canvas, view, straddle, [&](const Fragment& f) {
        positions.push_back(f.position);
        return Vec3(1.0f, 1.0f, 1.0f);
    });
    TEST_CHECK(ok);
    TEST_CHECK(!positions.empty());

    const std::vector<Vec2> painted = paintedPixelCenters(canvas);
    TEST_CHECK(painted.size() == positions.size());
    const float maxError = maxReprojectionError(view, positions, painted);
    TEST_CHECK(maxError < 0.05f);   // 裁出来的新顶点同样透视正确
    std::printf("  [diagnostic] 近平面裁剪用例最大重投影误差 = %.4f px\n", static_cast<double>(maxError));

    // 整个三角形都在相机后方 ⇒ 不绘制
    const ShadingTriangle behind{vertex(-1.0f, -1.0f, 5.0f), vertex(1.0f, -1.0f, 6.0f), vertex(-1.0f, 1.0f, 7.0f)};
    Canvas empty(kW, kH);
    const bool drewBehind = fillTriangleShaded(empty, view, behind, [](const Fragment&) {
        return Vec3(1.0f, 1.0f, 1.0f);
    });
    TEST_CHECK(!drewBehind);
}

// 属性透传：法向/切线/uv 都应到达片元（着色回调解耦的前提）
void testAttributesReachFragment() {
    const View view = makeIdentityView();
    ShadingTriangle tri{vertex(-1.0f, -1.0f, 0.0f), vertex(1.0f, -1.0f, 0.0f), vertex(-1.0f, 1.0f, 0.0f)};
    tri.a.normal = Vec3(0.0f, 1.0f, 0.0f);
    tri.b.normal = Vec3(0.0f, 1.0f, 0.0f);
    tri.c.normal = Vec3(0.0f, 1.0f, 0.0f);
    tri.a.tangent = Vec3(1.0f, 0.0f, 0.0f);
    tri.b.tangent = Vec3(1.0f, 0.0f, 0.0f);
    tri.c.tangent = Vec3(1.0f, 0.0f, 0.0f);
    tri.a.uv = Vec2(0.25f, 0.75f);
    tri.b.uv = Vec2(0.25f, 0.75f);
    tri.c.uv = Vec2(0.25f, 0.75f);

    int calls = 0;
    Canvas canvas(kW, kH);
    fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) {
        ++calls;
        TEST_NEAR(f.normal.y, 1.0f, 1e-5f);
        TEST_NEAR(f.tangent.x, 1.0f, 1e-5f);
        TEST_NEAR(f.uv.x, 0.25f, 1e-5f);
        TEST_NEAR(f.uv.y, 0.75f, 1e-5f);
        TEST_NEAR(f.position.z, 0.0f, 1e-5f);
        return Vec3(1.0f, 1.0f, 1.0f);
    });
    TEST_CHECK(calls > 100);

    // 空着色器 ⇒ 不绘制
    Canvas none(kW, kH);
    TEST_CHECK(!fillTriangleShaded(none, view, tri, FragmentShader()));
}

} // namespace

int main() {
    testCoverageMatchesFlatRasterizer();
    testOrthographicInterpolationIsAffine();
    testPerspectiveCorrectInterpolation();
    testDepthTest();
    testNearPlaneClipping();
    testAttributesReachFragment();
    return gfx::test::summary();
}
