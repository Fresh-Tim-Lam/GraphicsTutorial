@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
rem ============================================================================
rem run-euler-ops.cmd —— 作业二（欧拉操作）一键脚本【放在作业二目录里】
rem
rem   双击运行（无参数）⇒ 显示菜单
rem   也可带参数直接执行：
rem     run-euler-ops.cmd image     构建并生成结果图（PNG，并自动打开）
rem     run-euler-ops.cmd window    构建并打开可视化窗口
rem     run-euler-ops.cmd test      运行单元测试（ctest）
rem     run-euler-ops.cmd package   打包提交包（HW2.zip）
rem     run-euler-ops.cmd topo      运行纯拓扑演示（不依赖渲染）
rem ============================================================================

rem 仓库根 = 本脚本所在目录的上两级（tutorials\euler-ops\..\..)
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "EXE=%ROOT%\build\tutorials\EulerOps.exe"
set "OUT=%ROOT%\tutorials\euler-ops\output"
set "ACTION=%~1"

if "%ACTION%"=="" goto :menu
goto :dispatch

:menu
set "MENU_MODE=1"
echo.
echo ================= 作业二 欧拉操作 =================
echo   1. 生成结果图（PNG，并自动打开）
echo   2. 打开可视化窗口（关闭窗口即退出）
echo   3. 运行单元测试（ctest）
echo   4. 运行纯拓扑演示（打印每步数量与校验）
echo   5. 打包提交包（HW2.zip）
echo   0. 退出
echo ==================================================
set "CHOICE="
set /p "CHOICE=请输入序号："
if "%CHOICE%"=="1" set "ACTION=image"
if "%CHOICE%"=="2" set "ACTION=window"
if "%CHOICE%"=="3" set "ACTION=test"
if "%CHOICE%"=="4" set "ACTION=topo"
if "%CHOICE%"=="5" set "ACTION=package"
if "%CHOICE%"=="0" exit /b 0
if not defined ACTION goto :menu
if "!ACTION!"=="" goto :menu

:dispatch
if /i "%ACTION%"=="image"   goto :action_image
if /i "%ACTION%"=="window"  goto :action_window
if /i "%ACTION%"=="test"    goto :action_test
if /i "%ACTION%"=="topo"    goto :action_topo
if /i "%ACTION%"=="package" goto :action_package
echo 未知动作：%ACTION%
goto :end

rem ---------------------------------------------------------------- 生成结果图
:action_image
call :ensure_build
if errorlevel 1 goto :fail
if not exist "%OUT%" mkdir "%OUT%"
echo [运行] 生成结果图（三块：四面体 / KEMR 内环 / KFMRH 内环）...
"%EXE%" --output "%OUT%\euler-ops.png"
if errorlevel 1 goto :fail
echo.
echo 结果图：%OUT%\euler-ops.png
start "" "%OUT%\euler-ops.png"
goto :end

rem ------------------------------------------------------------ 可视化窗口
:action_window
call :ensure_build
if errorlevel 1 goto :fail
if not exist "%OUT%" mkdir "%OUT%"
echo [运行] 打开可视化窗口（关闭窗口即退出；同时也会写出 PNG）...
"%EXE%" --window --output "%OUT%\euler-ops.png"
if errorlevel 1 goto :fail
goto :end

rem ---------------------------------------------------------------- 单元测试
:action_test
call :ensure_build
if errorlevel 1 goto :fail
ctest --test-dir "%ROOT%\build" -C Release -R test_halfedge --output-on-failure
if errorlevel 1 goto :fail
goto :end

rem ------------------------------------------------------------ 纯拓扑演示
:action_topo
call :ensure_build
if errorlevel 1 goto :fail
echo [运行] 拓扑演示（控制台逐步打印 顶点/边/环/面 数量与校验结果）...
"%EXE%" --output "%OUT%\euler-ops.png"
if errorlevel 1 goto :fail
echo.
echo 结果图：%OUT%\euler-ops.png
goto :end

rem ---------------------------------------------------------------- 打包提交
:action_package
if "%~2"=="" (set "PNAME=林天佑") else (set "PNAME=%~2")
echo [打包] 姓名：%PNAME%
call "%ROOT%\scripts\package-homework2.cmd" "%PNAME%"
if errorlevel 1 goto :fail
echo.
echo 提交包：%ROOT%\artifacts\homework2\%PNAME%_HW2.zip
goto :end

rem ------------------------------------------------- 首次使用时自动构建
:ensure_build
if exist "%EXE%" exit /b 0
echo [构建] 首次使用需要编译，请稍候...
cmake -S "%ROOT%" -B "%ROOT%\build" >nul
if errorlevel 1 (echo [失败] CMake 配置失败。 & exit /b 1)
cmake --build "%ROOT%\build" --config Release --target tutorial_euler >nul
if errorlevel 1 (echo [失败] 编译失败。 & exit /b 1)
echo [构建] 完成。
exit /b 0

:fail
echo.
echo [失败] 动作未完成，请查看上方输出。
if defined MENU_MODE pause
exit /b 1

:end
rem 菜单模式停留一下让用户看清输出；命令行模式直接退出（便于自动化调用）
if defined MENU_MODE (
    echo.
    pause
)
exit /b 0
