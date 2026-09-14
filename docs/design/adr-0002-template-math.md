# ADR-0002 — gfx_math 全模板化：Mat<N>/Vec<N> + 形状别名

> **用途**：记录 gfx_math 从"三份形状直写代码"收敛为"模板 + 内部特化"的决策与理由，作为后续算法优化（CPU SIMD / GPU）的结构性前提。
> **目标读者**：参与 gfx_math 演进/优化的开发者与 AI Agent。
> **关联文档**：[adr-0001-gfx-math-canvas.md](adr-0001-gfx-math-canvas.md)；[src/../../src/include/gfx/math/mat.hpp](../../src/include/gfx/math/mat.hpp)；[api-docs.md](../conventions/api-docs.md)。

> **状态**：✅ 采纳（2026-09-09）。实现已落地并通过测试（0.1.0 未升级：类型名/行为保持不变，仅布局尺寸统一为 4 分量对齐，ABI 尚未对外稳定）。

## 背景

- 原实现为 mat2/mat3/mat4、vec2/3/4 三份独立直写代码，乘法/转置/伴随逻辑重复。
- 讨论中明确：小矩阵 CPU 最优 = 常数最优化（固定 N 全展开 + FMA）；GPU 路线靠并行度与共享内存，二者都是"算法替换"，与类型/接口无关。
- 结论：应把"形状"与"算法"分离——公共接口统一，算法收敛到模板内部，未来优化只改内部、不动 API。

## 决策

### D1：全模板化 Mat<N> / Vec<N>
- `Mat<N>`（N=2/3/4）模板承载全部矩阵逻辑；`mat2 = Mat<2>`、`mat3 = Mat<3>`、`mat4 = Mat<4>` 为别名，**公开类型名不变**。
- `Vec<N>` 同理；`vec2/3/4` 为别名。原调用方代码（`.x/.y`、`operator()(r,c)`、`determinant()/inverse()` 等）继续可用。

### D2：存储与布局
- `Vec<N>` 固定为 4 分量（x/y/z/w，各 float）连续存储；未用分量恒为 0。代价：Vec2/Vec3 占用 16 字节，换取 16 字节对齐、整块 SIMD 友好。若需省内存后续可做特化（见"待细化"）。
- `Mat<N>` 行主序**扁平**存储 `float data[N*N]`（元素 (r,c) 在 `data[r*N+c]`）：行字面量 `{{...},{...},{...}}` 由构造函数按行展平存入，故 `data` 本身即可作为扁平 `float*` 传给算法/批量层（无需 `flat()` 之类封装）；元素访问 `operator()(row, col)`。

### D3：算法收敛到 `math::detail`（未来优化插入点）
- 行列式：Laplace 展开递归（2×2 底，另含 1×1 底供 Mat2 逆用）。
- 逆矩阵：伴随矩阵法（教学规模 N≤4）。
- 未来 CPU SIMD / 分块 / GPU 路径：只替换/分派 `math::detail` 内实现，公共类型与 `operator*`/`inverse` 等接口不动。
- 乘法/变换的 `operator*` 为通用模板，编译期 N 已知，编译器可自行展开；现阶段不手写展开（教学优先，见 ADR-0001）。

### D4：不引入运行时通用矩阵
- 排除 `Mat(int n)` 动态分配形态（无编译期优化、缓存差、教学晦涩）。

## 后果

- 头文件数量：新增 vec.hpp / mat.hpp（形状别名 vec2/3/4、mat2/3/4 已并入这两个头，无独立别名文件）。
- 重复代码消除：乘法/转置/伴随/逆各一份实现。
- API 破坏程度：无（类型名、方法名、语义均兼容）；sizeof(Vec3) 由 12→16 字节，属 ABI 变化但 0.x 未对外稳定。
- 教学树 math-foundations 概念不变；demo/test 无需改动（已验证通过）。

## 待细化

- Vec<2>/Vec<3> 的省内存特化（若需要）——靠显式模板特化实现，不依赖别名。
- 真实性能优化专题（如逐像素热循环场景）落地时，在 detail 层做 SIMD 或批量路径。

## 补充澄清（2026-09-09，✅ 采纳为修正性结论）

- **别名并非"为加速而特化"**：N≤4 不存在形状专属算法加速（乘法靠编译器展开常量循环即为实际最优，见 D3/背景）。保留别名 mat2/3/4、vec2/3/4 的真正理由是：① 教学/文档惯用命名（vec3/mat4 贴近图形学用语）；② API 兼容（现有教程与测试均使用别名，删除需破坏性升版本）。
- **模板的真正价值**（与别名解耦）：为"维度专属布局/语义"提供可做显式特化的收口点（如未来 Vec3 省内存为 12B、cross 仅 3 维等）；别名只是让该收口点对调用方保持稳定命名。

## 补充（2026-09-09，✅ 采纳）：换基基元与轴角旋转的双实现

- **新增基元**：`transform3d::basis(u,v,w)`（列 = 基向量的基矩阵，即换基）与 `frameFromAxis(axis,u,v,w)`（由旋转轴构造右手正交基，零轴返回 false）。
- **两条等价路径**：
  - 闭式 `rotate(radians, axis)`（Rodrigues）——**生产路径**，省两次 4×4 乘法；
  - 组合 `rotateViaBasis(radians, axis) = B · Rz(θ) · Bᵀ`——**教学路径**，展示"旋转 = 换基 + 基元旋转 + 换回"。
- **同一模式推广到绕点旋转**：`rotate(rad, center)`（2D）/ `rotate(rad, axis, center)`（3D）为闭式重载（同名 `rotate`，用参数区分"绕原点/绕点"）；`rotateViaPrimitives(...)` = `T(c)·R·T(−c)` 为组合版（闭式即其展开：旋转部分不变、平移列 = c − R·c）。两条路径等价性同样由 test_transform 逐元素对照。
- **基元旋转用枚举选轴**：`rotateX/Y/Z` 三个函数收敛为 `rotate(radians, Axis)`（`math::Axis{X,Y,Z}`）；几何层 `Point3D/PointBatch3D` 同步为 `rotate(rad, Axis)`。理由：API 面更小、支持"用变量/循环选轴"（三个函数做不到），且与既有的 `rotate(rad, Vec3 轴)` 重载天然共存（枚举类不隐式转换，无歧义）。
- **轴角旋转以"三参数重载"为唯一实现**：`rotate(rad, axis, center)` 内联写出 Rodrigues 系数（具名局部）并在**单个矩阵字面量内一次写全**；`rotate(rad, axis)` 委托它（center = 原点，代价实测 +2 ns）。依据实测：二次组装 49.5 ns → 纯展开 42.8 ns（-O2，5e6 次）。规则与记录方式见 [performance.md](../conventions/performance.md)，基线见 [benchmarks/bench_rotate.cpp](../../benchmarks/bench_rotate.cpp)。
- **理由**：换基是图形学通用模式（view 矩阵、法线矩阵、切线空间同源），作为显式基元可复用，并使"Rodrigues 公式为何长这样"有可解释的来源；等价性由 [test_transform.cpp](../../tests/test_transform.cpp) 在多组轴/角度下逐元素对照锁定。
- **教学落点**：`tutorials/math-foundations` 的"换基视角"一节。
