# 作业 1 提交包：多边形扫描转换（独立构建入口）
#
# 说明：本包为自包含的最小工程，不依赖仓库其它文件。
#   构建：cmake -S . -B build ; cmake --build build
#   测试：ctest --test-dir build --output-on-failure
#   运行：build/PolygonFill.exe --help

cmake_minimum_required(VERSION 3.16)

project(Homework1_PolygonScan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件与 gfx.dll 输出到同一目录，便于直接运行
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# 渲染库：gfx_math（INTERFACE，头文件子库）+ gfx（SHARED，窗口上屏）
add_subdirectory(src)

# 单元测试
enable_testing()
add_subdirectory(tests)

# 命令行程序：PolygonFill
add_executable(PolygonFill app/polygon_fill.cpp)
target_link_libraries(PolygonFill PRIVATE gfx)
