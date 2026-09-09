# docs-management.md — docs 组织与登记规则

> **用途**：规定 docs 文档中心的子目录职责、文档书写规范、登记与索引自维护规则（含目录登记表）。
> **目标读者**：任何在 docs/ 下新增/移动/删除文档的开发者与 AI Agent。
> **关联文档**：[directory-organization.md](directory-organization.md)；[docs/README.md](../README.md)；[naming.md](naming.md)。

> 状态：生效中（核验于 2026-09-09）。

## 1. docs 根目录白名单

只允许 `README.md`（总索引）+ `to-dolist.md`（活跃待办）。其余文档按子目录职责唯一落位。

## 2. 子目录职责表

| 目录 | 职责（类型） | 内容示例 |
|------|------------|---------|
| `conventions/` | 契约性规范（人机都遵守） | directory-organization、docs-management、meta-rules、version、logging、naming |
| `guides/` | 操作指引 how-to | collaboration（协作）、operations（开发/发布操作） |
| `reference/` | 参考说明 understand（是什么/为何） | architecture（架构总览）、glossary（术语表） |
| `design/` | 设计文档与决策记录（ADR） | 各模块设计、决策归档 |
| `sessions/` | 历史对话记录（承接上下文） | 会话摘要 |
| `prompts/` | 可复用提示词模板 | new-project-init-prompt |
| `tmp/` | 少量诊断记录 | 调试会话 |

> 决策表：契约规则?→conventions ｜ how-to?→guides ｜ 理解系统?→reference ｜ 设计决策?→design ｜ 会话?→sessions ｜ 提示词?→prompts ｜ 诊断?→tmp。教学树不属 docs，放顶层 [tutorials/](../../tutorials/README.md)。

## 3. 文档书写规范

1. **命名**：kebab-case 小写（如 `directory-organization.md`）；中文语义先转英文再 kebab-case。
2. **头部三要素**：每份文档前 1–3 行必须含 `用途 / 目标读者 / 关联文档` 三项；用途本身独立可读，禁止"详见 xxx"式用途。
3. **状态与日期**：规范/决策类标注 `> **状态**：生效中（核验于 YYYY-MM-DD）`；草稿/已废弃同理。
4. **单一事实源 + 防双源漂移**：同一知识只维护一份权威文档；面向外部/内嵌摘要须在权威源标注"摘要同步自 X，冲突以 X 为准"。
5. **登记与索引自维护**：新增/移动/删除文档后，创建者**必须**同步父目录 README 索引；新增 docs 子目录登记到下文登记表。生成物自动维护，不等用户提示。

## 4. 目录登记表（新增子目录时追加）

| 日期 | 子目录 | 用途 | 说明 |
|------|--------|------|------|
| 2026-09-09 | conventions/ | 契约性规范 | 与骨架同建 |
| 2026-09-09 | guides/ | 操作指引 | 与骨架同建 |
| 2026-09-09 | reference/ | 参考说明 | 与骨架同建 |
| 2026-09-09 | design/ | 设计文档/ADR | 与骨架同建（空索引） |
| 2026-09-09 | sessions/ | 会话记录 | 与骨架同建（空索引） |
| 2026-09-09 | prompts/ | 提示词模板 | 已含 new-project-init-prompt |
| 2026-09-09 | tmp/ | 诊断记录 | 与骨架同建（空索引） |
| 2026-09-09 | ppt/ | 个人参考讲义（Lecture1–5.pdf） | 非交付物；已被 .gitignore 排除，不入库 |
