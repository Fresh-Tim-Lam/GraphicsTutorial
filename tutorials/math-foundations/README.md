# math-foundations —— 向量与矩阵：渲染的地基

> **用途**：讲解 gfx_math（Vec&lt;N&gt;/Mat&lt;N&gt; 与 vec2/3/4、mat2/3/4）为什么存在、怎么用，以及它和"把像素画到屏幕"的关系。
> **目标读者**：刚拿到像素画布、想继续做几何/变换的读者。
> **关联文档**：源码 [gfx/math/](../../src/include/gfx/math/)；ADR [adr-0001](../../docs/design/adr-0001-gfx-math-canvas.md)；测试 [test_math.cpp](../../tests/test_math.cpp)、[test_transform.cpp](../../tests/test_transform.cpp)。

> 状态：生效中（核验于 2026-09-09）。

## 一句话

后续画三角形、旋转/缩放物体、做 3D 投影，全靠向量和矩阵；这里先自己造一个最小可用的数学库。

## 内容

- 向量：点的位移/方向；`dot`（夹角/投影）、`cross`（法向/叉积）、长度与归一。
- 矩阵：变换"运算符"。行主序存储，`mat * vec` 表示"用矩阵变换向量"。
- 2D 平移需要 3x3 齐次坐标（`Vec3(x, y, 1)`）；3D 同理用 4x4（`Vec4(x,y,z,1)`）。
- 逆矩阵 = "撤销"变换；行列式非零才可逆（奇异即退化）。

## 常用变换（transform.hpp）

手写变换矩阵元素容易出错，库提供按"意图"构造的工厂函数（[transform.hpp](../../src/include/gfx/math/transform.hpp)）；矩阵本身也支持**按行书写**的字面量：

```cpp
math::Mat3 t{{1, 0, 10},      // 第 0 行
             {0, 1, 20},      // 第 1 行
             {0, 0,  1}};     // 第 2 行（行主序）
```

| 意图 | 2D（Mat3） | 3D（Mat4） |
|------|-----------|-----------|
| 平移 | `transform2d::translate(Vec2(tx,ty))` | `transform3d::translate(Vec3(x,y,z))` |
| 缩放 | `transform2d::scale(Vec2(sx,sy))` / `scale(s)` | `transform3d::scale(Vec3(sx,sy,sz))` / `scale(s)` |
| 旋转 | `transform2d::rotate(rad)` | `transform3d::rotate(rad, Axis::X/Y/Z)`、`rotate(rad, Vec3 轴)` |
| 绕点/绕轴旋转 | `transform2d::rotate(rad, Vec2 中心)` | `transform3d::rotate(rad, Vec3 轴, Vec3 中心)` |
| 翻转 | `transform2d::flipX()` / `flipY()` | `transform3d::flipX()` / `flipY()` / `flipZ()` |

组合顺序（两种口径，各在自己的类型上统一）：
- **矩阵**：`A * B` 表示**先作用 B 再作用 A**（数学复合），例如"先旋转再平移" = `translate(...) * rotate(...)`；
- **`Transform` 链**：`then()`、流式调用与 `a * b` 都是**书写顺序 = 作用顺序**（先 a 后 b），底层即"新步骤**左乘**"到已累积矩阵上。

**绕点旋转 = 基础变换的组合**（"移到原点 → 转 → 移回"）：

$$R_{\text{about }c}(\theta) = T(c) \cdot R(\theta) \cdot T(-c)$$

```cpp
math::Mat3 a = transform2d::rotate(math::Vec2(10, 20), rad);              // 闭式（生产路径）
math::Mat3 b = transform2d::rotateViaPrimitives(math::Vec2(10, 20), rad); // 组合版（教学等价）
```
> 闭式只是把上式展开（旋转部分不变、平移列 = c − R·c）；两者由 [test_transform.cpp](../../tests/test_transform.cpp) 逐元素对照锁定。3D 版多一个轴参数：`rotate(rad, axis, center)`。

## 换基视角：绕任意轴旋转是怎么来的

"绕任意轴 a 转 θ"并不是新公式，它就是**换基**（change of basis）——换到以 a 为坐标轴的新空间，做一次最平凡的轴旋转，再换回来：

$$R_a(\theta) = B \cdot R_z(\theta) \cdot B^{\mathsf T}$$

| 步骤 | 含义 | 代码 |
|------|------|------|
| ① 找新基 | 取 $u \perp a$、$v = a \times u$，组成右手正交基 $B=[u\mid v\mid a]$ | `frameFromAxis(axis, u, v, w)` |
| ② 换进 | $B^{\mathsf T}$（正交 ⇒ 逆 = 转置）把世界坐标送进新空间 | `b.transposed()` |
| ③ 基元旋转 | 新空间里 $a$ 就是 z 轴，只需绕 Z 轴转 | `rotate(θ, Axis::Z)` |
| ④ 换回 | $B$ 把结果送回世界坐标 | `b` |

```cpp
// 基矩阵：列 = 新空间的三根基向量
math::Mat4 b = transform3d::basis(u, v, w);          // B = [u | v | a]

// 基础变换组合版：换基 + 基元旋转 + 换回（教学等价实现）
math::Mat4 r = transform3d::rotateViaBasis(rad, axis);   // = B · Rz · Bᵀ

// 生产路径用闭式（Rodrigues，省两次 4×4 乘法），两者数值等价
math::Mat4 rc = transform3d::rotate(rad, axis);
```
> 对照验证见 [test_transform.cpp](../../tests/test_transform.cpp)：`rotateViaBasis` 与闭式 `rotate` 在多组轴/角度下逐元素相等。

展开 $BR_zB^{\mathsf T}$ 并利用 $uu^{\mathsf T}+vv^{\mathsf T}=I-aa^{\mathsf T}$、$vu^{\mathsf T}-uv^{\mathsf T}=[a]_\times$，即得 Rodrigues 闭式 $R = cI + (1-c)aa^{\mathsf T} + s[a]_\times$——**为什么旋转矩阵长成那样，答案就在"换基"里**。注意其中 $u,v$ 完全消掉了：任意垂直基的选择结果相同，这正是旋转只依赖 $a$ 与 $\theta$ 的原因。

## 连续变换：变换链与"点 apply 变换"

逐个矩阵相乘会立刻做完整矩阵乘（写 `T*R*S*p` 时甚至先算两次矩阵乘）。库提供**链对象**，把连续变换记录下来、延迟到消费点求值（单点走逐次 mat-vec、批点先折叠再一次算），详见 [ADR-0004](../../docs/design/adr-0004-transform-chain.md)。

**math 层（可存储的变换）**
```cpp
math::Transform<3> t;                                  // 3 对应 2D 齐次（Vec3）
t.then(transform2d::translate(math::Vec2(10, 20)))
 .then(transform2d::rotate(pi / 2));                   // 书写顺序 = 作用顺序

math::Vec3 p(1, 0, 1);
math::Vec3 o = t.apply(p);                             // 单点：内部即"单矩阵 apply"（折叠结果被缓存）
math::Mat3 m = t.matrix();                              // 需要矩阵时取折叠结果（惰性 + 缓存）

// 组合：`a * b` 与 then() 同口径（先 a 后 b）——层级变换 = 先局部、再父级
math::Transform<3> world = local;
world.then(parent);                                     // 等价写法：world = local * parent
```

**gfx 层（几何数据 apply 变换）**——见 [point.hpp](../../src/include/gfx/point.hpp)
```cpp
gfx::Point2D q(1, 2);
math::Vec2 r = q.translate(10, 0).rotate(pi / 2).value();   // 流畅链
math::Vec2 s = q.apply(savedTransform);                     // 存好的变换直接 apply

math::Vec2 pts[1000];                                       // 点批：视图零拷贝
math::Vec2 out[1000];
gfx::PointBatch2D batch(pts, 1000);
batch.translate(10, 0).rotate(pi / 2);
batch.apply(out);                                           // 折叠一次 + 批量
```
> AoS 交错布局（顶点还带其它字段）用带步长视图：`PointView2D(&verts[0].pos, n, sizeof(Vertex))`。

## 权威实现

`src/include/gfx/math/`（header-only，零依赖）。行为验证：`ctest -R test_math`、`ctest -R test_transform`。

## code/（演示）

- [demo_math.cpp](code/demo_math.cpp)：用库内变换验证"先旋转 90° 再平移 (10,20)"、逆变换还原、左右翻转，打印结果对照数学推导。
