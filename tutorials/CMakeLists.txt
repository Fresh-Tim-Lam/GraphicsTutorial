# tutorials/CMakeLists.txt —— 教学演示目标（随教学树生长而登记）
#
# 规则（见 tutorials/README.md）：教学 code 为演示/练习用途；权威实现归属 src/。
# 产物统一输出到 build/tutorials/，并把 gfx.dll 拷贝到同目录便于直接运行。

set(TUTORIAL_BIN_DIR "${CMAKE_BINARY_DIR}/tutorials")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TUTORIAL_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${TUTORIAL_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${TUTORIAL_BIN_DIR}")

# ---------- 数学演示 ----------
add_executable(tutorial_math math-foundations/code/demo_math.cpp)
target_link_libraries(tutorial_math PRIVATE gfx_math)

# ---------- 像素上屏演示 ----------
add_executable(tutorial_pixels pixels-on-screen/code/demo_window.cpp)
target_link_libraries(tutorial_pixels PRIVATE gfx)

# 运行 tutorial_pixels 时需要 gfx.dll 在可执行文件旁
add_custom_command(TARGET tutorial_pixels POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_pixels>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 多边形扫描转换演示（作业 1） ----------
# 产物名与作业要求的命令行示例一致：PolygonFill
add_executable(tutorial_polygon_fill polygon-scan/code/polygon_fill.cpp)
target_link_libraries(tutorial_polygon_fill PRIVATE gfx)
set_target_properties(tutorial_polygon_fill PROPERTIES OUTPUT_NAME PolygonFill)

add_custom_command(TARGET tutorial_polygon_fill POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_polygon_fill>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 正交投影 vs 透视投影演示（3D 线框，同框对比） ----------
add_executable(tutorial_projection_3d projection-3d/code/projection_demo.cpp)
target_link_libraries(tutorial_projection_3d PRIVATE gfx)
set_target_properties(tutorial_projection_3d PROPERTIES OUTPUT_NAME Projection3D)

add_custom_command(TARGET tutorial_projection_3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_projection_3d>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 三角形光栅化演示（点采样 vs 抗锯齿 + 深度测试） ----------
add_executable(tutorial_triangle triangle-raster/code/triangle_demo.cpp)
target_link_libraries(tutorial_triangle PRIVATE gfx)
set_target_properties(tutorial_triangle PROPERTIES OUTPUT_NAME TriangleRaster)

add_custom_command(TARGET tutorial_triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_triangle>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 欧拉操作演示（半边结构：MVFS/MEV/MEF/KEMR/KFMRH） ----------
add_executable(tutorial_euler euler-ops/code/euler_demo.cpp)
target_link_libraries(tutorial_euler PRIVATE gfx)
set_target_properties(tutorial_euler PROPERTIES OUTPUT_NAME EulerOps)

add_custom_command(TARGET tutorial_euler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_euler>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 欧拉操作交互演示（立方体掏空：单步 + 拖拽旋转 + 滚轮缩放） ----------
add_executable(tutorial_euler_interactive euler-ops/code/euler_interactive.cpp)
target_link_libraries(tutorial_euler_interactive PRIVATE gfx)
set_target_properties(tutorial_euler_interactive PROPERTIES OUTPUT_NAME EulerInteractive)

add_custom_command(TARGET tutorial_euler_interactive POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_euler_interactive>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 三种画直线算法对照（Naive / DDA / Bresenham：并排成图 + 像素统计） ----------
add_executable(tutorial_line_algorithms line-algorithms/code/line_algorithms.cpp)
target_link_libraries(tutorial_line_algorithms PRIVATE gfx)
set_target_properties(tutorial_line_algorithms PROPERTIES OUTPUT_NAME LineAlgorithms)

add_custom_command(TARGET tutorial_line_algorithms POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_line_algorithms>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")

# ---------- 光线求交地基演示（法向可视化：只用求交 + BVH，无光照；纯头文件，无需 gfx.dll） ----------
add_executable(tutorial_ray_basics ray-basics/code/ray_basics.cpp)
target_link_libraries(tutorial_ray_basics PRIVATE gfx_math)
set_target_properties(tutorial_ray_basics PROPERTIES OUTPUT_NAME RayBasics)

# ---------- 光线跟踪演示（光照 + 反射/折射 + 阴影 + BVH + 超采样，用 gfx 自己渲染；纯头文件，无需 gfx.dll） ----------
add_executable(tutorial_ray_tracing ray-tracing/code/ray_tracing.cpp)
target_link_libraries(tutorial_ray_tracing PRIVATE gfx_math)
set_target_properties(tutorial_ray_tracing PROPERTIES OUTPUT_NAME RayTracing)

# ---------- 课程框架（作业三）原始场景的 gfx 等价性复刻（与框架基准图逐像素比对；纯头文件，无需 gfx.dll） ----------
add_executable(tutorial_hw3_scene ray-tracing-framework-scene/code/hw3_scene_trace.cpp)
target_link_libraries(tutorial_hw3_scene PRIVATE gfx_math)
set_target_properties(tutorial_hw3_scene PROPERTIES OUTPUT_NAME Hw3SceneTrace)

# ---------- 法线贴图演示（光线跟踪路径：Texture + TBN + 程序化法线图；纯头文件，无需 gfx.dll） ----------
add_executable(tutorial_normal_mapping normal-mapping/code/normal_mapping.cpp)
target_link_libraries(tutorial_normal_mapping PRIVATE gfx_math)
set_target_properties(tutorial_normal_mapping PROPERTIES OUTPUT_NAME NormalMapping)

# ---------- 光栅化着色管线演示（顶点属性 + 透视校正插值 + 逐像素着色；复用 texture/normalmap） ----------
add_executable(tutorial_shaded_raster shaded-raster/code/shaded_raster.cpp)
target_link_libraries(tutorial_shaded_raster PRIVATE gfx_math)
set_target_properties(tutorial_shaded_raster PROPERTIES OUTPUT_NAME ShadedRaster)

# ---------- 光线跟踪交互演示（窗口内实时调相机：拖动 / WASD / 滚轮；需要 gfx.dll） ----------
add_executable(tutorial_ray_tracing_interactive ray-tracing/code/ray_tracing_interactive.cpp)
target_link_libraries(tutorial_ray_tracing_interactive PRIVATE gfx)
set_target_properties(tutorial_ray_tracing_interactive PROPERTIES OUTPUT_NAME RayTracingInteractive)

add_custom_command(TARGET tutorial_ray_tracing_interactive POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gfx>
        "$<TARGET_FILE_DIR:tutorial_ray_tracing_interactive>/"
    COMMENT "将 gfx.dll 拷贝到演示目录")
