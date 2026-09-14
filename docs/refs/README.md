# refs/ — 参考与资料（understand + 外部摘录）

> **用途**：存放"是什么/为什么"类参考说明与开发参考资料摘录：架构总览、术语表、第三方头文件/官方文档要点等。
> **目标读者**：需要系统级理解或查阅外部资料要点的开发者与 AI Agent。
> **关联文档**：[docs/README.md](../README.md)；[docs-management.md](../conventions/docs-management.md)（落位决策表：参考 → refs）。

> 状态：生效中（核验于 2026-09-09）。由原 `reference/` 与 `refs/` 合并而来（二者职责重叠，统一到本目录）。

## 导航（索引自维护）

| 文档 | 内容 |
|------|------|
| [architecture.md](architecture.md) | 架构总览与数据流（随开发持续补充） |
| [glossary.md](glossary.md) | 术语表 |
| [win32-api-notes.md](win32-api-notes.md) | Win32 / MinGW 头文件与类型速查 |

> 课程发放的作业题目（`assignment1-polygon-scan.pdf`，扫描线多边形填充）**不随仓库分发**（见 [.gitignore](../../.gitignore)），仅本地参考；
> 对应实现见 [tutorials/polygon-scan/](../../tutorials/polygon-scan/README.md) 与 [adr-0005](../design/adr-0005-raster-polygon-fill.md)。

## 约定

- 本目录内容为"参考/速查"：项目系统说明以 architecture/glossary 为权威；外部资料以官方文档或工具链头文件为权威，冲突以权威源为准。
- 不在此存放项目自身契约规范（属 conventions/）、操作指引（guides/）或设计决策（design/）。
