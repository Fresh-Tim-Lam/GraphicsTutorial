# 作业 1：多边形扫描转换（PolygonFill）

> 计算机图形学 · 作业 1 提交包
> 实现内容：按逆时针输入简单多边形顶点 → 构造边表（ET）→ 逐扫描线维护活性边表（AET）→ 求交点并排序 → 成对填充 → 输出边界与内部区域。

---

## 1. 环境与依赖

| 项 | 值 |
|---|---|
| 语言 | C++17 |
| 平台 | Windows 10/11（x64） |
| 编译器 | GCC 14.2（MinGW-w64）；MSVC 亦可（仅用标准库 + Win32 API） |
| 构建 | CMake ≥ 3.16 + Ninja（或任意 CMake 生成器） |
| 第三方依赖 | **无**。窗口用 Win32 GDI；PNG 编码器为自行实现（CRC32 / Adler32 / zlib 存储块） |

## 2. 编译与运行

在**干净目录**下：

```powershell
cmake -S . -B build
cmake --build build
# 可选：跑单元测试
ctest --test-dir build --output-on-failure
```

可执行文件：`build/PolygonFill.exe`（Windows 下同时会生成 `build/gfx.dll`，两者需在同一目录，CMake 已自动拷贝）。

运行方式：

```powershell
# ① 文本读入 → 输出 PNG
build\PolygonFill.exe --input data\convex-pentagon.txt --output output\convex-pentagon.png

# ② 凹多边形 → 窗口逐条扫描线动画演示
build\PolygonFill.exe --input data\concave-arrow.txt --window --animate

# ③ 交互输入（命令行逐个输入顶点个数与坐标）
build\PolygonFill.exe --interactive --window

# ④ 查看全部参数
build\PolygonFill.exe --help
```

| 参数 | 说明 |
|---|---|
| `--input <file>` | 从文本文件读顶点 |
| `--interactive` | 从标准输入输入顶点（与 `--input` 二选一） |
| `--output <file>` | 结果写 PNG（可省略；省略且未开窗口时只在终端打印摘要） |
| `--window` | 打开窗口展示结果（填充 + 边界） |
| `--animate` | 配合 `--window`：逐条扫描线推进演示（高亮当前扫描线） |
| `--size WxH` | 画布尺寸，默认 800x600 |

**退出码**：`0` 成功 · `2` 参数错误 · `3` 输入文件/格式错误 · `4` 多边形非法（顶点不足/共线/自交） · `5` 输出失败。

## 3. 输入格式

首行为顶点个数 `n`，随后 `n` 行每行 `x y`（世界坐标，y 轴向上，建议逆时针）：

```
5
100 80
220 80
280 180
160 260
60 180
```

- 空行、以 `#` 开头的行会被忽略（样例文件带注释）。
- 分隔符支持空格 / 逗号 / 括号：`100 80`、`100,80`、`(100, 80)`。
- **末尾允许重复写一次首点**（闭合点）：程序识别为闭合点，不产生零长度边。
- 相邻重合顶点（零长度边）会被自动剔除。
- 世界坐标按包围盒**等比缩放居中**映射到像素坐标（留 24 px 边距），y 轴翻转。

`data/` 中的样例：

| 文件 | 类型 |
|---|---|
| `convex-pentagon.txt` | 凸（5 顶点，题目示例坐标） |
| `convex-hexagon-closed.txt` | 凸（6 顶点 + 末尾重复首点） |
| `concave-arrow.txt` | 凹（7 顶点箭头） |
| `invalid-self-intersecting.txt` | 非法：蝴蝶结自交（退出码 4） |
| `invalid-collinear.txt` | 非法：三点共线（退出码 4） |
| `invalid-too-few.txt` | 非法：顶点数不足（退出码 3） |

## 4. 算法说明

### 4.1 数据结构

- **边表 ET**：每条非水平边按"它开始参与的第一条扫描线"分桶；桶内存边索引。
  边的字段：`yMin / yMax / xAtYMin / invSlope`（以及派生的 `enterScanline / leaveScanline`）。
- **活性边表 AET**：当前扫描线正在穿过的边的集合；每推进一行先"插入新边、移除已越过 `yMax` 的边"。
- **填充区间 Span**：一行内若干 `[xStart, xEnd]` 闭区间。

### 4.2 扫描线规则（一致性的关键）

| 规则 | 内容 | 作用 |
|---|---|---|
| 像素覆盖 | 像素 `i` 覆盖 `[i, i+1)`，采样其中心 `i+0.5` | 填充宽度与几何宽度一致 |
| y 方向半开 | 非水平边参与求交的条件为 `yMin ≤ y+0.5 < yMax` | **共享顶点、极值点只被计一次**，每条扫描线交点数为偶数 |
| 水平边 | 直接跳过（与扫描线重合，不产生有效交点） | 避免重复计数与除零 |
| x 方向半开 | 一行内按 `[xl, xr)` 取像素（左闭右开） | 相邻多边形共享边时既不重叠也不留缝 |

其中 y 方向半开区间是解决"顶点恰落在扫描线上"的标准做法：极值点（V/Λ 形）处两条边同时进入或同时退出，交点个数保持为偶数。

### 4.3 流程

1. 规范化顶点（剔除零长度边、识别闭合点）；
2. 校验：顶点数 ≥ 3、面积非 0（至少三点不共线）、非自交（简单多边形）；
3. 自动闭合（最后一点连回第一点）；
4. 建 ET（跳过水平边）；
5. 逐扫描线：ET→AET 插入/移除 → 求交点 x → 排序 → 成对生成区间 → 写像素；
6. 填充完成后再画边界（覆盖在最上层，保证边界清晰）。

**交点计算方式**：`x = xAtYMin + invSlope · (y + 0.5 − yMin)` 由边参数直接算出，而不是"每行累加 `invSlope`"，因此长边上没有累积误差。

### 4.4 复杂度

- 建表：`O(E)`（E 为边数）；每行：`O(A log A)`（A 为活性边数，排序）。
- 总计 `O(E + Σ_y A log A)`，最坏情形 `O(E·H)`；空间 `O(E + A)`。
- 输出（逐像素写）与填充面积成正比，是实际耗时的主要部分。

## 5. 已知限制

- 只支持**单个简单多边形**；带孔多边形/多个不相交轮廓需要"多轮廓统一求交 + 奇偶或非零绕组规则"（可选提高题，未实现）。
- 边界用 Bresenham 最近像素落点、填充用像素中心采样，两者在边界处最多相差 1 像素，故约定"先填充、后画轮廓"。
- PNG 输出固定 8bit/通道 RGB（丢弃 alpha），且使用 zlib 存储块（不压缩），文件体积约为 `宽×高×3` 字节。
- 高度不足 1 像素的多边形被判定为合法但无扫描线可填（输出仅有轮廓）。

## 6. 目录结构

```
Homework1/
├─ README.md              本文件（环境 / 构建 / 输入格式 / 算法 / 已知限制）
├─ REPORT.md              实验报告
├─ CMakeLists.txt         顶层构建入口
├─ app/polygon_fill.cpp   命令行程序（输入解析、坐标映射、窗口动画、PNG 输出）
├─ src/                   渲染库（权威实现）
│   ├─ include/gfx/raster.hpp   画线 + 扫描线填充（ET/AET）★算法核心
│   ├─ include/gfx/png.hpp      自写 PNG 编码器
│   ├─ include/gfx/canvas.hpp   像素帧缓冲
│   ├─ include/gfx/window.hpp + window_win32.cpp   Win32 窗口上屏
│   └─ include/gfx/math/        向量/矩阵（由前期作业积累，本作业用其 Vec2）
├─ tests/                 单元测试（test_raster / test_png）
├─ data/                  测试输入（凸、凹、非法样例）
└─ output/                运行结果图（凸、凹各一张，及闭合点样例）
```
