# ADR-0001 — 首个模块：gfx_math 子库 + Canvas 像素底座

> **用途**：记录首批模块（矩阵子库 gfx_math、像素画布 Canvas）的结构决策与理由，供后续演进参照。
> **目标读者**：参与本模块设计/后续扩展的开发者与 AI Agent。
> **关联文档**：[src/README.md](../../src/README.md)；[architecture.md](../refs/architecture.md)；[version.md](../conventions/version.md)。

> **状态**：✅ 采纳（2026-09-09）

## 背景

- 项目从 0 造轮子：首个可交付能力 = "在屏幕上按指定位置绘制像素" + 支撑几何运算的矩阵库。
- 用户已确认：矩阵**自己写**（零第三方依赖）；两个库的组织形态采用"gfx_math 独立子库 + 像素模块属主库 gfx"。
- 版本唯一事实源纪律：见 version.md。

## 决策

### D1：gfx_math 为独立头文件子库（INTERFACE target）
- 命名空间 `gfx::math`；纯头文件、无编译产物；头文件放 `src/include/gfx/math/`。
- 覆盖维度 **2D+3D 一步到位**：vec2/vec3/vec4 + mat2/mat3/mat4（齐次坐标可直接用）。
- 运算集：加/减/数乘/矩阵乘、转置、行列式、逆（核心线性代数集）。
- 理由：教学后续立刻要用齐次坐标做变换；头文件式免去 DLL 导出模板的复杂度，发布时随 include 分发。

### D2：像素底座用 Canvas 帧缓冲（内存像素缓冲 + 统一上屏）
- 主库 gfx 首个公开类型：`gfx::Canvas`（或同层），提供：
  - `putPixel(x, y, color)`：写入指定位置像素（含边界裁剪）；
  - `clear(color)` / 宽高/访问等基础能力；
  - `present()`：整帧一次上屏（内部走 Windows GDI：DIB 位图 + BitBlt）。
- 不采用每像素直接 `SetPixel`（慢且闪烁）；帧缓冲是后续画线/填充/光栅化的自然地基。
- Windows 具体 API 实现收在 src/ 内部（不暴露在公共 API），公共 API 保持平台无关观感（当前实现面向 Win32 GDI）。

### D3：首个正式 API 目标形态
- `gfx_math`：INTERFACE（纯头文件）→ 无 .lib/.dll。
- `gfx`：SHARED 动态库（DLL + 导入库 + 头文件），顶层命名空间 `gfx`。
- 发布形态沿用 packaging 约定：include + lib + bin。

### D4：测试用自写轻量断言（零依赖）
- 不引入第三方测试框架；tests/ 内置极简断言宏（tests/gfx_test.hpp），按测试程序跑断言。
- 符合"从 0 造轮子"约束，且对 math 这类纯值类型已足够。

## 后果

- src/ 公共 include 树开始出现：`include/gfx/math/*`、`include/gfx/canvas.hpp`（待实现落位）。
- 版本事实源 `include/gfx/version.hpp` 本次创建（0.1.0）。
- 未决/后续：窗口管理粒度、像素格式约定（BGRA/RGBA）、present 是否拆 swapchain 概念——见 todo.md。
