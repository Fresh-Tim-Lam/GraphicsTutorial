// test_view.cpp —— gfx 相机/视图测试：世界↔像素映射、fit 语义、3D 观察与投影
//
// 运行：ctest --test-dir build -C Release -R test_view
//
// 测试策略：
//   1. 2D：worldToPixel / pixelToWorld 往返；函数式接口、Mat3 矩阵、变换链（asTransform）
//      **三条入口同一公式**（逐点对照，防未来改一处漏一处）。
//   2. fit：contain（等比、完整装入、居中、留白）与退化输入（线/点/空集）。
//   3. 与历史手写映射公式（polygon_fill.cpp 曾用的 toPixelSpace，现已被 View 替代）对照，证明等价。
//   4. 3D：lookAt 与透视/正交投影用**手算已知值**校验（不依赖被测实现自身推导）。
//   5. 3D 投影链路：近平面裁剪（防"镜像鬼影"）、视口原点偏移（分屏）、drawLine3D。

#include "gfx_test.hpp"

#include <gfx/render/view.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using gfx::Canvas;
using gfx::Color;
using gfx::View;
using gfx::rgb;
using gfx::math::Mat3;
using gfx::math::Mat4;
using gfx::math::Transform;
using gfx::math::Vec2;
using gfx::math::Vec3;
using gfx::math::Vec4;

namespace {

constexpr float kEps = 1e-4f;

// 用 Mat3 作用于齐次点（等价于调用方的 `M * Vec3(x, y, 1)`）
Vec2 applyMat3(const Mat3& m, const Vec2& p) {
    const Vec3 r = m * Vec3(p.x, p.y, 1.0f);
    return Vec2(r.x, r.y);
}

// ---------- 1. 2D 基本映射 ----------
void test2DMapping() {
    View view;
    view.setViewport(800, 600);

    // 默认状态：观察中心在原点、缩放 1
    TEST_NEAR(view.scale(), 1.0f, kEps);
    TEST_NEAR(view.center().x, 0.0f, kEps);
    TEST_NEAR(view.center().y, 0.0f, kEps);

    // 世界原点 → 视口中心
    const Vec2 origin = view.worldToPixel(Vec2(0.0f, 0.0f));
    TEST_NEAR(origin.x, 400.0f, kEps);
    TEST_NEAR(origin.y, 300.0f, kEps);

    // x 增大 → 像素 x 增大；世界 y 增大 → 像素 y 减小（y 轴翻转）
    const Vec2 px = view.worldToPixel(Vec2(10.0f, 0.0f));
    TEST_NEAR(px.x, 410.0f, kEps);
    TEST_NEAR(px.y, 300.0f, kEps);
    const Vec2 py = view.worldToPixel(Vec2(0.0f, 10.0f));
    TEST_NEAR(py.x, 400.0f, kEps);
    TEST_NEAR(py.y, 290.0f, kEps);

    // 平移 + 缩放后的往返
    view.setScale(2.5f).setCenter(Vec2(-3.0f, 7.0f));
    const Vec2 w(12.5f, -4.25f);
    const Vec2 roundTrip = view.pixelToWorld(view.worldToPixel(w));
    TEST_NEAR(roundTrip.x, w.x, kEps);
    TEST_NEAR(roundTrip.y, w.y, kEps);

    // 批量接口与逐点调用一致
    const std::vector<Vec2> world{Vec2(1.0f, 2.0f), Vec2(-5.0f, 3.5f), Vec2(0.0f, -8.0f)};
    const std::vector<Vec2> pixels = view.worldToPixel(world);
    TEST_CHECK(pixels.size() == world.size());
    for (std::size_t i = 0; i < world.size(); ++i) {
        const Vec2 one = view.worldToPixel(world[i]);
        TEST_NEAR(pixels[i].x, one.x, kEps);
        TEST_NEAR(pixels[i].y, one.y, kEps);
    }
    const std::vector<Vec2> backToWorld = view.pixelToWorld(pixels);
    for (std::size_t i = 0; i < world.size(); ++i) {
        TEST_NEAR(backToWorld[i].x, world[i].x, kEps);
        TEST_NEAR(backToWorld[i].y, world[i].y, kEps);
    }

    // setViewport(Canvas) 便捷重载
    Canvas canvas(320, 240);
    view.setViewport(canvas);
    TEST_CHECK(view.viewportWidth() == 320 && view.viewportHeight() == 240);
}

// ---------- 2. 三条入口同一公式：函数 / Mat3 / 变换链 ----------
void testMatrixConsistency() {
    View view;
    view.setViewport(640, 480).setScale(3.0f).setCenter(Vec2(4.0f, -2.0f));

    const Mat3 m = view.matrix();
    const Mat3 inv = view.inverseMatrix();
    const std::vector<Vec2> probes{Vec2(0.0f, 0.0f), Vec2(1.0f, -1.0f), Vec2(-20.0f, 15.0f), Vec2(4.0f, -2.0f)};

    // ① matrix() 与 worldToPixel() 等价
    for (const Vec2& w : probes) {
        const Vec2 a = applyMat3(m, w);
        const Vec2 b = view.worldToPixel(w);
        TEST_NEAR(a.x, b.x, kEps);
        TEST_NEAR(a.y, b.y, kEps);
    }

    // ② inverseMatrix() 与 pixelToWorld() 等价
    for (const Vec2& p : probes) {
        const Vec2 a = applyMat3(inv, p);
        const Vec2 b = view.pixelToWorld(p);
        TEST_NEAR(a.x, b.x, kEps);
        TEST_NEAR(a.y, b.y, kEps);
    }

    // ③ asTransform() 与 worldToPixel() 等价（几何层变换链入口）
    const Transform<3> chain = view.asTransform();
    for (const Vec2& w : probes) {
        const Vec3 r = chain.apply(Vec3(w.x, w.y, 1.0f));
        const Vec2 b = view.worldToPixel(w);
        TEST_NEAR(r.x, b.x, kEps);
        TEST_NEAR(r.y, b.y, kEps);
    }

    // ④ 逆矩阵互逆：inv · m ≈ 单位阵
    const Mat3 prod = inv * m;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            TEST_NEAR(prod(r, c), (r == c) ? 1.0f : 0.0f, 1e-3f);
}

// ---------- 3. fit：contain 语义与退化输入 ----------
void testFit() {
    // 100×100 世界方框装入 800×600 视口：以较紧的高为准 ⇒ scale = min(8, 6) = 6
    View view;
    view.setViewport(800, 600);
    view.fitWorld(Vec2(0.0f, 0.0f), Vec2(100.0f, 100.0f));
    TEST_NEAR(view.scale(), 6.0f, kEps);
    TEST_NEAR(view.center().x, 50.0f, kEps);
    TEST_NEAR(view.center().y, 50.0f, kEps);

    const Vec2 bl = view.worldToPixel(Vec2(0.0f, 0.0f));       // 左下角 → 像素左下
    TEST_NEAR(bl.x, 100.0f, kEps);
    TEST_NEAR(bl.y, 600.0f, kEps);
    const Vec2 tr = view.worldToPixel(Vec2(100.0f, 100.0f));   // 右上角 → 像素右上
    TEST_NEAR(tr.x, 700.0f, kEps);
    TEST_NEAR(tr.y, 0.0f, kEps);

    // 留白 24：可用区 752×552 ⇒ scale = min(7.52, 5.52) = 5.52，四边恰好各余 24px
    View marginView;
    marginView.setViewport(800, 600);
    marginView.fitWorld(Vec2(0.0f, 0.0f), Vec2(100.0f, 100.0f), 24.0f);
    TEST_NEAR(marginView.scale(), 5.52f, kEps);
    const Vec2 mbl = marginView.worldToPixel(Vec2(0.0f, 0.0f));
    TEST_NEAR(mbl.x, 124.0f, kEps);
    TEST_NEAR(mbl.y, 576.0f, kEps);
    const Vec2 mtr = marginView.worldToPixel(Vec2(100.0f, 100.0f));
    TEST_NEAR(mtr.x, 676.0f, kEps);
    TEST_NEAR(mtr.y, 24.0f, kEps);

    // 宽扁矩形（400×100）⇒ 以宽为准 scale = 2；两轴同一缩放（等比、不拉伸）
    View wide;
    wide.setViewport(800, 600);
    wide.fitWorld(Vec2(0.0f, 0.0f), Vec2(400.0f, 100.0f));
    TEST_NEAR(wide.scale(), 2.0f, kEps);
    const Vec2 wOrigin = wide.worldToPixel(Vec2(0.0f, 0.0f));
    TEST_NEAR(wide.worldToPixel(Vec2(1.0f, 0.0f)).x - wOrigin.x, 2.0f, kEps);
    TEST_NEAR(wOrigin.y - wide.worldToPixel(Vec2(0.0f, 1.0f)).y, 2.0f, kEps);

    // 退化：水平线段（高 0）⇒ 用宽定缩放，不除零
    View segment;
    segment.setViewport(800, 600);
    segment.fitWorld(Vec2(0.0f, 5.0f), Vec2(100.0f, 5.0f));
    TEST_NEAR(segment.scale(), 8.0f, kEps);              // 800 / 100
    TEST_NEAR(segment.center().y, 5.0f, kEps);

    // 退化：单个点 ⇒ 只对准中心，缩放保持原值
    View point;
    point.setViewport(800, 600).setScale(3.0f);
    point.fitWorld(Vec2(2.0f, 3.0f), Vec2(2.0f, 3.0f));
    TEST_NEAR(point.scale(), 3.0f, kEps);
    TEST_NEAR(point.center().x, 2.0f, kEps);
    TEST_NEAR(point.center().y, 3.0f, kEps);

    // fitPoints 与 fitWorld(包围盒) 等价
    const std::vector<Vec2> pts{Vec2(10.0f, 10.0f), Vec2(60.0f, 30.0f), Vec2(-20.0f, 90.0f)};
    View byPoints;
    byPoints.setViewport(800, 600);
    byPoints.fitPoints(pts, 24.0f);
    View byBox;
    byBox.setViewport(800, 600);
    byBox.fitWorld(Vec2(-20.0f, 10.0f), Vec2(60.0f, 90.0f), 24.0f);
    TEST_NEAR(byPoints.scale(), byBox.scale(), kEps);
    TEST_NEAR(byPoints.center().x, byBox.center().x, kEps);
    TEST_NEAR(byPoints.center().y, byBox.center().y, kEps);

    // 空点集：不改动视图
    View untouched;
    untouched.setViewport(800, 600).setScale(2.0f).setCenter(Vec2(1.0f, 1.0f));
    untouched.fitPoints(std::vector<Vec2>{});
    TEST_NEAR(untouched.scale(), 2.0f, kEps);
    TEST_NEAR(untouched.center().x, 1.0f, kEps);
}

// ---------- 4. 可见世界包围盒 ----------
void testVisibleWorld() {
    View view;
    view.setViewport(800, 600).setScale(2.0f).setCenter(Vec2(10.0f, 20.0f));

    const Vec2 mn = view.visibleWorldMin();
    const Vec2 mx = view.visibleWorldMax();
    TEST_NEAR(mn.x, -190.0f, kEps);   // 10 − 400/2
    TEST_NEAR(mx.x, 210.0f, kEps);
    TEST_NEAR(mn.y, -130.0f, kEps);   // 20 − 300/2
    TEST_NEAR(mx.y, 170.0f, kEps);

    // 与 pixelToWorld 的角落一致（像素左上角 ↔ 世界 (min.x, max.y)）
    const Vec2 topLeft = view.pixelToWorld(Vec2(0.0f, 0.0f));
    TEST_NEAR(topLeft.x, mn.x, kEps);
    TEST_NEAR(topLeft.y, mx.y, kEps);
    const Vec2 bottomRight = view.pixelToWorld(Vec2(800.0f, 600.0f));
    TEST_NEAR(bottomRight.x, mx.x, kEps);
    TEST_NEAR(bottomRight.y, mn.y, kEps);
}

// ---------- 5. 参数守卫与链式调用 ----------
void testGuardsAndChaining() {
    View view;
    view.setViewport(800, 600);
    view.setViewport(0, -5);                                  // 非正视口：忽略
    TEST_CHECK(view.viewportWidth() == 800 && view.viewportHeight() == 600);

    view.setScale(-2.0f);                                     // 非正缩放：忽略
    TEST_NEAR(view.scale(), 1.0f, kEps);
    view.setScale(3.0f);
    view.zoomBy(0.0f);                                        // 非正倍率：忽略
    TEST_NEAR(view.scale(), 3.0f, kEps);
    view.zoomBy(2.0f);
    TEST_NEAR(view.scale(), 6.0f, kEps);

    View chained;
    chained.setViewport(100, 100).setScale(2.0f).setCenter(Vec2(1.0f, 2.0f)).zoomBy(0.5f);
    TEST_CHECK(chained.viewportWidth() == 100 && chained.viewportHeight() == 100);
    TEST_NEAR(chained.scale(), 1.0f, kEps);
    TEST_NEAR(chained.center().x, 1.0f, kEps);
}

// ---------- 6. 与历史手写映射公式（polygon_fill.cpp 曾用的 toPixelSpace）对照 ----------
// 独立复刻该手写实现（包围盒等比 + 居中 + y 翻转；现已由 View 承担），验证两者等价。
std::vector<Vec2> referenceToPixelSpace(const std::vector<Vec2>& world, int width, int height, float margin) {
    float minX = world.front().x, maxX = world.front().x;
    float minY = world.front().y, maxY = world.front().y;
    for (const Vec2& v : world) {
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
    }
    const float spanX = maxX - minX;
    const float spanY = maxY - minY;
    const float usableW = static_cast<float>(width) - 2.0f * margin;
    const float usableH = static_cast<float>(height) - 2.0f * margin;
    const float scale = std::min(usableW / spanX, usableH / spanY);
    const float offsetX = (static_cast<float>(width) - spanX * scale) * 0.5f;
    const float offsetY = (static_cast<float>(height) - spanY * scale) * 0.5f;

    std::vector<Vec2> out;
    out.reserve(world.size());
    for (const Vec2& v : world) {
        out.push_back(Vec2(offsetX + (v.x - minX) * scale,
                           offsetY + (maxY - v.y) * scale));
    }
    return out;
}

void testMatchesTutorialMapping() {
    const std::vector<Vec2> world{Vec2(100.0f, 80.0f), Vec2(220.0f, 80.0f), Vec2(260.0f, 180.0f),
                                  Vec2(160.0f, 260.0f), Vec2(60.0f, 180.0f)};
    const int width = 800;
    const int height = 600;
    const float margin = 24.0f;

    View view;
    view.setViewport(width, height).fitPoints(world, margin);
    const std::vector<Vec2> viaView = view.worldToPixel(world);
    const std::vector<Vec2> viaReference = referenceToPixelSpace(world, width, height, margin);

    TEST_CHECK(viaView.size() == viaReference.size());
    for (std::size_t i = 0; i < viaView.size(); ++i) {
        TEST_NEAR(viaView[i].x, viaReference[i].x, 1e-3f);
        TEST_NEAR(viaView[i].y, viaReference[i].y, 1e-3f);
    }
}

// ---------- 7. 3D 观察矩阵（lookAt） ----------
void testLookAt() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));

    // 手算：右轴 (1,0,0)、上轴 (0,1,0)、后轴 (0,0,1)，平移沿 −z 5 单位
    const float expected[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, -5.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
    const Mat4& v = view.viewMatrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(v(r, c), expected[r * 4 + c], kEps);

    // 世界原点 → 相机空间 (0,0,−5)：位于相机前方 5 个单位
    const Vec4 cam = v * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    TEST_NEAR(cam.x, 0.0f, kEps);
    TEST_NEAR(cam.y, 0.0f, kEps);
    TEST_NEAR(cam.z, -5.0f, kEps);

    // 退化输入：eye == target、up 与视线平行 ⇒ 保持原矩阵（单位阵）
    View degenerate;
    degenerate.setViewport(100, 100);
    degenerate.lookAt(Vec3(1.0f, 1.0f, 1.0f), Vec3(1.0f, 1.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4& dv1 = degenerate.viewMatrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(dv1(r, c), (r == c) ? 1.0f : 0.0f, kEps);

    degenerate.lookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f, 0.0f, 1.0f));
    const Mat4& dv2 = degenerate.viewMatrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(dv2(r, c), (r == c) ? 1.0f : 0.0f, kEps);
}

// ---------- 8. 3D 投影与像素投影 ----------
void testProjection() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(1.5707963f, 1.0f, 1.0f, 101.0f);   // fovY = 90°（弧度）、aspect = 1

    // 手算：f = 1/tan(45°) = 1 ⇒ [1,0,0,0; 0,1,0,0; 0,0,−1.02,−2.02; 0,0,−1,0]
    const Mat4& p = view.projectionMatrix();
    TEST_NEAR(p(0, 0), 1.0f, kEps);
    TEST_NEAR(p(1, 1), 1.0f, kEps);
    TEST_NEAR(p(2, 2), -1.02f, kEps);
    TEST_NEAR(p(2, 3), -2.02f, kEps);
    TEST_NEAR(p(3, 2), -1.0f, kEps);

    // 近 / 远平面 ⇒ NDC z = −1 / +1
    const Vec4 nearClip = p * Vec4(0.0f, 0.0f, -1.0f, 1.0f);
    TEST_NEAR(nearClip.z / nearClip.w, -1.0f, 1e-3f);
    const Vec4 farClip = p * Vec4(0.0f, 0.0f, -101.0f, 1.0f);
    TEST_NEAR(farClip.z / farClip.w, 1.0f, 1e-3f);

    // 相机正前方（世界原点）⇒ 视口中心
    const Vec2 centerPx = view.projectToPixel(Vec3(0.0f, 0.0f, 0.0f));
    TEST_NEAR(centerPx.x, 400.0f, 1e-2f);
    TEST_NEAR(centerPx.y, 300.0f, 1e-2f);

    // fovY = 90°、aspect = 1 ⇒ 相机前 5 单位处半宽 = 5 ⇒ 世界 (5,0,0) 恰在右边界
    const Vec2 rightPx = view.projectToPixel(Vec3(5.0f, 0.0f, 0.0f));
    TEST_NEAR(rightPx.x, 800.0f, 1e-2f);
    TEST_NEAR(rightPx.y, 300.0f, 1e-2f);

    // y 翻转：世界 (0,5,0) 在像素**上**边界
    const Vec2 upPx = view.projectToPixel(Vec3(0.0f, 5.0f, 0.0f));
    TEST_NEAR(upPx.x, 400.0f, 1e-2f);
    TEST_NEAR(upPx.y, 0.0f, 1e-2f);

    // 正交投影：近/远平面同样映射到 NDC z = ∓1
    View ortho;
    ortho.setViewport(800, 600);
    ortho.setOrthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    const Mat4& op = ortho.projectionMatrix();
    const Vec4 o1 = op * Vec4(0.0f, 0.0f, -1.0f, 1.0f);
    TEST_NEAR(o1.z / o1.w, -1.0f, 1e-3f);
    const Vec4 o2 = op * Vec4(0.0f, 0.0f, -11.0f, 1.0f);
    TEST_NEAR(o2.z / o2.w, 1.0f, 1e-3f);

    // 非法投影参数 ⇒ 保持原矩阵（初始为单位阵）
    View bad;
    bad.setViewport(100, 100);
    bad.setPerspective(0.0f, 1.0f, 1.0f, 10.0f);          // fovY = 0
    TEST_NEAR(bad.projectionMatrix()(0, 0), 1.0f, kEps);
    bad.setPerspective(1.0f, 0.0f, 1.0f, 10.0f);          // aspect = 0
    TEST_NEAR(bad.projectionMatrix()(0, 0), 1.0f, kEps);
    bad.setPerspective(1.0f, 1.0f, -1.0f, 10.0f);         // zNear < 0
    TEST_NEAR(bad.projectionMatrix()(0, 0), 1.0f, kEps);
    bad.setPerspective(1.0f, 1.0f, 5.0f, 1.0f);           // zFar ≤ zNear
    TEST_NEAR(bad.projectionMatrix()(0, 0), 1.0f, kEps);
    bad.setOrthographic(1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 10.0f);   // right ≤ left
    TEST_NEAR(bad.projectionMatrix()(3, 3), 1.0f, kEps);
}

// ---------- 9. 视口原点偏移（分屏 / 多视口） ----------
void testViewportOrigin() {
    View left;
    left.setViewport(400, 600);                                  // 左半屏
    View right;
    right.setViewport(400, 600).setViewportOrigin(400, 0);       // 右半屏

    TEST_CHECK(left.viewportX() == 0 && left.viewportY() == 0);
    TEST_CHECK(right.viewportX() == 400 && right.viewportY() == 0);

    // 同一世界点：两个视口的像素结果相差一个视口宽度
    const Vec2 w(3.0f, 4.0f);
    const Vec2 pl = left.worldToPixel(w);
    const Vec2 pr = right.worldToPixel(w);
    TEST_NEAR(pr.x - pl.x, 400.0f, kEps);
    TEST_NEAR(pr.y - pl.y, 0.0f, kEps);

    // 往返仍成立
    const Vec2 back = right.pixelToWorld(pr);
    TEST_NEAR(back.x, w.x, kEps);
    TEST_NEAR(back.y, w.y, kEps);

    // 三条入口仍同一公式（矩阵 / 变换链也含原点偏移）
    const Vec2 viaMatrix = applyMat3(right.matrix(), w);
    TEST_NEAR(viaMatrix.x, pr.x, kEps);
    TEST_NEAR(viaMatrix.y, pr.y, kEps);
    const Vec3 viaChain = right.asTransform().apply(Vec3(w.x, w.y, 1.0f));
    TEST_NEAR(viaChain.x, pr.x, kEps);
    TEST_NEAR(viaChain.y, pr.y, kEps);

    // 可见世界范围只取决于观察中心与缩放，与视口原点无关
    TEST_NEAR(right.visibleWorldMin().x, left.visibleWorldMin().x, kEps);
    TEST_NEAR(right.visibleWorldMax().y, left.visibleWorldMax().y, kEps);
}

// ---------- 10. 3D 线段投影：近平面裁剪 ----------
void testProjectionClipping() {
    // 相机在 (0,0,5) 看向原点；fovY = 90°、aspect = 1 ⇒ 相机前方 d 处的半宽 = d
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(1.5707963f, 1.0f, 1.0f, 101.0f);

    // ① 全可见线段：projectSegment 与逐点 projectToPixel 一致
    Vec2 a, b;
    TEST_CHECK(view.projectSegment(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), a, b));
    const Vec2 ea = view.projectToPixel(Vec3(0.0f, 0.0f, 0.0f));
    const Vec2 eb = view.projectToPixel(Vec3(2.0f, 0.0f, 0.0f));
    TEST_NEAR(a.x, ea.x, 1e-2f);
    TEST_NEAR(a.y, ea.y, 1e-2f);
    TEST_NEAR(b.x, eb.x, 1e-2f);
    TEST_NEAR(b.y, eb.y, 1e-2f);

    // ② 整段在相机后方（世界 z > 5 ⇒ 相机空间 z > 0 ⇒ 齐次 w < 0）⇒ 不可见
    TEST_CHECK(!view.projectSegment(Vec3(0.0f, 0.0f, 6.0f), Vec3(0.0f, 0.0f, 20.0f), a, b));
    TEST_CHECK(!view.projectSegment(Vec3(-1.0f, 0.0f, 10.0f), Vec3(20.0f, 0.0f, 10.0f), a, b));

    // ③ 一端在相机后方 ⇒ 裁到近平面：结果必须落在**正确的一侧**（而不是镜像到反侧）
    //    世界 (0,0,0) 在相机前 5（w = 5，可见）；世界 (10,0,10) 在相机后（w = −5）
    //    裁剪点是两者间 w = zNear = 1 处 ⇒ 世界 (4,0,4) ⇒ NDC x = 4 ⇒ 像素 x = 2000（画面右外侧）
    //    若不裁剪：clip.w = −5 ⇒ NDC x = 4 / −5 = −0.8 ⇒ 像素 x = 80（**错误地翻到左侧**）
    Vec2 ca, cb;
    TEST_CHECK(view.projectSegment(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 0.0f, 10.0f), ca, cb));
    TEST_NEAR(ca.x, 400.0f, 1e-2f);     // 可见端：世界原点 → 视口中心
    TEST_NEAR(ca.y, 300.0f, 1e-2f);
    TEST_CHECK(cb.x > 800.0f);          // 裁剪端：画面右侧之外（未裁剪会镜像到 x ≈ 80）

    // ④ 正交投影：w ≡ 1，裁剪不触发
    View ortho;
    ortho.setViewport(800, 600);
    ortho.setOrthographic(-2.0f, 2.0f, -2.0f, 2.0f, 1.0f, 101.0f);
    Vec2 oa, ob;
    TEST_CHECK(ortho.projectSegment(Vec3(-1.0f, 0.0f, -1.0f), Vec3(1.0f, 0.0f, -1.0f), oa, ob));
    TEST_NEAR(oa.x, 200.0f, 1e-2f);     // NDC x = −0.5 ⇒ 像素 0.25 × 800
    TEST_NEAR(ob.x, 600.0f, 1e-2f);     // NDC x = +0.5 ⇒ 像素 0.75 × 800
    TEST_NEAR(oa.y, 300.0f, 1e-2f);
}

// ---------- 11. 3D 线段绘制（drawLine3D） ----------
void testDrawLine3D() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(1.5707963f, 1.0f, 1.0f, 101.0f);

    const Color red = rgb(255, 0, 0);

    // 世界 x 轴线段 (0,0,0) → (2,0,0)：相机前 5 处半宽 = 5 ⇒ NDC x = 2/5 = 0.4 ⇒ 像素 x = 560
    Canvas canvas(800, 600);
    view.drawLine3D(canvas, Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), red);
    TEST_CHECK(canvas.pixelAt(400, 300) == red);   // 起点 → 视口中心
    TEST_CHECK(canvas.pixelAt(560, 300) == red);   // 终点 → NDC 0.4

    // 整段在相机后方：一个像素都不该画（否则就是"鬼影"）
    Canvas empty(800, 600);
    view.drawLine3D(empty, Vec3(0.0f, 0.0f, 10.0f), Vec3(20.0f, 0.0f, 10.0f), red);
    int painted = 0;
    for (int y = 0; y < empty.height(); ++y)
        for (int x = 0; x < empty.width(); ++x)
            if (empty.pixelAt(x, y) == red) ++painted;
    TEST_CHECK(painted == 0);
}

// ---------- 12. 视体参数查询与 fov ↔ l/r/b/t 换算 ----------
void testFrustumQueries() {
    View view;
    view.setViewport(800, 600);

    // 透视：fovY = 90°、aspect = 2、nearZ = 1 ⇒ top = 1、right = 2（top = nearZ·tan(fovY/2)）
    view.setPerspective(1.5707963f, 2.0f, 1.0f, 101.0f);
    TEST_CHECK(view.projectionKind() == gfx::ProjectionKind::Perspective);
    const gfx::ViewFrustum& f = view.frustum();
    TEST_NEAR(f.top, 1.0f, 1e-4f);
    TEST_NEAR(f.bottom, -1.0f, 1e-4f);
    TEST_NEAR(f.right, 2.0f, 1e-4f);
    TEST_NEAR(f.left, -2.0f, 1e-4f);
    TEST_NEAR(f.nearZ, 1.0f, 1e-5f);
    TEST_NEAR(f.farZ, 101.0f, 1e-5f);
    TEST_NEAR(view.fovYRadians(), 1.5707963f, 1e-5f);
    TEST_NEAR(view.aspect(), 2.0f, 1e-5f);
    TEST_NEAR(f.width(), 4.0f, 1e-4f);

    // 换算互逆：fov/aspect → 视体 → 回到 fov/aspect
    float fovBack = 0.0f, aspectBack = 0.0f;
    TEST_CHECK(View::perspectiveOfFrustum(f, fovBack, aspectBack));
    TEST_NEAR(fovBack, 1.5707963f, 1e-5f);
    TEST_NEAR(aspectBack, 2.0f, 1e-5f);

    // 正交：视体参数原样保留；aspect = 视体宽/高
    View ortho;
    ortho.setViewport(800, 600);
    ortho.setOrthographic(-3.0f, 3.0f, -1.5f, 1.5f, 0.5f, 50.0f);
    TEST_CHECK(ortho.projectionKind() == gfx::ProjectionKind::Orthographic);
    TEST_NEAR(ortho.frustum().right, 3.0f, 1e-5f);
    TEST_NEAR(ortho.frustum().nearZ, 0.5f, 1e-5f);
    TEST_NEAR(ortho.aspect(), 2.0f, 1e-5f);
    TEST_NEAR(ortho.fovYRadians(), 0.0f, 1e-5f);

    // 退化视体：反换算失败
    float dummyFov = 1.0f, dummyAspect = 1.0f;
    gfx::ViewFrustum degenerate;
    degenerate.nearZ = 0.0f;
    TEST_CHECK(!View::perspectiveOfFrustum(degenerate, dummyFov, dummyAspect));
}

// ---------- 13. NDC 深度输出 ----------
void testNdcDepth() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(1.5707963f, 1.0f, 1.0f, 101.0f);

    Vec3 ndc;
    // 近平面上的点（世界 z = 5 − 1 = 4）⇒ z_ndc = −1
    TEST_CHECK(view.projectToNdc(Vec3(0.0f, 0.0f, 4.0f), ndc));
    TEST_NEAR(ndc.z, -1.0f, 1e-3f);
    // 远平面（世界 z = 5 − 101 = −96）⇒ z_ndc = +1
    TEST_CHECK(view.projectToNdc(Vec3(0.0f, 0.0f, -96.0f), ndc));
    TEST_NEAR(ndc.z, 1.0f, 1e-3f);
    // 相机前 5 单位（世界原点）：z_ndc ≈ 0.616
    TEST_CHECK(view.projectToNdc(Vec3(0.0f, 0.0f, 0.0f), ndc));
    TEST_NEAR(ndc.z, 0.616f, 1e-2f);
    TEST_NEAR(ndc.x, 0.0f, 1e-4f);
    TEST_NEAR(ndc.y, 0.0f, 1e-4f);

    // 相机后方：失败
    TEST_CHECK(!view.projectToNdc(Vec3(0.0f, 0.0f, 6.0f), ndc));
}

// ---------- 14. 透视 → 正交的两阶段分解 ----------
void testProjectionDecomposition() {
    View view;
    view.setViewport(800, 600);
    view.setPerspective(1.5707963f, 1.5f, 1.0f, 101.0f);

    const View::ProjectionDecomposition d = view.projectionDecomposition();

    // ① 分解成立：ortho · perspToOrtho == projectionMatrix()
    const Mat4 prod = d.ortho * d.perspToOrtho;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(prod(r, c), view.projectionMatrix()(r, c), 1e-4f);

    // ② 压缩矩阵结构：前两行 = nearZ·I，最后一行产生 w' = −z
    TEST_NEAR(d.perspToOrtho(0, 0), 1.0f, 1e-5f);
    TEST_NEAR(d.perspToOrtho(1, 1), 1.0f, 1e-5f);
    TEST_NEAR(d.perspToOrtho(3, 2), -1.0f, 1e-5f);

    // ③ ortho == 用"近平面矩形"调 setOrthographic 得到的矩阵
    const gfx::ViewFrustum f = view.frustum();
    View ref;
    ref.setViewport(800, 600);
    ref.setOrthographic(f.left, f.right, f.bottom, f.top, f.nearZ, f.farZ);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(d.ortho(r, c), ref.projectionMatrix()(r, c), 1e-5f);

    // ④ 正交投影：压缩 = 单位阵，ortho = projectionMatrix()
    View ortho;
    ortho.setViewport(800, 600);
    ortho.setOrthographic(-2.0f, 2.0f, -2.0f, 2.0f, 1.0f, 101.0f);
    const View::ProjectionDecomposition od = ortho.projectionDecomposition();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            TEST_NEAR(od.perspToOrtho(r, c), (r == c) ? 1.0f : 0.0f, 1e-6f);
}

// ---------- 15. 相机基 / 位置 / 相机→世界 / ModelView / 相对运动不变性 ----------
void testCameraQueries() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));

    // 相机基：eye 在 +z 看原点 ⇒ 右 +x、上 +y、后 +z
    TEST_NEAR(view.cameraRight().x, 1.0f, 1e-5f);
    TEST_NEAR(view.cameraUp().y, 1.0f, 1e-5f);
    TEST_NEAR(view.cameraBack().z, 1.0f, 1e-5f);

    // 相机位置
    const Vec3 e = view.eyePosition();
    TEST_NEAR(e.x, 0.0f, 1e-4f);
    TEST_NEAR(e.y, 0.0f, 1e-4f);
    TEST_NEAR(e.z, 5.0f, 1e-4f);

    // 相机 → 世界矩阵 C = V⁻¹（与通用逆互校）；C 的第 4 列 = 相机位置
    const Mat4 c = view.cameraToWorldMatrix();
    Mat4 invV;
    TEST_CHECK(view.viewMatrix().inverse(invV));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            TEST_NEAR(c(i, j), invV(i, j), 1e-4f);
    TEST_NEAR(c(0, 3), e.x, 1e-4f);
    TEST_NEAR(c(1, 3), e.y, 1e-4f);
    TEST_NEAR(c(2, 3), e.z, 1e-4f);

    // C 把相机空间原点送到相机位置
    const Vec4 originCam = c * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    TEST_NEAR(originCam.x, e.x, 1e-4f);
    TEST_NEAR(originCam.z, e.z, 1e-4f);

    // ModelView V·M == 两步（先 M 后 V）
    const Mat4 model = gfx::math::transform3d::translate(Vec3(1.0f, 2.0f, 0.0f));
    const Vec4 viaMv = view.modelViewMatrix(model) * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 viaTwo = view.viewMatrix() * (model * Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    TEST_NEAR(viaMv.x, viaTwo.x, 1e-5f);
    TEST_NEAR(viaMv.y, viaTwo.y, 1e-5f);
    TEST_NEAR(viaMv.z, viaTwo.z, 1e-5f);

    // 相对运动不变性：对相机与物体施加同一刚体变换 G ⇒ V(G·相机) · G == V(相机)
    const Mat4 g = gfx::math::transform3d::frame(Vec3(0.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
                                                 Vec3(0.0f, 0.0f, 1.0f), Vec3(10.0f, -3.0f, 2.0f));
    const Vec4 eyeG = g * Vec4(0.0f, 0.0f, 5.0f, 1.0f);
    const Vec4 tgtG = g * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec3 upG = gfx::math::applyVector(g, Vec3(0.0f, 1.0f, 0.0f));
    View view2;
    view2.setViewport(800, 600);
    view2.lookAt(Vec3(eyeG.x, eyeG.y, eyeG.z), Vec3(tgtG.x, tgtG.y, tgtG.z), upG);

    const Mat4 v2g = view2.viewMatrix() * g;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            TEST_NEAR(v2g(i, j), view.viewMatrix()(i, j), 1e-4f);
}

// ---------- 16. 反投影射线（pixelToRay） ----------
void testPixelToRay() {
    View view;
    view.setViewport(800, 600);
    view.lookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    view.setPerspective(1.5707963f, 800.0f / 600.0f, 1.0f, 101.0f);

    // 视口中心：射线起点 = 相机位置，方向 = 相机前方（−cameraBack）
    Vec3 origin, dir;
    TEST_CHECK(view.pixelToRay(400.0f, 300.0f, origin, dir));
    TEST_NEAR(origin.x, 0.0f, 1e-3f);
    TEST_NEAR(origin.y, 0.0f, 1e-3f);
    TEST_NEAR(origin.z, 5.0f, 1e-3f);
    TEST_NEAR(dir.x, 0.0f, 1e-3f);
    TEST_NEAR(dir.y, 0.0f, 1e-3f);
    TEST_NEAR(dir.z, -1.0f, 1e-3f);

    // 画面右侧像素：方向偏 +x
    TEST_CHECK(view.pixelToRay(600.0f, 300.0f, origin, dir));
    TEST_CHECK(dir.x > 0.1f);
    // 画面上方像素：方向偏 +y（世界 y 向上）
    TEST_CHECK(view.pixelToRay(400.0f, 100.0f, origin, dir));
    TEST_CHECK(dir.y > 0.1f);

    // 往返：射线上一点投影回去应落在原像素
    Vec3 o2, d2;
    TEST_CHECK(view.pixelToRay(500.0f, 200.0f, o2, d2));
    const Vec3 p(o2.x + d2.x * 10.0f, o2.y + d2.y * 10.0f, o2.z + d2.z * 10.0f);
    const Vec2 back = view.projectToPixel(p);
    TEST_NEAR(back.x, 500.0f, 1e-1f);
    TEST_NEAR(back.y, 200.0f, 1e-1f);
}

} // namespace

int main() {
    test2DMapping();
    testMatrixConsistency();
    testFit();
    testVisibleWorld();
    testGuardsAndChaining();
    testMatchesTutorialMapping();
    testLookAt();
    testProjection();
    testViewportOrigin();
    testProjectionClipping();
    testDrawLine3D();
    testFrustumQueries();
    testNdcDepth();
    testProjectionDecomposition();
    testCameraQueries();
    testPixelToRay();
    return gfx::test::summary();
}
