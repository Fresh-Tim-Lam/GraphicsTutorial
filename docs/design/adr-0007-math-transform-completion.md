# ADR-0007 — gfx_math 变换补全（第三讲知识点落地）

> **用途**：记录"把《第三讲 变换、相机与投影》涉及的变换知识点补进 `gfx_math`"的决策：补什么、为什么补、哪些是"能力缺失"而哪些只是"缺个名字"，以及诊断类工具的边界。
> **目标读者**：使用/扩展 `gfx_math` 变换与矩阵工具的人与 AI Agent。
> **关联文档**：[adr-0002](adr-0002-template-math.md)（模板化）；[adr-0004](adr-0004-transform-chain.md)（变换链）；[adr-0006](adr-0006-camera-view.md)（相机/投影，同为本次补全的落点）；实现 [transform.hpp](../../src/include/gfx/math/transform.hpp)、[mat.hpp](../../src/include/gfx/math/mat.hpp)、[vec.hpp](../../src/include/gfx/math/vec.hpp)。

> **状态**：✅ 采纳（2026-09-16）。落地：`transform2d::shear` / `inverseTranslate` / `inverseRotate` / `inverseScale` / `inverseShear`；`transform3d::euler` / `frame` / `inverseTranslate` / `inverseRotate` / `inverseScale`；`applyVector` / `fromHomogeneous` / `translationOf` / `linearPartOf` / `isAffine`；`Transform<N>::applyVector`。

## 背景

- 触发材料：课程《第三讲：变换、相机与投影》（62 页，GAMES101 L3/L4 路线）。逐条对照后，**大部分能力已具备**（缩放/旋转/反射/绕点与绕轴旋转/齐次矩阵/组合顺序/正交与透视投影矩阵），但有若干**讲义点名而库中缺失**的项。
- 关键区分：补全时要分清两类——
  - **能力缺失**：写不出来，或要绕很远（如 2D 错切、带原点的坐标基、逆变换闭式）。
  - **只是缺名字**：能力可达但契约不清（如"向量变换"可用 `apply(Vec3(x, y, 0))` 实现，但 `Transform::apply` 的文档明确要求输入是**点**（w = 1），传 w = 0 属于绕过契约）。
- 项目约束：`gfx_math` 是零依赖头文件子库；新增公共符号须配注释与 ADR（[api-docs.md](../conventions/api-docs.md)）；**不为已有能力做重复封装**。

## 决策

### D1：只补"缺失或易错"，不为已有能力再造一层

判定过的"已有、不补"项：均匀/非均匀缩放、反射（`flipX/flipY`）、绕点旋转 `rotate(θ, center)`、绕任意轴旋转 `rotate(θ, axis[, center])`、齐次矩阵统一、组合顺序（`A*B` 语义）、`basis(u,v,w)`、通用逆 `Mat::inverse`、通用行列式。

### D2：2D 补 `shear` 与逆变换闭式

- `transform2d::shear(sxy, syx)`：`x' = x + sxy·y`、`y' = syx·x + y`（讲义 `Hx(a)` 即 `shear(a, 0)`）。补它的理由：错切在讲义里是"二维线性变换"的四大类之一，而库里完全缺；同时它是**保持面积**的典型例子（`det = 1 − sxy·syx`，单向错切 `det = 1`）。
- `inverseTranslate / inverseRotate / inverseScale / inverseShear`：直接给出基元变换的逆（`T⁻¹ = T(−t)`、`R⁻¹ = R(−θ)`、`S⁻¹ = S(1/s)`、错切逆的闭式）。
  - 与通用逆 `Mat::inverse()` 的分工：闭式**语义明确、无需求行列式/伴随矩阵**，适合已确定变换类型的场景；通用逆仍用于任意矩阵。
  - 不可逆情形（缩放分量为 0、错切 `det = 0`）返回 `false` 且不改输出，与 `Mat::inverse` / `normalize` 的"布尔返回"风格一致。

### D3：齐次坐标补两个"方向性"工具

- `math::applyVector(Mat, Vec)`：**向量变换**（齐次 w = 0），只作用矩阵的线性部分、忽略平移。
  - 为什么补：点写 w = 1（平移生效）、向量写 w = 0（方向不该被平移），这是齐次坐标的核心区分；而 `Transform::apply` 的契约明确要求 w = 1，调用方"自己补 0"属于绕过契约、且容易写成 1 得到错误结果。补一个**有名字、不会用错**的入口，比省几行代码重要。
  - 同时给 `Transform<N>::applyVector(const Vec<N-1>&)`，让变换链也能做向量变换。
- `math::fromHomogeneous(Vec4, Vec3&)` / `(Vec3, Vec2&)`：齐次归一化（除以 w）。
  - 为什么补：齐次等价（`(kx,ky,kz,k) ∼ (x,y,z,1)`）是齐次坐标的根基，且透视投影的本质就是"让 w 承载深度、最后除以 w"。此前这段除法散落在 `View` 内部，无法复用。
  - `w == 0`（方向/无穷远点）返回 `false`，不产出 inf/NaN。

### D4：3D 补 `euler` 与带原点的 `frame`

- `transform3d::euler(alpha, beta, gamma)` = `Rx(α)·Ry(β)·Rz(γ)`（Roll/Pitch/Yaw）。
  - 为什么补：**能力可达但极易写错**——`rotate(α,X)·rotate(β,Y)·rotate(γ,Z)` 就是它，但顺序、轴、约定全靠调用方记忆，而讲义专门强调"顺序依赖"。补一个入口并把顺序**写死在注释里**，比让每个人各写一遍更安全。
  - 其它顺序约定（ZYX / YPR 等）不提供，避免"看起来都支持、实际各不同"。
- `transform3d::frame(u, v, w, o)`：坐标基矩阵**带原点**（讲义 `F(u,v,w,o)`）。
  - 为什么补：`basis(u,v,w)` 只有三根方向列，缺原点平移 o。而"把某个坐标系摆到世界"（模型/关节/骨骼）和"相机 → 世界矩阵 `C`"都需要 o。
  - 关系写清：`basis(u,v,w)` = `frame(u,v,w, 0)` 的线性部分；`C = frame(cameraRight, cameraUp, cameraBack, eye)`（`View` 直接这样构造 `C`，避免两处实现）。
- 3D 也补 `inverseTranslate / inverseRotate(轴 或 任意轴) / inverseScale`，与 2D 对称。

### D5：诊断/元信息工具（`translationOf` / `linearPartOf` / `isAffine`）

- `translationOf(m)`：取平移列（`Mat3` 的 (0,2)(1,2)；`Mat4` 的 (0,3)(1,3)(2,3)）。
- `linearPartOf(m)`：取左上线性部分（去掉平移列与齐次行）。
- `isAffine(m)`：齐次行是否严格为 `(0,…,0,1)`。
- 定位明确：**这些不是画图必需**，是调试与教学工具（"这个矩阵含不含平移""它是不是仿射"）。`isAffine` 也用于区分"仿射变换"与"投影矩阵"（后者齐次行含非零项）。

## 后果

- 新增公共 API（`gfx::math`）：`transform2d::{shear, inverseTranslate, inverseRotate, inverseScale, inverseShear}`、`transform3d::{euler, frame, inverseTranslate, inverseRotate, inverseScale}`、`{applyVector, fromHomogeneous, translationOf, linearPartOf, isAffine}`、`Transform<N>::applyVector`。
- 既有 API 一行未改（纯新增，向后兼容）；`rotateViaBasis` 等"组合版"与闭式版的对照测试保持不变。
- `View` 复用 `frame`（构造相机→世界矩阵）与 `fromHomogeneous`（NDC 反投影），说明这两个补全确实被其它模块需要。

## 验证（可复现）

- 单元测试 `ctest -R test_transform`（**427 checks**，其中新增部分）：
  - `shear`：`y=1` 移动 `a`、`y=0` 不动（讲义的两个锚点）；单向 `det = 1`、双向 `det = 1 − sxy·syx`；闭式逆 × 原矩阵 = 恒等；`det = 0` 时不可逆。
  - 逆变换闭式：与通用逆 `Mat::inverse()` **逐元素互校**（2D 与 3D 各一组组合变换，按 `M⁻¹ = S⁻¹R⁻¹T⁻¹` 顺序组装）；零分量缩放不可逆。
  - `applyVector`：同一矩阵下点（平移生效）与向量（只缩放）结果不同；变换链上的 `applyVector` 等价。
  - `fromHomogeneous`：同一点的任意非零倍数结果相同；`w = 0` 失败且输出不变；2D 版同理。
  - `euler`：只留 `γ` 时等价 `Rz(90°)`；与"逐个 `rotate` 相乘"**逐元素一致**；`Rx·Ry ≠ Ry·Rx`（顺序敏感，实测存在差异项）；`det = 1` 且 `isAffine` 为真。
  - `frame`：`o` 分量作为平移生效；`frame(..., o=0) == basis(...)`；旋转坐标基结果与对应旋转矩阵的线性部分一致。
  - 拆解：`translationOf` / `linearPartOf` 取值正确；仿射下"线性部分行列式 = 整体行列式"；人为构造的非仿射矩阵被 `isAffine` 判否。

## 待细化

- **`euler` 的可选顺序**：若将来需要 ZYX / Yaw-Pitch-Roll 等约定，考虑加参数化顺序（当前只提供一种，避免隐性分叉）。
- **3D 错切**：讲义只讲 2D 错切，故未提供；若用于特殊投影（如偏斜视锥）再补。
- **负缩放的语义**：`inverseScale` 对负分量照常求逆（数学正确），但"负缩放 = 反射 + 缩放"的解读未在 API 层表达。
- **诊断工具的扩展**：`Mat4` 的"是否刚体/是否正交/是否纯旋转"等判定暂未提供，需要时再补。
