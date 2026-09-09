# naming.md — 命名与语言习惯

> **用途**：规定文件、目录、代码符号、提交信息的命名规范与语言习惯（本项目默认中文注释与提交）。
> **目标读者**：本仓库所有代码/文档/目录的创建者与 AI Agent。
> **关联文档**：[directory-organization.md](directory-organization.md)；[docs-management.md](docs-management.md)。

> 状态：生效中（核验于 2026-09-09）。

## 规则

1. **目录与文件名**：一律 kebab-case 小写（`tutorials/rendering-pipeline/`、`directory-organization.md`）；中文文件名先转英文语义再 kebab-case。
2. **代码符号（C++）**：类型/类 PascalCase（如 `class Canvas`）；函数/变量 camelCase（如 `putPixel`）；常量/宏 UPPER_SNAKE；前缀命名空间统一 `gfx`。
3. **教学树主题目录**：kebab-case，树状组织（见 [tutorials/](../../tutorials/README.md)）；禁止数字序号的伪瀑布命名。
4. **语言习惯**：文档/注释/提交信息默认**中文**（UTF-8）；代码标识符用英文；公共 API 头文件注释可中文（读者以中文教学为主）。
5. **提交信息**：中文，短句说明"改了什么/为何"。
6. **冲突处理**：命名与语言若有例外（如对外发布头文件需英文注释），在该处标注说明并回写本文档。
