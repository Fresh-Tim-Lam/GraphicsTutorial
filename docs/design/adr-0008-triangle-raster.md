# ADR-0008 — 三角形光栅化、抗锯齿与深度缓冲（第五讲知识点落地）

> **用途**：记录"三角形光栅化（采样判定 + 边界规则 + 重心坐标）、抗锯齿（覆盖率 / 超采样）、深度缓冲"的落位与决策，以及它与既有扫描线填充的关系。
> **目标读者**：使用/扩展光栅化与 3D 面片绘制的人与 AI Agent。
> **关联文档**：[adr-0005](adr-0005-raster-polygon-fill.md)（多边形扫描线填充，另一条路线）；[adr-0006](adr-0006-camera-view.md)（View，产出 NDC 深度）；实现 [triangle.hpp](../../src/include/gfx/render/triangle.hpp)、[depth.hpp](../../src/include/gfx/render/depth.hpp)、[color.hpp](../../src/include/gfx/core/color.hpp)、[canvas.hpp](../../src/include/gfx/core/canvas.hpp)；教学主题 [triangle-raster](../../tutorials/triangle-raster/README.md)。

> **状态**：✅ 采纳（2026-09-16）；填充接口于 2026-10-06 收敛为**单一入口**（`TriangleFillMode` 四模式 + 哨兵参数）。落地：`edgeFunction` / `insideTriangle`（top-left 规则）/ `barycentric` / `fillTriangle`（**唯一填充入口**，按 `TriangleFillMode` 分派，`samplesPerAxis = 0` / `background = nullptr` 表示**未指定**）/ `TriangleFiller`（增量式专属，可 `step`）/ `coverage` / `triangleFillWarningsEnabled` / `DepthBuffer` / `fillTriangleDepth` / `rgba` / `blend` / `Canvas::blendPixel`。（原独立公开的 `fillTriangleAA` / `fillTriangleSSAA` 已并入入口，降为 `detail::fillTriangleCoverageAA` / `detail::fillTriangleSSAA`。）

## 背景

- 触发材料：课程《第五讲：光栅化、采样与抗锯齿》（120 页）。与作业 1 的"多边形扫描转换"**同属光栅化但路线不同**：
  - 作业 1（已实现，ADR-0005）：**任意多边形** + 边表 ET/活性边表 AET，逐扫描线求交成对填充；
  - 本讲：**三角形** + 逐像素采样（`inside(tri,x,y)`）+ 包围盒/增量遍历 + 重心插值，并进入抗锯齿与可见性。
- 已有可复用的部分：像素中心采样约定、`View` 的 NDC→像素（视口变换）、帧缓冲、`View::projectToNdc`（深度输出）、`fovY`/aspect ↔ `l/r/b/t` 换算。
- 缺口：点在三角形内测试、top-left 边界规则、重心坐标插值、包围盒/增量遍历、抗锯齿（覆盖率/超采样）、alpha 混合、深度缓冲。

## 决策

### D1：落位 `triangle.hpp` + `depth.hpp`，与 `raster.hpp` 并列

- `triangle.hpp`：三角形光栅化 + 抗锯齿；`depth.hpp`：深度缓冲 + 带深度测试的填充。
- 理由：与 `raster.hpp`（多边形扫描线）是**并列的两条路线**，各自成篇更清晰；`depth.hpp` 独立是因为它会被后续 3D 管线继续扩展。
- 复用而非重复：`triangle.hpp` 复用 `raster.hpp` 的 `Span`；`depth.hpp` 复用 `triangle.hpp` 的 `insideTriangle` / `barycentric`。

### D2：边界规则用 **top-left**，与 `raster.hpp` 的半开区间**并存**

- 采样点恰好落在边上时，只有 top 边/left 边接受它 ⇒ 相邻三角形在共享边上**恰好一方接受**（不重不漏）。
- 为什么允许两套规则并存：它们服务不同算法——`raster` 的扫描线天然适合"y 半开 + x 左闭右开"，三角形的逐像素判定天然适合 top-left。**两者的目标相同（唯一归属），但对同一三角形的边界像素归属可以不同**，因此互校测试必须避开"采样点恰在边上"的坐标。
- 判定标准写死在代码里（屏幕坐标 y 向下）：`dy > 0`（向下走）算 left；`dy == 0 && dx < 0`（水平向左）算 top。

### D3：填充的**总接口**——唯一入口 `fillTriangle` + 模式枚举（四模式）

对外**只保留一个填充签名**（效仿 `raster.hpp` 的 `drawLine`：一个入口 + 枚举 + 默认最优）。四种模式是"同一件事（把三角形填到画布）的不同做法"：前两者只影响"哪些像素被覆盖"，后两者在此基础上做抗锯齿（更高代价换更平滑的边界）：

```cpp
enum class TriangleFillMode {
    SolidScanline,   // 增量式逐扫描线 + 点采样 —— 默认，最快
    SolidBBox,       // 包围盒 + 逐像素 inside test + 点采样 —— 最直白，用于对照
    CoverageAA,      // 覆盖率抗锯齿：按像素内覆盖面积 alpha 叠色
    SSAA,            // 超采样：渲染 factor 倍大图后下采样平均
};

bool fillTriangle(Canvas&, const Vec2& a, const Vec2& b, const Vec2& c, const Color&,
                  TriangleFillMode mode = TriangleFillMode::SolidScanline,
                  int samplesPerAxis = 0,
                  const Color* background = nullptr);
```

- 各模式的定位：
  - **`SolidScanline`（默认）**：三角形是凸的 ⇒ 每行被覆盖的像素必是**一个连续区间**，只需算出该行左右边界，不必逐像素测试 ⇒ 更快；边界口径（y 半开 + x 左闭右开）与 `fillPolygon` 一致。
  - **`SolidBBox`**：对包围盒内每个像素做 `insideTriangle`（含 top-left 边界规则）⇒ 最直白、便于教学对照；本质是逐像素测试，**不提供** `step` 版本。
  - **`CoverageAA`**：按像素被覆盖的**面积比例**（`coverage`）叠色（`Canvas::blendPixel`），像素可取中间值。
  - **`SSAA`**：渲染到 `samplesPerAxis` 倍分辨率的离屏画布再逐块平均（讲义 p110-119 的两步流程）；`background` 为离屏背景色（平均时计入）。
- 分派**只有一处**（即 `fillTriangle` 内的 switch）；实现全部在 `detail`：`detail::fillTriangleBBox`、`detail::fillTriangleScanline`（内部即 `TriangleFiller`）、`detail::fillTriangleCoverageAA`、`detail::fillTriangleSSAA`。
- **为什么保留 `TriangleFiller` 类**：它提供"可 `begin`/`step`/`fillCurrentRow` 的逐扫描线推进"（供动画/调试演示），是**增量式专属**的能力；包围盒法逐像素测试无法逐行给出区间，故不提供 step 版本。需要逐行推进就直接用本类。
- 旧的两个并列签名（`fillTriangle`＝包围盒、`fillTriangleScanline`＝增量式）**合并**：删除 `fillTriangleScanline`，其语义 = 新入口的默认参数。此后 `fillTriangleAA` / `fillTriangleSSAA` 也并入入口（`CoverageAA` / `SSAA` 模式），不再对外。

#### 哨兵值约定（0 / nullptr = 未指定）

C++ 默认参数**无法区分**"调用者没传"和"传了默认值"，因此用**非法值**当哨兵表示"未指定"：

- `samplesPerAxis = 0` ⇒ 未指定：按模式默认值 **4**（对 `CoverageAA` / `SSAA` 生效；即每轴 4 个子样本）。
- `background = nullptr` ⇒ 未指定：按不透明黑 `(0,0,0,255)`（仅 `SSAA` 使用）。

有了哨兵，就能判断"参数是否被**显式指定**"——这正是下面"无关参数告警"的前提。

#### 无关参数：警告并忽略（可关闭）

对当前模式**无意义**的参数**不中断、不影响返回值**，只打印**一行**中文警告并忽略该参数：

| mode | `samplesPerAxis > 0` | `background != nullptr` |
|---|---|---|
| `SolidScanline` / `SolidBBox` | ⚠️ 警告并忽略 | ⚠️ 警告并忽略 |
| `CoverageAA` | ✅ 使用 | ⚠️ 警告并忽略 |
| `SSAA` | ✅ 使用（= 超采样倍数 factor） | ✅ 使用 |

- **只在参数被显式指定时才警告**（靠哨兵判断）：`fillTriangle(c,a,b,c,col, SolidScanline)` 不传后两个参数 ⇒ **无任何警告**；一次调用对同一参数只打印一行。
- 警告文案形如：`[gfx] fillTriangle: 参数 samplesPerAxis 对填充方式 SolidScanline 无意义，已忽略`（参数名与模式名都在，便于定位）。
- **开关**：`gfx::triangleFillWarningsEnabled()`（默认 `true`，可读写）；仅当为 `true` 时才打印，批量渲染/测试时可关掉以免刷屏。

#### 实测：两法的差异像素数

两法只可能在"**采样点恰好落在边上**"处不同（top-left 规则 vs 半开区间规则，见 D2）。用同一编译器实测（逐像素比对，`==` 全通道相等）：

| 测试三角形（画布） | `SolidBBox` 覆盖像素 | `SolidScanline` 覆盖像素 | 差异像素 |
|---|---|---|---|
| 整数顶点·直角 `(2,2)-(22,2)-(2,22)`（28×28） | 210 | 190 | **20** |
| 45° 斜边 `(2,2)-(22,22)-(2,22)`（28×28） | 210 | 190 | **20** |
| 带小数顶点 `(2.3,2.7)-(20.1,5.4)-(6.2,21.9)`（28×28） | 166 | 166 | **0** |
| 共享边 T1 `(0,0)-(4,0)-(0,4)`（8×8） | 10 | 6 | **4** |
| 共享边 T2 `(4,0)-(4,4)-(0,4)`（8×8） | 6 | 10 | **4** |
| 上两三角形拼成正方形 `[0,4]²`（合并渲染） | 16 | 16 | **0** |
| 薄三角形 `(2,2)-(22,2.3)-(12,2.15)`（28×28） | 0 | 0 | **0** |
| 教程 panel0 `(24,24)-(276,276)-(24,276)`（300×300） | 31878 | 31626 | **252** |

- **顶点带小数（采样点不落边）时两法逐像素完全一致**（差异 0）——这也是单元测试互校时特意避开整数/半整数顶点的原因。
- 整数顶点、45° 斜边、共享边等**存在"采样点恰在边上"**的情形才会有差异；差异点全部落在边上：`SolidBBox` 按 top-left 把边界采样点归给本三角形，`SolidScanline` 按半开区间可能归给"邻接的另一侧"（或干脆不计）。
- **拼成正方形时合并结果仍逐像素一致**（16 = 16）：两法都把 16 个采样点覆盖满，只是共享边上的像素归给了不同的三角形 —— 这正是 D2"两套规则都保证不重不漏、但边界归属可不同"的体现。
- 注意：上表的两法差异**只属于点采样覆盖判定**（`SolidScanline` vs `SolidBBox`）。`CoverageAA` 与 `SSAA` 各自用 `coverage` / 超采样求面积，与这两套边界规则没有这条"一对一边界像素"的对应关系，故不列在同一张表里。

### D4：重心坐标作为"属性插值"的公共基础

- `barycentric(p, a, b, c, out)` 给出 `(wa, wb, wc)`，和为 1；顶点处为 `(1,0,0)` 等。
- 用途：深度插值（深度测试用）、颜色/法线插值（后续着色用）。也用于回答"点相对三角形在哪里"。

### D5：抗锯齿的前提是 **alpha 混合**，先补基础再谈方法

- `Color` 补 `rgba()`；新增 `blend(src, dst)`（source-over，整数运算，`a == 255` / `a == 0` 走精确分支）；`Canvas` 补 `blendPixel`（与覆盖式 `putPixel` 明确区分）。
- 不引入浮点颜色缓冲：8bit 整数已足够本阶段，且避免改变既有 `Canvas` 语义。

### D6：抗锯齿提供 **覆盖率着色** 与 **超采样 SSAA** 两条路线，不做 MSAA/FXAA/TAA

- `coverage(pixel, tri, N)`：N×N 子采样估计覆盖率（"预滤波"的离散近似，讲义 p107-109）。
- `TriangleFillMode::CoverageAA`（实现 `detail::fillTriangleCoverageAA`）：按覆盖率**叠色**（像素可取中间值）；`samplesPerAxis` 即 N。
- `TriangleFillMode::SSAA`（实现 `detail::fillTriangleSSAA`）：渲染到 N 倍分辨率离屏画布再逐块平均（讲义 p110-119 的两步流程）；`samplesPerAxis` 即倍数、`background` 为离屏底色。
- 两者都经**唯一入口** `fillTriangle` 调用，不再有独立公开函数。
- 边界：只更新三角形**包围盒内**的目标像素（不遍历整张画布——否则会抹掉画布其它内容；这是实现时被示例程序暴露并修掉的缺陷）。
- **不做**：MSAA（子像素模板 + 解析覆盖率）、FXAA/TAA/DLSS（讲义 p120 的当代方法，超出本库范围）。

#### 覆盖率 AA 的实现优化（快速判定；输出逐像素不变）

`detail::fillTriangleCoverageAA` 原实现对包围盒内**每个**像素都调用 `coverage()`（每像素 N² 次 `insideTriangle`）。但包围盒内绝大多数像素要么**完全在三角形内**（覆盖率恒为 1）、要么**完全在三角形外**（覆盖率恒为 0），只有"可能跨界"的像素才值得跑满 N² 次子采样。优化后对每个像素先做两次**快速判定**，只有判定落空才调用 `coverage()`：

- **完全在内 ⇒ 覆盖率 1**：三条**有向边函数**（三角形内部为正）在像素**闭包** `[x,x+1]×[y,y+1]` 上的**最小值**均 > 0 ⇒ 闭包严格落在三角形内部；三角形是**凸集**，而覆盖用的子样本全都落在闭包内 ⇒ 全部在内 ⇒ 覆盖率 = 1 ⇒ 直接按 `alpha = 255` 叠色（`blendPixel(...,255)`，与原实现逐字一致）。
- **完全在外 ⇒ 覆盖率 0（跳过）**：存在一条有向边函数在闭包上的**最大值** < 0 ⇒ 整个闭包在该边外侧 ⇒ 所有子样本都在外 ⇒ 覆盖率 = 0 ⇒ 直接跳过，**不调用** `coverage()`。

**安全性依据**：边函数是**仿射函数**，其在轴对齐闭包上的 min/max 必取在**四个角点**上，且恰为"像素中心值 ∓ 半跨度"（半跨度 = `0.5·(|∂f/∂x| + |∂f/∂y|)`）。因此两个判定都基于**整个闭包**（而非中心点）给出严格充分的结论，不会漏判。由于覆盖判定本身也用 `float` 的 `edgeFunction`（含与坐标平方同阶的舍入），比较时额外留出裕度 `eps = 1e-4 + 1e-5·maxCoord²`（`maxCoord` 为三个顶点坐标绝对值的最大值）；裕度只会让"擦边"像素**回退**到 `coverage()`，**不影响正确性**，仅极微降低速度。

**为什么不用 `TriangleFiller` 的扫描线区间 + 左右各扩 1**：该区间是按"像素中心所在扫描线"求交得到的，对**近水平/超扁**的三角形会严重偏离真实覆盖范围（例如底边近水平的扁三角形，某一行的真实覆盖可达几十像素宽，而中心扫描线求出的区间却窄或落在别处），左右各扩 1 **不足以构成覆盖像素集的超集**，会漏掉真实被覆盖的像素。故改用上述可证明安全的闭包判据。`TriangleFiller` 的对外行为未作任何改动。

**实测**（同一编译器 MinGW g++ · Release/`-O2`；"旧"= 优化前逐字实现，"新"= 优化后；耗时取多轮最小值）：

| 用例（画布） | N | 旧 coverage 调用 | 新 coverage 调用 | 降幅 | 旧 ms/次 | 新 ms/次 | 加速比 |
|---|---|---|---|---|---|---|---|
| 教程 panel0 `(24,24)-(276,276)-(24,276)`（300×300） | 2 | 63504 | 1253 | −98.0% | 0.83 | 0.21 | ≈4.1× |
| 同上 | 4 | 63504 | 1253 | −98.0% | 4.7 | 0.46 | ≈10× |
| 同上 | 8 | 63504 | 1253 | −98.0% | 18.2 | 0.72 | ≈25× |
| 小数顶点 `(2.3,2.7)-(20.1,5.4)-(6.2,21.9)`（28×28） | 4 | 380 | 72 | −81.1% | 0.031 | 0.009 | ≈3.4× |
| 45° 斜边 `(2,2)-(22,22)-(2,22)`（28×28） | 4 | 400 | 93 | −76.8% | 0.031 | 0.010 | ≈3.1× |
| 大而扁 `(0,0)-(300,40)-(150,41)`（320×64） | 4 | 12300 | 693 | −94.4% | 0.94 | 0.12 | ≈7.6× |

- 覆盖率调用次数只与**三角形边界像素数**（周长量级）有关、与 N 无关；而旧成本 ∝ N²、新成本近似与 N 无关（外加边界像素的 N²）⇒ **N 越大加速越明显**（panel0 从 N=2 的约 4× 上升到 N=8 的约 25×）。
- 小画布用例加速比较低，是因为画布小、每像素分类与循环的**固定开销**占比高；大三角形（panel0）才接近纯算法加速。
- 逐像素等价（含 alpha 合成）：66 组定向用例（45° 斜边、小数顶点、共享边、极薄、退化、贴边界、越界等）+ 4000 组随机 fuzz + 300 组大坐标（坐标绝对值 ~1024）fuzz，**差异像素数全为 0**；其中还覆盖了"`color` 自带 alpha"（原实现按覆盖率产生的 `alpha` 叠色，忽略 `color.a`，实现与优化后仍一致）。

### D7：深度缓冲 = 逐像素 test-and-set，语义取 `GL_LESS`

- `DepthBuffer` 与画布同尺寸，初始为远平面 `kFar = 1.0f`（NDC 深度越大越远）。
- `testAndSet` 仅在**严格更近**时写入：同深度时先画者保留（避免 z-fighting 反复覆盖导致闪烁）。
- 关键性质：**与绘制顺序无关**（测试锁定；示例程序故意乱序绘制三个三角形来演示）。

## 后果

- 新增公共 API：`gfx::edgeFunction`、`insideTriangle`、`barycentric`、`fillTriangle`（**唯一填充入口**，参数 `TriangleFillMode`，默认 `SolidScanline`，可选 `samplesPerAxis = 0` / `background = nullptr`）、`TriangleFiller`、`coverage`、`triangleFillWarningsEnabled`；`gfx::DepthBuffer`、`fillTriangleDepth`；`gfx::rgba`、`gfx::blend`、`Canvas::blendPixel`。抗锯齿的两种实现不再对外，降为 `detail::fillTriangleCoverageAA` / `detail::fillTriangleSSAA`。
- 既有 API 未改（纯新增）：`raster.hpp` 的多边形扫描线与 `Color`/`Canvas` 的原有语义保持不变。
- 填充接口的统一：删除并列的 `fillTriangleScanline`（其语义 = 新入口的默认参数），`fillTriangle` 由"包围盒"改为"按模式枚举分派、默认增量式"；随后把 `fillTriangleAA` / `fillTriangleSSAA` 也并入同一入口（`CoverageAA` / `SSAA` 模式），**全仓只有一处分派**（`fillTriangle` 内的 switch）。这是对 D3 早期"两条路线各一个并列签名"的进一步收敛。
- 既有调用点的影响（见下"默认路径行为变化"）：整幅渲染里只有"采样点恰落在边上"的像素会变。
- 新增教学主题 [triangle-raster](../../tutorials/triangle-raster/README.md) 与演示目标 `tutorial_triangle`（产物 `TriangleRaster`），演示"三种采样方式对比 + 乱序深度测试"。
- 为后续 3D 管线（三角形面片渲染）备好了：投影（`View`）→ 光栅化（`triangle.hpp`）→ 可见性（`depth.hpp`）三段。

### 默认路径行为变化（对既有调用点）

新默认 = `SolidScanline`（旧 `fillTriangle` 语义为包围盒），故旧的 `fillTriangle` 调用点需逐一核对：

- `tutorials/triangle-raster/code/triangle_demo.cpp`（点采样 panel）：三角形为整数顶点 45° 直角 `(24,24)-(276,276)-(24,276)`，斜边恰为 `y=x`，**252 个采样点落在斜边上**。改后这 252 个边界像素不再填入（31878 → 31626）。自检的"中间色像素数"仍为 **0**（点采样口径不变），画面仅在斜边处少一圈像素、肉眼几乎无差别；与 panel 2 的 SSAA（内部同样走默认）保持一致。**建议：接受新默认**（与 `fillPolygon` 口径统一；自检数值不变）。如需保留旧外观，可在该处显式传 `TriangleFillMode::SolidBBox`。
- `triangle.hpp` 内部 `detail::fillTriangleSSAA`（离屏大图渲染与 `factor<=1` 回退）：4× 离屏画布上同样相差边界像素（实测 1200×1200 差 **1008** 像素），经下采样平均后 SSAA 面板边界像素颜色有极微变化；自检的中间色像素数仍为 **252**。**建议：接受**（SSAA 本就是"多采样再平均"，边界子样本的归属差异属预期）。
- `tests/test_triangle.cpp`：互校测试已将 `viaBox` 显式固定为 `SolidBBox`、`viaScan` 显式固定为 `SolidScanline`（保持"两条路线互校"的原意）；点采样断言与 `fillPolygon` 互校断言均不受影响（见"验证"）。
- `fillTriangleAA` / `fillTriangleSSAA` 现已并入总入口（`CoverageAA` / `SSAA` 模式）：逐像素比对（教程 panel、45° 斜边、带小数顶点、共享边等，见"验证"）显示，新入口在 `CoverageAA` / `SSAA` 下的输出与旧独立函数**逐像素完全一致（差异 0）**。
- 其余调用点（`fillTriangleDepth` / `barycentric` / `coverage`）不经过总填充入口，输出**完全不变**。`depth.hpp` 的 `fillTriangleDepth` 直接使用 `insideTriangle` + `barycentric`，故其对外签名与行为均未改动。

## 验证（可复现）

- 单元测试 `ctest -R test_triangle`（**80 checks**）：
  - 边界规则：正方形被对角线切成的两个三角形，16 个采样点**各覆盖一次**（不重复 0 / 不遗漏 0）；
  - 三条路线互校（坐标避开边界歧义）：`fillTriangle(SolidBBox)` == `fillTriangle(SolidScanline)` == `fillPolygon`（逐像素）；并锁定"不传 `mode` 时默认走 `SolidScanline`"；
  - 重心坐标：顶点权重、重心权重、权重和为 1、退化失败；
  - 覆盖率：内部 1、外部 0、45° 边界恰 0.5；共享边上两三角形覆盖率之和为 1；
  - 抗锯齿：点采样中间色为 0、覆盖率着色 > 0；SSAA 更接近覆盖率着色而非点采样；SSAA 不越界影响包围盒外像素；
  - **覆盖率 AA 快速判定不改变输出**：`fillTriangle(..., CoverageAA, N)`（N = 2/4/8）与"对包围盒内每像素都调用 `coverage()`"的朴素参考实现，在 45° 斜边、小数顶点、共享边、极薄等用例上**逐像素完全一致**（作为优化回归的锁定断言）；
  - **无关参数告警**：只传 5 参（`SolidScanline`）无告警；`SolidScanline + samplesPerAxis=4` 恰一行关于 `samplesPerAxis` 的告警；`CoverageAA + background` 恰一行关于 `background` 的告警；关闭 `triangleFillWarningsEnabled()` 后无告警（测试内重定向 fd 2 捕获 stderr 校验）；
  - alpha：不透明/全透明两端精确；`blendPixel` 叠色 vs `putPixel` 覆盖；
  - 深度：`testAndSet` 语义、遮挡正确、两种绘制顺序结果**逐像素相同**。
- 行为零变化（`ctest` 之外，临时程序逐像素比对）：`fillTriangle(..., CoverageAA, N)` 与旧 `fillTriangleAA(..., N)` 在教程 panel、45° 斜边、带小数顶点、共享边等用例上**差异 0**；`fillTriangle(..., SSAA, factor, &bg)` 与旧 `fillTriangleSSAA(..., factor, bg)` 同样**差异 0**。覆盖率 AA 的快速判定优化另经 66 组定向用例 + 4000 组随机 fuzz + 300 组大坐标 fuzz 逐像素比对，**差异 0**（见 D6 小节"覆盖率 AA 的实现优化"）。
- 示例程序自检（`ctest` 之外）：`TriangleRaster` 打印三个 panel 的中间色像素数——点采样 0、覆盖率 AA 252、SSAA 252（45° 斜边下两者一致，符合预期）。

## 待细化

- **MSAA / 解析覆盖率**：覆盖率目前用子采样估计；解析面积（边与像素方块裁剪）更快更准，需要时再补。
- **透视校正插值**：当前深度/属性用屏幕空间线性插值；3D 面片大面积渲染时需按 `1/w` 校正（属 3D 管线议题）。
- **凹多边形三角化**：当前三角形填充只处理凸（三角形本身），凹多边形仍走扫描线填充。
- **背面剔除与光照**：`barycentric` 已备好插值基础，剔除/着色未实现。
- **深度缓冲的早测优化**：逐像素先判内再测深度；GPU 式"先深度再着色"的优化未做。
