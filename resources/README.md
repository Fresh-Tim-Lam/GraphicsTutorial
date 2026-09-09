# resources/ — 随项目分发的静态资源

> **用途**：存放随应用/示例分发或仓库级使用的静态资源（图标、文案、示例输入、截图等）。
> **目标读者**：维护资源文件的开发者与 AI Agent。
> **关联文档**：[docs/conventions/directory-organization.md](../docs/conventions/directory-organization.md)。

> 状态：生效中（核验于 2026-09-09）。内容随需要添加。

## 职责一句话

集中管理"非代码但随项目走"的静态资源，避免散落仓库根与源码目录。

## 约定

- 教学相关截图/示例输入：若属 tutorials 主题内容，就近放主题目录；通用仓库级截图放此处。
- 临时诊断截图进 [artifacts/](../artifacts/README.md)，不落 resources/。
