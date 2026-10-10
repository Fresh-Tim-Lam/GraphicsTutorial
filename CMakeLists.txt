# ============================================================================
# GraphicsTutorial 顶层 CMake 构建入口（唯一事实源见 docs/conventions/version.md）
# 仅承载目标组织与通用配置；各目标实现随模块演进注册到 src/CMakeLists.txt
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(GraphicsTutorial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 版本号唯一事实源：src/include/gfx/core/version.hpp（随首个模块创建，禁止在此硬编码）
# 版本策略见 docs/conventions/version.md

# 主渲染库 gfx（动态库 + 头文件分发）
add_subdirectory(src)

# 测试（随 tests/ 演进后启用）
enable_testing()
add_subdirectory(tests)

# 教学演示（教程 code，随教学树登记）
add_subdirectory(tutorials)
