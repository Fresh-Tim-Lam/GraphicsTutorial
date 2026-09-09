# gfx 渲染库目标（骨架，随首个模块落地细化）
# 版本号不在此硬编码：随首个模块创建 src/include/gfx/version.hpp 后，
# 通过 configure_file/读取方式引用（见 docs/conventions/version.md）。

# 待细化：首个模块（如 window / 屏幕像素输出）落地后，在此定义：
#   add_library(gfx SHARED ...)
#   target_include_directories(gfx PUBLIC include)
# 并设置导出宏、输出目录（lib/bin），供 packaging 打包。
message(STATUS "src/CMakeLists.txt: gfx 目标骨架已就位，待首个模块添加源文件。")
