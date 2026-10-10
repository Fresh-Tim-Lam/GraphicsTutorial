@echo off
chcp 65001 >nul
rem ============================================================================
rem package-homework3.cmd —— 组装「作业三 光线跟踪」提交包
rem   包内容：
rem     main.cpp / RayTracer.cpp / RayTracer.h   课程框架内我们修改的源码
rem     HW3.exe                                  预编译可执行文件（免编译直接运行）
rem     opencv_world4120.dll + VC 运行库           运行时依赖（分发后免装环境即可运行 HW3.exe）
rem     使用说明.md                               怎么运行 / 怎么重新编译 / 实现说明（作业要求"说明如何使用"）
rem     gfx-results\                              自研渲染引擎 gfx（附加内容）：
rem        include/ + code/ + CMakeLists.txt        引擎头文件 + 演示源码 + 独立构建脚本
rem        bin/GfxHw3Scene.exe / GfxRayTracing.exe  免依赖可直接运行的 exe（静态链接）
rem        bin/GfxRayTracingInteractive.exe          光线跟踪：窗口内实时调相机
rem        bin/GfxShadedRaster.exe                   光栅化着色管线（展厅：程序化贴图 + 阴影贴图）
rem        bin/GfxShadedRasterInteractive.exe        光栅化着色管线：窗口内实时调相机（双击即用）
rem        results/                                渲染结果图
rem   说明：作业三框架是 VS2019 + Eigen + OpenCV，框架部分**不重复触发 MSVC 构建**，
rem         直接复用已构建好的 artifacts\homework3\bin\HW4.exe（框架 Release 工程 TargetName=HW4）；
rem         gfx-results 的自研引擎用 CMake 现场构建（静态链接，零 DLL 依赖）。
rem   用法：scripts\package-homework3.cmd [姓名]
rem   产物：artifacts\homework3\<姓名>_HW3.zip；不带参数为 artifacts\homework3\学生_HW3.zip
rem   组装源（要改包内容请改这些，不要改 artifacts 下的产物）：
rem     artifacts\homework3\HW3\{main.cpp,RayTracer.cpp,RayTracer.h}
rem     artifacts\homework3\bin\{HW4.exe,opencv_world4120.dll}
rem     tutorials\ray-tracing-framework-scene\{submit,code,output} + tutorials\ray-tracing\{code,output} + src\include\gfx
rem ============================================================================

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "SRCDIR=%ROOT%\artifacts\homework3"
set "NAME=%~1"
if "%NAME%"=="" set "NAME=学生"
set "PKGDIR=%SRCDIR%\%NAME%_HW3"
set "ZIP=%SRCDIR%\%NAME%_HW3.zip"
set "GFXDIR=%PKGDIR%\gfx-results"
set "GFXBLD=%SRCDIR%\_gfxbuild"

rem 预编译可执行文件：框架 Release 工程 TargetName=HW4，故构建产物名为 HW4.exe
set "EXE="
if exist "%SRCDIR%\bin\HW3.exe" set "EXE=%SRCDIR%\bin\HW3.exe"
if not defined EXE if exist "%SRCDIR%\bin\HW4.exe" set "EXE=%SRCDIR%\bin\HW4.exe"
if not defined EXE (
    echo [错误] 找不到预编译可执行文件：%SRCDIR%\bin\HW3.exe 或 HW4.exe
    echo         请先在 Visual Studio 中构建 Release 工程，构建产物应位于本目录的 bin 子目录。
    goto :fail
)
set "OCVDLL=%SRCDIR%\bin\opencv_world4120.dll"
if not exist "%OCVDLL%" (
    echo [错误] 找不到 OpenCV 运行库：%OCVDLL%
    goto :fail
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo [错误] PATH 中找不到 cmake，构建 gfx-results\bin 下的 exe 需要 CMake。
    goto :fail
)

echo [1/6] 组装作业源码与可执行文件：%PKGDIR%
if exist "%PKGDIR%" rmdir /s /q "%PKGDIR%"
mkdir "%GFXDIR%\include" "%GFXDIR%\code" "%GFXDIR%\results" 2>nul
copy /y "%SRCDIR%\HW3\main.cpp"      "%PKGDIR%\" >nul || goto :fail
copy /y "%SRCDIR%\HW3\RayTracer.cpp" "%PKGDIR%\" >nul || goto :fail
copy /y "%SRCDIR%\HW3\RayTracer.h"   "%PKGDIR%\" >nul || goto :fail
copy /y "%EXE%"                      "%PKGDIR%\HW3.exe" >nul || goto :fail
rem 使用说明（作业要求"对自己写的代码加上适当的注释，并说明如何使用"）
copy /y "%ROOT%\tutorials\ray-tracing-framework-scene\submit\使用说明.md" "%PKGDIR%\使用说明.md" >nul || goto :fail

echo [2/6] 收集运行时依赖（分发后免装环境即可运行）
copy /y "%OCVDLL%" "%PKGDIR%\opencv_world4120.dll" >nul || goto :fail
copy /y "%SystemRoot%\System32\vcruntime140.dll"   "%PKGDIR%\vcruntime140.dll"   >nul || goto :fail
copy /y "%SystemRoot%\System32\vcruntime140_1.dll" "%PKGDIR%\vcruntime140_1.dll" >nul || goto :fail
copy /y "%SystemRoot%\System32\msvcp140.dll"       "%PKGDIR%\msvcp140.dll"       >nul || goto :fail

echo [3/6] 收集自研渲染引擎 gfx（引擎头文件 + 演示源码 + 构建脚本 + 结果图）
xcopy /y /e /i /q "%ROOT%\src\include\gfx" "%GFXDIR%\include\gfx\" >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing-framework-scene\submit\CMakeLists.txt"     "%GFXDIR%\CMakeLists.txt" >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing-framework-scene\submit\README.md"          "%GFXDIR%\README.md"      >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing-framework-scene\code\hw3_scene_trace.cpp" "%GFXDIR%\code\hw3_scene_trace.cpp" >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing\code\ray_tracing.cpp"                     "%GFXDIR%\code\ray_tracing.cpp"     >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing\code\ray_tracing_scene.hpp"               "%GFXDIR%\code\ray_tracing_scene.hpp"       >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing\code\ray_tracing_interactive.cpp"         "%GFXDIR%\code\ray_tracing_interactive.cpp" >nul || goto :fail
rem 光栅化着色管线（展厅场景：程序化颜色/法线贴图 + 阴影贴图）—— 离线出图版 + 窗口交互版
copy /y "%ROOT%\tutorials\shaded-raster\code\shaded_raster.cpp"             "%GFXDIR%\code\shaded_raster.cpp"             >nul || goto :fail
copy /y "%ROOT%\tutorials\shaded-raster\code\shaded_raster_interactive.cpp" "%GFXDIR%\code\shaded_raster_interactive.cpp" >nul || goto :fail
copy /y "%ROOT%\tutorials\shaded-raster\code\shaded_raster_scene.hpp"       "%GFXDIR%\code\shaded_raster_scene.hpp"       >nul || goto :fail
rem 窗口交互演示需要 gfx 的 Win32 窗口实现；直接编进 exe ⇒ 仍然零 DLL 依赖
copy /y "%ROOT%\src\window_win32.cpp"                                           "%GFXDIR%\code\window_win32.cpp"            >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing-framework-scene\output\hw3_gfx_result.png" "%GFXDIR%\results\gfx_engine_hw3_scene.png"    >nul || goto :fail
copy /y "%ROOT%\tutorials\ray-tracing\output\ray-tracing.png"                    "%GFXDIR%\results\gfx_engine_custom_scene.png" >nul || goto :fail
copy /y "%ROOT%\tutorials\shaded-raster\output\shaded-raster.png"                 "%GFXDIR%\results\gfx_engine_shaded_raster.png"           >nul || goto :fail
copy /y "%ROOT%\tutorials\shaded-raster\output\shaded-raster-no-shadow.png"       "%GFXDIR%\results\gfx_engine_shaded_raster_no_shadow.png" >nul || goto :fail

echo [4/6] 构建 gfx-results 的可执行文件（静态链接，零 DLL 依赖）
rem Ninja 无法 chdir 到含中文的路径 ⇒ 构建放到纯 ASCII 的临时目录
if exist "%GFXBLD%" rmdir /s /q "%GFXBLD%"
xcopy /y /e /i /q "%GFXDIR%" "%GFXBLD%" >nul || goto :fail
pushd "%GFXBLD%"
cmake -S . -B build >nul
if errorlevel 1 goto :failpop
cmake --build build --config Release >nul
if errorlevel 1 goto :failpop
popd
mkdir "%GFXDIR%\bin" 2>nul
copy /y "%GFXBLD%\build\GfxHw3Scene.exe"   "%GFXDIR%\bin\GfxHw3Scene.exe"   >nul || goto :fail
copy /y "%GFXBLD%\build\GfxRayTracing.exe" "%GFXDIR%\bin\GfxRayTracing.exe" >nul || goto :fail
copy /y "%GFXBLD%\build\GfxRayTracingInteractive.exe" "%GFXDIR%\bin\GfxRayTracingInteractive.exe" >nul || goto :fail
copy /y "%GFXBLD%\build\GfxShadedRaster.exe"             "%GFXDIR%\bin\GfxShadedRaster.exe"             >nul || goto :fail
copy /y "%GFXBLD%\build\GfxShadedRasterInteractive.exe"  "%GFXDIR%\bin\GfxShadedRasterInteractive.exe"  >nul || goto :fail
rmdir /s /q "%GFXBLD%" 2>nul

echo [5/6] 校验包内容
if not exist "%PKGDIR%\HW3.exe"              goto :fail
if not exist "%PKGDIR%\使用说明.md"          goto :fail
if not exist "%PKGDIR%\main.cpp"             goto :fail
if not exist "%PKGDIR%\RayTracer.cpp"        goto :fail
if not exist "%PKGDIR%\RayTracer.h"          goto :fail
if not exist "%PKGDIR%\opencv_world4120.dll" goto :fail
if not exist "%PKGDIR%\vcruntime140.dll"     goto :fail
if not exist "%PKGDIR%\vcruntime140_1.dll"   goto :fail
if not exist "%PKGDIR%\msvcp140.dll"         goto :fail
if not exist "%GFXDIR%\CMakeLists.txt"                  goto :fail
if not exist "%GFXDIR%\README.md"                       goto :fail
if not exist "%GFXDIR%\include\gfx\render\raytracer.hpp" goto :fail
if not exist "%GFXDIR%\code\hw3_scene_trace.cpp"        goto :fail
if not exist "%GFXDIR%\code\ray_tracing.cpp"            goto :fail
if not exist "%GFXDIR%\code\ray_tracing_scene.hpp"      goto :fail
if not exist "%GFXDIR%\code\ray_tracing_interactive.cpp" goto :fail
if not exist "%GFXDIR%\code\shaded_raster.cpp"           goto :fail
if not exist "%GFXDIR%\code\shaded_raster_interactive.cpp" goto :fail
if not exist "%GFXDIR%\code\shaded_raster_scene.hpp"     goto :fail
if not exist "%GFXDIR%\include\gfx\render\pattern.hpp"   goto :fail
if not exist "%GFXDIR%\code\window_win32.cpp"           goto :fail
if not exist "%GFXDIR%\bin\GfxHw3Scene.exe"             goto :fail
if not exist "%GFXDIR%\bin\GfxRayTracing.exe"           goto :fail
if not exist "%GFXDIR%\bin\GfxRayTracingInteractive.exe" goto :fail
if not exist "%GFXDIR%\bin\GfxShadedRaster.exe"              goto :fail
if not exist "%GFXDIR%\bin\GfxShadedRasterInteractive.exe"   goto :fail
if not exist "%GFXDIR%\results\gfx_engine_hw3_scene.png"    goto :fail
if not exist "%GFXDIR%\results\gfx_engine_custom_scene.png" goto :fail
if not exist "%GFXDIR%\results\gfx_engine_shaded_raster.png" goto :fail

echo [6/6] 打包
if exist "%ZIP%" del /q "%ZIP%"
rem 用 .NET ZipFile 直接压目录（比 Compress-Archive 更能给出明确退出码），并显式校验产物。
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::CreateFromDirectory('%PKGDIR%', '%ZIP%')"
if errorlevel 1 goto :fail
if not exist "%ZIP%" goto :fail
for %%A in ("%ZIP%") do if %%~zA LSS 1000 (
    echo [失败] zip 体积异常，可能未正确写入。
    goto :fail
)
for %%A in ("%ZIP%") do echo [完成] 提交包：%ZIP%   %%~zA 字节
echo         HW3.exe 已附带 OpenCV 与 VC 运行库，分发后可直接运行
echo         gfx-results\ 内为自研渲染引擎 gfx（引擎 + 源码 + CMakeLists + 可直接运行的 5 个 exe + 结果图）
echo         bin 里的可交互 exe：GfxRayTracingInteractive.exe / GfxShadedRasterInteractive.exe（双击即用）
exit /b 0

:failpop
popd
:fail
echo [失败] 打包中止（详见上方输出）。
exit /b 1
