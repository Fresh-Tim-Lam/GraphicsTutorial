@echo off
rem ===========================================================================
rem GraphicsTutorial 打包发布唯一入口 build_release.cmd
rem 用途：构建 Release 态 gfx 动态库，并产出 packaging/release/ 分发目录。
rem 目标读者：执行发布的维护者 / AI Agent（发布前请先确认 Ask First 边界）。
rem 关联：packaging/README.md、docs/conventions/version.md。
rem 状态：骨架，随首个模块落地细化；当前不含具体构建细节，仅占位+报错引导。
rem ===========================================================================
setlocal

echo [packaging] 打包发布入口已就位。
echo [packaging] 提示：本脚本为骨架——首个 gfx 模块（src/CMakeLists.txt 中 add_library(gfx ...)）落地后，
echo [packaging] 在此补充：cmake --build Release → 拷贝 dll/lib/include 至 packaging/release/。

rem 占位失败分支：防止误以为已可发布
echo [packaging] 错误：尚无发布产物可打包（首个模块未落地）。请先完成首个模块再发布。
exit /b 1
