# packaging/ — 打包发布（API 分发）

> **用途**：将 `gfx` API（动态库 DLL + 导入库 + 头文件）打包为可被调用方直接使用的发布产物。
> **目标读者**：执行发布/打包的维护者与 AI Agent。
> **关联文档**：[src/](../src/README.md)；[version.md](../docs/conventions/version.md)；[operations.md](../docs/guides/operations.md)。

> 状态：生效中（核验于 2026-09-09）。发布产物结构随首个模块落地细化。

## 职责一句话

打包发布的**唯一入口**是 [build_release.cmd](build_release.cmd)；产出目录为 `packaging/release/`（gitignore）。

## 产出形态（首个模块 0.1.0 已落地）

```
packaging/release/
├─ include/gfx/        # 公共头文件（含 gfx/math/ 头文件子库）
├─ lib/                # 导入库（MinGW: libgfx.dll.a；MSVC: gfx.lib）
└─ bin/                # 动态库（MinGW: libgfx.dll；MSVC: gfx.dll）
```

> 调用方：include 指向 `release/include`，链接 `release/lib` 导入库，运行时把 `release/bin` 下的 dll 放到 exe 旁或加入 PATH。

## 约定

- 版本号只出自 src/ 唯一事实源（version.hpp），打包脚本读取之，不硬编码。
- 打包前必须通过测试；发布动作属于 AGENT.md 的 Ask First 项。
