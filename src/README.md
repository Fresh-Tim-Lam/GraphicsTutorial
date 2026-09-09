# src/ — 正式源码（gfx 渲染 API）

> **用途**：`gfx` 渲染 API 的权威实现所在，随教学模块演进。对外交付为动态库 + 头文件。
> **目标读者**：实现 gfx 模块的开发者与 AI Agent。
> **关联文档**：[README.cn.md](../README.cn.md)；[docs/reference/architecture.md](../docs/reference/architecture.md)；[packaging/](../packaging/README.md)。

> 状态：生效中（核验于 2026-09-09）。模块结构随首个模块落地时细化。

## 职责一句话

存放 `gfx` 图形渲染 API 的正式源码（唯一事实源）：头文件（公共 API + 内部）、实现、模块化单元。

## 规划结构（待首个模块落地细化）

```
src/
├─ README.md
├─ include/gfx/     # 公共头文件（对外分发；如 window.hpp、canvas.hpp 等按模块）
├─ internal/        # 内部实现头（不对外）
├─ *.cpp            # 实现
└─ CMakeLists.txt   # 定义 gfx 动态库目标（DLL + 导入库）
```

## 约束

- 版本唯一事实源：`include/gfx/version.hpp`（内容从 [docs/conventions/version.md](../docs/conventions/version.md) 规则创建）。
- 只用最基础的 Windows 屏幕像素着色能力；不引入第三方图形库（从 0 造轮子）。
- 命名语言：C++ 标识符用英文；注释用中文（见 [naming.md](../docs/conventions/naming.md)）。
