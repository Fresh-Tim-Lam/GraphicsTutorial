# src/CMakeLists.txt —— gfx 库与 gfx_math 头文件子库定义
#
# 版本号唯一事实源：include/gfx/core/version.hpp（本文件解析其宏，禁止硬编码，见 docs/conventions/version.md）

# ---------- 从 version.hpp 解析版本号 ----------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/gfx/core/version.hpp" GFX_VERSION_HEADER)
foreach(COMPONENT MAJOR MINOR PATCH)
    if(GFX_VERSION_HEADER MATCHES "GFX_VERSION_${COMPONENT} ([0-9]+)")
        set(GFX_VERSION_${COMPONENT} "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR "src/include/gfx/core/version.hpp 缺少 GFX_VERSION_${COMPONENT}")
    endif()
endforeach()
set(GFX_VERSION "${GFX_VERSION_MAJOR}.${GFX_VERSION_MINOR}.${GFX_VERSION_PATCH}")
message(STATUS "gfx version (from version.hpp): ${GFX_VERSION}")

# ---------- gfx_math：独立头文件子库（INTERFACE，无编译产物） ----------
add_library(gfx_math INTERFACE)
target_include_directories(gfx_math INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gfx_math INTERFACE cxx_std_17)

# ---------- gfx：主渲染动态库（DLL + 导入库 + 头文件） ----------
add_library(gfx SHARED
    include/gfx/core/core.hpp
    include/gfx/core/canvas.hpp
    include/gfx/core/color.hpp
    include/gfx/core/gfx_export.hpp
    include/gfx/core/version.hpp
    include/gfx/geom/geom.hpp
    include/gfx/geom/point.hpp
    include/gfx/geom/halfedge.hpp
    include/gfx/geom/ray.hpp
    include/gfx/geom/bvh.hpp
    include/gfx/render/render.hpp
    include/gfx/render/raster.hpp
    include/gfx/render/triangle.hpp
    include/gfx/render/depth.hpp
    include/gfx/render/view.hpp
    include/gfx/render/png.hpp
    include/gfx/render/window.hpp
    include/gfx/render/lighting.hpp
    include/gfx/render/raytracer.hpp
    include/gfx/render/texture.hpp
    include/gfx/render/normalmap.hpp
    include/gfx/render/shading.hpp
    include/gfx/render/shadowmap.hpp
    include/gfx/render/pattern.hpp
    window_win32.cpp)

target_include_directories(gfx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(gfx
    PUBLIC gfx_math
    PRIVATE user32 gdi32)   # Win32 GDI（用户输入/窗口/绘图）；当前阶段平台依赖集中在 gfx 实现

target_compile_features(gfx PUBLIC cxx_std_17)

# 构建 gfx.dll 时导出公共符号（见 gfx_export.hpp）
target_compile_definitions(gfx PRIVATE GFX_BUILDING_DLL)

set_target_properties(gfx PROPERTIES
    VERSION ${GFX_VERSION}
    SOVERSION ${GFX_VERSION_MAJOR}
    OUTPUT_NAME gfx)
