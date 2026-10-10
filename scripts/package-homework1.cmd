@echo off
chcp 65001 >nul
rem ===========================================================================
rem package-homework1.cmd —— 一键组装"作业 1"提交包：
rem     组装 -> 干净目录构建验证 -> 跑测试 -> 生成结果图 -> 打 zip
rem
rem 用法：
rem   双击运行                              -> 生成 artifacts\homework1\Homework1.zip
rem   scripts\package-homework1.cmd 张三     -> 生成 artifacts\homework1\张三_Homework1.zip
rem
rem 组装源（要改内容请改这些，不要改 artifacts 下的产物）：
rem   tutorials/polygon-scan/code/polygon_fill.cpp      命令行程序
rem   tutorials/polygon-scan/data/*.txt                 样例输入（凸/凹/非法）
rem   tutorials/polygon-scan/submit/                    包内 README / 实验报告 / 独立 CMakeLists
rem   src/（渲染库）+ tests/（单元测试）
rem 关联文档：tutorials/polygon-scan/README.md；scripts/README.md
rem ===========================================================================
setlocal enabledelayedexpansion
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "NODE=%ROOT%\tutorials\polygon-scan"
set "SUBMIT=%NODE%\submit"
set "STAGE=%ROOT%\artifacts\homework1\Homework1"
set "ZIPDIR=%ROOT%\artifacts\homework1"

set "NAME=%~1"
if "%NAME%"=="" (
    set "ZIP=%ZIPDIR%\Homework1.zip"
) else (
    set "ZIP=%ZIPDIR%\%NAME%_Homework1.zip"
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [错误] PATH 中找不到 cmake，请先安装 CMake 并加入 PATH。
    goto :fail
)

echo [1/6] 构建主工程 ...
cmake -S "%ROOT%" -B "%ROOT%\build" >nul
if errorlevel 1 goto :fail
cmake --build "%ROOT%\build" --target tutorial_polygon_fill
if errorlevel 1 goto :fail

echo.
echo [2/6] 组装包目录 ...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\app" "%STAGE%\src" "%STAGE%\tests" "%STAGE%\data" "%STAGE%\output" 2>nul
copy /y "%SUBMIT%\README.md"      "%STAGE%\README.md"       >nul || goto :fail
copy /y "%SUBMIT%\REPORT.md"      "%STAGE%\REPORT.md"       >nul || goto :fail
copy /y "%SUBMIT%\CMakeLists.txt" "%STAGE%\CMakeLists.txt"  >nul || goto :fail
copy /y "%SUBMIT%\tests\CMakeLists.txt" "%STAGE%\tests\CMakeLists.txt" >nul || goto :fail
copy /y "%NODE%\code\polygon_fill.cpp"  "%STAGE%\app\polygon_fill.cpp" >nul || goto :fail
copy /y "%NODE%\data\*.txt"             "%STAGE%\data\"       >nul || goto :fail
copy /y "%ROOT%\src\CMakeLists.txt"     "%STAGE%\src\"       >nul || goto :fail
copy /y "%ROOT%\src\window_win32.cpp"   "%STAGE%\src\"       >nul || goto :fail
xcopy /e /i /q /y "%ROOT%\src\include"  "%STAGE%\src\include\" >nul || goto :fail
copy /y "%ROOT%\tests\gfx_test.hpp"     "%STAGE%\tests\"     >nul || goto :fail
copy /y "%ROOT%\tests\test_raster.cpp"  "%STAGE%\tests\"     >nul || goto :fail
copy /y "%ROOT%\tests\test_png.cpp"     "%STAGE%\tests\"     >nul || goto :fail

echo [3/6] 干净目录构建验证（独立于主仓库）...
pushd "%STAGE%"
cmake -S . -B build >nul
if errorlevel 1 goto :fail
cmake --build build
if errorlevel 1 goto :fail
ctest --test-dir build --output-on-failure
if errorlevel 1 goto :fail

echo.
echo [4/6] 用包内程序生成结果图 ...
build\PolygonFill.exe --input data\convex-pentagon.txt        --output output\convex-pentagon.png
if errorlevel 1 goto :fail
build\PolygonFill.exe --input data\concave-arrow.txt          --output output\concave-arrow.png
if errorlevel 1 goto :fail
build\PolygonFill.exe --input data\convex-hexagon-closed.txt  --output output\convex-hexagon-closed.png
if errorlevel 1 goto :fail
popd

echo.
echo [5/6] 清理验证用的构建产物 ...
if exist "%STAGE%\build" rmdir /s /q "%STAGE%\build"

echo [6/6] 打包 ...
if exist "%ZIP%" del /q "%ZIP%"
rem 用 .NET ZipFile 直接压目录（比 Compress-Archive 更能给出明确退出码），并显式校验产物。
rem 注：若 IDE 索引器正占用 staging 下的文件，压缩会失败 —— 此时脚本必须报错而不是"假成功"。
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::CreateFromDirectory('%STAGE%', '%ZIP%')"
if errorlevel 1 goto :fail
if not exist "%ZIP%" (
    echo [失败] zip 未生成。
    goto :fail
)
for %%A in ("%ZIP%") do if %%~zA LSS 1000 (
    echo [失败] zip 体积异常（%%~zA 字节），可能未正确写入。
    goto :fail
)

echo.
echo [完成] 提交包：%ZIP%
echo         组装源在 tutorials\polygon-scan\（code / data / submit），改完重跑本脚本即可
echo. 
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [失败] 打包中止，请查看上面的输出。
pause
exit /b 1
