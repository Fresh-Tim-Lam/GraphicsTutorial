// mat.hpp —— gfx::math 通用矩阵模板 Mat<N>（行主序；2D/3D 齐次坐标）
//
// @file 通用模板矩阵实现：零()/单位()、乘法、转置、行列式、逆。
//       算法按维度 N 模板化（det/inverse 走 Laplace 递归 + 伴随矩阵法），
//       后续 CPU(SIMD)/GPU 等优化只需在 math::detail 内部替换/分派，公共接口不变。
//
// 用途：N×N 浮点矩阵（N=2/3/4），含惯用别名 mat2/mat3/mat4。
//       行主序扁平存储：元素 m[row][col] 存于 data[row*N + col]（字面量按行写、构造时展平）。
// 目标读者：gfx 内部模块与调用方（#include <gfx/math/mat.hpp>）。
// 关联文档：docs/design/adr-0002-template-math.md；docs/conventions/api-docs.md
//
// 状态：生效中（2026-09-09）。重写自 ADR-0002（✅ 采纳）。

#ifndef GFX_MATH_MAT_HPP
#define GFX_MATH_MAT_HPP

#include "vec.hpp"

#include <cstddef>
#include <initializer_list>

namespace gfx::math {

// 内部算法细节（勿在公共 API 使用）；行主序内存布局，N 为编译期维度。
// @note 未来性能优化的插入点：替换本 namespace 内实现/增加分派即可。
namespace detail {

// 行列式底：2x2 = ad-bc
template <int N>
inline float det(const float* m);

// 提取去 skipRow/skipCol 后 (N-1)x(N-1) 子矩阵并求其行列式
template <int N>
inline float minorDet(const float* m, int skipRow, int skipCol) {
    float sub[(N - 1) * (N - 1)];
    int k = 0;
    for (int r = 0; r < N; ++r) {
        if (r == skipRow) continue;
        for (int c = 0; c < N; ++c) {
            if (c == skipCol) continue;
            sub[k++] = m[r * N + c];
        }
    }
    return det<N - 1>(sub);
}

// 通用 N×N 行列式：对第 0 行做 Laplace 展开（N<=4 教学规模足够）
template <int N>
inline float det(const float* m) {
    float acc = 0.0f;
    for (int c = 0; c < N; ++c) {
        const float sign = ((c & 1) == 0) ? 1.0f : -1.0f;
        acc += sign * m[c] * minorDet<N>(m, 0, c);
    }
    return acc;
}

// 2×2 底（避免模板自递归到底未定义）
template <>
inline float det<2>(const float* m) {
    return m[0] * m[3] - m[1] * m[2];
}

// 1×1 底（Mat2::inverse 的余子式会用到 1×1 子矩阵）
template <>
inline float det<1>(const float* m) {
    return m[0];
}

} // namespace detail

// 矩阵：N×N 行主序**扁平**存储（`float data[N*N]`，元素 (r,c) 在 `data[r*N+c]`）。默认零矩阵。
// @note 支持**行字面量**初始化：`Mat3 m{{1,0,tx},{0,1,ty},{0,0,1}};`（按行书写，构造时展平存入）。
//       扁平存储的收益：`data` 直接是可传给算法/批量层的 `float*`，无需任何取扁平指针的封装。
template <int N>
struct Mat {
    static_assert(N >= 2 && N <= 4, "Mat<N> 支持 N=2/3/4");

    float data[N * N] = {};   // 行主序扁平；未显式给出的元素补 0

    Mat() = default;

    // 行字面量构造：{ 第0行, 第1行, ... }；这里的"2 → 1"展平；缺省元素补 0，超出 N 的行/列忽略。
    Mat(std::initializer_list<std::initializer_list<float>> rows) {
        int r = 0;
        for (const std::initializer_list<float>& row : rows) {
            if (r >= N) break;
            int c = 0;
            for (float v : row) {
                if (c >= N) break;
                data[r * N + c] = v;
                ++c;
            }
            ++r;
        }
    }

    static Mat zero() { Mat m; return m; }
    static Mat identity() {
        Mat m;
        for (int i = 0; i < N; ++i) m(i, i) = 1.0f;
        return m;
    }

    // 元素访问：operator()(row, col)
    float& operator()(int r, int c) { return data[r * N + c]; }
    const float& operator()(int r, int c) const { return data[r * N + c]; }

    // 矩阵乘法（数学约定：先作用右边矩阵；result = (*this) * o）
    Mat& operator*=(const Mat& o) {
        const Mat self = *this;   // 防别名（a *= a 安全）
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                float acc = 0.0f;
                for (int k = 0; k < N; ++k) acc += self(r, k) * o(k, c);
                data[r * N + c] = acc;
            }
        }
        return *this;
    }

    Mat transposed() const {
        Mat t;
        for (int r = 0; r < N; ++r)
            for (int c = 0; c < N; ++c)
                t(c, r) = (*this)(r, c);
        return t;
    }

    float determinant() const { return detail::det<N>(data); }

    // 逆矩阵（伴随矩阵法，教学规模 N<=4）；不可逆返回 false 且 out 不变。
    bool inverse(Mat& out) const {
        const float d = determinant();
        if (d == 0.0f) return false;
        const float inv = 1.0f / d;
        Mat adj;
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                // 伴随 = 余子式矩阵的转置：adj(r,c) = cofactor(c,r)
                const float sign = (((r + c) & 1) == 0) ? 1.0f : -1.0f;
                adj(r, c) = sign * detail::minorDet<N>(data, c, r) * inv;
            }
        }
        out = adj;
        return true;
    }
};

template <int N>
inline Mat<N> operator*(Mat<N> a, const Mat<N>& b) { a *= b; return a; }

// 列向量变换（齐次坐标：Mat4 * Vec4(x,y,z,1) 等）
template <int N>
inline Vec<N> operator*(const Mat<N>& m, const Vec<N>& v) {
    Vec<N> r;
    for (int i = 0; i < N; ++i) {
        float acc = 0.0f;
        for (int k = 0; k < N; ++k) acc += m(i, k) * v[k];
        r[i] = acc;
    }
    return r;
}

// ---- 形状别名（教学/调用惯用命名；非加速特化，见 ADR-0002 补充澄清）----
using Mat2 = Mat<2>;
using Mat3 = Mat<3>;
using Mat4 = Mat<4>;

} // namespace gfx::math

#endif // GFX_MATH_MAT_HPP
