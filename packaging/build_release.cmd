@echo off
rem ===========================================================================
rem GraphicsTutorial single packaging entry: build_release.cmd
rem Purpose: build Release gfx library, produce packaging/release/ for consumers.
rem Audience: release maintainers / AI Agent (Ask First boundary applies).
rem See: packaging/README.md, docs/conventions/version.md, docs/guides/operations.md
rem Status: active (2026-09-09). Handles MinGW naming (libgfx.dll / libgfx.dll.a).
rem ===========================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build"
set "REL_DIR=%~dp0release"
set "SRC_INC=%ROOT%\src\include"

echo [packaging] 1/3 configure + build Release ...
cmake -S "%ROOT%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release >nul
if errorlevel 1 goto :fail
cmake --build "%BUILD_DIR%" --config Release >nul
if errorlevel 1 goto :fail

echo [packaging] 2/3 prepare release tree ...
if exist "%REL_DIR%" rmdir /s /q "%REL_DIR%"
mkdir "%REL_DIR%\include" "%REL_DIR%\lib" "%REL_DIR%\bin"

rem ---- include: all public headers ----
xcopy /e /i /q /y "%SRC_INC%" "%REL_DIR%\include" >nul
if errorlevel 1 goto :fail

rem ---- bin: shared library (gfx.dll or libgfx.dll) ----
set "DLL="
if exist "%BUILD_DIR%\src\gfx.dll"      set "DLL=%BUILD_DIR%\src\gfx.dll"
if exist "%BUILD_DIR%\src\libgfx.dll"   set "DLL=%BUILD_DIR%\src\libgfx.dll"
if not defined DLL (
    echo [packaging] ERROR: gfx.dll not found under %BUILD_DIR%\src
    goto :fail
)
copy /y "!DLL!" "%REL_DIR%\bin\" >nul

rem ---- lib: import library (MinGW: libgfx.dll.a; MSVC: gfx.lib) ----
set "IMP="
if exist "%BUILD_DIR%\src\gfx.lib"      set "IMP=%BUILD_DIR%\src\gfx.lib"
if exist "%BUILD_DIR%\src\libgfx.dll.a" set "IMP=%BUILD_DIR%\src\libgfx.dll.a"
if defined IMP copy /y "!IMP!" "%REL_DIR%\lib\" >nul

echo [packaging] 3/3 done. Release dir: %REL_DIR%
dir /b "%REL_DIR%" "%REL_DIR%\include" "%REL_DIR%\lib" "%REL_DIR%\bin"
exit /b 0

:fail
echo [packaging] FAILED. Check output above.
exit /b 1
