// vec.hpp —— gfx::math 通用向量模板 Vec<N>（全模板化，2D/3D 一步到位）
//
// @file 向量与矩阵通用模板系列（vec.hpp / mat.hpp）。教学库从"三份形状代码"收敛为
//       一份模板实现，内部按维度 N 分派；后续 CPU(SIMD)/GPU 等算法优化只改模板内部，
//       不改变公共接口（mat2/3/4、vec2/3/4 别名保持）。
//
// 用途：N 维浮点向量（N=2/3/4）与运算，含惯用别名 vec2/vec3/vec4。
//       元素 .x/.y/.z/.w 恒可用（未用分量恒为 0）。
// 目标读者：gfx 内部模块与调用方（#include <gfx/math/vec.hpp>）。
// 关联文档：docs/design/adr-0002-template-math.md；docs/conventions/api-docs.md
//
// 状态：生效中（2026-09-09）。重写自 ADR-0002（✅ 采纳）。

#ifndef GFX_MATH_VEC_HPP
#define GFX_MATH_VEC_HPP

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace gfx::math {

// 向量：固定 4 分量存储（x/y/z/w 各一 float，SIMD 友好的对齐布局）。
// 语义维度为 N（2/3/4）；dot/length/分量访问只作用于前 N 个分量。
// @note 存储始终为 16 字节（对齐 4 float），方便未来整块 SIMD；如需省内存可改特化。
template <int N>
struct Vec {
    static_assert(N >= 2 && N <= 4, "Vec<N> 支持 N=2/3/4");

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec() = default;
    constexpr Vec(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec(float x_, float y_, float z_, float w_)
        : x(x_), y(y_), z(z_), w(w_) {}

    // 前 N 个分量的下标访问（[0..N) 有效；越界未定义）
    float& operator[](std::size_t i) { return (&x)[i]; }
    const float& operator[](std::size_t i) const { return (&x)[i]; }

    // ---- 逐分量运算 ----
    Vec& operator+=(const Vec& o) { for (int i=0;i<N;++i) (*this)[i] += o[i]; return *this; }
    Vec& operator-=(const Vec& o) { for (int i=0;i<N;++i) (*this)[i] -= o[i]; return *this; }
    Vec& operator*=(float s)      { for (int i=0;i<N;++i) (*this)[i] *= s; return *this; }
    Vec& operator/=(float s)      { for (int i=0;i<N;++i) (*this)[i] /= s; return *this; }

    float dot(const Vec& o) const { float d=0.0f; for (int i=0;i<N;++i) d += (*this)[i]*o[i]; return d; }
    float lengthSquared() const { return dot(*this); }
    float length() const { return std::sqrt(lengthSquared()); }
    bool isZero() const { return lengthSquared() == 0.0f; }
};

template <int N>
inline Vec<N> operator+(Vec<N> a, const Vec<N>& b) { a += b; return a; }
template <int N>
inline Vec<N> operator-(Vec<N> a, const Vec<N>& b) { a -= b; return a; }
template <int N>
inline Vec<N> operator-(const Vec<N>& v) {
    Vec<N> r;
    for (int i=0;i<N;++i) r[i] = -v[i];
    return r;
}
template <int N>
inline Vec<N> operator*(Vec<N> v, float s) { v *= s; return v; }
template <int N>
inline Vec<N> operator*(float s, Vec<N> v) { v *= s; return v; }
template <int N>
inline Vec<N> operator/(Vec<N> v, float s) { v /= s; return v; }

template <int N>
inline float dot(const Vec<N>& a, const Vec<N>& b) { return a.dot(b); }

// 叉积只对 3 维有意义（静态断言在调用 Vec<N≠3> 时报错）
template <int N>
inline Vec<N> cross(const Vec<N>& a, const Vec<N>& b) {
    static_assert(N == 3, "cross 仅支持 Vec3");
    return Vec<3>(a.y * b.z - a.z * b.y,
                  a.z * b.x - a.x * b.z,
                  a.x * b.y - a.y * b.x);
}

// 归一化：零向量保持零向量（返回是否成功）
template <int N>
inline bool normalize(const Vec<N>& in, Vec<N>& out) {
    const float len = in.length();// 不是维度数而是向量长度，用于归一化
    if (len == 0.0f) { out = Vec<N>(); return false; }// 零向量保持零向量
    out = in / len;
    return true;
}

// ---- 形状别名（教学/调用惯用命名；非加速特化，见 ADR-0002 补充澄清）----
using Vec2 = Vec<2>;
using Vec3 = Vec<3>;
using Vec4 = Vec<4>;

} // namespace gfx::math

#endif // GFX_MATH_VEC_HPP
