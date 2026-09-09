# docs/ — 文档中心（总索引）

> **用途**：GraphicsTutorial 的文档中心。docs 根目录**只允许**本 README（总索引）与 [to-dolist.md](to-dolist.md)（活跃待办）；其余文档按目录职责表唯一落位。
> **目标读者**：需要找文档的开发者 / AI Agent / 维护者。
> **关联文档**：[docs-management.md](conventions/docs-management.md)（docs 组织与登记规则）；[directory-organization.md](conventions/directory-organization.md)（顶层目录职责）。

> 状态：生效中（核验于 2026-09-09）。

## 目录导航（索引自维护：新增/移动/删除文档须同步本节）

| 目录 | 职责（一句话） | 导航/索引 |
|------|--------------|----------|
| [conventions/](conventions/README.md) | 契约性规范（人机都遵守的规则） | 目录组织、docs 管理、meta-rules、版本、日志、命名 |
| [guides/](guides/README.md) | 操作指引 how-to（协作、开发/发布操作） | collaboration、operations |
| [reference/](reference/README.md) | 参考说明 understand（架构、术语、机制） | architecture、glossary |
| [design/](design/README.md) | 设计文档与决策记录（ADR） | （随项目发生） |
| [sessions/](sessions/README.md) | 历史对话记录（承接上下文） | （随项目发生） |
| [prompts/](prompts/README.md) | 可复用提示词模板 | new-project-init-prompt |
| [tmp/](tmp/README.md) | 少量诊断记录 | （随项目发生） |

> 注：教学/教程树在顶层 [tutorials/](../tutorials/README.md)（树状教学，非 docs 体系）；示例数据用 docs/example（未启用）。
> `docs/ppt/`（Lecture1–5.pdf）为**个人参考讲义，非交付物**，已被 .gitignore 整目录排除、不入库，仅保留原位供查阅（登记见 docs-management）。

## 根目录白名单

`docs/` 根目录只允许：`README.md`（本文件）+ `to-dolist.md`。其他文档按决策表唯一落位（见 docs-management）。
