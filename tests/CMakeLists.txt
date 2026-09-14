# tests/CMakeLists.txt —— 测试目标（自写轻量断言，零第三方依赖）
#
# 测试框架：tests/gfx_test.hpp（自写断言宏）。运行：ctest --test-dir build -C Release

# ---------- gfx_math 测试（纯头文件子库） ----------
add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE gfx_math)
add_test(NAME test_math COMMAND test_math)

# ---------- gfx 像素模块测试（Canvas 为头文件实现，可离屏验证） ----------
add_executable(test_canvas test_canvas.cpp)
target_link_libraries(test_canvas PRIVATE gfx_math)
add_test(NAME test_canvas COMMAND test_canvas)

# ---------- 批量变换/后端钩子测试 ----------
add_executable(test_batch test_batch.cpp)
target_link_libraries(test_batch PRIVATE gfx_math)
add_test(NAME test_batch COMMAND test_batch)

# ---------- 常用变换矩阵测试 ----------
add_executable(test_transform test_transform.cpp)
target_link_libraries(test_transform PRIVATE gfx_math)
add_test(NAME test_transform COMMAND test_transform)

# ---------- 变换链测试（math::Transform） ----------
add_executable(test_chain test_chain.cpp)
target_link_libraries(test_chain PRIVATE gfx_math)
add_test(NAME test_chain COMMAND test_chain)

# ---------- 几何层点/点批测试（gfx::Point* / PointBatch*） ----------
add_executable(test_point test_point.cpp)
target_link_libraries(test_point PRIVATE gfx_math)
add_test(NAME test_point COMMAND test_point)

# ---------- 光栅化测试（画线 + 扫描线多边形填充，Canvas 为头文件实现） ----------
add_executable(test_raster test_raster.cpp)
target_link_libraries(test_raster PRIVATE gfx_math)
add_test(NAME test_raster COMMAND test_raster)

# ---------- PNG 编码器测试（自写编码器：CRC32/Adler32/块结构） ----------
add_executable(test_png test_png.cpp)
target_link_libraries(test_png PRIVATE gfx_math)
add_test(NAME test_png COMMAND test_png)
