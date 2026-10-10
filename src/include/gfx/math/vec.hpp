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

// ---- 光线追踪用向量运算（反射 / 折射）----

// 镜面反射方向：r = d − 2(d·n)n。
// @param d 入射方向（约定：**指向表面**，即与单位法向 n 的夹角 > 90°，d·n < 0）
// @param n 表面单位法向（朝外）
// @return 反射方向（长度与 d 相同；d 为单位矢量时 r 亦为单位矢量）
// @note 对任意维度 N 成立；只做线性组合，不要求 d、n 严格单位化（调用方保证时结果为理想反射）。
template <int N>
inline Vec<N> reflect(const Vec<N>& d, const Vec<N>& n) {
    return d - n * (2.0f * dot(d, n));
}

// 折射方向（Snell 定律）：eta = n1 / n2（入射介质折射率 / 出射介质折射率）。
// @param d   入射方向（约定：**指向表面**，与朝外单位法向 n 满足 d·n < 0）
// @param n   表面单位法向（朝外）
// @param eta 折射率之比 eta = n1 / n2
// @param out 输出折射方向（成功时被写入；全反射时保持不变）
// @return true 并写入 out；当发生**全反射**时返回 false（out 不变）
// @note 符号约定：以 c = d·n（< 0）代入 k = 1 − eta²(1 − c²)；k < 0 ⇒ 全反射。
//       否则 out = normalize(eta·d − (eta·c + √k)·n)。当 eta = 1 时 out 与 d 同向（直线穿过的退化情形）。
template <int N>
inline bool refract(const Vec<N>& d, const Vec<N>& n, float eta, Vec<N>& out) {
    const float c = dot(d, n);                                  // d 指向表面 ⇒ c < 0
    const float k = 1.0f - eta * eta * (1.0f - c * c);          // cos²θ_t
    if (k < 0.0f) return false;                                 // 全反射：无实折射方向
    const float s = eta * c + std::sqrt(k);
    Vec<N> r = d * eta - n * s;
    const float len = r.length();                               // 数值误差下的安全归一化
    out = (len == 0.0f) ? Vec<N>() : r / len;
    return true;
}

// ---- 形状别名（教学/调用惯用命名；非加速特化，见 ADR-0002 补充澄清）----
using Vec2 = Vec<2>;
using Vec3 = Vec<3>;
using Vec4 = Vec<4>;

// ---- 齐次坐标：归一化（透视除法） ----

// 齐次归一化：Vec4(x, y, z, w) → Vec3(x/w, y/w, z/w)。
// @return w ≠ 0 时写入 out 并返回 true；w == 0（方向/无穷远点）返回 false，out 不变。
// @note 齐次坐标里 (kx, ky, kz, k) 与 (x, y, z, 1) 表示同一点，除以 w 就把它拉回 w = 1 的规范形式；
//       透视投影正是"先让 w 承载深度，最后除以 w"（见 transform3d::perspectiveToOrtho 与 View）。
inline bool fromHomogeneous(const Vec4& h, Vec3& out) {
    if (h.w == 0.0f) return false;
    const float inv = 1.0f / h.w;
    out = Vec3(h.x * inv, h.y * inv, h.z * inv);
    return true;
}

// 二维版齐次归一化：Vec3(x, y, w) → Vec2(x/w, y/w)（二维点写作 Vec3(x, y, 1)）。
// @return w ≠ 0 时写入 out 并返回 true；w == 0 返回 false，out 不变。
inline bool fromHomogeneous(const Vec3& h, Vec2& out) {
    if (h.z == 0.0f) return false;
    const float inv = 1.0f / h.z;
    out = Vec2(h.x * inv, h.y * inv);
    return true;
}

} // namespace gfx::math

#endif // GFX_MATH_VEC_HPP
