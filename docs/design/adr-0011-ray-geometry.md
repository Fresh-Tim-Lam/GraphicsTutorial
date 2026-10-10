# ADR-0011 — 光线求交与包围体加速（作业三沉淀为库能力）

> **用途**：记录"光线求交 + 包围体加速"这套**纯几何能力**落位 `gfx` 库的决策：类型与函数落点、算法选型（三角形 Möller–Trumbore、AABB slab、BVH 中位数划分）、与具体图元解耦的接口约定、以及为后续"用 gfx 自立门户做光线跟踪"打的地基。
> **目标读者**：实现/使用光线求交与 BVH 的人与 AI Agent。
> **关联文档**：[adr-0002](adr-0002-template-math.md)（模板与头文件子库风格）；实现 [ray.hpp](../../src/include/gfx/geom/ray.hpp) / [bvh.hpp](../../src/include/gfx/geom/bvh.hpp) / [vec.hpp](../../src/include/gfx/math/vec.hpp)；测试 [test_ray.cpp](../../tests/test_ray.cpp)；教学主题 [ray-basics](../../tutorials/ray-basics/README.md)。

> **状态**：✅ 采纳（2026-10-08）。落地：`Ray` / `Aabb` / `HitRecord`、`intersectSphere` / `intersectPlane` / `intersectTriangle` / `intersectCylinder` / `intersectAabb`、`Bvh`；`math::reflect` / `math::refract`。

## 背景

- 触发材料：课程《作业三》要求实现光线求交与包围体加速（BVH）。这套能力是**光线跟踪的地基**，应沉入库而不是留在作业脚手架里。
- 项目现状：`gfx` 只有渲染（光栅化/投影/像素）与几何（点、半边 B-rep），**没有**光线求交与加速结构。
- 阶段边界：本阶段**只做纯几何**（命中判定/参数 t/交点/朝外法向）与加速；**不做**光照模型与递归跟踪（后续阶段）。
- 分层约束：项目规定 `geom/` **只依赖 `math/`**，与 `core/`、`render/` 零耦合（见 [src/README.md](../../src/README.md)）。光线求交属纯几何 ⇒ 全部放 `geom/`。

## 决策

### D1：全部落 `geom/`（`ray.hpp` / `bvh.hpp`），数学原语落 `math/vec.hpp`

- `geom/ray.hpp`：`Ray`（起点 + **单位**方向）、`Aabb`、`HitRecord` 与五个求交函数。
- `geom/bvh.hpp`：`Bvh`（与图元类型解耦的加速结构，只依赖 `Aabb` + `Ray`）。
- `math/vec.hpp`：`reflect` / `refract` 两个自由函数（模板 `Vec<N>`，与既有 `dot` / `cross` / `normalize` 同风格）。
- 理由：`geom/` 已被定义成"几何/建模库不需要渲染"，光线求交正是纯几何；放这里既守住分层，又能将来把 `geom` 单独拆库。`reflect`/`refract` 是通用向量运算，属 `math/`。

### D2：三角形求交用 **Möller–Trumbore**（双面、闭区间边界）

- 出处：T. Möller, B. Trumbore, *Fast, Minimum Storage Ray/Triangle Intersection*, JGT 2(1):21-28, 1997。
- 选它而非"平面求交 + 重心判定"：**无需预计算平面方程、无需先算交点**，一次行列式判据同时给出重心坐标 (u,v) 与 t；代码短、数值稳定。
- **不剔除背面**：`det` 取绝对值判定 ⇒ 正反面都能命中，保持几何通用性（后续按需在光照阶段做单面处理）。法向由 `(b−a)×(c−a)` 归一化得到（几何法向，与观察侧无关）。
- 边界口径写死并测出：u、v 判定取**闭区间**（`u ≥ 0`、`v ≥ 0`、`u+v ≤ 1`），故命中**边/顶点**也算命中。
- 退化三角形（三顶点共线，面积 0）⇒ `det ≈ 0` 返回 false。

### D3：圆柱求交支持**任意轴**，含两端圆盖

- 轴未对齐：先把轴归一化为 â，取方向/原点在**垂直于 â 的平面**内的分量解二次方程（侧面），再用沿轴偏移 `|h| ≤ halfHeight` 裁剪；两端圆盖 = 平面求交 + 径向 `≤ radius` 判定；在所有有效解里取最近者。
- 法向一律**朝外**：侧面为径向向外 `normalize(径向分量)`；圆盖为 `±â`。
- 理由：只支持 Y 轴无法覆盖任意朝向的实例（旋转后的圆柱）；解耦成"任意轴"一次做对。

### D4：AABB 用 **slab 法**；方向分量为 0 不除零；`tMin` 忽略背后

- 各轴求 `t1,t2` 后取交集 `[tNear, tFar]`（`tNear` 取 max、`tFar` 取 min）。
- 方向分量 ≈ 0 时**不除零**：改用"起点是否落在该轴 slab 内"的分支判断（在外 ⇒ 直接无交）。
- `tMin` 用于忽略"整段都在起点之前"的盒（`tFar < tMin` ⇒ 无交）；`tNear` 允许为负（起点在盒内时便于 BVH 剪枝，故**不**把 `tNear` 钳到 `tMin`）。

### D5：BVH —— 最长轴中位数划分 + 当前最优 t 剪枝；回调**带图元索引**

- 构建：按包围盒**中心**在最**长轴**上的分布取中位数（`std::nth_element`）划分，叶子阈值 4（2–4 之间）。节点用索引池（`std::vector<Node>`），`order_` 记录图元索引的重排。
- 遍历：递归，**先近后远**访问孩子，用当前最优 `t` 与节点 AABB 的 `tNear` 比较剪枝（`tNear > bestT` 则跳过）。结果与"对所有图元线性遍历取最近"**完全一致**。
- **与图元类型解耦的接口约定（对规格的一处必要修正）**：回调签名实现为 `bool(const Ray&, int index, float& outT)`——多出 `index`（图元下标，与 `build` 传入的 `boxes` 同序）。理由：BVH 只持有 AABB、不持有图元本身，回调必须知道"当前该对哪个图元做精确求交"，否则无法完成解耦；任务描述给的 `bool(const Ray&, float& outT)` 缺此信息。
- 可选的 `candidates()`（收集候选叶子）与 `lastVisitCount()`（最近一次查询遍历的节点数，`mutable` 计数器）用于调试/统计，**不改变查询语义**。

### D6：`reflect` / `refract` 落 `math/vec.hpp`（模板化）

- `reflect(d, n) = d − 2(d·n)n`，对任意 `N` 成立。
- `refract(d, n, eta, out)`：Snell 定律，`k = 1 − eta²(1 − (d·n)²)`；`k < 0` ⇒ **全反射**返回 false（`out` 不变）；否则 `out = normalize(eta·d − (eta·(d·n) + √k)·n)`。
- **符号约定**：`d` 指向表面、`n` 朝外时 `d·n < 0`；`eta = n1 / n2`。该约定用测试锁定（含 `eta = 1` 时 `out == d`、垂直入射方向不变、掠射全反射）。

### D7：命中判定与 `t > tMin` 严格一致

- 所有求交函数只接受 `t > tMin`（默认 `1e-4f`）的解，杜绝"t 略小于 0 却命中"——否则后续做阴影会因自交产生黑斑。

## 后果

- 新增公共 API（`geom` 层）：`gfx::Ray` / `gfx::Aabb` / `gfx::HitRecord` / `gfx::aabbUnion`、`intersectSphere` / `intersectPlane` / `intersectTriangle` / `intersectCylinder` / `intersectAabb`、`gfx::Bvh`。
- 新增公共 API（`math` 层）：`gfx::math::reflect` / `gfx::math::refract`（补入既有 [vec.hpp](../../src/include/gfx/math/vec.hpp)）。
- 新增测试 `tests/test_ray.cpp`（**85 checks**）；新增教学主题 [ray-basics](../../tutorials/ray-basics/README.md) 与演示目标 `tutorial_ray_basics`（产物 `RayBasics`，渲染一张"只用求交、不用光照"的法向可视化 PNG）。
- **为后续打地基**：光照/反射/折射/递归跟踪（下一阶段）将直接复用本层的命中与法向，可在不改几何层的前提下叠加；`reflect`/`refract` 已就位。
- 不改动既有模块行为：`geom/` 仍只依赖 `math/`（`ray.hpp` 只 include `gfx/math/math.hpp`；`bvh.hpp` 只 include `ray.hpp`），未引入 `core/`、`render/` 依赖。

## 验证（可复现）

- 单元测试 `ctest -R test_ray`（**85 checks，0 failures**）：
  - `reflect`：45° 斜射/垂直入射/非轴向法向（手算核对）+ 二维（模板通用性）。
  - `refract`：空气→玻璃 `eta=1/1.5` 与手算方向 / Snell 角度核对；`eta=1.5` 掠射**全反射**返回 false 且 `out` 不变；`eta=1` 斜入射方向与入射相同；垂直入射方向不变。
  - 球：正对球心 `t = |origin−center| − r` 且法向、擦边（判别式 = 0）、完全错过、起点在球内命中远面、负解被忽略。
  - 平面：正射命中 t/法向、平行、在背后。
  - 三角形：命中心/边/顶点（闭区间口径）、在外、退化、**背面也命中**。
  - 圆柱：侧面（正面/轴心出发）、两端圆盖、超出半高被裁掉、未命中、**倾斜轴**，法向朝外。
  - AABB：正射相交、错过、起点在盒内、方向分量为 0（不除零）、在背后。
- **BVH 与暴力法一致性**（`test_ray` 原始输出）：

  ```
  [BVH] N=200   图元= 200 光线=300 | 命中=36 不一致=0 | 节点=127 叶子=64 | 回调: 暴力=60000 BVH=461 (平均 1.5, 剪枝率 99.2%)
  [BVH] N=2000  图元=2000 光线=300 | 命中=90 不一致=0 | 节点=1023 叶子=512 | 回调: 暴力=600000 BVH=685 (平均 2.3, 剪枝率 99.9%)
  ```

  两组规模 × 300 条随机光线：`closestHit` 与线性遍历取最近的 `t` **完全一致**（0 不一致，容差 `1e-5`），且回调调用次数远小于 `N × 光线数`（剪枝生效）。
- 教学演示自检：`RayBasics` 渲染 800×600 法向可视化图，控制台输出命中像素占比与每像素平均回调/节点访问次数，产物 `tutorials/ray-basics/output/ray-basics.png`。

## 待细化

- **光照与递归**：本阶段不做（后续阶段）—— 光线与材质的着色、阴影、反射/折射递归、抗锯齿采样。
- **单面剔除**：三角形目前双面；如需背面剔除在光照阶段按法向与视线关系处理。
- **SAH 划分**：BVH 当前用中位数（课程要求），`Aabb::surfaceArea()` 已备好但未用于划分代价。
- **图元扩充**：圆锥、圆环、任意二次曲面等未提供。
- **并行/包遍历**：BVH 遍历为单线程递归；SIMD/多线程、packet traversal 未做。
