# ADR-0016 — 程序化图案（颜色 + 高度 ⇒ 颜色贴图与法线贴图成对生成）

> **用途**：记录"给渲染路径提供**无需外部图片**的材质外观"落位 `gfx` 的决策：为什么在 `normalmap.hpp` 之上另开 `pattern.hpp`、噪声基元怎么选（含**可无缝平铺**的 `period`）、一个图案如何同时给出颜色与高度、四个内置图案的口径、以及颜色如何落到画面上（库层与演示层的分工）。
> **目标读者**：实现/使用程序化贴图的人与 AI Agent。
> **关联文档**：[adr-0013](adr-0013-normal-mapping.md)（纹理与法线贴图）、[adr-0014](adr-0014-shaded-raster-pipeline.md)（光栅化着色管线）、[adr-0015](adr-0015-shadow-map.md)（阴影贴图）；实现 [pattern.hpp](../../src/include/gfx/render/pattern.hpp)；测试 [test_pattern.cpp](../../tests/test_pattern.cpp)；教学主题 [shaded-raster](../../tutorials/shaded-raster/README.md)。

> **状态**：✅ 采纳（2026-10-10）。落地：`hash2` / `valueNoise` / `fbm` / `turbulence`、`PatternSample`、`marblePattern` / `tilePattern` / `brickPattern` / `weavePattern`、`makeColorTexture` / `PatternMaps` / `makePatternMaps`。

## 背景

- 触发需求："球与地面的纹理不好看 / 地面看不出纹理，想要更酷的材质"。
- 项目现状：`ADR-0013` 的 `makeNormalMap` 只做"高度场 → **法线**贴图"，`Texture` 本身能存任意 RGB，但**没有**"生成颜色贴图"的入口。于是所有表面都只有**法线扰动**、颜色全靠 `Material::diffuse` 常量 ⇒ 平直或远处的面（如地面）看起来就是一片纯色，"看不清纹理"是必然结果。
- 约束：`gfx` 只有 PNG **编码器**（自写 stored-deflate），**没有解码器**（无 inflate、无 BMP/TGA 读取）⇒ 想用网上的照片纹理，得先自己写一个图片加载器，并把二进制资源带进仓库。教学库的取向是"零第三方依赖 + 结果可复现"，故选择**程序化生成**。

## 决策

### D1：新建 `render/pattern.hpp`，`normalmap.hpp` 保持"高度 → 法线"不变

- `pattern.hpp` 只多两件事：**颜色贴图的生成**（`makeColorTexture`）与**若干图案**（同时给出颜色与高度）。
- 法线仍由既有的 `makeNormalMap` 产出（`makePatternMaps` 内部调用它）—— 中心差分求梯度的口径只有一处实现，不会分叉。

### D2：噪声基元用**整数哈希**的值噪声，并支持 `period`（可无缝平铺）

- `hash2(x, y, seed)`：整数位运算混合（xorshift 风格），**不用**"`sin(dot(p, k))*43758`"那类浮点混沌 —— 后者在大坐标下会退化，且不同 `sin` 实现的结果不保证一致。整数哈希逐位可复现。
- `valueNoise`：格点随机值 + **五次淡入曲线**（`6t⁵−15t⁴+10t³`，一阶/二阶导在格点处均为 0）+ 双线性插值。比三次的 `3t²−2t³` 更平滑（Perlin 的改进版淡入曲线）。
- `fbm`：`Σ gainⁱ · noise(lacunarityⁱ · x)`（默认 2 / 0.5）；`turbulence`：`Σ gainⁱ · |2·noise−1|`（取绝对值 ⇒ 强调"脊线"，是脉络/褶皱的关键）。
- **`period` 参数**：> 0 时格点索引按 `period·lacunarityⁱ` 环绕 ⇒ 噪声以 `period` 为周期。**这是无缝平铺的前提**：`makeNormalMap` 的中心差分在 uv 边界处是环绕取样的，高度场若不是 uv 的周期函数，贴图接缝上就会留下一条法线突变（一条明显的亮/暗线）。因此所有图案都设计成对 `u`、`v` 以 1 为周期（格数取整数、噪声取 `period = cells`）。
  - 代价：`period` 只在 **lacunarity 为整数**时才逐倍成立；`offsetRows` 的砖墙还要求 `rows` 为偶数（否则奇偶交错在 v 方向平移 1 后会错半格）。两者都写进了文档注释与测试。

### D3：一个图案同时给出 `color` 与 `height`

- `PatternSample { math::Vec3 color; float height; }` —— 颜色与起伏来自**同一次**图案求值，天然对齐。
- `makePatternMaps(size, pattern, normalStrength)` 产出 `PatternMaps { Texture albedo; Texture normal; }`：颜色图逐 texel 取 `pattern(u,v).color`，法线图把 `pattern(u,v).height` 交给 `makeNormalMap`。
- 代价：图案函数被调用两遍（法线图内部还要中心差分 ⇒ 共 5 次）。**这是有意的取舍**：生成期多花几十毫秒，换来"只有一处差分实现、两种图必然对齐"。图案函数必须是纯函数（无副作用）。

### D4：四个内置图案的口径

| 图案 | 做法 | 关键点 |
|------|------|--------|
| `marblePattern` | `s = sin(2π·(u·stripes + turbulence·k))`，再取高次幂压出"宽底 + 细亮脉" | 脉络来自**湍流扭曲的正弦条带**；再叠一点 fBm 斑驳，避免纯渐变的塑料感 |
| `tilePattern` | 方格 + 有宽度的缝隙（`edge` 由"到缝中心距离"线性映射） | 面子按**环绕后的格子索引**做哈希深浅差异（否则平移一个周期后颜色会变 ⇒ 不能平铺）；`offsetRows` 即砖墙 |
| `brickPattern` | `tilePattern(offsetRows = true)` + 中性偏暖的砖面 | 与瓷砖共用同一套格子逻辑，只换参数与配色 |
| `weavePattern` | 两条丝带互相压过：`ribbonU = sin(π·fu)`、`ribbonV = sin(π·fv)`，按 3/1 斜纹（或 2/2 平纹）决定"谁在上" | 高度在格边恒为 0 ⇒ 切换"谁在上"时不会跳变（高度场连续）；上层的丝带亮、下层的暗 —— 碳纤维一眼可辨的特征 |

- 配色刻意**偏中性**（面子亮、缝隙暗、脉络亮）：颜色最终会被材质 `diffuse` 再染一次（见 D5），图案只负责"结构"。

### D5：颜色贴图在**演示层**以"乘到 diffuse 上"落地，库层不认识材质

- 用法固定为 `material.diffuse ⊙ albedo`（Hadamard 积）：**图案给结构、材质给色调** —— 同一张砖纹换个 tint 就是另一种颜色的墙（演示里四面墙正是如此）。
- `pattern.hpp` 只产出 `Texture`，不 include `lighting.hpp`、不认识 `Material`/`Light`。这是本库一贯的分层纪律（与 `shading.hpp` 的片元回调、`raytracer.hpp` 的求交回调同一条）。
- 想叠真正的多色马赛克（不是单色系）时，让图案直接给出颜色、把材质 tint 置白即可 —— 两种用法都不需要改库。

### D6：已知边界（刻意不做）

- **不做 Worley/元胞噪声**（蜂巢、石砌等）与**三平面投影**（uv 来自网格，没有 uv 的网格就用不了贴图）。
- **不做 mipmap / 各向异性过滤**：纹理被缩小到几十个像素时会有摩尔纹（`Texture` 只有最近邻/双线性两种滤波）。
- 图案全部在**生成期**烘进 `Texture`，不是逐像素实时求值 —— 想省内存可以改小分辨率，想更锐可以改大（代价是启动时多几十毫秒）。

## 验证（可复现）

- 单元测试 `ctest -R test_pattern`（**19965 checks，0 failures**）：
  1. **噪声基元**：哈希逐位可复现且落在 `[0,1)`、种子参与哈希；值噪声范围/连续性/确定性；`fbm`、`turbulence` 范围；三者的 `period` 周期性（`x + period` 处取值一致，容差 1e-5 —— 格点索引精确回绕，只有"格内小数位置"的浮点最低位可能不同）。
  2. **四个图案**：颜色恒在 `[0,1]`、高度有界、**对 u 与 v 都以 1 为周期**（逐点比对，任何一点不周期都会报错）；结构性断言：瓷砖的缝隙比面子暗且低、砖墙相邻行交错、编织里"在上"的丝带比"在下"的亮且高。
  3. **贴图生成**：`makeColorTexture` 走 texel 中心（与 `makeNormalMap` 口径一致 ⇒ 两图对齐）；`makePatternMaps` 的法线逐 texel 是单位向量，且明显倾斜的 texel 占比 > 2%（高度确实起作用）；`normalStrength = 0` 时法线全平坦。
- `ctest --test-dir build`：**17/17 通过**。
- 演示实跑（`tutorials/shaded-raster`，800×600，场景改为"瓷砖地面 + 四面彩色砖墙 + 碳纤维立方体 + 大理石球"，2486 个三角形 / 634198 个片元）：

  | 对照 | 结果 |
  |------|------|
  | **颜色贴图** 开 vs `--no-albedo` | **479371 / 480000 像素不同（99.87%）**，平均差 38.9 ⇒ 颜色贴图是画面差异的**最大来源**（此前根本没有颜色贴图，这正是"地面看不出纹理"的根因）|
  | **法线贴图** 开 vs `--no-map` | **272923 / 480000 像素不同（56.86%）**，平均差 29.3 |
  | **阴影** 开 vs `--no-shadow` | **109876 / 480000 像素不同（22.89%）**，平均差 10.7 |

- 参考做法（噪声 / fBm / 大理石脉络的经典配方）：见 [The Book of Shaders — Fractal Brownian Motion](https://thebookofshaders.com/13/)（fBm 的倍频叠加与 lacunarity/gain 约定）与 [Procedural Textures in GLSL](https://www.mysimulator.uk/content/articles/procedural-textures.html)（`marble ≈ 0.5 + 0.5·sin(x·stripes + turbulence·k)`、值噪声 vs 梯度噪声、五次淡入曲线）。
