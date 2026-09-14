@echo off
chcp 65001 >nul
rem ===========================================================================
rem build-and-test.cmd —— 一键：配置 + 构建 + 跑全部测试（无需手敲 cmake/ctest）
rem
rem 用法：
rem   双击运行                              -> 配置 + 构建 + ctest（结束后暂停，便于看结果）
rem   scripts\build-and-test.cmd --clean    -> 先删掉 build\ 再全量重建
rem 关联文档：docs/guides/operations.md；scripts/README.md
rem ===========================================================================
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "BUILD=%ROOT%\build"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [错误] PATH 中找不到 cmake，请先安装 CMake 并加入 PATH。
    goto :fail
)

if /i "%~1"=="--clean" (
    echo [1/3] 清理构建目录 build\ ...
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
)

echo [1/3] 配置（cmake -S . -B build）...
cmake -S "%ROOT%" -B "%BUILD%"
if errorlevel 1 goto :fail

echo.
echo [2/3] 构建（cmake --build build）...
cmake --build "%BUILD%"
if errorlevel 1 goto :fail

echo.
echo [3/3] 测试（ctest）...
ctest --test-dir "%BUILD%" --output-on-failure
if errorlevel 1 goto :fail

echo.
echo [完成] 可执行文件目录：%BUILD%\tutorials
echo         PolygonFill.exe（多边形填充）/ tutorial_pixels.exe（像素上屏）/ tutorial_math.exe（数学演示）
echo.
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [失败] 请查看上面的输出。
pause
exit /b 1
