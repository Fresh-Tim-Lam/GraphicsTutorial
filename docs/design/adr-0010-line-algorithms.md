# ADR-0010 — 三种画直线算法对照（Naive / DDA / Bresenham，默认 Bresenham）

> **用途**：记录 `gfx::drawLine`（**唯一签名** `drawLine(Canvas&, float x0, float y0, float x1, float y1, const Color&, LineAlgorithm = Bresenham)`）的**算法选择**：把讲义里的三种画直线算法都落进库内，坐标**平铺为 `float`**（可同时接住 `int` 与 `float` 实参，故无需 `Vec2` 或 `int` 重载），**默认一律走最优的 Bresenham**；说明各自的落位、接口约定与取舍。
> **目标读者**：实现/使用光栅化画线的人与 AI Agent。
> **关联文档**：[adr-0005](adr-0005-raster-polygon-fill.md)（光栅化模块首次落地，含 Bresenham 画线）；实现 [raster.hpp](../../src/include/gfx/render/raster.hpp)；教学主题 [line-algorithms](../../tutorials/line-algorithms/README.md)；讲义《Line Segment and Polygon Scan Conversion》p3–p35。

> **状态**：✅ 采纳（2026-09-28）；2026-10-01 收敛为**单一签名**；2026-10-03 唯一签名的参数改为**平铺的 `float` 坐标**。落地：`enum class LineAlgorithm { Bresenham, DDA, Naive }`
> 与**唯一的公开入口** `drawLine(Canvas&, float x0, float y0, float x1, float y1, const Color&, LineAlgorithm = LineAlgorithm::Bresenham)`
> （内部由**唯一一处** `detail::dispatchLine` 分派；`detail::drawLineBresenham` 保留 int / float 两版，
> 由分派按"四个坐标是否为整数值"**自动选择**；`detail::drawLineDDA` / `drawLineNaive` 各为**单一函数模板**）。

## 背景

- 触发材料：课程讲义《Line Segment and Polygon Scan Conversion》p3–p35 依次讲了三种画直线算法：平凡算法（直线方程法，p9–p11）、DDA（数值微分分析器，p12–p15）、Bresenham（整数判别式，p16–p31）。
- 项目现状：`raster.hpp` 此前只有**一个**整数端点入口 `drawLine(..., int x0,y0,x1,y1, ...)`，实现就是 Bresenham（ADR-0005），另有一条独立的**浮点端点**入口 `drawLine(..., const math::Vec2& a, const math::Vec2& b, ...)`（保留亚像素）。三种算法里只暴露了 Bresenham，教学上缺少对照。
- 需求：把三种算法都提供出来做对照，并保证**默认走最优算法**，同时不改变任何既有调用的行为。

## 决策

### D1：新增 `LineAlgorithm` 枚举；默认参数 = `Bresenham`

```cpp
enum class LineAlgorithm { Bresenham, DDA, Naive };   // 顺序即"推荐度"
inline void drawLine(Canvas&, int x0, int y0, int x1, int y1, const Color& color,
                     LineAlgorithm algo = LineAlgorithm::Bresenham);
```

- 默认值必须是 `Bresenham`：用户明确要求"默认参数采用最优算法"。
- 在原整数版签名**末尾**加默认参数 ⇒ 既有所有 `drawLine(c,x0,y0,x1,y1,color)` 调用**零改动、行为完全不变**。

### D2：三种实现收敛到 `detail`，公开入口只做分派

- `detail::drawLineBresenham`：原实现**原样保留**（整数误差项 `err` + `e2` 两判断，覆盖八卦限）。
- `detail::drawLineDDA`：`steps = max(|dx|,|dy|)`，`x/y` 各按增量步进，每步 `std::lround` 取最近像素。
- `detail::drawLineNaive`：沿**较长轴**步进，另一个坐标每步用 `y=m·(x−x0)+y0`（或 `x=m'·(y−y0)+x0`）现算再 `std::lround`。

### D3：DDA 覆盖任意斜率；Naive 必须处理 `|m|>1` 与垂直线

- DDA 用 `max(|dx|,|dy|)` 归一化 ⇒ 长轴每步恰好走一格、线段不断开，任意斜率（含垂直）一致处理。
- Naive 若只按 x 步进，`|m|>1` 时 y 每步会跳多格 ⇒ 必须**沿较长轴步进**；垂直线（`dx==0`）单列处理，避免除零与斜率无穷。

### D4：三条实现共用同一套落点约定

- 都**含两端点**；都取"最近像素"、硬边、无抗锯齿；越界像素交给 `Canvas::putPixel` 忽略（与既有实现一致）。
- 坐标口径：像素坐标、y 向下。

### D5：默认选 Bresenham 的理由

- **纯整数、无除法、无浮点** ⇒ 结果精确可复现、跨平台一致，且对早期/嵌入式硬件最友好；
- 每步只 1~2 次整数加法 + 比较，最快；
- 一次覆盖八个卦限，无需按斜率分支。
- Naive（每步乘法 + 浮点取整）与 DDA（每步浮点加法 + 取整）**仅作教学对照**，不推荐用于生产。

## 统一接口（最终形态，2026-10-03：唯一签名 + 平铺 `float` 坐标）

对外**只有一个** `drawLine`：四个坐标是**平铺的 `float`**（像素坐标，y 向下），末位一个算法参数（默认 `Bresenham`）。
原先的 `Vec2` 端点版与"整数端点版"重载**均已删除**；整数端点的性能路径改由入口**按坐标数值自动选择**。

```cpp
// 画线段（含两端点）。坐标为像素坐标（浮点，y 向下，与 Canvas 一致）；算法默认 Bresenham（最优）。
inline void drawLine(Canvas& canvas, float x0, float y0, float x1, float y1, const Color& color,
                     LineAlgorithm algo = LineAlgorithm::Bresenham);
```

- **为什么是平铺 `float` 而不是 `Vec2`**：`float` 形参能同时接住 `int` 与 `float` 实参，因此
  `drawLine(canvas, 10, 20, 100, 60, color)` 与 `drawLine(canvas, 10.5f, 20.5f, 100.f, 60.f, color)`
  都能用**同一个签名**表达 —— **不需要 `Vec2` 重载，也不需要 `int` 重载**。

```cpp
gfx::drawLine(canvas, 10, 20, 100, 60, color);                             // int 实参（默认 Bresenham）
gfx::drawLine(canvas, 10.0f, 20.0f, 100.0f, 60.0f, color);                 // float 实参
gfx::drawLine(canvas, 10.0f, 20.0f, 100.0f, 60.0f, color, gfx::LineAlgorithm::DDA);
gfx::drawLine(canvas, px0, py0, px1, py1, color, gfx::LineAlgorithm::Naive);  // 端点可带小数（亚像素）
```

- **单一签名 + 按坐标数值自动选择整数路径**（关键，不能退化）：入口内部——
  - 若 `algo == Bresenham` 且四个坐标**皆为整数值**（判定用 `v == std::floor(v)`，无 epsilon、
    不引入误判）⇒ 调用 **int 版纯整数 Bresenham**（把坐标转 `int` 后传入；纯整数判别式，无浮点、无除法，即性能路径）；
  - 否则 ⇒ 调用 **float 版**（浮点步进 + 每步取最近像素，保留亚像素）。
  ⇒ **既保持单一 API，又不丢整数性能**。
- **分派只有一处**：唯一入口调用唯一的内部函数
  `detail::dispatchLine(Canvas&, float x0, float y0, float x1, float y1, const Color&, LineAlgorithm)`
  （不再需要模板、不再有第二个公开重载，也不复制 `switch`）。
- **内部实现的分化原则**（只保留必要的分化，消除真重复）：
  - **`Bresenham` 保留 int / float 两版**：整数版是**纯整数判别式**（无浮点、无除法），这是它的性能价值，
    必须独立存在（仅由分派在"四坐标皆为整数值"时选中，**不再作为公开重载暴露**）；浮点版按 `max(|dx|,|dy|)`
    归一化浮点步进 + 每步取最近像素（保留亚像素）。
  - **`DDA` / `Naive` 各只有一版**（函数模板 `template<typename T>`，内部按需转 `float`）：两者的算法**本质就是浮点**，
    整数端点只是其特例 ⇒ 原来的 int / float 两份是**真重复**，已合并为单一模板。DDA 用加法**累积**增量
    （保留累积误差特征）；Naive 每步用直线方程**现算**（无累积）。整数端点经唯一入口时以 `T=float` 实例化，
    结果与旧整数端点**逐像素等价**（测试 243 checks + 演示统计逐行锁定）。
- **既有行为不变**：整数端点（坐标皆为整数）仍走纯整数 Bresenham，逐像素不变；带小数端点仍走浮点步进，
  逐像素不变。`drawPolygonOutline` **未改算法**（仍是先 `lround` 顶点、再把平铺坐标交给唯一入口 ⇒ 因坐标已成整数值而自动走整数 Bresenham），行为逐像素不变。

## 后果

- 公共 API 收敛为**单一签名** `gfx::drawLine(Canvas&, float x0, float y0, float x1, float y1, const Color&, LineAlgorithm = Bresenham)`
  与 `gfx::LineAlgorithm`；原 `Vec2` 端点版与"整数端点（int x0,y0,x1,y1）"重载**均已删除**（整数性能路径由内部 int 版承接）。
- **不改变既有公开行为**：所有既有 `drawLine` 调用（调用点已改为平铺坐标）仍走 Bresenham、输出逐像素不变（与收敛前逐像素一致）。
- 新增测试：`tests/test_raster.cpp` 增"画直线算法对照"一组（见下）。
- 新增教学主题 [line-algorithms](../../tutorials/line-algorithms/README.md) 与演示目标 `tutorial_line_algorithms`（产物 `LineAlgorithms`）。
- 头文件顶部补了"三种算法对照表"（每步运算量 / 是否浮点 / 是否取整 / 硬件友好度 / 角色）。

## 验证（可复现）

- 单元测试 `ctest -R test_raster`：新增 16 条**代表性线段**（水平、垂直、45°、缓斜 `1/3`、缓斜 `3/7`、陡斜 `3/1`、陡斜 `7/3`、负缓斜 ±`3/5`、负陡斜 ±`5/3`、长度 1、退化点、极长缓斜/陡斜），对 Naive / DDA / Bresenham 各画一图，**逐像素比对三者完全一致**；并逐条校验**非空、含两端点、像素落在包围盒内**；另断言"**不传算法 == 显式传 Bresenham**"（锁定默认参数即最优）。
  - checks 由 **86 → 168**（`test_raster` 单个可执行；`ctest` 由 11 → 12 个测试、全部通过）。
- 演示程序 `LineAlgorithms` 打印**真实统计**（非写死）：11 条线段，**差异线段数 = 2**——即两条"整数端点下直线恰穿过像素中心中线（严格 `.5` 并列）"的线段；其余 9 条三算法一致；`Naive↔DDA` 恒为 0。
- 单元测试新增"**浮点端点的算法选择（统一接口）**"一组（5 条浮点端点线段：短线、长缓斜、一般斜、陡斜、退化点），断言：
  (a) 浮点版"不传算法 == 显式传 `Bresenham`"逐像素一致；(b) 浮点版传 `DDA` / `Naive` 能编译、**非空、含两端点、像素落在包围盒内**；
  (c) 与 Bresenham 的对称差像素数**实测并打印**：只有严格 `.5` 并列的长缓斜线（`dy=10.0`）出现差异（`Naive`=2、`DDA`=16），
  其余线段三者逐像素一致（均为 0）；断言 ≤ `max(2, 总数/4)`，未用"强行对齐"掩盖。
  - `test_raster` checks 由 **168 → 243**；`ctest` **11/11** 通过。

## 已知限制 / 待细化

- **并列取整的 ±1 差异**：当直线恰穿过像素中心的中线（例如斜率 `1/4`）时，Bresenham 的整数判别式在相等时朝**终点方向**走一步，而 DDA/Naive 的 `std::lround` 对半值**远离零**取整 ⇒ 个别像素相差 1。这不是 bug 而是并列取整方向的定义差异，已在教学 README 与演示统计中如实量化（未用"强行对齐"掩盖）。
- **只覆盖"最近像素 + 硬边"**：无抗锯齿（需 AA 见 `triangle.hpp` / ADR-0008）；不做线宽、端点样式。
- 演示固定画布尺寸，未提供 `--size` / `--window`。
