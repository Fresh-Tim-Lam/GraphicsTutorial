# conventions/ — 契约性规范

> **用途**：存放 GraphicsTutorial 的契约性规范文档（人机都遵守的规则）。
> **目标读者**：任何修改本仓库代码/文档/目录的开发者与 AI Agent（先读对应规范再动手）。
> **关联文档**：[docs/README.md](../README.md)；[meta-rules.md](meta-rules.md)（规范如何制定/维护）。

> 状态：生效中（核验于 2026-09-09）。

## 导航（索引自维护）

| 文档 | 内容 | 何时读 |
|------|------|--------|
| [directory-organization.md](directory-organization.md) | 顶层目录职责、临时文件铁律、根白名单 | 放文件/建目录前 |
| [docs-management.md](docs-management.md) | docs 组织、书写规范、登记表 | 增删/移动文档前 |
| [meta-rules.md](meta-rules.md) | 规范元规则（结论落盘/标记/依附场景） | 制定或修改规范时 |
| [version.md](version.md) | 版本唯一事实源 | 涉及版本号/发布时 |
| [logging.md](logging.md) | 日志规范 | 涉及日志时 |
| [naming.md](naming.md) | 命名与语言习惯 | 命名文件/符号/提交时 |
| [code-style.md](code-style.md) | 代码风格：类定义内 `private:` 段先于 `public:` 段（成员在前、接口在后）及其例外 | 新增/调整类定义成员组织顺序时 |
| [api-docs.md](api-docs.md) | 公共接口说明文档的维护规范（Doxygen 注释/变更同步） | 增改 gfx / gfx_math 公共接口或其说明时 |
| [performance.md](performance.md) | 性能敏感写法（[矩阵构造] 只构造一次/公式单源）与性能结论记录方式（基准+测量条件） | 改热路径（变换构造、逐点/逐像素循环、批处理）或给出"更快"结论时 |
