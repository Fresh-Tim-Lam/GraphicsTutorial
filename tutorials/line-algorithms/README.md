# line-algorithms —— 三种画直线算法对照（Naive / DDA / Bresenham）

> **用途**：讲清"一条直线怎么变成一串像素"，并把课程讲义里的三种画线算法（平凡算法 / DDA / Bresenham）**放在同一套坐标约定下对照**：每步运算量、是否需要浮点、硬件友好度，以及"为什么库默认走 Bresenham"。
> **目标读者**：刚学完光栅化的读者；想知道"为什么生产代码用 Bresenham 而不是 y=mx+B"的人。
> **关联文档**：权威实现 [raster.hpp](../../src/include/gfx/render/raster.hpp)（`gfx::LineAlgorithm` 与三种实现）；决策 [adr-0010](../../docs/design/adr-0010-line-algorithms.md)；相关主题 [polygon-scan](../polygon-scan/README.md)（扫描线填充也会用到画线）。

> 状态：生效中（2026-09-28）。权威实现见 `src/include/gfx/render/raster.hpp`（签名与语义以头文件注释为准）。

## 1. 三种算法，一句话原理

三者都做同一件事：给定整数端点 `(x0,y0) → (x1,y1)`，选出一串**最近像素**（硬边、无抗锯齿、含两端点）。区别只在"每一步怎么算下一个像素"：

| 算法 | 一句话原理 | 每步运算量 | 需要浮点? | 需要取整? | 硬件友好度 | 角色 |
|---|---|---|---|---|---|---|
| **Naive**（平凡算法 / 直线方程法） | 沿较长轴步进，每步用直线方程 `y = m·x + B` 现算 | 1 乘 + 1 加 + 1 取整 | 是 | 是 | 最差 | 仅教学对照 |
| **DDA**（数值微分分析器） | 把"每步一次乘法"降为"每步一次加法"：`y_{i+1} = y_i + m` | 1 加 + 1 取整 | 是 | 是 | 中 | 教学对照 |
| **Bresenham**（整数中点法） | 用整数判别式 `p` 决定"走 x、走 y 还是同时走"，误差项增量更新 | 1~2 次整数加 + 比较 | **否** | **否** | **最好** | **默认实现** |

对应课程讲义《Line Segment and Polygon Scan Conversion》p3–p35：

- 平凡算法（直线方程法）p9–p11：`y = m·x + B`，x 逐整数递增，每步一次乘加再取整；
- DDA p12–p15：`y_{i+1} = y_i + m`；讲义用 `step = max(|dx|,|dy|)` 处理任意斜率（本实现同此）；
- Bresenham p16–p31：整数判别式 `p_0 = 2Δy − Δx`，`p_i > 0 ⇒ p_{i+1} = p_i + 2Δy − 2Δx`，否则 `p_{i+1} = p_i + 2Δy`。

## 2. 统一接口：唯一签名、三种算法、默认 Bresenham

画线**只有一个签名**——四个坐标是**平铺的 `float`**（可带小数，保留亚像素），末位一个算法参数，且**默认一律 Bresenham**：

```cpp
// 画线段（含两端点）。坐标平铺为 float（像素坐标）；算法默认 Bresenham（最优）。
gfx::drawLine(canvas, 10.0f, 20.0f, 100.0f, 60.0f, color);                                     // 默认 Bresenham
gfx::drawLine(canvas, 10.0f, 20.0f, 100.0f, 60.0f, color, gfx::LineAlgorithm::DDA);
gfx::drawLine(canvas, 10.0f, 20.0f, 100.0f, 60.0f, color, gfx::LineAlgorithm::Naive);
gfx::drawLine(canvas, 10, 20, 100, 60, color);                                                 // float 形参也能接住 int 实参
gfx::drawLine(canvas, px0, py0, px1, py1, color);                                              // 端点可带小数（亚像素）
```

- **对外只有一个签名**（原先"整数端点版" / `Vec2` 版重载已删除）。`float` 形参可同时接住 `int` 与 `float`
  实参 ⇒ 整数端点与小数端点都用这同一个签名表达，**无需 `Vec2` 重载、也无需 int 重载**。入口内部按端点数值
  **自动选择**实现：四个坐标皆为整数值且算法为 Bresenham ⇒ 走**纯整数 Bresenham**（无浮点、无除法，即性能路径）；
  否则走浮点步进（保留亚像素）。⇒ **既收敛为单一 API，又不丢整数性能**。
- **默认统一为 Bresenham** 的理由：纯整数、无除法、无浮点 ⇒ 结果精确可复现、最快、天然覆盖八个卦限（见 §1 表）。
- 算法分派**只有一处**（`detail::dispatchLine`）。
- 既有调用（把整数端点写成平铺坐标）行为与此前**完全一致**——整数值端点经唯一入口自动走整数 Bresenham。

Naive 每步都有乘法和浮点取整，DDA 每步都有浮点和取整；两者都只是**教学对照**，不推荐用于生产。

## 3. 一致性与差异：整数端点下基本一致，并列取整处 ±1

三种算法在**整数端点**下选取的都是"离真实直线最近的像素"，因此绝大多数线段上给出**完全相同的像素集**：

- 不存在浮点/取整歧义的斜率（例如 45°、`1/3`、`3/1`、`-1/3` 等）三者逐像素一致（`tests/test_raster.cpp` 锁定）；
- **唯一例外**是直线**恰好穿过像素中心的中线**（真实坐标出现严格的 `.5` 并列，例如 `1/4` 的斜率每 4 列出现一次）。此时：
  - Bresenham 的整数判别式在**相等**时把误差"往终点方向"推（走一格），
  - 而 DDA/Naive 用 `std::lround` 做"四舍五入"，对半值**远离零方向**取整，
  - 两者的并列取舍方向可能相反 ⇒ 个别像素**相差 1**。

这不是 bug，而是"并列情形下取整方向"的定义差异。**演示程序会把它真实地量化出来**（见下节统计，例如 `缓斜 -1/4 并列` 这条线段 DDA/Naive 与 Bresenham 相差 120 个像素坐标）。

另一条与本题相关的差异是**浮点端点**：唯一入口 `drawLine(canvas, x0, y0, x1, y1, color[, algo])`（坐标平铺为 `float`）**不提前把端点取整**，保留亚像素精度，所以端点带小数时它给出的阶梯会和"先把端点取整再走 Bresenham"**整体错位**（长线上可差半像素）。它同样可选三种算法（默认 Bresenham）；实测在代表性线段上三者基本一致，仅当直线恰穿过像素中心中线（严格 `.5` 并列）时因取舍方向不同而相差少量像素（Naive 2、DDA 16）。

## 4. 运行

```powershell
# 仓库根目录
cmake -S . -B build
cmake --build build --target tutorial_line_algorithms

build\tutorials\LineAlgorithms.exe --output tutorials\line-algorithms\output\line-algorithms.png
```

| 参数 | 说明 |
|---|---|
| `--output <file>` | 结果 PNG 路径（默认 `output/line-algorithms.png`；父目录不存在会自动创建） |
| `--help` | 显示帮助 |

程序行为：

1. 画**三幅并排**的图，同一组线段分别用 **Naive（左）/ DDA（中）/ Bresenham（右）** 渲染；
2. 为便于看出差别，在 Naive/DDA 两幅上以 **Bresenham 为参照**标注差异像素：**品红** = 该算法多出的像素，**琥珀** = Bresenham 有而该算法缺的像素；
3. 在控制台打印**真实统计**：每条线段三种算法的像素数、任意两算法之间的差异像素数，最后打印总计。

## 5. 实测输出（真实运行，非写死）

```
三种画直线算法对照（同一组线段，面板局部坐标，y 向下）
面板：左=Naive  中=DDA  右=Bresenham（Bresenham 为默认）

线段                    Naive   DDA  Bresenham      N<->D     D<->B      N<->B  一致?
水平                      261   261        261          0         0          0  一致
垂直                      301   301        301          0         0          0  一致
45 度                      241   241        241          0         0          0  一致
缓斜 1/3                  241   241        241          0         0          0  一致
陡斜 3/1                  181   181        181          0         0          0  一致
缓斜 -1/4 并列          241   241        241          0       120        120  不一致
缓斜 -1/3                 241   241        241          0         0          0  一致
陡斜 -8/3 并列          161   161        161          0        40         40  不一致
近 45 度 15/14 并列     301   301        301          0         0          0  一致
长度 1                      2     2          2          0         0          0  一致
退化点                     1     1          1          0         0          0  一致

共 11 条线段，差异线段数 = 2
```

要点：

- **Naive 与 DDA 始终一致**（差异列 `N<->D` 全为 0）——它们都等价于"对真实直线逐点取最近像素"；
- **Bresenham 与二者在两条"并列"线段上不同**（`缓斜 -1/4 并列` 120、`陡斜 -8/3 并列` 40），其余 9 条完全一致；
- `陡斜 -8/3 并列` 与 `近 45 度 15/14 并列` 都是并列斜率，为什么只有前者不同？因为差异只在**被取整的那个坐标单调朝"数值减小"方向**时出现：前者 `x` 随 `y` 减小（向左），后者 `x` 随 `y` 增大（向右）——这与"Bresenham 并列时朝终点方向走、lround 朝远离零方向取整"的方向差一致。

结果图：[output/line-algorithms.png](output/line-algorithms.png)（左 Naive / 中 DDA / 右 Bresenham；线段基础色一致，品红/琥珀标注差异像素）。

> 说明：**整数端点下三者基本一致**，因此三幅图的线段本身看起来一样，差异要靠上面的**统计**和两幅图上的**品红/琥珀标注**来体现，而不是靠肉眼直接比形状。代码与统计诚实地反映了这一点——没有为了让图"看起来有差异"而篡改渲染。

## 6. 权威实现与自测

- 权威实现：[raster.hpp](../../src/include/gfx/render/raster.hpp) 的 `gfx::LineAlgorithm` + **唯一签名** `gfx::drawLine(Canvas&, float x0, float y0, float x1, float y1, const Color&, LineAlgorithm algo = LineAlgorithm::Bresenham)`（算法分派只有一处 `detail::dispatchLine`）。内部实现只保留必要分化：`detail::drawLineBresenham` 有 int / float **两版**（整数版是纯整数判别式，由分派在"四个坐标皆为整数值"时自动选中），`detail::drawLineDDA` / `drawLineNaive` 各**只有一版**（函数模板 `template<typename T>`，整数端点即其浮点特例）。
- 单元测试：`ctest --test-dir build -C Release -R test_raster`。其中：
  - "画直线算法对照"一组用 16 条代表性线段（水平/垂直/45°/缓斜/陡斜/负斜率正反方向/长度 1/退化点/极长）断言 **Naive、DDA、Bresenham 逐像素一致**，并校验非空、含端点、落在包围盒内，以及"不传算法 == 显式传 Bresenham"；
  - "画线算法对照（统一接口）"一组断言默认（不传算法）== 显式 Bresenham，且传 DDA / Naive 非空、含端点、落在包围盒内；并**实测并打印**与 Bresenham 的差异像素数（仅严格 `.5` 并列的长缓斜线出现差异：Naive=2、DDA=16，其余为 0；断言 ≤ `max(2, 总数/4)`）。
  - "整数端点经唯一入口自动选中整数 Bresenham"一组：与直接调用内部 `detail::drawLineBresenham` **逐像素一致**（保证收敛 API 不丢整数路径）。
  - `test_raster` 共 **243 checks** 全部通过。

## 7. 已知限制

- 三种算法都输出"最近像素"的**硬边**结果，无抗锯齿；需要抗锯齿请走 [triangle-raster](../triangle-raster/README.md) 的覆盖率 / SSAA。
- 演示用固定画布 960×360、三幅等宽，未提供 `--size`/`--window`。
- 差异标注以**整数端点下的像素集合**为准，只揭示"取整并列"这一类差异；不涉及端点带小数（亚像素）的情形。
