@echo off
chcp 65001 >nul
rem ===========================================================================
rem run-polygon-fill.cmd —— 一键构建并运行"多边形扫描转换"演示（作业 1）
rem
rem 用法：
rem   双击运行                                   -> 交互菜单（选样例 / 动画 / 交互输入 / 非法输入）
rem   scripts\run-polygon-fill.cmd convex        -> 凸多边形        -> 输出 PNG
rem   scripts\run-polygon-fill.cmd concave       -> 凹多边形        -> 输出 PNG
rem   scripts\run-polygon-fill.cmd animate       -> 凹多边形        -> 窗口逐条扫描线动画
rem   scripts\run-polygon-fill.cmd window        -> 凸多边形        -> 窗口静态展示
rem   scripts\run-polygon-fill.cmd interactive   -> 命令行输入顶点  -> 窗口展示
rem   scripts\run-polygon-fill.cmd invalid       -> 非法输入演示（自交，看提示与退出码）
rem   scripts\run-polygon-fill.cmd --input a.txt --output b.png   -> 其余参数原样透传给 PolygonFill
rem
rem 输出图默认落在 artifacts\homework1\output\（仓库临时区，不入库）
rem 关联文档：tutorials/polygon-scan/README.md；scripts/README.md
rem ===========================================================================
setlocal enabledelayedexpansion
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "BUILD=%ROOT%\build"
set "EXE=%BUILD%\tutorials\PolygonFill.exe"
set "DATA=%ROOT%\tutorials\polygon-scan\data"
set "OUT=%ROOT%\artifacts\homework1\output"

rem ---- 增量构建（首次才配置）----
if not exist "%BUILD%\CMakeCache.txt" (
    echo [构建] 首次运行：配置 + 构建 tutorial_polygon_fill ...
    cmake -S "%ROOT%" -B "%BUILD%" >nul
    if errorlevel 1 goto :fail
)
cmake --build "%BUILD%" --target tutorial_polygon_fill >nul
if errorlevel 1 goto :fail
if not exist "%EXE%" (
    echo [错误] 未生成 %EXE%
    goto :fail
)
if not exist "%OUT%" mkdir "%OUT%" >nul

rem ---- 选择场景 ----
set "SCENE=%~1"
if "%SCENE%"=="" (
    echo.
    echo   多边形扫描转换演示（作业 1）
    echo   ------------------------------------------
    echo   1^) 凸多边形   -^> 输出 PNG
    echo   2^) 凹多边形   -^> 输出 PNG
    echo   3^) 凹多边形   -^> 窗口扫描线动画
    echo   4^) 凸多边形   -^> 窗口静态展示
    echo   5^) 交互输入顶点 -^> 窗口展示
    echo   6^) 非法输入演示（自交）
    echo   0^) 退出
    echo.
    set /p SCENE=请选择 [0-6]:
)
if "%SCENE%"=="1" set "SCENE=convex"
if "%SCENE%"=="2" set "SCENE=concave"
if "%SCENE%"=="3" set "SCENE=animate"
if "%SCENE%"=="4" set "SCENE=window"
if "%SCENE%"=="5" set "SCENE=interactive"
if "%SCENE%"=="6" set "SCENE=invalid"
if "%SCENE%"=="0" exit /b 0

if /i "%SCENE%"=="convex" (
    echo [运行] 凸多边形 -^> "%OUT%\convex-pentagon.png"
    "%EXE%" --input "%DATA%\convex-pentagon.txt" --output "%OUT%\convex-pentagon.png"
    goto :done
)
if /i "%SCENE%"=="concave" (
    echo [运行] 凹多边形 -^> "%OUT%\concave-arrow.png"
    "%EXE%" --input "%DATA%\concave-arrow.txt" --output "%OUT%\concave-arrow.png"
    goto :done
)
if /i "%SCENE%"=="animate" (
    echo [运行] 窗口扫描线动画（关闭窗口即退出）
    "%EXE%" --input "%DATA%\concave-arrow.txt" --window --animate
    goto :done
)
if /i "%SCENE%"=="window" (
    echo [运行] 窗口静态展示（关闭窗口即退出）
    "%EXE%" --input "%DATA%\convex-pentagon.txt" --window
    goto :done
)
if /i "%SCENE%"=="interactive" (
    echo [运行] 交互输入：先输顶点个数，再逐个输 "x y"（建议逆时针）
    "%EXE%" --interactive --window
    goto :done
)
if /i "%SCENE%"=="invalid" (
    echo [运行] 非法输入（自交）——预期退出码 4
    "%EXE%" --input "%DATA%\invalid-self-intersecting.txt"
    echo [退出码] !errorlevel!
    goto :done
)

rem ---- 其余情况：原样透传给 PolygonFill ----
if "%~1"=="" (
    echo [错误] 无效选择：%SCENE%
    goto :fail
)
"%EXE%" %*
if errorlevel 1 goto :fail

:done
echo.
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [失败] 见上面的错误信息（PolygonFill --help 可查看全部参数）。
pause
exit /b 1
