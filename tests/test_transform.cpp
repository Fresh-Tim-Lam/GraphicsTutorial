// test_transform.cpp —— 常用变换矩阵测试（平移/缩放/旋转/翻转 2D/3D）
//
// 运行：ctest --test-dir build -C Release -R test_transform

#include "gfx_test.hpp"
#include <gfx/math/math.hpp>

#include <cmath>

using namespace gfx::math;

namespace {
constexpr float kPi = 3.14159265358979f;
}

int main() {
    // ---- 2D：平移 ----
    {
        Mat3 t = transform2d::translate(Vec2(5.0f, -3.0f));
        Vec3 p = t * Vec3(1.0f, 2.0f, 1.0f);
        TEST_NEAR(p.x, 6.0f, 1e-5f);
        TEST_NEAR(p.y, -1.0f, 1e-5f);
        TEST_NEAR(p.z, 1.0f, 1e-5f);
    }

    // ---- 2D：缩放（等比 + 分量）----
    {
        Mat3 s = transform2d::scale(Vec2(2.0f, 3.0f));
        Vec3 p = s * Vec3(1.0f, 1.0f, 1.0f);
        TEST_NEAR(p.x, 2.0f, 1e-5f);
        TEST_NEAR(p.y, 3.0f, 1e-5f);

        Mat3 su = transform2d::scale(4.0f);
        Vec3 q = su * Vec3(1.0f, 1.0f, 1.0f);
        TEST_NEAR(q.x, 4.0f, 1e-5f);
        TEST_NEAR(q.y, 4.0f, 1e-5f);
    }

    // ---- 2D：旋转 90°（(1,0) -> (0,1)），且逆旋转可还原 ----
    {
        Mat3 r = transform2d::rotate(kPi / 2.0f);
        Vec3 p = r * Vec3(1.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 0.0f, 1e-5f);
        TEST_NEAR(p.y, 1.0f, 1e-5f);

        Mat3 ri = transform2d::rotate(-kPi / 2.0f);
        Vec3 back = ri * p;
        TEST_NEAR(back.x, 1.0f, 1e-5f);
        TEST_NEAR(back.y, 0.0f, 1e-5f);

        // 与逐点矩阵对照：旋转矩阵行列式应为 1
        TEST_NEAR(r.determinant(), 1.0f, 1e-5f);
    }

    // ---- 2D：翻转（镜像）----
    {
        Vec3 p = transform2d::flipX() * Vec3(2.0f, 1.0f, 1.0f);
        TEST_NEAR(p.x, -2.0f, 1e-5f);
        TEST_NEAR(p.y, 1.0f, 1e-5f);

        Vec3 q = transform2d::flipY() * Vec3(2.0f, 1.0f, 1.0f);
        TEST_NEAR(q.x, 2.0f, 1e-5f);
        TEST_NEAR(q.y, -1.0f, 1e-5f);

        // 两次翻转 = 恒等
        Vec3 r = transform2d::flipX() * (transform2d::flipX() * Vec3(2.0f, 1.0f, 1.0f));
        TEST_NEAR(r.x, 2.0f, 1e-5f);
        TEST_NEAR(r.y, 1.0f, 1e-5f);
    }

    // ---- 2D：组合顺序（先旋转，再平移：M = T * R）----
    {
        Mat3 m = transform2d::translate(Vec2(10.0f, 20.0f)) * transform2d::rotate(kPi / 2.0f);
        Vec3 p = m * Vec3(1.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 10.0f, 1e-4f);
        TEST_NEAR(p.y, 21.0f, 1e-4f);
    }

    // ---- 3D：平移 / 缩放 ----
    {
        Mat4 t = transform3d::translate(Vec3(1.0f, 2.0f, 3.0f));
        Vec4 p = t * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 1.0f, 1e-5f);
        TEST_NEAR(p.y, 2.0f, 1e-5f);
        TEST_NEAR(p.z, 3.0f, 1e-5f);

        Mat4 s = transform3d::scale(2.0f);
        Vec4 q = s * Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        TEST_NEAR(q.x, 2.0f, 1e-5f);
        TEST_NEAR(q.z, 2.0f, 1e-5f);
    }

    // ---- 3D：绕各轴旋转（右手系，逆时针为正）----
    {
        Vec4 px = transform3d::rotate(kPi / 2.0f, Axis::X) * Vec4(0.0f, 1.0f, 0.0f, 1.0f);
        TEST_NEAR(px.y, 0.0f, 1e-5f);
        TEST_NEAR(px.z, 1.0f, 1e-5f);   // y -> z

        Vec4 py = transform3d::rotate(kPi / 2.0f, Axis::Y) * Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(py.x, 0.0f, 1e-5f);
        TEST_NEAR(py.z, -1.0f, 1e-5f);  // x -> -z

        Vec4 pz = transform3d::rotate(kPi / 2.0f, Axis::Z) * Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(pz.x, 0.0f, 1e-5f);
        TEST_NEAR(pz.y, 1.0f, 1e-5f);   // x -> y
    }

    // ---- 3D：绕任意轴旋转（z 轴 90° 应等价 rotate(rad, Axis::Z)）----
    {
        Mat4 ra = transform3d::rotate(kPi / 2.0f, Vec3(0.0f, 0.0f, 5.0f));  // 未归一化轴
        Vec4 p = ra * Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 0.0f, 1e-4f);
        TEST_NEAR(p.y, 1.0f, 1e-4f);
        TEST_NEAR(p.z, 0.0f, 1e-4f);

        // 零轴：视为无旋转（恒等）
        Mat4 zeroAxis = transform3d::rotate(kPi / 2.0f, Vec3(0.0f, 0.0f, 0.0f));
        Vec4 q = zeroAxis * Vec4(1.0f, 2.0f, 3.0f, 1.0f);
        TEST_NEAR(q.x, 1.0f, 1e-5f);
        TEST_NEAR(q.y, 2.0f, 1e-5f);
    }

    // ---- 3D：翻转 ----
    {
        Vec4 p = transform3d::flipZ() * Vec4(0.0f, 0.0f, 2.0f, 1.0f);
        TEST_NEAR(p.z, -2.0f, 1e-5f);
        Vec4 q = transform3d::flipY() * Vec4(0.0f, 2.0f, 0.0f, 1.0f);
        TEST_NEAR(q.y, -2.0f, 1e-5f);
    }

    // ---- 3D：逆变换还原（旋转 + 平移组合）----
    {
        Mat4 m = transform3d::translate(Vec3(4.0f, -2.0f, 1.0f)) * transform3d::rotate(kPi / 3.0f, Axis::Z);
        Mat4 inv;
        TEST_CHECK(m.inverse(inv));
        Vec4 p(1.0f, 2.0f, 3.0f, 1.0f);
        Vec4 back = inv * (m * p);
        TEST_NEAR(back.x, p.x, 1e-4f);
        TEST_NEAR(back.y, p.y, 1e-4f);
        TEST_NEAR(back.z, p.z, 1e-4f);
    }

    // ---- 3D：换基基元（正交基性质 + 基矩阵正交性）----
    {
        Vec3 u, v, w;
        TEST_CHECK(transform3d::frameFromAxis(Vec3(1.0f, 2.0f, 3.0f), u, v, w));
        TEST_NEAR(u.length(), 1.0f, 1e-5f);
        TEST_NEAR(v.length(), 1.0f, 1e-5f);
        TEST_NEAR(w.length(), 1.0f, 1e-5f);
        TEST_NEAR(dot(u, v), 0.0f, 1e-5f);
        TEST_NEAR(dot(u, w), 0.0f, 1e-5f);
        TEST_NEAR(dot(v, w), 0.0f, 1e-5f);

        // 右手系：u × v = w
        const Vec3 uv = cross(u, v);
        TEST_NEAR(uv.x, w.x, 1e-5f);
        TEST_NEAR(uv.y, w.y, 1e-5f);
        TEST_NEAR(uv.z, w.z, 1e-5f);

        // 基矩阵正交：B · Bᵀ = I（故逆 = 转置）
        const Mat4 b = transform3d::basis(u, v, w);
        const Mat4 bbt = b * b.transposed();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                TEST_NEAR(bbt(r, c), (r == c) ? 1.0f : 0.0f, 1e-5f);

        // 零轴：构造失败
        Vec3 zu, zv, zw;
        TEST_CHECK(!transform3d::frameFromAxis(Vec3(0.0f, 0.0f, 0.0f), zu, zv, zw));
    }

    // ---- 3D：基础变换组合版绕轴旋转（换基）== 闭式 Rodrigues ----
    {
        const Vec3 axes[4] = {Vec3(0.0f, 0.0f, 1.0f),
                              Vec3(1.0f, 1.0f, 0.0f),
                              Vec3(1.0f, 2.0f, 3.0f),      // 未归一化
                              Vec3(-2.0f, 1.0f, -4.0f)};
        const float angles[3] = {kPi / 6.0f, kPi / 2.0f, -1.3f};
        for (const Vec3& ax : axes) {
            for (float ang : angles) {
                const Mat4 closed = transform3d::rotate(ang, ax);
                const Mat4 viaBasis = transform3d::rotateViaBasis(ang, ax);
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        TEST_NEAR(viaBasis(r, c), closed(r, c), 1e-4f);
            }
        }

        // 轴上的点不动
        const Vec3 axis(1.0f, 2.0f, 2.0f);
        Vec3 unitAxis;
        normalize(axis, unitAxis);
        const Vec4 onAxis = transform3d::rotateViaBasis(kPi / 2.0f, axis)
                          * Vec4(unitAxis.x * 3.0f, unitAxis.y * 3.0f, unitAxis.z * 3.0f, 1.0f);
        TEST_NEAR(onAxis.x, unitAxis.x * 3.0f, 1e-4f);
        TEST_NEAR(onAxis.y, unitAxis.y * 3.0f, 1e-4f);
        TEST_NEAR(onAxis.z, unitAxis.z * 3.0f, 1e-4f);

        // z 轴 90°：x -> y
        const Vec4 p = transform3d::rotateViaBasis(kPi / 2.0f, Vec3(0.0f, 0.0f, 1.0f))
                     * Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 0.0f, 1e-4f);
        TEST_NEAR(p.y, 1.0f, 1e-4f);

        // 零轴：视为无旋转（恒等）
        const Vec4 q = transform3d::rotateViaBasis(kPi / 2.0f, Vec3(0.0f, 0.0f, 0.0f))
                     * Vec4(1.0f, 2.0f, 3.0f, 1.0f);
        TEST_NEAR(q.x, 1.0f, 1e-5f);
        TEST_NEAR(q.y, 2.0f, 1e-5f);
        TEST_NEAR(q.z, 3.0f, 1e-5f);
    }

    // ---- 2D：绕点旋转（闭式 == 基础变换组合版）----
    {
        const Vec2 center(10.0f, 20.0f);
        const Mat3 closed = transform2d::rotate(kPi / 2.0f, center);
        const Mat3 composed = transform2d::rotateViaPrimitives(kPi / 2.0f, center);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                TEST_NEAR(composed(r, c), closed(r, c), 1e-5f);

        // 旋转中心是不动点
        const Vec3 o = closed * Vec3(center.x, center.y, 1.0f);
        TEST_NEAR(o.x, center.x, 1e-5f);
        TEST_NEAR(o.y, center.y, 1e-5f);

        // 中心右侧 1 的点：逆时针 90° -> 中心上方 1
        const Vec3 p = closed * Vec3(center.x + 1.0f, center.y, 1.0f);
        TEST_NEAR(p.x, center.x, 1e-5f);
        TEST_NEAR(p.y, center.y + 1.0f, 1e-5f);

        // 组合版逐点结果一致（平移 -> 旋转 -> 平移回）
        const Vec3 q = composed * Vec3(center.x + 1.0f, center.y, 1.0f);
        TEST_NEAR(q.x, p.x, 1e-5f);
        TEST_NEAR(q.y, p.y, 1e-5f);
    }

    // ---- 3D：绕"过 center 的 axis"旋转（闭式 == 基础变换组合版）----
    {
        const Vec3 axis(0.0f, 0.0f, 1.0f);
        const Vec3 center(1.0f, 1.0f, 0.0f);
        const Mat4 closed = transform3d::rotate(kPi / 2.0f, axis, center);
        const Mat4 composed = transform3d::rotateViaPrimitives(kPi / 2.0f, axis, center);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                TEST_NEAR(composed(r, c), closed(r, c), 1e-4f);

        // 轴（过 center 的 z 向直线）上的点不动
        const Vec4 onAxis = closed * Vec4(center.x, center.y, 5.0f, 1.0f);
        TEST_NEAR(onAxis.x, center.x, 1e-5f);
        TEST_NEAR(onAxis.y, center.y, 1e-5f);
        TEST_NEAR(onAxis.z, 5.0f, 1e-5f);

        // 绕 center 转 90°：(2,1,0) -> (1,2,0)
        const Vec4 p = closed * Vec4(2.0f, 1.0f, 0.0f, 1.0f);
        TEST_NEAR(p.x, 1.0f, 1e-4f);
        TEST_NEAR(p.y, 2.0f, 1e-4f);
        TEST_NEAR(p.z, 0.0f, 1e-4f);

        // 零轴：视为无旋转（恒等，与 rotate(rad, axis) 一致）
        const Vec4 q = transform3d::rotate(kPi / 2.0f, Vec3(0.0f, 0.0f, 0.0f), center)
                     * Vec4(1.0f, 2.0f, 3.0f, 1.0f);
        TEST_NEAR(q.x, 1.0f, 1e-5f);
        TEST_NEAR(q.y, 2.0f, 1e-5f);
        TEST_NEAR(q.z, 3.0f, 1e-5f);
    }

    return gfx::test::summary();
}
