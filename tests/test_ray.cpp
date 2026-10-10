// test_ray.cpp —— gfx 几何层：光线求交（反射/折射/球/平面/三角形/圆柱/AABB）与 BVH 加速
//
// 运行：ctest --test-dir build -C Release -R test_ray
//
// 测试策略：
//   1. reflect / refract：手算核对（含 45° 斜射、垂直入射、非轴向法向；Snell 角度；全反射）。
//   2. 各图元求交：命中/错过/擦边/起点在内/背面/退化/超出范围等边界，逐条锁定 t 与法向。
//   3. AABB：slab 法正射/错过/起点在盒内/方向分量为 0（不除零）/在背后。
//   4. BVH：随机 N 个 AABB × 随机光线，closestHit 与"线性遍历取最近"**完全一致**（同 t，容差 1e-5），
//      并统计回调调用次数证明剪枝生效（远小于 N × 光线数）。

#include "gfx_test.hpp"

#include <gfx/geom/geom.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using gfx::Aabb;
using gfx::Bvh;
using gfx::HitRecord;
using gfx::Ray;
using gfx::math::Vec2;
using gfx::math::Vec3;

namespace {

// 归一化后的向量（零向量回退为 (0,0,-1)，避免退化光线）
Vec3 unit(float x, float y, float z) {
    Vec3 v;
    if (!gfx::math::normalize(Vec3(x, y, z), v)) v = Vec3(0.0f, 0.0f, -1.0f);
    return v;
}

// 逐分量近似相等（用于法向/方向比对）
bool nearVec(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

// 单位长度校验
bool isUnit(const Vec3& v) { return std::fabs(v.length() - 1.0f) <= 1e-4f; }

// 图元求交回调（与 BVH 解耦的通用"命中小盒子"原语）：命中则写最近的 t 并返回 true。
bool boxPrimHit(const Ray& ray, int index, const std::vector<Aabb>& boxes, float& outT) {
    float tNear = 0.0f, tFar = 0.0f;
    if (!gfx::intersectAabb(ray, boxes[static_cast<std::size_t>(index)], tNear, tFar)) return false;
    outT = std::max(tNear, 1e-4f);   // 与 intersectAabb 的 tMin 约定一致
    return true;
}

// BVH 一致性：随机 N 个 AABB × rayCount 条随机光线，比较 BVH 与暴力法的最近命中。
// 返回（BVH 回调次数、暴力法调用次数、命中光线数、不一致数）；断言在函数内。
void runConsistency(int N, int rayCount, unsigned seed, const char* label) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(-10.0f, 10.0f);
    std::uniform_real_distribution<float> he(0.1f, 1.0f);
    std::uniform_real_distribution<float> ro(-20.0f, 20.0f);
    std::uniform_real_distribution<float> rd(-1.0f, 1.0f);

    std::vector<Aabb> boxes;
    boxes.reserve(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        const Vec3 c(pos(rng), pos(rng), pos(rng));
        const float h = he(rng);
        boxes.push_back(Aabb{Vec3(c.x - h, c.y - h, c.z - h), Vec3(c.x + h, c.y + h, c.z + h)});
    }

    std::vector<Ray> rays;
    rays.reserve(static_cast<std::size_t>(rayCount));
    for (int i = 0; i < rayCount; ++i) {
        rays.emplace_back(Vec3(ro(rng), ro(rng), ro(rng)), unit(rd(rng), rd(rng), rd(rng)));
    }

    Bvh bvh;
    bvh.build(boxes);
    TEST_CHECK(bvh.nodeCount() > 0);
    TEST_CHECK(bvh.leafCount() > 0);
    TEST_CHECK(bvh.nodeCount() >= bvh.leafCount());

    std::size_t bvhCalls = 0;
    std::size_t bruteCalls = 0;
    int hits = 0;
    int mismatches = 0;
    double sumBvhCalls = 0.0;

    for (const Ray& ray : rays) {
        // 暴力法：线性遍历全部图元取最近
        bool bruteFound = false;
        float bruteT = 0.0f;
        for (int i = 0; i < N; ++i) {
            ++bruteCalls;
            float t = 0.0f;
            if (boxPrimHit(ray, i, boxes, t) && (!bruteFound || t < bruteT)) {
                bruteFound = true;
                bruteT = t;
            }
        }

        // BVH：回调内统计调用次数
        std::size_t calls = 0;
        float bvhT = 0.0f;
        const bool bvhFound = bvh.closestHit(ray, [&](const Ray& r, int i, float& t) {
            ++calls;
            return boxPrimHit(r, i, boxes, t);
        }, bvhT);
        bvhCalls += calls;
        sumBvhCalls += static_cast<double>(calls);

        if (bruteFound) ++hits;
        if (bruteFound != bvhFound) {
            ++mismatches;
        } else if (bruteFound && std::fabs(bruteT - bvhT) > 1e-5f) {
            ++mismatches;
            std::fprintf(stderr, "  [BVH 不一致] %s: 暴力 t=%.6f, BVH t=%.6f\n", label, bruteT, bvhT);
        }
    }

    std::printf("  [BVH] %-7s 图元=%4d 光线=%d | 命中=%d 不一致=%d | 节点=%zu 叶子=%zu | "
                "回调: 暴力=%zu BVH=%zu (平均 %.1f, 剪枝率 %.1f%%)\n",
                label, N, rayCount, hits, mismatches, bvh.nodeCount(), bvh.leafCount(),
                bruteCalls, bvhCalls, sumBvhCalls / rayCount,
                100.0 * (1.0 - static_cast<double>(bvhCalls) / static_cast<double>(bruteCalls)));

    TEST_CHECK(mismatches == 0);                       // 结果与暴力法完全一致
    TEST_CHECK(bvhCalls < bruteCalls);                 // 剪枝生效：BVH 调用次数少于暴力法
    TEST_CHECK(bvhCalls * 4 < bruteCalls);             // 且显著少于（< 1/4）
}

} // namespace

int main() {
    // ================= 1. reflect =================
    {
        // ① 45° 斜射到法向 (0,1,0)：入射 (1,-1,0)/√2 ⇒ 反射 (1,1,0)/√2
        const Vec3 r1 = gfx::math::reflect(unit(1, -1, 0), Vec3(0, 1, 0));
        TEST_CHECK(nearVec(r1, unit(1, 1, 0)));

        // ② 垂直入射：方向反向
        const Vec3 r2 = gfx::math::reflect(Vec3(0, -1, 0), Vec3(0, 1, 0));
        TEST_CHECK(nearVec(r2, Vec3(0, 1, 0)));

        // ③ 非轴向法向：n = (1,1,0)/√2，入射 (0,-1,0) ⇒ 反射 (1,0,0)（手算：r = d − 2(d·n)n）
        const Vec3 n3 = unit(1, 1, 0);
        const Vec3 r3 = gfx::math::reflect(Vec3(0, -1, 0), n3);
        TEST_CHECK(nearVec(r3, Vec3(1, 0, 0)));

        // ④ 对任意 N 成立：二维反射（d=(1,-1)/√2，n=(0,1) ⇒ r=(1,1)/√2）
        const Vec2 r2d = gfx::math::reflect(Vec2(0.7071068f, -0.7071068f), Vec2(0, 1));
        TEST_CHECK(std::fabs(r2d.x - 0.7071068f) < 1e-4f && std::fabs(r2d.y - 0.7071068f) < 1e-4f);
    }

    // ================= 2. refract =================
    {
        // ① 空气→玻璃 eta = 1/1.5，45° 入射：手算 out = (0.4714, -0.8819, 0)
        Vec3 out(9, 9, 9);
        const bool ok1 = gfx::math::refract(unit(1, -1, 0), Vec3(0, 1, 0), 1.0f / 1.5f, out);
        TEST_CHECK(ok1);
        TEST_CHECK(nearVec(out, Vec3(0.4714045f, -0.8819171f, 0.0f)));
        TEST_CHECK(isUnit(out));
        // Snell 角度核对：sinθt = eta·sinθi = (1/1.5)·(√2/2)
        const float sinT = std::sqrt(out.x * out.x + out.z * out.z);
        TEST_NEAR(sinT, (1.0f / 1.5f) * 0.7071068f, 1e-4f);

        // ② 全反射：玻璃内部 eta = 1.5、掠射（超过临界角）⇒ 返回 false，out 不变
        Vec3 sentinel(7, 7, 7);
        const bool ok2 = gfx::math::refract(unit(1, -0.1f, 0), Vec3(0, 1, 0), 1.5f, sentinel);
        TEST_CHECK(!ok2);
        TEST_CHECK(nearVec(sentinel, Vec3(7, 7, 7)));   // 全反射时不写 out

        // ③ eta = 1 且斜入射（非全反射）⇒ 方向与入射相同
        Vec3 same(0, 0, 0);
        const Vec3 d3 = unit(1, -1, 0);
        TEST_CHECK(gfx::math::refract(d3, Vec3(0, 1, 0), 1.0f, same));
        TEST_CHECK(nearVec(same, d3));

        // ④ 垂直入射 eta = 1.5 ⇒ 方向不变（直线穿过）
        Vec3 normalThrough(0, 0, 0);
        TEST_CHECK(gfx::math::refract(Vec3(0, -1, 0), Vec3(0, 1, 0), 1.5f, normalThrough));
        TEST_CHECK(nearVec(normalThrough, Vec3(0, -1, 0)));
    }

    // ================= 3. 球 =================
    {
        HitRecord h;
        // ① 正对球心：中心 (0,0,5) r=2，射线从原点朝 +z ⇒ t = 5−2 = 3，法向 (0,0,-1)
        TEST_CHECK(gfx::intersectSphere(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), Vec3(0, 0, 5), 2.0f, h));
        TEST_CHECK(h.hit);
        TEST_NEAR(h.t, 3.0f, 1e-5f);
        TEST_CHECK(nearVec(h.point, Vec3(0, 0, 3)));
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, -1)));

        // ② 擦过：判别式 = 0（中心 (0,0,-5) r=1，射线 x=1 处沿 -z）⇒ 命中，t=5，法向 (1,0,0)
        TEST_CHECK(gfx::intersectSphere(Ray(Vec3(1, 0, 0), Vec3(0, 0, -1)), Vec3(0, 0, -5), 1.0f, h));
        TEST_NEAR(h.t, 5.0f, 1e-4f);
        TEST_CHECK(nearVec(h.normal, Vec3(1, 0, 0)));

        // ③ 完全错过
        TEST_CHECK(!gfx::intersectSphere(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), Vec3(0, 3, 5), 1.0f, h));

        // ④ 起点在球内 ⇒ 命中远面（中心与起点重合，r=2 ⇒ t=2，法向 +z）
        TEST_CHECK(gfx::intersectSphere(Ray(Vec3(0, 0, 5), Vec3(0, 0, 1)), Vec3(0, 0, 5), 2.0f, h));
        TEST_NEAR(h.t, 2.0f, 1e-4f);
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, 1)));

        // ⑤ 球整体在背后（两解均为负）⇒ 忽略
        TEST_CHECK(!gfx::intersectSphere(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), Vec3(0, 0, -5), 2.0f, h));
    }

    // ================= 4. 平面 =================
    {
        HitRecord h;
        // ① 正射命中：平面点 (0,0,5)、法向 (0,0,-1)，射线朝 +z ⇒ t=5，法向 (0,0,-1)
        TEST_CHECK(gfx::intersectPlane(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), Vec3(0, 0, 5), Vec3(0, 0, -1), h));
        TEST_NEAR(h.t, 5.0f, 1e-6f);
        TEST_CHECK(nearVec(h.point, Vec3(0, 0, 5)));
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, -1)));

        // ② 平行 ⇒ false
        TEST_CHECK(!gfx::intersectPlane(Ray(Vec3(0, 0, 0), Vec3(1, 0, 0)), Vec3(0, 0, 5), Vec3(0, 0, -1), h));

        // ③ 平面在光线背后 ⇒ false（t = −5）
        TEST_CHECK(!gfx::intersectPlane(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), Vec3(0, 0, -5), Vec3(0, 0, 1), h));
    }

    // ================= 5. 三角形（Möller–Trumbore，双面，闭区间边界口径）=================
    {
        const Vec3 a(0, 0, 0), b(4, 0, 0), c(0, 4, 0);   // z=0 平面，几何法向 (0,0,1)
        HitRecord h;

        // ① 命中心：射线 (1,1,5) 朝 −z ⇒ t=5，法向 (0,0,1)
        TEST_CHECK(gfx::intersectTriangle(Ray(Vec3(1, 1, 5), Vec3(0, 0, -1)), a, b, c, h));
        TEST_NEAR(h.t, 5.0f, 1e-5f);
        TEST_CHECK(nearVec(h.point, Vec3(1, 1, 0)));
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, 1)));

        // ② 命中边（v=0，在边 a-b 上）
        TEST_CHECK(gfx::intersectTriangle(Ray(Vec3(2, 0, 5), Vec3(0, 0, -1)), a, b, c, h));
        TEST_NEAR(h.t, 5.0f, 1e-5f);

        // ③ 命中顶点（u=0,v=0 即顶点 a）
        TEST_CHECK(gfx::intersectTriangle(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), a, b, c, h));
        TEST_NEAR(h.t, 5.0f, 1e-5f);

        // ④ 在外 ⇒ false
        TEST_CHECK(!gfx::intersectTriangle(Ray(Vec3(5, 5, 5), Vec3(0, 0, -1)), a, b, c, h));

        // ⑤ 退化三角形（三顶点共线）⇒ false
        TEST_CHECK(!gfx::intersectTriangle(Ray(Vec3(1, 0, 5), Vec3(0, 0, -1)),
                                           Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(4, 0, 0), h));

        // ⑥ 背面（射线从 −z 侧朝 +z）⇒ 仍命中，法向仍为几何法向 (0,0,1)（不剔除背面）
        TEST_CHECK(gfx::intersectTriangle(Ray(Vec3(1, 1, -5), Vec3(0, 0, 1)), a, b, c, h));
        TEST_NEAR(h.t, 5.0f, 1e-5f);
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, 1)));
    }

    // ================= 6. 圆柱（任意轴，含两端圆盖）=================
    {
        HitRecord h;
        // ① 侧面·正面：Y 轴圆柱中心 (0,0,0)、r=1、半高 2；射线从 (5,0,0) 朝 −x ⇒ t=4，法向 (1,0,0)
        TEST_CHECK(gfx::intersectCylinder(Ray(Vec3(5, 0, 0), Vec3(-1, 0, 0)),
                                          Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 2.0f, h));
        TEST_NEAR(h.t, 4.0f, 1e-5f);
        TEST_CHECK(nearVec(h.normal, Vec3(1, 0, 0)));

        // ② 起点在轴心：朝 +x ⇒ 命中远侧 t=1，法向 (1,0,0)
        TEST_CHECK(gfx::intersectCylinder(Ray(Vec3(0, 0, 0), Vec3(1, 0, 0)),
                                          Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 2.0f, h));
        TEST_NEAR(h.t, 1.0f, 1e-5f);
        TEST_CHECK(nearVec(h.normal, Vec3(1, 0, 0)));

        // ③ 顶端圆盖：射线从 (0,5,0) 沿 −y 正对顶盖 ⇒ t=3，法向 (0,1,0)
        TEST_CHECK(gfx::intersectCylinder(Ray(Vec3(0, 5, 0), Vec3(0, -1, 0)),
                                          Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 2.0f, h));
        TEST_NEAR(h.t, 3.0f, 1e-5f);
        TEST_CHECK(nearVec(h.normal, Vec3(0, 1, 0)));

        // ④ 超出半高被裁掉：无限圆柱在 t≈4.134 有解，但沿轴偏移 h=3
        //    – 半高 2 ⇒ 被裁掉（且与两圆盖平行）⇒ false
        TEST_CHECK(!gfx::intersectCylinder(Ray(Vec3(0.5f, 3, 5), Vec3(0, 0, -1)),
                                           Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 2.0f, h));
        //    – 半高 5 ⇒ 命中，t≈4.134，法向为单位径向 (0.5,0,0.866)
        TEST_CHECK(gfx::intersectCylinder(Ray(Vec3(0.5f, 3, 5), Vec3(0, 0, -1)),
                                          Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 5.0f, h));
        TEST_NEAR(h.t, 4.1339746f, 1e-4f);
        TEST_CHECK(isUnit(h.normal));
        TEST_CHECK(nearVec(h.normal, Vec3(0.5f, 0.0f, 0.8660254f)));

        // ⑤ 未命中：射线离轴太远
        TEST_CHECK(!gfx::intersectCylinder(Ray(Vec3(5, 5, 5), Vec3(0, 0, -1)),
                                           Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0f, 2.0f, h));

        // ⑥ 倾斜轴（轴 = (1,1,0)/√2）：射线沿 −z 命中，t=4，法向 (0,0,1)
        TEST_CHECK(gfx::intersectCylinder(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)),
                                          Vec3(0, 0, 0), Vec3(1, 1, 0), 1.0f, 3.0f, h));
        TEST_NEAR(h.t, 4.0f, 1e-4f);
        TEST_CHECK(nearVec(h.normal, Vec3(0, 0, 1)));
    }

    // ================= 7. AABB（slab）=================
    {
        const Aabb box{Vec3(-1, -1, -1), Vec3(1, 1, 1)};
        float tn = 0.0f, tf = 0.0f;

        // ① 正射相交：tNear=4，tFar=6
        TEST_CHECK(gfx::intersectAabb(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), box, tn, tf));
        TEST_NEAR(tn, 4.0f, 1e-6f);
        TEST_NEAR(tf, 6.0f, 1e-6f);

        // ② 完全错过：方向分量为 0（x），起点在 slab 外
        TEST_CHECK(!gfx::intersectAabb(Ray(Vec3(5, 5, 5), Vec3(0, 0, -1)), box, tn, tf));

        // ③ 起点在盒内：tNear<0<tFar（不除零，方向 y/x 分量为 0）
        TEST_CHECK(gfx::intersectAabb(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), box, tn, tf));
        TEST_NEAR(tn, -1.0f, 1e-6f);
        TEST_NEAR(tf, 1.0f, 1e-6f);

        // ④ 方向分量为 0 且起点在外面 ⇒ false（不崩溃、不除零）
        TEST_CHECK(!gfx::intersectAabb(Ray(Vec3(3, 0, -5), Vec3(0, 0, 1)), box, tn, tf));

        // ⑤ 盒在光线背后 ⇒ false
        TEST_CHECK(!gfx::intersectAabb(Ray(Vec3(0, 0, 5), Vec3(0, 0, 1)), box, tn, tf));
    }

    // ================= 8. BVH 与暴力法一致性 + 剪枝 =================
    std::printf("BVH 一致性（暴力法 vs BVH）:\n");
    runConsistency(200, 300, 12345u, "N=200");
    runConsistency(2000, 300, 67890u, "N=2000");

    return gfx::test::summary();
}
