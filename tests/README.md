# tests/ — 正式测试

> **用途**：存放 `gfx` API 的单元/集成测试。
> **目标读者**：编写测试的开发者与 AI Agent。
> **关联文档**：[src/](../src/README.md)；[docs/guides/operations.md](../docs/guides/operations.md)（运行命令）。

> 状态：生效中（核验于 2026-09-09）。测试框架待首个可测模块落地后选定并记录于此。

## 职责一句话

验证 `gfx` 各模块行为与接口契约，守护"从 0 造轮子"过程中不回归。

## 约定（待细化）

- 测试文件与被测模块一一对应（命名见 [docs/conventions/naming.md](../docs/conventions/naming.md)）。
- 需要 GUI/像素验证的场景，先在 artifacts/ 下做人工诊断，能自动化后再固化为正式测试。
- 根 CMakeLists 已 `enable_testing()`；实际测试目标随框架选定后注册。
