// test_shadowmap.cpp —— 阴影贴图测试
//
// 覆盖：
//   1) 配置/就绪/深度 pass 的基本行为（未配置 ⇒ 一律"受光"）；
//   2) **与解析求交一致**：把"盒子遮挡地面"的判定与 AABB 光线求交逐点对照
//      （近邻边界处用"膨胀/收缩"办法留出容差，故比较是严格的，不含模糊阈值）；
//   3) 光源后方 / 光源视锥之外的点不产生阴影；
//   4) **无自阴影（acne）**：无遮挡物时倾斜平面上的任何点都不得自阴影；
//      把两道防线（法向偏移 + 深度容差）关掉后必然出现自阴影 —— 用它证明防线确实在起作用；
//   5) PCF 的量化：半径 r ⇒ 可见性只取 1 − k/(2r+1)²，且边界上确实出现中间值；
//   6) View::forward()（阴影贴图据此取得正交光源方向）。
//
// 运行：ctest --test-dir build -C Release

#include <gfx/core/canvas.hpp>
#include <gfx/render/shading.hpp>
#include <gfx/render/shadowmap.hpp>
#include <gfx/render/view.hpp>

#include "gfx_test.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace gfx;
using math::Vec2;
using math::Vec3;

namespace {

constexpr int kMapSize = 256;
constexpr float kExtent = 6.0f;        // 光源视角正交半宽
constexpr float kLightHeight = 6.0f;

float texelWorld() { return 2.0f * kExtent / static_cast<float>(kMapSize); }

// ---------- 场景搭建 ----------

void addQuad(std::vector<ShadingTriangle>& out, const Vec3& origin, const Vec3& edgeU,
             const Vec3& edgeV, const Vec3& normal) {
    const auto vertex = [&](const Vec3& p) {
        VertexAttributes a;
        a.position = p;
        a.normal = normal;
        a.tangent = Vec3();
        a.uv = Vec2();
        return a;
    };
    const Vec3 p00 = origin;
    const Vec3 p10 = origin + edgeU;
    const Vec3 p11 = origin + edgeU + edgeV;
    const Vec3 p01 = origin + edgeV;
    out.push_back(ShadingTriangle{vertex(p00), vertex(p10), vertex(p11)});
    out.push_back(ShadingTriangle{vertex(p00), vertex(p11), vertex(p01)});
}

// 轴对齐立方体：6 个面 / 12 个三角形，法向朝外
void addBox(std::vector<ShadingTriangle>& out, const Vec3& center, float half) {
    const Vec3 normals[6] = {Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(1, 0, 0),
                             Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0)};
    for (int i = 0; i < 6; ++i) {
        const Vec3 n = normals[i];
        Vec3 edgeU;
        if (!math::normalize(math::cross(Vec3(0.0f, 1.0f, 0.0f), n), edgeU)) edgeU = Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 edgeV = math::cross(n, edgeU);
        const Vec3 origin = center + n * half - edgeU * half - edgeV * half;
        addQuad(out, origin, edgeU * (2.0f * half), edgeV * (2.0f * half), n);
    }
}

// 地面：y = 0，法向 +y
std::vector<ShadingTriangle> makeGround(float half) {
    std::vector<ShadingTriangle> out;
    addQuad(out, Vec3(-half, 0.0f, -half), Vec3(2.0f * half, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 2.0f * half),
            Vec3(0.0f, 1.0f, 0.0f));
    return out;
}

// ---------- 光源视角（都是正交 ⇒ 等效平行光）----------

// 垂直向下：视线方向 = (0, −1, 0)，阴影 = 遮挡物在平面上的**正投影**，便于解析对照
// @note up 取 +z 而非 +y —— 视线与 up 平行时 lookAt 的叉积会退化
View makeVerticalLightView() {
    View view;
    view.setViewport(kMapSize, kMapSize);
    view.setOrthographic(-kExtent, kExtent, -kExtent, kExtent, 0.1f, 20.0f);
    view.lookAt(Vec3(0.0f, kLightHeight, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
    return view;
}

// 斜射：光源在 (3, 6, 2)
View makeTiltedLightView() {
    View view;
    view.setViewport(kMapSize, kMapSize);
    view.setOrthographic(-kExtent, kExtent, -kExtent, kExtent, 0.1f, 20.0f);
    view.lookAt(Vec3(3.0f, kLightHeight, 2.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    return view;
}

// ---------- 解析参照：光线 × 轴对齐盒（slab 法）----------

bool rayHitsBox(const Vec3& origin, const Vec3& dir, const Vec3& center, float half) {
    float tNear = -1e30f;
    float tFar = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = (axis == 0) ? origin.x : ((axis == 1) ? origin.y : origin.z);
        const float d = (axis == 0) ? dir.x : ((axis == 1) ? dir.y : dir.z);
        const float c = (axis == 0) ? center.x : ((axis == 1) ? center.y : center.z);
        const float lo = c - half;
        const float hi = c + half;
        if (std::fabs(d) < 1e-8f) {
            if (o < lo || o > hi) return false;   // 该轴上不可能进入
            continue;
        }
        float t0 = (lo - o) / d;
        float t1 = (hi - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) return false;
    }
    return tFar > 0.0f;
}

// ---------- 用例 ----------

void testForwardVector() {
    View plain;   // 默认视图：观察矩阵为单位阵 ⇒ 看向 −z
    const Vec3 f0 = plain.forward();
    TEST_NEAR(f0.x, 0.0f, 1e-6f);
    TEST_NEAR(f0.y, 0.0f, 1e-6f);
    TEST_NEAR(f0.z, -1.0f, 1e-6f);

    const View tilted = makeTiltedLightView();
    Vec3 expected;
    TEST_CHECK(math::normalize(Vec3(0.0f, 0.0f, 0.0f) - Vec3(3.0f, kLightHeight, 2.0f), expected));
    TEST_NEAR(math::dot(tilted.forward(), expected), 1.0f, 1e-5f);
}

void testUnconfigured() {
    ShadowMap map;
    TEST_CHECK(!map.ready());
    TEST_CHECK(map.size() == 0);
    TEST_CHECK(map.build(makeGround(1.0f)) == 0);   // 未配置 ⇒ 深度 pass 什么也不做

    // 未配置 ⇒ 一律受光，绝不产生阴影
    TEST_NEAR(map.visibility(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)), 1.0f, 1e-6f);
    TEST_CHECK(!map.isShadowed(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
}

void testBuildBasics() {
    std::vector<ShadingTriangle> scene = makeGround(5.0f);
    addBox(scene, Vec3(0.0f, 1.0f, 0.0f), 0.5f);   // 18 个三角形

    ShadowMap map;
    map.configure(makeVerticalLightView(), kMapSize);
    TEST_CHECK(map.ready());
    TEST_CHECK(map.size() == kMapSize);
    TEST_CHECK(map.depthBuffer().width() == kMapSize);
    TEST_CHECK(map.depthBuffer().height() == kMapSize);
    TEST_CHECK(map.build(scene) > 0);
    TEST_CHECK(map.lightView().viewportWidth() == kMapSize);

    // 重复 build 结果一致（内部先 clear 再写）
    TEST_NEAR(map.visibility(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)), 0.0f, 1e-6f);
    TEST_CHECK(map.build(scene) > 0);
    TEST_NEAR(map.visibility(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)), 0.0f, 1e-6f);
}

void testVerticalAndTiltedOcclusion() {
    // 垂直光：阴影就是盒子的正投影（沿 x/z 的足迹）
    {
        const Vec3 dir(0.0f, 1.0f, 0.0f);
        std::vector<ShadingTriangle> scene = makeGround(5.0f);
        addBox(scene, Vec3(0.0f, 1.0f, 0.0f), 0.5f);

        ShadowMap map;
        map.configure(makeVerticalLightView(), kMapSize);
        TEST_CHECK(map.build(scene) > 0);

        const float margin = 5.0f * texelWorld();
        const Vec3 up(0.0f, 1.0f, 0.0f);
        int shadowedChecked = 0;
        int litChecked = 0;
        int mismatch = 0;
        for (int i = -60; i <= 60; ++i) {
            for (int j = -60; j <= 60; ++j) {
                const Vec3 p(static_cast<float>(i) * 0.05f, 0.0f, static_cast<float>(j) * 0.05f);
                if (rayHitsBox(p, dir, Vec3(0.0f, 1.0f, 0.0f), 0.5f - margin)) {
                    ++shadowedChecked;
                    if (!map.isShadowed(p, up)) ++mismatch;
                } else if (!rayHitsBox(p, dir, Vec3(0.0f, 1.0f, 0.0f), 0.5f + margin)) {
                    ++litChecked;
                    if (map.isShadowed(p, up)) ++mismatch;
                }
            }
        }
        TEST_CHECK(shadowedChecked > 50);
        TEST_CHECK(litChecked > 500);
        TEST_CHECK(mismatch == 0);
    }

    // 斜射光：阴影子区域必然落在盒子的反方向（−x, −z 侧），且与解析求交一致
    {
        std::vector<ShadingTriangle> scene = makeGround(5.0f);
        addBox(scene, Vec3(0.0f, 1.2f, 0.0f), 0.5f);

        ShadowMap map;
        map.configure(makeTiltedLightView(), kMapSize);
        TEST_CHECK(map.build(scene) > 0);

        Vec3 dir;
        TEST_CHECK(math::normalize(Vec3(3.0f, kLightHeight, 2.0f), dir));
        const float margin = 5.0f * texelWorld();
        const Vec3 up(0.0f, 1.0f, 0.0f);
        int shadowedChecked = 0;
        int litChecked = 0;
        int mismatch = 0;
        float shadowCentroidX = 0.0f;
        float shadowCentroidZ = 0.0f;
        for (int i = -60; i <= 60; ++i) {
            for (int j = -60; j <= 60; ++j) {
                const Vec3 p(static_cast<float>(i) * 0.05f, 0.0f, static_cast<float>(j) * 0.05f);
                if (rayHitsBox(p, dir, Vec3(0.0f, 1.2f, 0.0f), 0.5f - margin)) {
                    ++shadowedChecked;
                    if (!map.isShadowed(p, up)) ++mismatch;
                    shadowCentroidX += p.x;
                    shadowCentroidZ += p.z;
                } else if (!rayHitsBox(p, dir, Vec3(0.0f, 1.2f, 0.0f), 0.5f + margin)) {
                    ++litChecked;
                    if (map.isShadowed(p, up)) ++mismatch;
                }
            }
        }
        TEST_CHECK(shadowedChecked > 50);
        TEST_CHECK(litChecked > 500);
        TEST_CHECK(mismatch == 0);

        // 参考阴影区质心应偏在 −x/−z 侧（光从 +x/+z 来 ⇒ 影子往反方向投）
        const float cx = shadowCentroidX / static_cast<float>(shadowedChecked);
        const float cz = shadowCentroidZ / static_cast<float>(shadowedChecked);
        TEST_CHECK(cx < -0.3f);
        TEST_CHECK(cz < -0.2f);
    }
}

void testBehindLightAndOutsideFrustum() {
    std::vector<ShadingTriangle> scene = makeGround(5.0f);
    addBox(scene, Vec3(0.0f, 1.0f, 0.0f), 0.5f);

    ShadowMap map;
    map.configure(makeVerticalLightView(), kMapSize);
    TEST_CHECK(map.build(scene) > 0);

    const Vec3 up(0.0f, 1.0f, 0.0f);

    // 光在 y = 6：y = 7 的"接收面"在光源**后方** ⇒ 不该被投影阴影
    TEST_NEAR(map.visibility(Vec3(0.0f, 7.0f, 0.0f), up), 1.0f, 1e-6f);

    // 光源视锥（±6）之外 ⇒ 贴图里查不到，视为受光（不误判成阴影）
    TEST_NEAR(map.visibility(Vec3(20.0f, 0.0f, 0.0f), up), 1.0f, 1e-6f);
    TEST_NEAR(map.visibility(Vec3(0.0f, 0.0f, -30.0f), up), 1.0f, 1e-6f);
}

// 无遮挡物时，倾斜平面上的任何点都不该自阴影（acne）。
// 用同一块平面做深度 pass + 采样它自身，正是 acne 的触发方式。
int countSelfShadowed(const View& lightView, float normalOffset, float depthBias, float slopeScale) {
    Vec3 normal;   // 平面：过原点、法向 (1, 0.5, 0)，即 y = −2x（对光源是斜的）
    if (!math::normalize(Vec3(1.0f, 0.5f, 0.0f), normal)) return -1;

    std::vector<ShadingTriangle> plane;
    const float x0 = -2.2f;
    const Vec3 origin(x0, -2.0f * x0, -1.2f);
    addQuad(plane, origin, Vec3(4.4f, -8.8f, 0.0f), Vec3(0.0f, 0.0f, 2.4f), normal);

    ShadowMap map;
    map.configure(lightView, kMapSize);
    map.setNormalOffset(normalOffset);
    map.setDepthBias(depthBias);
    map.setSlopeScale(slopeScale);
    if (map.build(plane) == 0) return -1;

    int acne = 0;
    int samples = 0;
    for (int i = -40; i <= 40; ++i) {
        for (int j = -10; j <= 10; ++j) {
            const float x = static_cast<float>(i) * 0.05f;
            const float z = static_cast<float>(j) * 0.1f;
            const Vec3 p(x, -2.0f * x, z);
            ++samples;
            if (map.isShadowed(p, normal)) ++acne;
        }
    }
    TEST_CHECK(samples == 81 * 21);
    return acne;
}

void testNoSelfShadowAcne() {
    const View lightView = makeTiltedLightView();

    // 默认防线（法向偏移 0.02 + 容差 0.01 + 斜率缩放 2.5）⇒ 一点自阴影都不该有
    TEST_CHECK(countSelfShadowed(lightView, 0.02f, 0.01f, 2.5f) == 0);

    // 把两道防线全部关掉 ⇒ 必然出现大量自阴影（证明防线确实在起作用，而非场景本身不出问题）
    const int acneWithout = countSelfShadowed(lightView, 0.0f, 0.0f, 0.0f);
    TEST_CHECK(acneWithout > 50);

    // 只做法向偏移、不做斜率缩放 ⇒ 仍会残留自阴影（这是"斜率项"存在的理由）
    const int acneNormalOnly = countSelfShadowed(lightView, 0.02f, 0.0f, 0.0f);
    TEST_CHECK(acneNormalOnly > 0);
}

void testPcfQuantizationAndBoundary() {
    std::vector<ShadingTriangle> scene = makeGround(5.0f);
    addBox(scene, Vec3(0.0f, 1.0f, 0.0f), 0.5f);

    const Vec3 up(0.0f, 1.0f, 0.0f);
    const int radius = 2;

    ShadowMap map;
    map.configure(makeVerticalLightView(), kMapSize);
    map.setPcfRadius(radius);
    TEST_CHECK(map.pcfRadius() == radius);
    TEST_CHECK(map.build(scene) > 0);

    // 可见性只能取 1 − k/(2r+1)²，且边界附近必须出现中间值
    const float total = static_cast<float>((2 * radius + 1) * (2 * radius + 1));
    int partial = 0;
    int checks = 0;
    for (int i = -200; i <= 200; ++i) {
        const Vec3 p(static_cast<float>(i) * 0.005f, 0.0f, 0.0f);
        const float v = map.visibility(p, up);
        const float blocked = (1.0f - v) * total;
        TEST_NEAR(blocked, std::round(blocked), 1e-3f);
        ++checks;
        if (v > 1e-4f && v < 1.0f - 1e-4f) ++partial;
    }
    TEST_CHECK(checks == 401);
    TEST_CHECK(partial > 0);

    // radius = 0 ⇒ 只有 0 / 1 两种取值
    ShadowMap hard;
    hard.configure(makeVerticalLightView(), kMapSize);
    hard.setPcfRadius(0);
    TEST_CHECK(hard.build(scene) > 0);
    int hardPartial = 0;
    for (int i = -200; i <= 200; ++i) {
        const Vec3 p(static_cast<float>(i) * 0.005f, 0.0f, 0.0f);
        const float v = hard.visibility(p, up);
        if (v > 1e-4f && v < 1.0f - 1e-4f) ++hardPartial;
    }
    TEST_CHECK(hardPartial == 0);
}

} // namespace

int main() {
    testForwardVector();
    testUnconfigured();
    testBuildBasics();
    testVerticalAndTiltedOcclusion();
    testBehindLightAndOutsideFrustum();
    testNoSelfShadowAcne();
    testPcfQuantizationAndBoundary();
    return gfx::test::summary();
}
