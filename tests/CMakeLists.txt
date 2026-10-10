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

# ---------- 相机/视图测试（世界↔像素映射、fit 语义、3D 观察与投影） ----------
add_executable(test_view test_view.cpp)
target_link_libraries(test_view PRIVATE gfx_math)
add_test(NAME test_view COMMAND test_view)

# ---------- 三角形光栅化测试（边界规则、重心坐标、抗锯齿、深度测试） ----------
add_executable(test_triangle test_triangle.cpp)
target_link_libraries(test_triangle PRIVATE gfx_math)
add_test(NAME test_triangle COMMAND test_triangle)

# ---------- 半边结构与欧拉操作测试（MVFS/MEV/MEF/KEMR/KFMRH） ----------
add_executable(test_halfedge test_halfedge.cpp)
target_link_libraries(test_halfedge PRIVATE gfx_math)
add_test(NAME test_halfedge COMMAND test_halfedge)

# ---------- 光线求交测试（反射/折射/球/平面/三角形/圆柱/AABB + BVH 一致性） ----------
add_executable(test_ray test_ray.cpp)
target_link_libraries(test_ray PRIVATE gfx_math)
add_test(NAME test_ray COMMAND test_ray)

# ---------- 光照模型与递归光线跟踪测试（局部光照/阴影/反射/折射/深度/超采样 + 作业三数值对齐） ----------
add_executable(test_raytracer test_raytracer.cpp)
target_link_libraries(test_raytracer PRIVATE gfx_math)
add_test(NAME test_raytracer COMMAND test_raytracer)

# ---------- 纹理与法线贴图测试（环绕/滤波采样、TBN、高度场→法线图、着色法向接口） ----------
add_executable(test_normalmap test_normalmap.cpp)
target_link_libraries(test_normalmap PRIVATE gfx_math)
add_test(NAME test_normalmap COMMAND test_normalmap)

# ---------- 光栅化着色管线测试（透视校正插值、覆盖一致性、深度测试、近平面裁剪、属性透传） ----------
add_executable(test_shading test_shading.cpp)
target_link_libraries(test_shading PRIVATE gfx_math)
add_test(NAME test_shading COMMAND test_shading)

# ---------- 阴影贴图测试（与解析求交一致、光源后方/视锥外、无自阴影 acne、PCF 量化） ----------
add_executable(test_shadowmap test_shadowmap.cpp)
target_link_libraries(test_shadowmap PRIVATE gfx_math)
add_test(NAME test_shadowmap COMMAND test_shadowmap)
