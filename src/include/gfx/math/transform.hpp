// transform.hpp —— gfx::math 常用变换矩阵构造（平移/缩放/旋转/翻转）
//
// @file 提供 2D（Mat3 齐次）与 3D（Mat4 齐次）的常用变换矩阵工厂函数。
//       另含"换基"基元（basis/frameFromAxis）与绕任意轴旋转的两种等价实现
//       （闭式 rotate、换基组合 rotateViaBasis）。
//       约定：行主序 + 列向量（v' = M * v）；角度单位为**弧度**。
//       **左乘口径（本库统一）**：新变换总是左乘到已累积的矩阵上 ⇒ 后写的步骤后作用。
//       因此矩阵连乘 `A * B` 读作"先 B 后 A"（数学复合），而链/流式写作顺序 = 作用顺序；
//       `Transform` 的 `then()` 与 `a * b` 同口径（先 a 后 b），见 Transform<N> 注释。
//
// 用途：免去手写变换矩阵元素，直接表达"平移/旋转/翻转"等意图，供绘制与教学使用。
// 目标读者：gfx 内部模块与调用方（#include <gfx/math/transform.hpp>）。
// 关联文档：docs/design/adr-0002-template-math.md；docs/conventions/api-docs.md
//
// 状态：生效中（2026-09-09）。新增（教学下一步：几何变换）。

#ifndef GFX_MATH_TRANSFORM_HPP
#define GFX_MATH_TRANSFORM_HPP

#include "batch.hpp"
#include "mat.hpp"
#include "vec.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace gfx::math {

// 坐标轴选择（供基元旋转 transform3d::rotate(radians, Axis) 与几何层 p.rotate(rad, Axis) 用）
enum class Axis { X, Y, Z };

// 2D 变换（Mat3，齐次坐标：点写作 Vec3(x, y, 1)）
// @note 矩阵按**行**书写（行主序）：[[第0行],[第1行],[第2行]]。
// @note 屏幕坐标 y 轴向下时，正角度的旋转在视觉上表现为顺时针（数学上仍是逆时针）。
namespace transform2d {

// 平移：点 + (tx, ty)
inline Mat3 translate(const Vec2& t) {
    return Mat3{{1.0f, 0.0f, t.x},
                {0.0f, 1.0f, t.y},
                {0.0f, 0.0f, 1.0f}};
}

// 缩放（分量）
inline Mat3 scale(const Vec2& s) {
    return Mat3{{s.x,  0.0f, 0.0f},
                {0.0f, s.y,  0.0f},
                {0.0f, 0.0f, 1.0f}};
}

// 缩放（等比）
inline Mat3 scale(float s) { return scale(Vec2(s, s)); }

// 旋转（弧度，逆时针为正）
inline Mat3 rotate(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Mat3{{ c,   -s,   0.0f},
                { s,    c,   0.0f},
                {0.0f, 0.0f, 1.0f}};
}

// 绕**点**旋转（闭式）：`rotate(rad)` 的重载 —— 绕 center 转 θ（T(center)·R(θ)·T(-center) 的展开）。
// @note 展开后旋转部分不变，平移列 = center - R·center；点 center 本身为不动点。
inline Mat3 rotate(float radians, const Vec2& center) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Mat3{{ c,   -s,   center.x * (1.0f - c) + center.y * s},
                { s,    c,   center.y * (1.0f - c) - center.x * s},
                {0.0f, 0.0f, 1.0f}};
}

// 绕**点**旋转（基础变换组合版）：由右往左作用 —— T(-center) 把 center 移到原点 → R(θ) → T(center) 移回。
// @note 书写顺序与作用顺序相反（`A * B` 先作用 B）；与闭式 rotate(rad, center) 数值等价（见 tests/test_transform.cpp）；
//       生产路径用闭式（省两次矩阵乘法）。
inline Mat3 rotateViaPrimitives(float radians, const Vec2& center) {
    return translate(center) * rotate(radians) * translate(-center);// 我们是左乘变换
}

// X 轴镜像翻转（左右翻转；负 x）
inline Mat3 flipX() { return scale(Vec2(-1.0f, 1.0f)); }

// Y 轴镜像翻转（上下翻转；负 y）
inline Mat3 flipY() { return scale(Vec2(1.0f, -1.0f)); }

} // namespace transform2d









// 3D 变换（Mat4，齐次坐标：点写作 Vec4(x, y, z, 1)）
// @note 矩阵按**行**书写（行主序）：[[第0行],[第1行],[第2行],[第3行]]。
namespace transform3d {

// 平移：点 + (x, y, z)
inline Mat4 translate(const Vec3& t) {
    return Mat4{{1.0f, 0.0f, 0.0f, t.x},
                {0.0f, 1.0f, 0.0f, t.y},
                {0.0f, 0.0f, 1.0f, t.z},
                {0.0f, 0.0f, 0.0f, 1.0f}};
}

// 缩放（分量）
inline Mat4 scale(const Vec3& s) {
    return Mat4{{s.x,  0.0f, 0.0f, 0.0f},
                {0.0f, s.y,  0.0f, 0.0f},
                {0.0f, 0.0f, s.z,  0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}};
}

// 缩放（等比）
inline Mat4 scale(float s) { return scale(Vec3(s, s, s)); }

// 绕坐标轴旋转（基元；弧度，逆时针为正）：一个函数 + 枚举参数选轴。
// @note 轴上的点不动；Y 轴矩阵的 sin 符号位置与 X/Z 不同——沿 y 轴反向看，另两轴序为 z→x（仍是逆时针）。
inline Mat4 rotate(float radians, Axis axis) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    switch (axis) {
        case Axis::X:
            return Mat4{{1.0f, 0.0f, 0.0f, 0.0f},
                        {0.0f,  c,   -s,   0.0f},
                        {0.0f,  s,    c,   0.0f},
                        {0.0f, 0.0f, 0.0f, 1.0f}};
        case Axis::Y:
            return Mat4{{ c,   0.0f,  s,   0.0f},
                        {0.0f, 1.0f, 0.0f, 0.0f},
                        {-s,   0.0f,  c,   0.0f},
                        {0.0f, 0.0f, 0.0f, 1.0f}};
        case Axis::Z:
            break;
    }
    return Mat4{{ c,   -s,   0.0f, 0.0f},
                { s,    c,   0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}};
}

// 绕"过 center、方向为 axis"的直线旋转（闭式，**本文件唯一的轴角旋转实现**）。
// @note 推导：T(c)·R·T(-c) 对 p 的作用 = R(p-c)+c = R·p + (c - R·c)；
//       故线性部分 = Rodrigues 系数，平移列 = c - R·c，在同一字面量内一次写全
//       （矩阵字面量构造非零成本，见 docs/conventions/performance.md）。
inline Mat4 rotate(float radians, const Vec3& axis, const Vec3& center) {
    Vec3 a;
    if (!normalize(axis, a)) return Mat4::identity();   // 零轴：视为无旋转
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0f - c;
    const float r00 = t * a.x * a.x + c,       r01 = t * a.x * a.y - s * a.z, r02 = t * a.x * a.z + s * a.y;
    const float r10 = t * a.x * a.y + s * a.z, r11 = t * a.y * a.y + c,       r12 = t * a.y * a.z - s * a.x;
    const float r20 = t * a.x * a.z - s * a.y, r21 = t * a.y * a.z + s * a.x, r22 = t * a.z * a.z + c;
    return Mat4{{r00, r01, r02, center.x - (r00 * center.x + r01 * center.y + r02 * center.z)},
                {r10, r11, r12, center.y - (r10 * center.x + r11 * center.y + r12 * center.z)},
                {r20, r21, r22, center.z - (r20 * center.x + r21 * center.y + r22 * center.z)},
                {0.0f, 0.0f, 0.0f, 1.0f}};
}

// 绕过经原点的任意轴旋转（弧度，Rodrigues 公式）；axis 无需预归一化。
// @note 即"过原点"特例（center = 原点）：委托给三参数重载，保持**公式唯一实现**（见 performance.md P2）。
inline Mat4 rotate(float radians, const Vec3& axis) {
    return rotate(radians, axis, Vec3(0.0f, 0.0f, 0.0f));
}

// 绕"过 center、方向为 axis"的直线旋转（基础变换组合版）：由右往左作用 —— T(-center) → rotate(axis) → T(center)。
// @note 书写顺序与作用顺序相反（`A * B` 先作用 B）；与闭式 rotate(rad, axis, center) 数值等价（见 tests/test_transform.cpp）。
inline Mat4 rotateViaPrimitives(float radians, const Vec3& axis, const Vec3& center) {
    return translate(center) * rotate(radians, axis) * translate(-center);
}

// X 轴镜像翻转（负 x）
inline Mat4 flipX() { return scale(Vec3(-1.0f, 1.0f, 1.0f)); }
// Y 轴镜像翻转（负 y）
inline Mat4 flipY() { return scale(Vec3(1.0f, -1.0f, 1.0f)); }
// Z 轴镜像翻转（负 z）
inline Mat4 flipZ() { return scale(Vec3(1.0f, 1.0f, -1.0f)); }








// ---- 换基（change of basis）—— 基础变换基元 ----
//
// @brief 基矩阵：列 = 新空间的三根基向量，B = [u | v | w]。
//        B 把"新空间坐标"映射到"当前空间"：p_当前 = B · p_新空间；
//        正交基时 B⁻¹ = Bᵀ，"换进新空间"即 p_新空间 = Bᵀ · p_当前。
// @note 行字面量里每行的第 k 个元素来自第 k 个**列**向量（列基在行主序下看着像转置）。
inline Mat4 basis(const Vec3& u, const Vec3& v, const Vec3& w) {
    return Mat4{{u.x, v.x, w.x, 0.0f},
                {u.y, v.y, w.y, 0.0f},
                {u.z, v.z, w.z, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f}};
}

// 由旋转轴构造右手正交基：w = normalize(axis)，u ⊥ w，v = w × u（故 u × v = w）。
// @param axis 任意非零向量（无需预归一化）；零轴返回 false（u/v/w 置零）。
inline bool frameFromAxis(const Vec3& axis, Vec3& u, Vec3& v, Vec3& w) {
    if (!normalize(axis, w)) {
        u = Vec3();
        v = Vec3();
        return false;
    }
    // 取与 w 最不平行的坐标轴做参考，保证叉积不退化（|sin| >= sqrt(2/3)）
    const float ax = std::fabs(w.x), ay = std::fabs(w.y), az = std::fabs(w.z);//fabs 取绝对值
    const Vec3 ref = (ax <= ay && ax <= az) ? Vec3(1.0f, 0.0f, 0.0f)
                   : (ay <= az)             ? Vec3(0.0f, 1.0f, 0.0f)
                                            : Vec3(0.0f, 0.0f, 1.0f);
    u = cross(ref, w);
    normalize(u, u);
    v = cross(w, u);//不用归一化
    return true;
}

// 绕任意轴旋转（**基础变换组合**版）：R = B · Rz(θ) · Bᵀ
//   左乘，从右往左作用
//   ① Bᵀ  换进轴空间（axis 在该空间里就是 z 轴）正交矩阵的转置就是 B⁻¹
//   ② Rz  在轴空间里做最平凡的基元旋转
//   ③ B   换回当前空间
// @note 与闭式 rotate(radians, axis) 数值等价（tests/test_transform.cpp 有对照）；
//       生产路径用闭式（省两次 4×4 乘法），本函数用于教学展示"旋转 = 换基 + 基元 + 换回"。
inline Mat4 rotateViaBasis(float radians, const Vec3& axis) {
    Vec3 u, v, w;//w 是旋转轴
    if (!frameFromAxis(axis, u, v, w)) return Mat4::identity();   // 零轴：视为无旋转
    const Mat4 b = basis(u, v, w);
    return b * rotate(radians, Axis::Z) * b.transposed();
}

} // namespace transform3d

// Transform<N> —— 变换链（可存储、可折叠、可 apply）
//
// @brief 记录"连续变换"的操作序列（追加只记录、不计算），延迟到消费点求值：
//        - 折叠矩阵 matrix()：**惰性 + 缓存**，首次消费折叠一次（O((k-1)·N³)），之后直接复用
//        - 单点 apply(p)：内部即单矩阵 apply（1 次 mat-vec）
//        - 批点 apply(...)：复用同一折叠矩阵逐点（P·N²），连续布局可走后端钩子（SIMD/GPU）
// @note 步骤按**书写顺序**记录（steps_[0] 最先作用）；`then()` 与 `a * b` 同口径
//       （都是"先 a 后 b"）：链式流式 API 从左往右读；底层矩阵是"新步骤左乘"，见文件头约定。
//       同维方阵链的乘法顺序不影响标量乘次数（见 ADR-0004）。
template <int N>
class Transform {
public:
    Transform() = default;
    Transform(const Mat<N>& m) { steps_.push_back(m); }

    // 追加一步（在该链之后作用；书写顺序 = 作用顺序）；只让折叠缓存失效，不做矩阵乘
    Transform& then(const Mat<N>& step) { steps_.push_back(step); valid_ = false; return *this; }
    // 追加整条链
    Transform& then(const Transform& t) {
        steps_.insert(steps_.end(), t.steps_.begin(), t.steps_.end());
        valid_ = false;
        return *this;
    }

    // 组合（拼接）：结果 = **先作用 *this，再作用 o** —— 与 then() 同口径（书写顺序 = 作用顺序）。
    // @note 故 matrix(a * b) == matrix(b) * matrix(a)（把 b 的矩阵**左乘**到 a 上）；
    //       这与 Mat 的 `A * B`（先作用 B）互为镜像，两套口径各自在自己的类型上统一，勿混读。
    Transform operator*(const Transform& o) const {
        Transform r = *this;
        r.steps_.insert(r.steps_.end(), o.steps_.begin(), o.steps_.end());
        r.valid_ = false;
        return r;
    }

    bool isIdentity() const { return steps_.empty(); }
    std::size_t stepCount() const { return steps_.size(); }

    // 折叠矩阵（**惰性 + 缓存**）：首次消费时折叠一次，之后直接返回缓存。
    // @note "链"的价值就在这里——追加只记录、不计算，延迟到消费点；折叠结果被缓存后，
    //       重复 apply 不再付矩阵乘（见 ADR-0004 与 benchmarks/bench_point_apply.cpp）。
    //       缓存经 mutable 实现 ⇒ 本类型**非线程安全**（多线程请各自持一份）。
    const Mat<N>& matrix() const {
        if (!valid_) {
            cached_ = fold();
            valid_ = true;
        }
        return cached_;
    }

    // 单点求值：内部即"**单矩阵 apply**"（1 次 mat-vec；折叠只在首次发生并被缓存）
    Vec<N> apply(const Vec<N>& p) const { return matrix() * p; }

    // 批量求值：折叠矩阵 + 批量后端（连续布局可被 setBatchTransformBackend 换成 SIMD/GPU）
    // @param in/out  点缓冲首地址（可为同一块内存做原地变换）
    // @param strideBytes 相邻点间隔字节数（连续数组 = sizeof(Vec<N>) = 16）
    // @note 前置条件：输入点须是**已齐次**的（N=4 时第 4 分量为 1、N=3 时第 3 分量为 1），
    //       且 in 与 out 使用相同 stride。2D/3D 的"位置 → 齐次"注入由 gfx 几何层负责（point.hpp）。
    void apply(const float* in, float* out, std::size_t count, std::size_t strideBytes) const {
        const Mat<N>& m = matrix();
        // 连续布局：复用 batch 后端（可被 setBatchTransformBackend 替换为 SIMD/GPU 实现）
        if (strideBytes == sizeof(Vec<N>)) {
            batchTransform<N>(m, reinterpret_cast<const Vec<N>*>(in),
                              reinterpret_cast<Vec<N>*>(out), count);
            return;
        }
        // 带步长（AoS 交错）布局：CPU 逐点路径
        // @note 先把源点读到局部变量，保证 in == out（原地）时结果正确。
        for (std::size_t i = 0; i < count; ++i) {
            const float* srcPtr = reinterpret_cast<const float*>(
                reinterpret_cast<const char*>(in) + i * strideBytes);
            float src[4];
            for (int c = 0; c < 4; ++c) src[c] = srcPtr[c];
            float* dst = reinterpret_cast<float*>(
                reinterpret_cast<char*>(out) + i * strideBytes);
            for (int r = 0; r < N; ++r) {
                float acc = 0.0f;
                for (int k = 0; k < N; ++k) acc += m(r, k) * src[k];
                dst[r] = acc;
            }
        }
    }

private:
    // 折叠：steps_[k-1] * ... * steps_[0]（新步骤**左乘** ⇒ 后写的后作用）
    Mat<N> fold() const {
        Mat<N> acc = Mat<N>::identity();
        for (const Mat<N>& s : steps_) acc = s * acc;
        return acc;
    }

    std::vector<Mat<N>> steps_;
    mutable Mat<N> cached_;       // matrix() 的折叠缓存
    mutable bool valid_ = false;  // 缓存是否有效（then / operator* 之后失效）
};

} // namespace gfx::math

#endif // GFX_MATH_TRANSFORM_HPP
