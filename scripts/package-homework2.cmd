@echo off
chcp 65001 >nul
setlocal
rem ============================================================================
rem package-homework2.cmd —— 组装「作业二 欧拉操作」提交包
rem   流程：组装 → 干净目录独立构建 → 跑单元测试 → 跑拓扑演示 → 收集 exe → 打包 zip
rem   包内附带编译好的可执行文件（bin\*.exe，已静态链接，零 DLL 依赖），可免编译直接运行
rem   用法：scripts\package-homework2.cmd [姓名]
rem   产物：artifacts\homework2\<姓名>_HW2.zip
rem ============================================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "NAME=%~1"
if "%NAME%"=="" set "NAME=学生"
set "PKGDIR=%ROOT%\artifacts\homework2\%NAME%_HW2"
set "WORK=%ROOT%\artifacts\homework2\build-check"
set "ZIP=%ROOT%\artifacts\homework2\%NAME%_HW2.zip"

echo [1/5] 组装提交目录：%PKGDIR%
if exist "%PKGDIR%" rmdir /s /q "%PKGDIR%"
mkdir "%PKGDIR%\code" 2>nul
mkdir "%PKGDIR%\tests" 2>nul
mkdir "%PKGDIR%\include\gfx" 2>nul

copy /y "%ROOT%\tutorials\euler-ops\submit\CMakeLists.txt" "%PKGDIR%\" >nul
copy /y "%ROOT%\tutorials\euler-ops\submit\REPORT.md" "%PKGDIR%\" >nul
copy /y "%ROOT%\tutorials\euler-ops\submit\run.cmd" "%PKGDIR%\" >nul
if exist "%ROOT%\tutorials\euler-ops\submit\*.docx" copy /y "%ROOT%\tutorials\euler-ops\submit\*.docx" "%PKGDIR%\" >nul
copy /y "%ROOT%\tutorials\euler-ops\submit\code\demo.cpp" "%PKGDIR%\code\" >nul
copy /y "%ROOT%\tests\test_halfedge.cpp" "%PKGDIR%\tests\" >nul
copy /y "%ROOT%\tests\gfx_test.hpp" "%PKGDIR%\tests\" >nul
rem 完整 gfx 头文件子库（可视化演示需要 Canvas/View/Window/PNG；核心 halfedge.hpp 在其中）
xcopy /y /e /i /q "%ROOT%\src\include\gfx" "%PKGDIR%\include\gfx\" >nul
rem Win32 窗口实现直接编入可执行文件（免 DLL 依赖）+ 渲染版演示程序
copy /y "%ROOT%\src\window_win32.cpp" "%PKGDIR%\code\" >nul
copy /y "%ROOT%\tutorials\euler-ops\code\euler_demo.cpp" "%PKGDIR%\code\" >nul
rem 28 步贯穿孔实体交互演示（窗口 / --dump / --audit / --selfcheck / --png）
copy /y "%ROOT%\tutorials\euler-ops\code\euler_interactive.cpp" "%PKGDIR%\code\" >nul

rem 注意：构建放到**纯 ASCII 路径**下进行 —— Ninja 无法 chdir 到含中文的目录（会报 Invalid argument）
echo [2/5] 复制到 ASCII 工作目录并独立构建（只用提交包内文件）
if exist "%WORK%" rmdir /s /q "%WORK%"
xcopy /y /e /i /q "%PKGDIR%" "%WORK%" >nul
pushd "%WORK%"
cmake -S . -B build >nul
if errorlevel 1 goto :failpop
cmake --build build --config Release >nul
if errorlevel 1 goto :failpop

echo [3/5] 运行单元测试
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 goto :failpop

echo [4/5] 运行拓扑演示并收集可执行文件（bin\）
if exist "build\demo.exe" (build\demo.exe) else (build\demo)
if errorlevel 1 goto :failpop

rem 把构建产物中的可执行文件复制进待打包目录 bin\ —— 提交包免编译即可直接运行
rem 命名：EulerInteractive.exe（28 步贯穿孔）/ EulerOps.exe（五操作可视化）/ Demo.exe（纯拓扑）
mkdir "%PKGDIR%\bin" 2>nul
if not exist "%PKGDIR%\bin" goto :failpop
copy /y "build\EulerInteractive.exe" "%PKGDIR%\bin\EulerInteractive.exe" >nul
if errorlevel 1 goto :failpop
copy /y "build\euler_ops.exe" "%PKGDIR%\bin\EulerOps.exe" >nul
if errorlevel 1 goto :failpop
copy /y "build\demo.exe" "%PKGDIR%\bin\Demo.exe" >nul
if errorlevel 1 goto :failpop
rem 测试可执行文件 test_halfedge.exe 不放入 bin\（保持包简洁）
rem 静态链接校验：若构建目录出现 DLL，说明 -static 未生效 —— 如实告警，且绝不把 DLL 打进包
if exist "build\*.dll" echo [警告] 构建目录出现 DLL，静态链接可能未生效；已如实报告且未打包任何 DLL。
popd

echo [5/5] 打包
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::CreateFromDirectory('%PKGDIR%', '%ZIP%')"
if errorlevel 1 goto :fail
if not exist "%ZIP%" goto :fail
for %%A in ("%ZIP%") do if %%~zA LSS 1000 (
    echo [失败] zip 体积异常，可能未正确写入。
    goto :fail
)
echo [完成] 提交包：%ZIP%
exit /b 0

:failpop
popd
:fail
echo [失败] 打包中止（详见上方输出）。
exit /b 1
