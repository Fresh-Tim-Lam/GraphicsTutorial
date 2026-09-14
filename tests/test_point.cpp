// test_point.cpp —— gfx 几何层：点/点批 apply 变换测试
//
// 覆盖：单点单步/多步求值、apply 外部 Transform、点批（连续视图 + 步长视图）、原地输出、2D→3D 一致。
// 运行：ctest --test-dir build -C Release -R test_point

#include "gfx_test.hpp"
#include <gfx/point.hpp>

#include <cstddef>

using namespace gfx;
using namespace gfx::math;

namespace {
constexpr float kPi = 3.14159265358979f;

struct Particle {          // AoS：位置 + 速度，用于验证步长视图
    Vec2 pos;
    Vec2 vel;
};
} // namespace

int main() {
    // ---- 2D 单点：流畅链求值 ----
    {
        Point2D p(1.0f, 2.0f);
        const Vec2 v = p.translate(10.0f, 20.0f).rotate(kPi / 2.0f).value();
        // 书写顺序 = 作用顺序：(1,2) --T--> (11,22) --R(90°)--> (-22,11)
        TEST_NEAR(v.x, -22.0f, 1e-4f);
        TEST_NEAR(v.y, 11.0f, 1e-4f);

        // 隐式转换同 value()
        const Vec2 w = p;
        TEST_NEAR(w.x, v.x, 1e-5f);
        TEST_NEAR(w.y, v.y, 1e-5f);
    }

    // ---- 2D 单点：apply 外部存储的 Transform ----
    {
        Transform<3> t;
        t.then(transform2d::scale(2.0f)).then(transform2d::translate(Vec2(1.0f, 1.0f)));

        Point2D p(3.0f, 4.0f);
        const Vec2 a = p.apply(t);        // 无自身链：等价 t 单独作用
        TEST_NEAR(a.x, 7.0f, 1e-5f);      // 3*2+1
        TEST_NEAR(a.y, 9.0f, 1e-5f);      // 4*2+1

        // 自身链 + 外部 transform：外部变换最后作用
        Point2D q(1.0f, 1.0f);
        q.translate(1.0f, 0.0f);                        // -> (2,1)
        const Vec2 b = q.apply(Transform<3>(transform2d::scale(3.0f)));   // *3 -> (6,3)
        TEST_NEAR(b.x, 6.0f, 1e-5f);
        TEST_NEAR(b.y, 3.0f, 1e-5f);
    }

    // ---- 2D 点批：连续视图 + 输出 ----
    {
        Vec2 pts[3] = { Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };
        Vec2 out[3];

        PointBatch2D batch(pts, 3);
        TEST_CHECK(batch.count() == 3);
        batch.translate(2.0f, 3.0f).scale(2.0f);        // 先平移再缩放? 注意顺序：书写顺序即作用顺序
        batch.apply(out);

        // 书写顺序 = 先作用 translate 再 scale：(p+t)*s
        TEST_NEAR(out[0].x, 4.0f, 1e-5f);   // (0+2)*2
        TEST_NEAR(out[0].y, 6.0f, 1e-5f);   // (0+3)*2
        TEST_NEAR(out[1].x, 6.0f, 1e-5f);   // (1+2)*2
        TEST_NEAR(out[2].y, 8.0f, 1e-5f);   // (1+3)*2
    }

    // ---- 2D 点批：apply 外部 Transform + 原地输出 ----
    {
        Vec2 pts[2] = { Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };
        PointBatch2D batch(pts, 2);
        Transform<3> t(transform2d::rotate(kPi / 2.0f));
        batch.apply(t, pts);                            // 原地写回

        TEST_NEAR(pts[0].x, 0.0f, 1e-5f);
        TEST_NEAR(pts[0].y, 1.0f, 1e-5f);
        TEST_NEAR(pts[1].x, -1.0f, 1e-5f);
        TEST_NEAR(pts[1].y, 0.0f, 1e-5f);
    }

    // ---- 2D 点批：步长（AoS）视图，零拷贝且不碰干扰字段 ----
    {
        Particle ps[3] = {
            { Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f) },
            { Vec2(1.0f, 0.0f), Vec2(1.0f, 1.0f) },
            { Vec2(0.0f, 1.0f), Vec2(1.0f, 1.0f) },
        };
        Vec2 out[3];

        PointView2D view(reinterpret_cast<const float*>(&ps[0].pos), 3, sizeof(Particle));
        PointBatch2D batch(view);
        batch.translate(10.0f, 20.0f);
        batch.apply(out);

        TEST_NEAR(out[0].x, 10.0f, 1e-5f);
        TEST_NEAR(out[1].x, 11.0f, 1e-5f);
        TEST_NEAR(out[2].y, 21.0f, 1e-5f);
        TEST_NEAR(ps[2].vel.x, 1.0f, 1e-6f);            // 干扰字段未变
    }

    // ---- 3D 单点：链 + 外部 transform ----
    {
        Point3D p(1.0f, 0.0f, 0.0f);
        const Vec3 v = p.rotate(kPi / 2.0f, Axis::Z).value();
        TEST_NEAR(v.x, 0.0f, 1e-4f);
        TEST_NEAR(v.y, 1.0f, 1e-4f);

        Transform<4> t(transform3d::translate(Vec3(0.0f, 0.0f, 5.0f)));
        const Vec3 w = p.apply(t);
        TEST_NEAR(w.z, 5.0f, 1e-4f);
    }

    // ---- 3D 点批：连续 Vec3 视图 + 原地 ----
    {
        Vec3 pts[2] = { Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
        PointBatch3D batch(pts, 2);
        batch.rotate(kPi / 2.0f, Axis::Z).translate(0.0f, 0.0f, 1.0f);
        batch.apply(pts);

        TEST_NEAR(pts[0].x, 0.0f, 1e-4f);
        TEST_NEAR(pts[0].y, 1.0f, 1e-4f);
        TEST_NEAR(pts[0].z, 1.0f, 1e-4f);
        TEST_NEAR(pts[1].x, -1.0f, 1e-4f);
    }

    // ---- 3D 点批：步长视图 ----
    {
        struct V3 { Vec3 pos; float extra; } verts[2] = {
            { Vec3(0.0f, 0.0f, 0.0f), 7.0f },
            { Vec3(0.0f, 0.0f, 1.0f), 7.0f },
        };
        Vec3 out[2];
        PointView3D view(reinterpret_cast<const float*>(&verts[0].pos), 2, sizeof(V3));
        PointBatch3D batch(view);
        batch.scale(2.0f);
        batch.apply(out);

        TEST_NEAR(out[1].z, 2.0f, 1e-5f);
        TEST_NEAR(verts[0].extra, 7.0f, 1e-6f);
    }

    // ---- 取出链复用（存一个变换，多处 apply）----
    {
        Point2D src(1.0f, 1.0f);
        src.translate(5.0f, 0.0f);
        const Transform<3> saved = src.transform();      // 取出链

        Point2D a(0.0f, 0.0f);
        const Vec2 ra = a.apply(saved);
        TEST_NEAR(ra.x, 5.0f, 1e-5f);

        Vec2 pts[1] = { Vec2(2.0f, 2.0f) };
        Vec2 out[1];
        PointBatch2D batch(pts, 1);
        batch.apply(saved, out);
        TEST_NEAR(out[0].x, 7.0f, 1e-5f);
        TEST_NEAR(out[0].y, 2.0f, 1e-5f);
    }

    return gfx::test::summary();
}
