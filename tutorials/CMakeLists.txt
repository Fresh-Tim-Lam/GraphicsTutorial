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
