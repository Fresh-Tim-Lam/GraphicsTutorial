# operations.md — 开发与发布操作手册

> **用途**：集中记录常用操作命令：配置、构建、测试、运行、打包发布。AGENT.md Commands 以此为详源。
> **目标读者**：执行开发/测试/发布的开发者与 AI Agent。
> **关联文档**：[AGENT.md](../../AGENT.md)（Commands 摘要）；[CMakeLists.txt](../../CMakeLists.txt)；[packaging/](../../packaging/README.md)。

> 状态：生效中（核验于 2026-09-09）。构建目标随模块落地后补充。

## 配置与构建

```bat
rem 配置（生成构建文件到 build/）
cmake -S . -B build

rem 构建
cmake --build build --config Release
```

## 测试

```bat
rem 测试框架选定后在此补充具体命令；当前可先用 ctest
ctest --test-dir build -C Release
```

## 运行示例

- tutorials/ 各主题 code/ 目录的运行方式随首个主题落地后在此登记。

## 打包发布 API

```bat
rem 唯一发布入口（当前为骨架，首个模块落地后补全）
packaging\build_release.cmd
```

## 注意事项

- 版本号出自 src/include/gfx/version.hpp（见 [version.md](../conventions/version.md)），勿手改 CMake/打包脚本。
- 发布属 AGENT.md 的 Ask First 边界。
