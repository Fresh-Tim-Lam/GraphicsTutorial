// test_raytracer.cpp —— gfx 渲染层：光照模型与递归光线跟踪
//
// 运行：ctest --test-dir build -C Release -R test_raytracer
//
// 测试策略（尽量复用 geom 的既有求交函数搭小场景：球 + 平面 + 圆柱 + 盒）：
//   1. 局部光照手算核对：单光源垂直照射（N·L=1），逐分量与手算值比对。
//   2. 阴影：遮挡回调返回"被挡" ⇒ 只剩 emission + ambient·diffuse（与有光时差值显著）；
//      并用"平面 + 挡光球"做一次端到端（经 RayTracer 的阴影光线路径）。
//   3. 镜面反射：反射光打到红色自发光面 ⇒ 显著红色；对照"反射光打空" ⇒ 只剩背景。
//   4. 折射：透明球正对入射 ⇒ 结果有限且穿过（= 背景）；全反射 ⇒ 不产生折射光（结果为零）。
//   5. 递归深度：depth=1 只有局部光照；depth=0 / 负 ⇒ 零向量。
//   6. 背景：光线打空 ⇒ 返回 background。
//   7. 与作业三对齐：空场景 / 球距离与法向 / 漫反射手算 / 阴影 / 45°反射 / 全反射 / 圆柱命中，
//      用 gfx 的类型与函数重跑一遍，逐条打印"期望 vs 实际"。
//   8. 超采样：n=1 与 n=3 渲染同一场景，不同的像素数 > 0；n=1 与"直接单射线渲染"完全一致（差 0）。

#include "gfx_test.hpp"

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/geom.hpp>
#include <gfx/math/vec.hpp>
#include <gfx/render/raytracer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using gfx::Canvas;
using gfx::Color;
using gfx::HitRecord;
using gfx::Light;
using gfx::Material;
using gfx::Ray;
using gfx::RayTracer;
using gfx::SurfaceHit;
using gfx::math::Vec3;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// 归一化向量（零向量回退为 (0,0,-1)）
Vec3 unit(float x, float y, float z) {
    Vec3 v;
    if (!gfx::math::normalize(Vec3(x, y, z), v)) v = Vec3(0.0f, 0.0f, -1.0f);
    return v;
}

bool nearVec(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

bool isFiniteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 针孔相机：把像素坐标映射成世界射线（测试内自给，避免依赖 View）。
struct Camera {
    Vec3  eye, forward, right, up;
    float width = 1.0f, height = 1.0f, tanHalfFov = 1.0f;

    Ray rayFor(float px, float py) const {
        const float nx = (px / width) * 2.0f - 1.0f;
        const float ny = 1.0f - (py / height) * 2.0f;
        const float aspect = width / height;
        Vec3 dir;
        gfx::math::normalize(forward + right * (nx * aspect * tanHalfFov) + up * (ny * tanHalfFov), dir);
        return Ray(eye, dir);
    }
};

Camera makeCamera(const Vec3& eye, const Vec3& target, float fovYDeg, float width, float height) {
    Camera c;
    c.eye = eye;
    gfx::math::normalize(target - eye, c.forward);
    gfx::math::normalize(gfx::math::cross(c.forward, Vec3(0, 1, 0)), c.right);
    c.up = gfx::math::cross(c.right, c.forward);
    c.tanHalfFov = std::tan(fovYDeg * 0.5f * kPi / 180.0f);
    c.width = width;
    c.height = height;
    return c;
}

} // namespace

int main() {
    // ================= 1. 局部光照手算核对 =================
    Material litMat;
    litMat.diffuse = Vec3(0.8f, 0.2f, 0.1f);
    litMat.specular = Vec3(0.5f, 0.5f, 0.5f);
    litMat.emission = Vec3(0.1f, 0.0f, 0.0f);
    litMat.shininess = 32.0f;
    const Light litLight{Vec3(0.0f, 0.0f, 5.0f), Vec3(1.0f, 1.0f, 1.0f), 1.0f};

    const Vec3 P(0.0f, 0.0f, 0.0f), N(0.0f, 0.0f, 1.0f), V(0.0f, 0.0f, 1.0f);
    auto noShadow = [](const Vec3&, const Vec3&, float) { return false; };
    auto alwaysShadow = [](const Vec3&, const Vec3&, float) { return true; };

    Vec3 litNoShadow;
    {
        litNoShadow = gfx::shadeLocal(P, N, V, litMat, std::vector<Light>{litLight}, noShadow, 0.05f);
        // 手算：N·L=1、H=normalize(L+V)=(0,0,1)、N·H=1 ⇒ spec=1
        const Vec3 expected(0.1f + 0.05f * 0.8f + 0.8f + 0.5f,
                            0.0f + 0.05f * 0.2f + 0.2f + 0.5f,
                            0.0f + 0.05f * 0.1f + 0.1f + 0.5f);
        TEST_CHECK(nearVec(litNoShadow, expected, 1e-4f));
        std::printf("  [1] 局部光照手算: 实际=(%.4f, %.4f, %.4f) 期望=(%.4f, %.4f, %.4f)\n",
                    litNoShadow.x, litNoShadow.y, litNoShadow.z, expected.x, expected.y, expected.z);
    }

    // ================= 2. 阴影 =================
    Vec3 litShadowed;
    {
        litShadowed = gfx::shadeLocal(P, N, V, litMat, std::vector<Light>{litLight}, alwaysShadow, 0.05f);
        const Vec3 expected = litMat.emission + litMat.diffuse * 0.05f;   // 只剩 emission + ambient·diffuse
        TEST_CHECK(nearVec(litShadowed, expected, 1e-4f));
        TEST_CHECK(litNoShadow.length() - litShadowed.length() > 0.5f);   // 与有光时差值显著
        std::printf("  [2] 阴影(回调): 遮挡后=(%.4f, %.4f, %.4f) 有光=(%.4f, %.4f, %.4f) 差值=%.4f\n",
                    litShadowed.x, litShadowed.y, litShadowed.z,
                    litNoShadow.x, litNoShadow.y, litNoShadow.z,
                    litNoShadow.length() - litShadowed.length());
    }
    {
        // 端到端：平面 z=0（法向 +z）+ 挡光球在"交点→光源"之间；主光线从 (0,3,3) 斜射到原点。
        Material planeMat;
        planeMat.diffuse = Vec3(0.8f, 0.8f, 0.8f);
        const Vec3 planNormal(0.0f, 0.0f, 1.0f);
        const Vec3 planePoint(0.0f, 0.0f, 0.0f);
        const Vec3 sphereCenter(0.0f, 0.0f, 2.5f);
        const float sphereRadius = 0.5f;
        const Light l{Vec3(0.0f, 0.0f, 5.0f), Vec3(1.0f, 1.0f, 1.0f), 1.0f};

        auto makeHit = [&](bool withOccluder) {
            return RayTracer::HitFunction([&, withOccluder](const Ray& ray, SurfaceHit& out) -> bool {
                HitRecord h;
                bool found = false;
                if (gfx::intersectPlane(ray, planePoint, planNormal, h)) {
                    out.geometry = h; out.material = &planeMat; found = true;
                }
                HitRecord hs;
                if (withOccluder && gfx::intersectSphere(ray, sphereCenter, sphereRadius, hs) &&
                    (!found || hs.t < out.geometry.t)) {
                    out.geometry = hs; out.material = &planeMat; found = true;
                }
                return found;
            });
        };

        RayTracer rtOpen(makeHit(false), std::vector<Light>{l});
        RayTracer rtBlocked(makeHit(true), std::vector<Light>{l});
        const Ray primary(Vec3(0.0f, 3.0f, 3.0f), Vec3(0.0f, -3.0f, -3.0f));

        const Vec3 open = rtOpen.trace(primary, 3);
        const Vec3 blocked = rtBlocked.trace(primary, 3);
        TEST_CHECK(nearVec(open, Vec3(0.84f, 0.84f, 0.84f), 1e-4f));      // emission + 0.05·diffuse + diffuse·1
        TEST_CHECK(nearVec(blocked, Vec3(0.04f, 0.04f, 0.04f), 1e-4f));   // 只剩 ambient·diffuse
        std::printf("  [2] 阴影(端到端): 无遮挡=(%.4f,%.4f,%.4f) 有遮挡=(%.4f,%.4f,%.4f)\n",
                    open.x, open.y, open.z, blocked.x, blocked.y, blocked.z);
    }

    // ================= 3. 镜面反射 + 5. 递归深度（共用场景）=================
    Material mirrorMat;
    mirrorMat.diffuse = Vec3(0.3f, 0.3f, 0.3f);
    mirrorMat.reflectivity = 1.0f;
    mirrorMat.shininess = 32.0f;
    Material redMat;                        // 红色自发光面
    redMat.emission = Vec3(1.0f, 0.0f, 0.0f);

    const Vec3 mirrorPoint(0.0f, 0.0f, 0.0f), mirrorNormal(0.0f, 0.0f, 1.0f);
    const Vec3 redPoint(3.0f, 0.0f, 0.0f), redNormal(-1.0f, 0.0f, 0.0f);

    auto makeMirrorHit = [&](bool withRed) {
        return RayTracer::HitFunction([&, withRed](const Ray& ray, SurfaceHit& out) -> bool {
            HitRecord h;
            bool found = false;
            if (gfx::intersectPlane(ray, mirrorPoint, mirrorNormal, h)) {
                out.geometry = h; out.material = &mirrorMat; found = true;
            }
            HitRecord hr;
            if (withRed && gfx::intersectPlane(ray, redPoint, redNormal, hr) &&
                (!found || hr.t < out.geometry.t)) {
                out.geometry = hr; out.material = &redMat; found = true;
            }
            return found;
        });
    };

    // 主光线以 45° 打到镜面 z=0（入射 (1,0,-1)/√2 ⇒ 反射 (1,0,1)/√2）
    const Ray mirrorPrimary(Vec3(-1.0f, 0.0f, 3.0f), Vec3(1.0f, 0.0f, -1.0f));
    {
        RayTracer rtA(makeMirrorHit(true), std::vector<Light>{});
        RayTracer rtB(makeMirrorHit(false), std::vector<Light>{});
        rtA.setBackground(Vec3(0.0f, 0.1f, 0.2f));
        rtB.setBackground(Vec3(0.0f, 0.1f, 0.2f));

        const Vec3 reflectedRed = rtA.trace(mirrorPrimary, 5);   // 反射光打到红色自发光面
        TEST_CHECK(reflectedRed.x > 0.9f);
        TEST_CHECK(reflectedRed.y < 0.05f);
        TEST_CHECK(reflectedRed.z < 0.05f);

        const Vec3 reflectedMiss = rtB.trace(mirrorPrimary, 5);  // 反射光打空 ⇒ 只剩镜面局部光照 + 背景
        TEST_CHECK(nearVec(reflectedMiss, Vec3(0.015f, 0.115f, 0.215f), 1e-4f));
        std::printf("  [3] 镜面反射: 命中红面=(%.4f,%.4f,%.4f) 打空=(%.4f,%.4f,%.4f)\n",
                    reflectedRed.x, reflectedRed.y, reflectedRed.z,
                    reflectedMiss.x, reflectedMiss.y, reflectedMiss.z);

        // ---- 5. 递归深度 ----
        const Vec3 localOnly = rtA.trace(mirrorPrimary, 1);      // 只算局部光照（0.05·diffuse）
        TEST_CHECK(nearVec(localOnly, Vec3(0.015f, 0.015f, 0.015f), 1e-4f));
        TEST_CHECK(!nearVec(localOnly, reflectedRed, 1e-3f));    // 深度不同 ⇒ 结果不同
        TEST_CHECK(nearVec(rtA.trace(mirrorPrimary, 0), Vec3(0.0f, 0.0f, 0.0f), 1e-6f));
        TEST_CHECK(nearVec(rtA.trace(mirrorPrimary, -3), Vec3(0.0f, 0.0f, 0.0f), 1e-6f));
        std::printf("  [5] 递归深度: depth=1(局部)=(%.4f,%.4f,%.4f) depth=5=(%.4f,%.4f,%.4f) depth<=0=黑\n",
                    localOnly.x, localOnly.y, localOnly.z,
                    reflectedRed.x, reflectedRed.y, reflectedRed.z);
    }

    // ================= 4. 折射（透明球 / 全反射）=================
    {
        // (a) 透明球 n=1.5 正对入射：穿过球体 ⇒ 结果 = 背景（有限、无 NaN/Inf）
        Material glass;
        glass.transparency = 1.0f;
        glass.refractiveIndex = 1.5f;
        const Vec3 glassCenter(0.0f, 0.0f, 0.0f);
        const float glassRadius = 1.0f;
        auto glassHit = RayTracer::HitFunction([&](const Ray& ray, SurfaceHit& out) -> bool {
            HitRecord h;
            if (!gfx::intersectSphere(ray, glassCenter, glassRadius, h)) return false;
            out.geometry = h; out.material = &glass; return true;
        });
        RayTracer rtGlass(glassHit, std::vector<Light>{});
        rtGlass.setBackground(Vec3(0.1f, 0.1f, 0.1f));
        const Vec3 through = rtGlass.trace(Ray(Vec3(0.0f, 0.0f, -3.0f), Vec3(0.0f, 0.0f, 1.0f)), 5);
        TEST_CHECK(isFiniteVec(through));
        TEST_CHECK(nearVec(through, Vec3(0.1f, 0.1f, 0.1f), 1e-4f));

        // (b) 全反射：从"内侧"以 45° 命中（超过临界角）⇒ refract 返回 false ⇒ 不产生折射光 ⇒ 结果为零
        Material tirMat;
        tirMat.transparency = 1.0f;
        tirMat.refractiveIndex = 1.5f;
        const Vec3 tirNormal(0.0f, 0.0f, 1.0f);
        auto tirHit = RayTracer::HitFunction([&](const Ray& ray, SurfaceHit& out) -> bool {
            HitRecord h;
            if (!gfx::intersectPlane(ray, Vec3(0.0f, 0.0f, 0.0f), tirNormal, h)) return false;
            out.geometry = h; out.material = &tirMat; return true;
        });
        RayTracer rtTir(tirHit, std::vector<Light>{});
        rtTir.setBackground(Vec3(0.2f, 0.2f, 0.2f));
        const Vec3 tir = rtTir.trace(Ray(Vec3(0.0f, 0.0f, -1.0f), unit(1.0f, 0.0f, 1.0f)), 3);
        TEST_CHECK(isFiniteVec(tir));
        TEST_CHECK(nearVec(tir, Vec3(0.0f, 0.0f, 0.0f), 1e-6f));
        std::printf("  [4] 折射: 透明球穿透=(%.4f,%.4f,%.4f) 全反射结果=(%.4f,%.4f,%.4f)(无折射光)\n",
                    through.x, through.y, through.z, tir.x, tir.y, tir.z);
    }

    // ================= 6. 背景 =================
    {
        RayTracer rt(RayTracer::HitFunction([](const Ray&, SurfaceHit&) { return false; }),
                     std::vector<Light>{});
        const Vec3 bg(0.05f, 0.06f, 0.07f);
        rt.setBackground(bg);
        TEST_CHECK(nearVec(rt.trace(Ray(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)), 5), bg, 1e-6f));
        TEST_CHECK(nearVec(rt.trace(Ray(Vec3(1.0f, 2.0f, 3.0f), unit(1.0f, 1.0f, 1.0f)), 3), bg, 1e-6f));
        std::printf("  [6] 背景: 打空 ⇒ (%.4f, %.4f, %.4f)\n", bg.x, bg.y, bg.z);
    }

    // ================= 7. 与作业三对齐（逐条期望 vs 实际）=================
    std::printf("作业三对齐（用 gfx 的类型与函数重跑 7 组数值用例）:\n");
    auto reportVec = [](const char* name, const char* what, const Vec3& exp, const Vec3& got, float eps) {
        std::printf("  [%s] %-11s 期望=(%.4f, %.4f, %.4f) 实际=(%.4f, %.4f, %.4f) %s\n",
                    name, what, exp.x, exp.y, exp.z, got.x, got.y, got.z,
                    nearVec(got, exp, eps) ? "OK" : "MISMATCH");
        TEST_CHECK(nearVec(got, exp, eps));
    };
    auto reportScalar = [](const char* name, const char* what, float exp, float got, float eps) {
        const bool ok = std::fabs(got - exp) <= eps;
        std::printf("  [%s] %-11s 期望=%.6f 实际=%.6f %s\n", name, what, exp, got, ok ? "OK" : "MISMATCH");
        TEST_CHECK(ok);
    };

    // ① 空场景 ⇒ 背景色
    {
        RayTracer rt(RayTracer::HitFunction([](const Ray&, SurfaceHit&) { return false; }),
                     std::vector<Light>{});
        rt.setBackground(Vec3(0.05f, 0.06f, 0.07f));
        reportVec("空场景", "背景色", Vec3(0.05f, 0.06f, 0.07f),
                  rt.trace(Ray(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)), 5), 1e-6f);
    }
    // ② 球距离与法向
    {
        HitRecord h;
        const bool ok = gfx::intersectSphere(Ray(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                                             Vec3(0.0f, 0.0f, 5.0f), 2.0f, h);
        TEST_CHECK(ok);
        reportScalar("球距离", "t", 3.0f, h.t, 1e-5f);
        reportVec("球法向", "normal", Vec3(0.0f, 0.0f, -1.0f), h.normal, 1e-5f);
    }
    // ③ 漫反射手算（与第 1 项同场景）
    reportVec("漫反射手算", "局部色", Vec3(1.44f, 0.71f, 0.605f), litNoShadow, 1e-4f);
    // ④ 阴影（与第 2 项同场景）
    reportVec("阴影", "遮挡后", Vec3(0.14f, 0.01f, 0.005f), litShadowed, 1e-4f);
    // ⑤ 45° 反射
    {
        const Vec3 r = gfx::math::reflect(unit(1.0f, -1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        reportVec("45°反射", "方向", unit(1.0f, 1.0f, 0.0f), r, 1e-5f);
    }
    // ⑥ 全反射：refract 返回 false；跟踪器不产生折射光（结果为零）
    {
        Vec3 outV;
        const bool ok = gfx::math::refract(unit(1.0f, -0.1f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 1.5f, outV);
        std::printf("  [全反射]    折射返回    期望=false 实际=%s %s\n", ok ? "true" : "false",
                    ok ? "MISMATCH" : "OK");
        TEST_CHECK(!ok);

        Material tirMat;
        tirMat.transparency = 1.0f;
        tirMat.refractiveIndex = 1.5f;
        RayTracer rt(RayTracer::HitFunction([&](const Ray& ray, SurfaceHit& out) -> bool {
            HitRecord h;
            if (!gfx::intersectPlane(ray, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), h)) return false;
            out.geometry = h; out.material = &tirMat; return true;
        }), std::vector<Light>{});
        rt.setBackground(Vec3(0.2f, 0.2f, 0.2f));
        reportVec("全反射", "跟踪结果", Vec3(0.0f, 0.0f, 0.0f),
                  rt.trace(Ray(Vec3(0.0f, 0.0f, -1.0f), unit(1.0f, 0.0f, 1.0f)), 3), 1e-6f);
    }
    // ⑦ 圆柱命中
    {
        HitRecord h;
        const bool ok = gfx::intersectCylinder(Ray(Vec3(5.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f)),
                                               Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 1.0f, 2.0f, h);
        TEST_CHECK(ok);
        reportScalar("圆柱", "t", 4.0f, h.t, 1e-5f);
        reportVec("圆柱", "normal", Vec3(1.0f, 0.0f, 0.0f), h.normal, 1e-5f);
    }

    // ================= 8. 超采样 =================
    {
        Material sphereMat;
        sphereMat.diffuse = Vec3(0.8f, 0.3f, 0.3f);
        sphereMat.specular = Vec3(0.6f, 0.6f, 0.6f);
        sphereMat.shininess = 32.0f;
        Material planeMat;
        planeMat.diffuse = Vec3(0.5f, 0.5f, 0.55f);
        const Light l{Vec3(3.0f, 5.0f, 3.0f), Vec3(1.0f, 1.0f, 1.0f), 1.0f};

        auto hitFn = RayTracer::HitFunction([&](const Ray& ray, SurfaceHit& out) -> bool {
            HitRecord h;
            bool found = false;
            if (gfx::intersectPlane(ray, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), h)) {
                out.geometry = h; out.material = &planeMat; found = true;
            }
            HitRecord hs;
            if (gfx::intersectSphere(ray, Vec3(0.0f, 0.0f, 0.0f), 1.0f, hs) &&
                (!found || hs.t < out.geometry.t)) {
                out.geometry = hs; out.material = &sphereMat; found = true;
            }
            return found;
        });

        const int W = 48, H = 36;
        const Camera cam = makeCamera(Vec3(0.0f, 0.8f, 4.0f), Vec3(0.0f, 0.0f, 0.0f), 45.0f,
                                      static_cast<float>(W), static_cast<float>(H));
        RayTracer rt(hitFn, std::vector<Light>{l});
        rt.setBackground(Vec3(0.1f, 0.12f, 0.16f));

        Canvas c1(W, H), c3(W, H);
        rt.setSamplesPerAxis(1);
        rt.render(c1, [&](float px, float py) { return cam.rayFor(px, py); }, 3);
        rt.setSamplesPerAxis(3);
        rt.render(c3, [&](float px, float py) { return cam.rayFor(px, py); }, 3);

        int diff = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (c1.pixelAt(x, y) != c3.pixelAt(x, y)) ++diff;
        TEST_CHECK(diff > 0);   // 超采样改变了边缘像素

        // n=1 应与"直接单射线渲染"逐像素完全一致（差 0）
        Canvas cManual(W, H);
        const auto to8 = [](float v) -> std::uint8_t {
            const float c = std::min(1.0f, std::max(0.0f, v));
            return static_cast<std::uint8_t>(std::lround(c * 255.0f));
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const Vec3 c = rt.trace(cam.rayFor(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f), 3);
                cManual.putPixel(x, y, Color(to8(c.x), to8(c.y), to8(c.z)));
            }
        int diffManual = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (c1.pixelAt(x, y) != cManual.pixelAt(x, y)) ++diffManual;
        TEST_CHECK(diffManual == 0);
        std::printf("  [8] 超采样: n=1 vs n=3 不同像素=%d (共 %d)；n=1 vs 单射线渲染 差=%d\n",
                    diff, W * H, diffManual);
    }

    return gfx::test::summary();
}
