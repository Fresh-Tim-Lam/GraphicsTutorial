# artifacts/ — 临时/调试产物区（gitignore）

> **用途**：全仓唯一"临时物/一次性调试物"落地区。任何名字带 `tmp`/`debug`/`diag`/`shot` 前缀或一次性产物，一律进此目录。
> **目标读者**：需要在仓库里临时放东西的开发者与 AI Agent。
> **关联文档**：[directory-organization.md](../docs/conventions/directory-organization.md)（临时文件铁律）。

> 状态：生效中（核验于 2026-09-09）。

## 规则

- 本目录整体被 .gitignore 忽略，`README.md` 为保留文件。
- 一次性脚本、诊断输出、截图、复现用例 → 放这里；**禁止**出现在仓库根、src/、scripts/、packaging/、docs/ 正文目录。
- 产物过期自行清理；正式化后移入正式目录（scripts/、tests/、tutorials/ 等）。
