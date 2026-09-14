# tests/CMakeLists.txt —— 作业 1 提交包内的测试目标（自写轻量断言，零第三方依赖）

# ---------- 光栅化测试（画线 + 扫描线多边形填充） ----------
add_executable(test_raster test_raster.cpp)
target_link_libraries(test_raster PRIVATE gfx_math)
add_test(NAME test_raster COMMAND test_raster)

# ---------- PNG 编码器测试（自写编码器：CRC32/Adler32/块结构） ----------
add_executable(test_png test_png.cpp)
target_link_libraries(test_png PRIVATE gfx_math)
add_test(NAME test_png COMMAND test_png)
