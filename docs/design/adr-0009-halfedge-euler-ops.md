# ADR-0009 — 半边数据结构与欧拉操作（作业二）

> **用途**：记录 `gfx::HalfEdgeMesh`（半边/B-rep）与五个欧拉操作（MVFS/MEV/MEF/KEMR/KFMRH）的落位与关键约定：数据结构选型、每个操作的前置条件与环链维护规则、校验策略。
> **目标读者**：实现/使用网格拓扑编辑的人与 AI Agent。
> **关联文档**：[adr-0002](adr-0002-template-math.md)（模板与头文件子库风格）；实现 [halfedge.hpp](../../src/include/gfx/geom/halfedge.hpp)；教学主题 [euler-ops](../../tutorials/euler-ops/README.md)；讲义《Half edge data structure and Euler Operation》。

> **状态**：✅ 采纳（2026-09-16）。落地：`HalfEdgeMesh`（`mvfs` / `mev` / `mevInLoop` / `mef` / `kemr` / `kfmrh` / `mfkrh` / `mefr` / `validate` / `counts` / `eulerCharacteristic` / `topologyText` / `loopVertices` / `firstInnerLoop` / `forEachEdge` / `forEachVertex`）。

## 背景

- 触发材料：课程《Half edge data structure and Euler Operation》与《作业二 欧拉操作》。作业要求**自行实现** MVFS、MEV、MEF、KEMR、KFMRH 五个操作，维护体/面/环/边/顶点的拓扑关系，并**通过测试验证**（检查环闭合、归属一致、删除对象不再被引用；半边结构还需检查配对与前驱后继）。
- 项目现状：`gfx` 库此前聚焦渲染（光栅化/投影/像素），**没有**拓扑数据结构。作业要求"不提供预先的程序框架"，故数据结构与接口自行设计。
- 参考：讲义给出半边结构的字段清单（Body→first face；Face→first Loop；Loop→first halfedge；HalfEdge→next/prev/opposite/start/to/edge；Vertex→half edge；Edge→一对半边）与欧拉公式 `V − E + F = 2(s − h) + r`。

## 决策

### D1：落位 `halfedge.hpp`；用**索引池**而非指针

- 单一头文件（结构 + 五个操作 + 校验 + 文本输出），与 `gfx` 其余模块同为零依赖头文件实现。
- 元素存 `std::vector`，用 `MeshId`（int，`kNoMesh = -1` 表示"无"）互相引用；删除只置 `alive = false`。
- 理由：id 稳定 ⇒ 测试断言与文本 dump 可读、可复现；避免悬垂指针；教学规模下不回收元素的代价可忽略。

### D2：字段约定 —— `Vertex::halfEdge` 指向"以该顶点为起点"的半边

- 这是**一邻域访问**的入口（讲义 p10-15 的绕点遍历都从这里出发），必须成立。
- 每次拓扑修改后统一重扫（`repairVertexHalfEdges()`）重建该入口，而不是在每处小心翼翼地改 —— 用一个**不变量 + 一次修复**换掉一堆易错的特例代码。

### D3：MEV 处理两种情形；另提供**显式指定环**的版本

- 情形①：MVFS 之后环上还没有边（孤点环）⇒ 新边的两条半边自成闭环；
- 情形②：环上已有边 ⇒ 在"以 v0 为起点的半边"**之前**插入 `[v0→v1, v1→v0]`。
- `mev(v0, p)` 按 `v0` 自动推断所属环；但 MEF 之后一个顶点常同时属于**多个环**，自动推断会选错 ⇒ 提供 `mevInLoop(loop, v0, p)` 供调用方精确控制（测试与示例都显式指定环）。

### D4：MEF 的拆环约定（新环给新面）

- 在指定环中从 v0 沿 `next` 走到 v1，得到路径 P1；**P1 + 新边**（v1→v0）构成**新环**（新面的外环），**其余部分 + 新边的另一半**留在**原环**（原面）。
- 约定写死并测出结果：调用方据此安排顶点顺序即可得到想要的两个面。
- 边界：v0 == v1、环中找不到 v1、P2 为空（会留下空环）时拒绝。

### D5：KEMR 的判据是"两条半边属于同一环"，且必须**非紧邻往返**

- 判据直接对应"桥接边"的几何含意：环在此处折返（外边界—桥—内环的 8 字形）。
- 额外拒绝"两条半边互为 `next`"的边：删它只会得到退化环（一个顶点的往返），没有拆环意义。
- 拆环归属：段 A（h1.next…h2.prev）保留为**原环/外环**；段 B（h2.next…h1.prev）独立成环并标记 `isOuter = false`（**内环**）。
- 数量变化：E−1、L+1，V/F 不变。

### D6：KFMRH 只改**归属**，不动几何

- 前提（基础实现）：两面同体；donor **只有一个外环**、不含内环。
- 动作：把 donor 的外环 `face` 改为 receiver、`isOuter = false`、加入 receiver 的 `loops`；删除 donor 面并从体的面集合移除。
- 数量变化：F−1，V/E/L 总数不变。
- 明确写清：这是**拓扑**上的"孔"（内环），**不等同于**几何挖孔或自动生成孔壁 —— 与作业说明一致。

### D7：`validate()` 只查结构一致性；欧拉公式单独报告

- `validate()`：半边配对互反、`next/prev` 互反、环闭合且**顶点连续**（`h.end == h.next.start`）、next 与本半边同环、面/环/体归属一致、无悬挂引用、顶点入口约定。
- **不**在 `validate()` 里检查欧拉公式：它只在**封闭体**上成立（`V − E + F = 2(s − h) + r`），而带边界的中间状态（面片、管道）本就不满足。故提供 `eulerCharacteristic()` 由使用方按场景判断（测试对四面体断言 == 2）。

### D8：附加扩展 `mfkrh` 与 `mefr`（超出作业要求）

> 作业只要求 MVFS/MEV/MEF/KEMR/KFMRH 五个操作，且明确"不要求……完整带孔实体建模或额外欧拉操作"。D8 为**自主扩展，非评分必需**；五个操作本身已完整达标。

- **`mfkrh`（Make Face Kill Ring Hole，KFMRH 的逆）**：把某面的一个**内环**摘出，成为**新面的外环**（讲义 p25 的 mfkrh）。数量变化 **F+1**（V/E/L 不变）。前提：环存在、属于该面、且是**内环**（`isOuter == false`）。
- **`mefr`（Make Edge Face Ring，跨环建面）**：在两圈"平行环"之间缝出一圈四边形面（把两环缝成一根方管）。语义：设环 A 顶点序 `VA[0..N−1]`、环 B 类似；
  ① 用几何最近邻确定对位起点；
  ② 取**反向索引** `B_i = q_{off−i}`，使 `B_{i+1}→B_i` 恰为 loopB 的**己有半边**（可复用；同向索引会落到配对半边上）；
  ③ 新建 N 条竖棱 `VA[i]—B_i`，第 i 张新面 = `VA[i]→VA[i+1]→B_{i+1}→B_i→VA[i]`（上、下边分别复用环 A / 环 B 的半边）；
  ④ 环 A、环 B 被**消费**（从所属面的环列表移除；若为外环则让同面另一环接任并标记为外环）。
- **为什么需要它（半边配额 + 环归属）**：
  ① `KFMRH` 只会把外环**并成**内环（F−1），而构造中常先切出"盖子"面，需要反过来把它**摘出**（F+1）——这是 `mfkrh`；
  ② 两个孔口分属不同面、是两个**不同环**，需要一条能同时引用两环的新操作——这是 `mefr`；
  ③ 每条 `Edge` 恰好携带 2 条方向相反的半边（**配额限制**），用 MEV/MEF 逐面拼墙面会导致边被反复复用/冲突，无法干净地"两环之间一次缝一圈"，故由 `mefr` 一次性建满 N 张面（新建 N 条边，两环自身半边复用）。
- **与 `mfkrh` 的关系**：二者互补 —— `mfkrh` 把内环"摘出成独立面"（F+1），`mefr` 把两个分属不同面的内环"缝成方管"；构造"贯穿孔"时先在顶/底面各用一次 KEMR+KFMRH 得到内环，再用 `mefr` 跨环缝合。
- **数量变化**：`mfkrh` F+1；`mefr` **V 不变、E+N、F+N、L+(N−2)**（两环被消费 ⇒ 内环 −2），新面均 `isOuter = true`。
- **验证方式**：`tests/test_halfedge.cpp` 用例 10 —— 断言 V 不变、E+N、F+N、L+(N−2)、半边 +2N、内环 −2；被消费的环不再存活、不再出现在任何面；N 个新面外环均 4 顶点且上/下环各 2 点；终态 `V−E+F=2` 且 `V−E+F−内环数=0`；并有失败路径（非法环 / 已消费环 / 同一环）被拒绝且结构不变。

## 后果

- 新增公共 API：`gfx::HalfEdgeMesh` 与 `MeshVertex` / `MeshHalfEdge` / `MeshEdge` / `MeshLoop` / `MeshFace` / `MeshSolid` / `MeshCounts` / `MeshId`。
- 新增测试 `tests/test_halfedge.cpp`（**176 checks**）；新增教学主题 [euler-ops](../../tutorials/euler-ops/README.md) 与演示目标 `tutorial_euler`（产物 `EulerOps`，五个操作的六块快照）与 `tutorial_euler_interactive`（产物 `EulerInteractive`，28 步构造"上下贯穿的方孔立方体"，亏格 1，属**附加**）。
- 不改动既有模块；`halfedge.hpp` 仅依赖 `math/vec.hpp`（顶点坐标）。

## 验证（可复现）

- 单元测试 `ctest -R test_halfedge`（**176 checks**）：
  - MVFS：S/F/L/V 各 +1、无边、归属（体的面集合、面→外环、环→面）正确；
  - MEV 两种情形：情形① 环闭合（两条半边互为 next/prev）、情形② 新顶点确实只连到 v1；
  - MEF：V/E/F/L 变化与两环归属；环闭合；
  - **连续调用**：MVFS + MEV×3 + MEF×3 ⇒ 四面体（`V=4 E=6 F=4`、`V−E+F=2`、每顶点度数 3、无重复边）；
  - KEMR：前提（同环）成立、删边后 E−1/L+1/内环出现、被删边不再存活、原面同时拥有内外环；
  - KFMRH：F−1、环总数不变、内环 +1、donor 面消失、接收面有两个环；
  - 失败路径：非法参数与前提不满足时返回失败且**结构不变**（数量与 validate 均不变）。
- **附加扩展**：MEFR 用例断言 V 不变 / E+N / F+N / L+(N−2) / 半边 +2N / 内环 −2、被消费环不再被引用、新面均四顶点且上/下环各 2 点；终态 `V−E+F=2` 且 `V−E+F−内环数=0`。
- 演示程序自检：`EulerOps` 与 `EulerInteractive` 对每一步都调用 `validate()` 并打印计数 —— 每一步均"校验：通过"，四面体终态 `V-E+F=2`；`EulerInteractive --audit` 报 `V=16 E=24 F=10 L=12（内环 2）`，`--selfcheck` 俯视/仰视各统计到内腔壁像素 23932（上下方孔可见）。

## 待细化

- **其余反操作/辅助操作**：`kev` / `kef` / `kvfs` / `semv`（分割边）/ `jemv`（合并边）未实现（`mfkrh`、`mefr` 已作为附加扩展实现，见 D8）。
- **带孔实体**：原计划**不做**（超出作业要求）；现**已实现（附加）** —— `EulerInteractive` 28 步构造"上下贯穿的方孔立方体"（亏格 1），见 D8。
- **多内环与多环 donor**：KFMRH 目前限定 donor 只有一个外环；带内环的 donor 未支持。
- **几何有效性**：自交、非流形、面朝向一致性、共面合并等未检查。
- **网格 I/O**：OBJ/OFF 读写、三角化/四边形化未提供。
- **元素回收**：死亡元素不回收（id 单调增长），大规模编辑场景可引入 free list。
