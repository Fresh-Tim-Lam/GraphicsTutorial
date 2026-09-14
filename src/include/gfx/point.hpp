// point.hpp —— gfx 几何层：点与点批的"apply 变换"
//
// @file 提供面向几何的流畅 API：把变换链挂在点/点批上（`.translate().rotate()`），
//       或把存好的 math::Transform 直接 apply 到点/点批上。
//       链只记录、不计算；消费时折叠一次并缓存（单点 = 1 次 mat-vec，批点 = 复用该矩阵逐点）。
//
// 用途：图形层几何数据（点 / 顶点批）的变换入口；像素/图元绘制的前置几何处理。
// 目标读者：使用几何变换的调用方与教学读者（#include <gfx/point.hpp>）。
// 关联文档：docs/design/adr-0004-transform-chain.md；math/transform.hpp；math/batch.hpp
//
// 状态：生效中（2026-09-09）。ADR-0004（✅ 采纳）。

#ifndef GFX_POINT_HPP
#define GFX_POINT_HPP

#include "gfx/math/math.hpp"

#include <cstddef>

namespace gfx {

// ============================ 2D ============================

// 一批 2D 点的**视图**（零拷贝）：base 指向第一个点的 x 分量。
// @note 等距性是调用方前置条件（C++ 数组/结构体布局天然满足）；连续 Vec2 数组的 stride = sizeof(Vec2) = 16。
struct PointView2D {
    const float* base = nullptr;
    std::size_t count = 0;
    std::size_t strideBytes = sizeof(math::Vec2);

    PointView2D() = default;
    PointView2D(const float* base_, std::size_t count_,
                std::size_t strideBytes_ = sizeof(math::Vec2))
        : base(base_), count(count_), strideBytes(strideBytes_) {}

    // 连续 Vec2 数组便捷入口
    static PointView2D of(const math::Vec2* points, std::size_t count) {
        return PointView2D(reinterpret_cast<const float*>(points), count, sizeof(math::Vec2));
    }
};

// 单个 2D 点：可累积变换链，也可 apply 外部变换
class Point2D {
public:
    Point2D(float x, float y) : p_(x, y) {}
    explicit Point2D(const math::Vec2& p) : p_(p) {}

    // 流畅链（累积到内部 Transform<3>）
    Point2D& translate(float tx, float ty) { t_.then(math::transform2d::translate(math::Vec2(tx, ty))); return *this; }
    Point2D& rotate(float radians)         { t_.then(math::transform2d::rotate(radians)); return *this; }
    Point2D& scale(float s)                { t_.then(math::transform2d::scale(s)); return *this; }
    Point2D& scale(float sx, float sy)     { t_.then(math::transform2d::scale(math::Vec2(sx, sy))); return *this; }
    Point2D& flipX()                       { t_.then(math::transform2d::flipX()); return *this; }
    Point2D& flipY()                       { t_.then(math::transform2d::flipY()); return *this; }

    // 取出累积链（可存储、可复用）
    const math::Transform<3>& transform() const { return t_; }
    // 原始点
    const math::Vec2& position() const { return p_; }

    // 用**自身累积链**求值
    math::Vec2 value() const {
        const math::Vec3 h = t_.apply(math::Vec3(p_.x, p_.y, 1.0f));
        return math::Vec2(h.x, h.y);
    }

    // apply 一个变换：t 作用在（已有链结果）之上，返回新点（不改自身）
    // @note 用"外部变换矩阵 × 自身链矩阵"一次算出，**不复制链**（复制 vector 会带来堆分配）。
    math::Vec2 apply(const math::Transform<3>& t) const {
        const math::Mat3 m = t.matrix() * t_.matrix();
        const math::Vec3 h = m * math::Vec3(p_.x, p_.y, 1.0f);
        return math::Vec2(h.x, h.y);
    }

    // 隐式转换：Vec2 v = p.translate(...);
    operator math::Vec2() const { return value(); }

private:
    math::Vec2 p_;
    math::Transform<3> t_;
};

// 一批 2D 点：零拷贝视图 + 累积变换链
class PointBatch2D {
public:
    explicit PointBatch2D(const PointView2D& view) : view_(view) {}
    PointBatch2D(const math::Vec2* points, std::size_t count) : view_(PointView2D::of(points, count)) {}

    PointBatch2D& translate(float tx, float ty) { t_.then(math::transform2d::translate(math::Vec2(tx, ty))); return *this; }
    PointBatch2D& rotate(float radians)         { t_.then(math::transform2d::rotate(radians)); return *this; }
    PointBatch2D& scale(float s)                { t_.then(math::transform2d::scale(s)); return *this; }
    PointBatch2D& scale(float sx, float sy)     { t_.then(math::transform2d::scale(math::Vec2(sx, sy))); return *this; }
    PointBatch2D& flipX()                       { t_.then(math::transform2d::flipX()); return *this; }
    PointBatch2D& flipY()                       { t_.then(math::transform2d::flipY()); return *this; }

    const math::Transform<3>& transform() const { return t_; }
    std::size_t count() const { return view_.count; }

    // 用自身链求值，结果写入连续 Vec2 输出缓冲
    void apply(math::Vec2* out) const { applyMatrix(t_.matrix(), out); }

    // apply 一个外部变换（作用在自身链之上），结果写入 out
    // @note 不复制链：直接用"外部变换矩阵 × 自身链矩阵"（一次矩阵乘，零分配）。
    void apply(const math::Transform<3>& t, math::Vec2* out) const {
        applyMatrix(t.matrix() * t_.matrix(), out);
    }

private:
    // 逐点：读 (x,y) → 齐次 (x,y,1) → 矩阵变换 → 写回 (x,y)
    void applyMatrix(const math::Mat3& m, math::Vec2* out) const {
        if (view_.base == nullptr || out == nullptr) return;
        for (std::size_t i = 0; i < view_.count; ++i) {
            const float* src = reinterpret_cast<const float*>(
                reinterpret_cast<const char*>(view_.base) + i * view_.strideBytes);
            const math::Vec3 r = m * math::Vec3(src[0], src[1], 1.0f);
            out[i] = math::Vec2(r.x, r.y);
        }
    }

    PointView2D view_;
    math::Transform<3> t_;
};

// ============================ 3D ============================

// 一批 3D 点的视图（零拷贝）：base 指向第一个点的 x 分量。
struct PointView3D {
    const float* base = nullptr;
    std::size_t count = 0;
    std::size_t strideBytes = sizeof(math::Vec3);

    PointView3D() = default;
    PointView3D(const float* base_, std::size_t count_,
                std::size_t strideBytes_ = sizeof(math::Vec3))
        : base(base_), count(count_), strideBytes(strideBytes_) {}

    static PointView3D of(const math::Vec3* points, std::size_t count) {
        return PointView3D(reinterpret_cast<const float*>(points), count, sizeof(math::Vec3));
    }
};

// 单个 3D 点
class Point3D {
public:
    Point3D(float x, float y, float z) : p_(x, y, z) {}
    explicit Point3D(const math::Vec3& p) : p_(p) {}

    Point3D& translate(float tx, float ty, float tz) { t_.then(math::transform3d::translate(math::Vec3(tx, ty, tz))); return *this; }
    Point3D& scale(float s)                           { t_.then(math::transform3d::scale(s)); return *this; }
    Point3D& scale(float sx, float sy, float sz)      { t_.then(math::transform3d::scale(math::Vec3(sx, sy, sz))); return *this; }
    Point3D& rotate(float radians, math::Axis axis)   { t_.then(math::transform3d::rotate(radians, axis)); return *this; }
    Point3D& rotate(float radians, const math::Vec3& axis) { t_.then(math::transform3d::rotate(radians, axis)); return *this; }
    Point3D& flipX()                                  { t_.then(math::transform3d::flipX()); return *this; }
    Point3D& flipY()                                  { t_.then(math::transform3d::flipY()); return *this; }
    Point3D& flipZ()                                  { t_.then(math::transform3d::flipZ()); return *this; }

    const math::Transform<4>& transform() const { return t_; }
    const math::Vec3& position() const { return p_; }

    math::Vec3 value() const {
        const math::Vec4 h = t_.apply(math::Vec4(p_.x, p_.y, p_.z, 1.0f));
        return math::Vec3(h.x, h.y, h.z);
    }

    // @note 不做透视除法：返回 (x, y, z)（仿射变换下 w 恒为 1；投影场景请自行用 matrix() 处理）
    // @note 不复制链：外部变换矩阵 × 自身链矩阵（一次矩阵乘，零分配）。
    math::Vec3 apply(const math::Transform<4>& t) const {
        const math::Mat4 m = t.matrix() * t_.matrix();
        const math::Vec4 h = m * math::Vec4(p_.x, p_.y, p_.z, 1.0f);
        return math::Vec3(h.x, h.y, h.z);
    }

    operator math::Vec3() const { return value(); }

private:
    math::Vec3 p_;
    math::Transform<4> t_;
};

// 一批 3D 点
class PointBatch3D {
public:
    explicit PointBatch3D(const PointView3D& view) : view_(view) {}
    PointBatch3D(const math::Vec3* points, std::size_t count) : view_(PointView3D::of(points, count)) {}

    PointBatch3D& translate(float tx, float ty, float tz) { t_.then(math::transform3d::translate(math::Vec3(tx, ty, tz))); return *this; }
    PointBatch3D& scale(float s)                           { t_.then(math::transform3d::scale(s)); return *this; }
    PointBatch3D& scale(float sx, float sy, float sz)      { t_.then(math::transform3d::scale(math::Vec3(sx, sy, sz))); return *this; }
    PointBatch3D& rotate(float radians, math::Axis axis)   { t_.then(math::transform3d::rotate(radians, axis)); return *this; }
    PointBatch3D& rotate(float radians, const math::Vec3& axis) { t_.then(math::transform3d::rotate(radians, axis)); return *this; }
    PointBatch3D& flipX()                                  { t_.then(math::transform3d::flipX()); return *this; }
    PointBatch3D& flipY()                                  { t_.then(math::transform3d::flipY()); return *this; }
    PointBatch3D& flipZ()                                  { t_.then(math::transform3d::flipZ()); return *this; }

    const math::Transform<4>& transform() const { return t_; }
    std::size_t count() const { return view_.count; }

    // 用自身链求值 → 连续 Vec3 输出
    void apply(math::Vec3* out) const { applyMatrix(t_.matrix(), out); }

    // apply 一个外部变换（作用在自身链之上）→ 连续 Vec3 输出
    // @note 不复制链：外部变换矩阵 × 自身链矩阵（一次矩阵乘，零分配）。
    void apply(const math::Transform<4>& t, math::Vec3* out) const {
        applyMatrix(t.matrix() * t_.matrix(), out);
    }

private:
    // 逐点：读 (x,y,z) → 齐次 (x,y,z,1) → 矩阵变换 → 写回 (x,y,z)
    void applyMatrix(const math::Mat4& m, math::Vec3* out) const {
        if (view_.base == nullptr || out == nullptr) return;
        for (std::size_t i = 0; i < view_.count; ++i) {
            const float* src = reinterpret_cast<const float*>(
                reinterpret_cast<const char*>(view_.base) + i * view_.strideBytes);
            const math::Vec4 r = m * math::Vec4(src[0], src[1], src[2], 1.0f);
            out[i] = math::Vec3(r.x, r.y, r.z);
        }
    }

    PointView3D view_;
    math::Transform<4> t_;
};

} // namespace gfx

#endif // GFX_POINT_HPP
