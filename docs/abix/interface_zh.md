# ABIX Interface — 非侵入式多态

<p align="center">
  <a href="interface.md">English</a> · 中文
</p>

ABIX Interface 层定义**跨语言接口契约**与**方法分派**机制。它是 ABIX 的一层，而不是任何编译器对象模型的一部分：接口是一份独立契约，实现方类型不需要为它做任何修改。

它位于 `SKL/ABIX/Interface/`（命名空间 `skl::abix::poly`），由 `SKL/ABIX/ABIX.h` 暴露。

## 为什么统一 Interface，而不是统一 vtable

C++ 虚调用并不只是从对象取出一个函数指针：它可能涉及多重继承、`this` 调整、thunk、RTTI 以及编译器私有的 vtable 布局。Rust trait object、Zig 函数表又是另一种表示。即使两边都叫 “vtable”，表项顺序、指针调整和调用规则也可能不同。

因此 ABIX **不规定任何私有 vtable**，而是在 vtable 之上定义接口：

- **Interface 身份** —— 接口名、版本与有序方法签名的稳定内容哈希。不是类名，也不是 vtable 地址。
- **方法身份** —— `(interface_id, slot)`，方法签名独立校验；名称或形状相同并不等于两个接口兼容。
- **实现绑定** —— 具体类型如何到达某个方法，通过生成的适配器完成；适配器以对象句柄为第一个参数，绑定按签名命名，从不暴露 vtable 槽位。

这些是**契约而非布局事实**：它们不会改变 `TypeID` / `LayoutHash` / `ABIHash`。接口也可以写入 Metadata Region（扩展 kind 7–11），供消费端在运行时发现。

## 与 C++ 虚函数对比

同一件事——三个形状各自算面积，再对一组形状做多态调用——两种写法。左栏是 C++ 原生虚函数，右栏是 ABIX Interface；两段程序都输出 `area=9`、`area=12`、`area=10`，都能直接编译运行。

<table>
<tr>
<th align="left">C++ 虚函数（侵入式）</th>
<th align="left">ABIX Interface（非侵入式）</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include <cstdio>

struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Rectangle : Shape {
    Rectangle(int w, int h) : width(w), height(h) {}
    int area() const override { return width * height; }
    int width, height;
};
struct Triangle : Shape {
    Triangle(int b, int h) : base(b), height(h) {}
    int area() const override { return base * height / 2; }
    int base, height;
};
int total(const Shape &s) { return s.area(); }
int main() {
    Square square{3};
    Rectangle rectangle{2, 6};
    Triangle triangle{4, 5};
    const Shape *shapes[] = {&square, &rectangle, &triangle};
    for (const Shape *shape : shapes) std::printf("area=%d\n", total(*shape));
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
#include <cstdio>
#include "SKL/ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");
constexpr auto area_sig = method_signature("area", i32, nullptr, 0);

// 接口类：Traits 描述契约，Shape 只补方法。
struct ShapeTraits {
    static constexpr skl::mics::util::array signatures = {area_sig};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};
class Shape : public UniqueInterfaceProxy<ShapeTraits> {
    using Base = UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
};

// 三个实现类：普通 struct，不继承 Shape，不含虚函数。
struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
};

int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int triangle_area(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}

constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId rectangle_id{0x12, 0x00};
constexpr TypeId triangle_id{0x13, 0x00};

InterfaceStatus register_shape(InterfaceRegistry<4> &registry, TypeId type, const InterfaceMethod *methods) {
    return registry.add({ShapeTraits::id, type, methods, 1, ShapeTraits::version, 0});
}

int main() {
    const InterfaceMethod square_methods[] = {{area_sig, interface_entry(&square_area), 0, 0}};
    const InterfaceMethod rectangle_methods[] = {{area_sig, interface_entry(&rectangle_area), 0, 0}};
    const InterfaceMethod triangle_methods[] = {{area_sig, interface_entry(&triangle_area), 0, 0}};
    InterfaceRegistry<4> registry;
    if (register_shape(registry, square_id, square_methods) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, rectangle_id, rectangle_methods) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, triangle_id, triangle_methods) != InterfaceStatus::ok) return 1;

    Square square{3};
    Rectangle rectangle{2, 6};
    Triangle triangle{4, 5};
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        Shape shape(registry, c.object, c.type);
        if (!shape.valid()) return 1;
        std::printf("area=%d\n", shape.area());
    }
    return 0;
}
```

</td>
</tr>
</table>

差别不在「能不能多态」，而在**多态的载体是什么**：

- 左栏把多态编码进**类型系统**：`Shape` 是基类，`Square`/`Rectangle`/`Triangle` 必须继承并 `override`，`area()` 的分派靠对象里的 vptr 指向编译器私有的 vtable。
- 右栏把多态编码进**接口契约**：`ShapeTraits` 描述接口身份、版本与 slot，`Shape` 代理缓存已解析的适配器入口；`Square`/`Rectangle`/`Triangle` 是普通 `struct`，不继承、不含虚函数。绑定后 `shape.area()` 就是原生间接调用。

### 逐项对照

| 维度                   | C++ 虚函数                         | ABIX Interface                                                                     |
| ---------------------- | ---------------------------------- | ---------------------------------------------------------------------------------- |
| 抽象载体               | 基类 + 纯虚函数（类型系统）        | 接口身份 `InterfaceId` + 方法 `slot`（契约）                                       |
| 对具体类型的侵入       | 必须继承、必须 `override`          | 零修改；C++ `struct` / Rust / Zig 类型皆可                                         |
| 分派依据               | 对象内 vptr → 编译器私有 vtable 槽 | 显式解析的适配器入口 `(interface_id, slot)`                                        |
| `this` 调整 / 多重继承 | 编译器生成 thunk，藏进 vtable      | provider 的适配器承担，首参即对象句柄                                              |
| 解析时机               | 每次调用隐式取 vptr                | `resolve()` 一次，之后零查找                                                       |
| 异构集合               | 靠基类指针 `const Shape*`          | `{ void* object, 适配器 }`，签名统一                                               |
| 跨语言                 | 不可跨（布局/顺序各异）            | 同一 `interface_id` 多语言实现                                                     |
| 失败模式               | UB（错调、悬垂 vptr）              | `InterfaceStatus`：`interface_not_found` / `version_mismatch` / `method_not_found` |
| ABI 稳定性             | 新增虚函数/改继承即 ABI break      | 接口是契约，不参与 `TypeID` / `ABIHash`                                            |

### 何时用哪个

- **纯 C++、编译期同构、无跨边界需求**：虚函数足够，零额外约定。
- **跨 DLL / 跨语言 / 需要「面向接口、不认具体类型」**：用 Interface。消费者代码里可以只出现 `InterfaceId`（见[示例 2](#示例)的跨模块路径），provider 换了具体类型也无需重编消费者。
- **两者共存**：ABIX 不要求你放弃 C++ 语义——provider 内部照常用虚函数，只在边界处用适配器把某个具体类型的方法绑定到接口槽位（AMC 的 `[[interface]]` / `[[implements]]` 声明）。接口可以像 C++ 抽象类一样在头文件里声明：`[[interface]]` 定义契约、`[[implements]]` 建立绑定，`amc generate -l cpp` 生成 `k<Name>Interface` / `k<Name>Version` / `<Name>_<method>_fn` / 带类型化 `Slots` 表的 `Traits`，消费者据此封装出 `Shape s = Shape::bind(...); s.area();` 这样的用法。

## ABIX Interface 与 ngcpp/proxy 的对比

[ngcpp/proxy](https://github.com/ngcpp/proxy)（前身是 Microsoft `proxy`）与 ABIX Interface 解决的是同一类大问题——不要求实现类型进入类继承体系，也能进行运行时多态——但两者把抽象放在不同的边界上。

ABIX Interface 建模的是**独立、带版本、跨语言的接口契约**：`InterfaceId`、有序方法签名、按 slot 缓存的 adapter，以及显式的 bind 步骤。ngcpp/proxy 建模的是 **C++20 facade**：`pro::proxy<Facade>` 像指针一样使用，facade 由 dispatch 组合出 convention，任何在编译期满足这些表达式的类型都可以作为实现。

下面的两个示例都使用各自库的宏写法，并且都返回 `area == 12`。

<table>
<tr>
<th align="left">ABIX Interface：契约宏 + 成员适配</th>
<th align="left">ngcpp/proxy：facade + dispatch 宏</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include "SKL/ABIX/Interface/Declaration.h"

using namespace skl::abix::poly;
constexpr auto i32 =
    skl::abix::model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X) X(area, int32_t, i32, {})
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);

struct Box {
    int32_t width;
    int32_t height;
    int32_t area() const noexcept {
        return width * height;
    }
};

constexpr skl::abix::model::TypeId box_id{0x51, 0x01};

int main() {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(
            ShapeTraits, area, &Box::area),
    };

    InterfaceRegistry<4> registry;
    registry.add({ShapeTraits::id, box_id, methods,
        1, ShapeTraits::version, 0});

    Box box{3, 4};
    Shape shape(registry, &box, box_id);
    return shape.valid() && shape.area() == 12 ? 0 : 1;
}
```

宏负责生成 `Traits`、`signatures`、零基 `Slots` 和 `Shape` 代理；`SKL_ABIX_INTERFACE_MEMBER_ENTRY` 把 `Box::area` 变成 adapter entry，再由 registry 绑定具体 `TypeId`。

</td>
<td valign="top" markdown="1">

```cpp
#include <proxy/proxy.h>

PRO_DEF_MEM_DISPATCH(MemArea, area);

struct Shape : pro::facade_builder
    ::add_convention<MemArea, int() const noexcept>
    ::build {};

struct Box {
    int width;
    int height;
    int area() const noexcept {
        return width * height;
    }
};

int main() {
    Box box{3, 4};
    pro::proxy<Shape> shape = &box;
    return shape->area() == 12 ? 0 : 1;
}
```

`PRO_DEF_MEM_DISPATCH` 生成 `area` 成员表达式的 dispatch；`Shape` 是 facade builder 组合出的 facade。用 `&box` 构造 proxy 时完成编译期适配检查，`shape->area()` 通过类型擦除后的指针调用成员。

</td>
</tr>
</table>

两者的宏职责不同：ABIX 的 `SKL_ABIX_INTERFACE_DECLARE` 生成契约数据（`id`、`signatures`、`Slots`）和代理类型；`PRO_DEF_MEM_DISPATCH` 只生成某一个成员表达式的 dispatch，facade 由 `facade_builder` 组合。ABIX 需要 provider 显式注册方法表；ngcpp/proxy 则是在用指针构造 proxy 时完成编译期适配检查。

### 主要区别

| 维度            | ABIX Interface                                                                          | ngcpp/proxy                                                             |
| --------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| 抽象单位        | `InterfaceId` + 版本 + 有序方法签名 + 类型化 slot                                       | 由 convention / dispatch / overload 组合出的 facade                     |
| provider 适配   | 显式 `InterfaceRegistry` 注册 + provider 持有的 adapter 表                              | 编译期隐式满足：传入的指针类型必须能通过 facade 检查                    |
| 绑定方式        | 运行时 `bind` / `bind_methods` 解析一次，之后使用缓存的 slot                            | 没有 registry 或 resolve 步骤，直接从指针构造 proxy                     |
| 分派方式        | 按 slot 缓存的 adapter，做原生间接调用                                                  | facade 生成的 dispatch/accessor，通过类型擦除指针调用                   |
| 版本            | 显式契约版本；支持精确/最新版本选择及 `version_mismatch`                                | facade 类型由源码定义；没有运行时版本协商                               |
| 跨语言 / 跨 DLL | 固定 C ABI、`abix_module_query_v1`、lease，支持跨语言消费者                             | 仅 C++；facade 与实现共享同一 C++ ABI/工具链                            |
| 所有权          | 借用的对象句柄；module lease；owned handle 由独立 Owner ABI 管理                        | 裸指针（不拥有）、`unique_ptr` / `shared_ptr`、或 `make_proxy` 原位存储 |
| 失败模型        | `InterfaceStatus`（`interface_not_found`、`version_mismatch`、`signature_mismatch` 等） | 编译期约束；可选 `rtti` / `bad_proxy_cast`；可配置 weak dispatch        |
| 重载            | 不同 slot/签名；成员重载需要显式成员指针转换                                            | 原生 overload resolution，支持一个 convention 下多个 overload           |
| 生命周期        | provider 对象必须比 proxy 活得久；跨模块时 lease 保证 adapter 仍映射                    | 由指针类型表达：裸指针不拥有，智能指针拥有，`make_proxy` 可原位存储     |
| 依赖            | C++17，header-only 的 Interface 层                                                      | C++20，header-only 的 facade/pointer 库                                 |

### 如何选择

- 选择 **ABIX Interface**：边界是 DLL/module 或其它语言；契约需要显式身份与版本；调用前必须校验方法签名；provider 对象已经存在且不能修改。
- 选择 **ngcpp/proxy**：整个程序是 C++20；需要指针式所有权变化（裸指针、`unique_ptr`、`shared_ptr`、原位值）；需要原生重载/运算符 convention；希望只靠编译期 facade 检查，不需要 registry。
- 两者可以组合：跨边界契约层使用 ABIX，内部 C++ 值/所有权多态使用 `pro::proxy`。ABIX 提供 ABI 和带版本的契约，ngcpp/proxy 提供纯 C++ 的指针抽象。

### 性能与体积

以下测量使用同一个方法 `int32_t area() const noexcept`、非拥有 proxy，并对所有机制使用同一个 box 对象。ABIX 以 `C++17` 构建，ngcpp/proxy 以 `C++20` 构建。

原生虚调用基线使用 `-fno-devirtualize` 编译。这点很关键：GCC 16 在 `-O3` 下即使通过 `noinline` 不透明访问器，也可能做投机去虚化，生成 vptr guard 后内联 `Derived::area`；测到的是约 0.27 ns 的带守卫直接调用，而不是通用 vtable 分派。真正的间接 vtable 路径约为 0.75 ns。

**调用延迟**（x86-64，GCC 16.2.1，`-O3 -DNDEBUG`，Google Benchmark，约 2 秒，CPU scaling 开启；关注趋势而非绝对纳秒）：

| 机制                                   | ns/次 | 相对真实原生虚调用 |
| -------------------------------------- | ----: | -----------------: |
| 原生直接调用                           |  0.19 |              0.25x |
| 原生虚调用（真实间接）                 |  0.75 |               1.0x |
| 原生虚调用（编译器去虚化）             |  0.27 |              0.36x |
| ABIX 直接 adapter                      |  0.19 |              0.25x |
| ABIX `InterfaceRef` 缓存入口           |  0.75 |               1.0x |
| ABIX `UniqueInterfaceProxy`            |  0.75 |               1.0x |
| ABIX `SharedInterfaceProxy`            |  0.75 |               1.0x |
| ngcpp/proxy `pro::proxy`               |  0.75 |               1.0x |

在这个微基准里，非侵入式 proxy 与真实 vtable 分派基本同速。ABIX 的 unique/shared 布局与 ngcpp/proxy 都收敛在相同成本；主导因素是间接调用，shared 模式多出的一次表加载没有单独显现。剩下的差距来自**直接/去虚化**调用：编译器能证明动态类型时可以完全消除分派。SKL/ABIX/proxy 无法做到这一点，因为实现是运行期选择（或在编译期被擦除）。这是不接管对象模型（注册、类型擦除、跨 DLL/跨语言契约）的代价，而不是每次调用都查 registry。

**体积**（64 位；`N` = 接口方法数）：

| 机制                        |               每对象 | 每实现 / 每类型                | 5 方法对象 | 6 方法对象 |
| --------------------------- | -------------------: | ------------------------------ | ---------: | ---------: |
| 原生 vtable                 | 8 字节 `vptr` + 字段 | vtable ≈ `8N` + RTTI/头部      |   8 + 字段 |   8 + 字段 |
| ngcpp/proxy `pro::proxy`    |                24–32 | 静态 dispatch/accessor 表      |      24–32 |      24–32 |
| ABIX `UniqueInterfaceProxy` |            `24 + 8N` | 无                             |         64 |         72 |
| ABIX `SharedInterfaceProxy` |               **24** | `InterfaceSlotTable`：`8N + 8` |    24 + 48 |    24 + 56 |
| ABIX `InterfaceRef`         |           `32 + 24N` | 无                             |        152 |        176 |
| ABIX `InterfaceRegistry<8>` |                    — | 464                            |     绑定期 |     绑定期 |

`pro::proxy_view` 为 16–24 字节。ABIX 的 registry 和共享表属于设置/实现存储，不是每对象存储。对象很多时，`SharedInterfaceProxy` 是内存密度优先的选择：每对象一个 24 字节句柄，每实现一张 slot 表。对象很少时，`UniqueInterfaceProxy` 自包含、不需要共享表生命周期；当同类型的不同对象需要不同 adapter 表时，也必须使用 Unique。

```cpp
SKL_ABIX_INTERFACE_DECLARE_BOTH(Shape, SHAPE_METHODS, 1);

// 对象少：内联 slot 入口
Shape shape(registry, &box, box_id);

// 对象多：每实现一张共享表
InterfaceSlotTable<ShapeTraits::signatures.size()> table;
make_shared_slot_table<ShapeTraits>(table, registry, box_id);

Box a{3, 4}, b{5, 6};
SharedShape first(table, &a);   // 24 字节
SharedShape second(table, &b);  // 24 字节，同一张表
```

## 快速开始

三步：**注册**（把「某具体类型实现某接口」写入注册表）→ **解析**（一次性查表并缓存适配器入口） → **调用**（普通原生间接调用）。下面的完整程序不需要动态库。

```cpp
#include <cstdio>

#include "SKL/ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

// ① Provider 类型：无 vtable，不被修改。适配器收到对象句柄作为首参。
struct Vec2 { int x, y; };
int vec2_axis(const void *self, int axis) {
    const auto *v = static_cast<const Vec2 *>(self);
    return axis == 0 ? v->x : v->y;
}
using AxisFn = int (*)(const void *, int);

int main() {
    constexpr TypeId      vec2_id{0x11, 0x12};   // AMC 记录的类型身份
    constexpr InterfaceId ivec   {0x31, 0x32};   // AMC 计算的接口身份

    // ② 注册一次：方法按稳定 slot 绑定（slot 0 -> vec2_axis）。
    const InterfaceMethod methods[] = {
        {{0xa1, 0x0}, interface_entry(&vec2_axis), /*slot*/ 0, /*flags*/ 0},
    };
    InterfaceRegistry<8> registry;
    if (registry.add({ivec, vec2_id, methods, 1, /*version*/ 1, 0}) != InterfaceStatus::ok) return 1;

    // ③ 解析一次，之后每次调用都是普通原生间接调用。
    Vec2 v{2, 3};
    InterfaceRef<8> ref;
    if (ref.resolve(registry, &v, vec2_id, ivec, 1) != InterfaceStatus::ok) return 1;

    const auto axis = ref.method<AxisFn>(0);
    std::printf("x=%d y=%d\n", axis(ref.object(), 0), axis(ref.object(), 1));   // x=2 y=3
}
```

`resolve()` 是唯一的查表动作——`registry.lookup_count()` 始终是 `1`；之后调用不经过任何查找、名比较或类型检查。完整的 API 见 [API 参考](#api-参考)，更多场景见[示例](#示例)。

## 绑定期解析，调用期原生调用

运行时设施刻意是*绑定期*的。`InterfaceRegistry` 保存「该具体类型实现了该接口」的记录；`InterfaceRef` 只解析一次并按 slot 缓存适配器入口，之后每次调用都是普通原生间接调用。runtime 不会变成通用 dispatch loop。

```cpp
#include "SKL/ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

// Provider 类型：无 vtable，不被修改。
struct Vec2 { int x, y; };
int vec2_axis(const void *self, int axis) { /* ... */ }

// 适配器，注册一次。
const InterfaceMethod methods[] = {
    {{0xa1, 0x0}, interface_entry(&vec2_axis), /*slot*/ 0, /*flags*/ 0},
};
InterfaceRegistry<8> registry;
registry.add({ivec, vec2_id, methods, 1, /*version*/ 1, 0});

// 查询一次，之后原生调用。
InterfaceRef<8> ref;
ref.resolve(registry, &object, vec2_id, ivec, 1);
const auto axis = ref.method<int (*)(const void *, int)>(0);
int x = axis(ref.object(), 0);
```

`resolve()` 会区分 `interface_not_found`、`version_mismatch`、`method_not_found` 与 `method_capacity_exceeded`，而不是猜测。

## 跨模块的动态发现

生产者（动态库）暴露一个固定入口 `abix_module_query_v1`，返回稳定的 `ModuleManifest`，列出它实现的接口。平台导出表只用于找到该入口：不解码 mangled name，也不从符号推断接口含义。

```cpp
const InterfaceMethod methods[] = {
    {{0xa1, 0x0}, interface_entry(&vec2_axis), 0, 0},
};
const InterfaceManifestEntry interfaces[] = {
    {ivec, /*version*/ 1, 0, methods, 1, 0},
};
const ModuleManifest manifest = {
    abix_manifest_version, /*module_abi*/ 1, 0, 1, interfaces, 0, 0, nullptr,
};
SKL_ABIX_DEFINE_MODULE_QUERY(&manifest)
```

消费端先 `validate_manifest` 校验清单，再注册与查询。每次加载都得到新的 `ModuleInstanceId`；只要绑定持有租约，模块就不能卸载。

```cpp
ModuleControl control;
control.name = "renderer";
ModuleRegistry registry;
registry.register_module(control, manifest);

const auto *binding = registry.find_first(ivec, 1);
auto bound = bind_interface(*binding, &object);   // 取模块租约
const auto axis = bound.ref.method<AxisFn>(0);
int x = axis(bound.ref.object(), 0);

registry.unregister_module(control.id);           // 租约未释放前返回 busy
```

规则：

- 多个模块可实现同一接口；查询返回全部；
- 请求版本时精确匹配，`0` 表示取最新；
- 同一实例重复注册幂等；同一模块重复声明 `(interface_id, version)` 被拒绝；
- 卸载先进入 draining（拒绝新租约），仅在所有租约释放后完成，因此缓存绑定永远不会是悬空函数指针；
- 未知清单版本或结构损坏会明确失败，而不是猜测。

## Owner ABI

Interface ABI 说明「怎么调用对象」，Owner ABI 说明「谁拥有、谁释放」。只正式化三种语义—— **borrowed / owned / copied**——且释放必须回到创建方的分配域：消费者不得 `free()`/`delete` 指针，也不得按地址或布局推断析构方式。

```cpp
#include "SKL/ABIX/Interface/Owner.h"

// 生产者返回 POD handle；销毁入口位于该模块内。
skl::abix::owner::handle h = make_owned(new Renderer, &destroy_renderer, control.id);

// 消费端 adopt，并在对象存活期间持有模块租约。
skl::abix::owner::ref object = skl::abix::owner::ref::adopt(h, &control);
// ... 通过 Interface binding 使用 object.object() ...
// 释放时恰好调用一次 destroy_renderer，然后归还租约。
```

Owner handle 会在释放前保持其创建模块处于加载状态，因此长期存活的对象不会比负责销毁它的模块更长寿。移动 handle 即转移所有权；重复释放为无操作。

## 示例

示例 1 是完整可编译的进程内流程（一个接口、三个实现类、注册、解析与调用）；示例 2–4 分别演示跨模块发现、版本共存与 Owner ABI。

### 示例 1 —— 完整流程：接口类 + 三个实现类

契约放在共享头文件里：`Traits` 承载接口身份、版本、类型化槽位表与期望签名，代理类只补方法。

```cpp
// shape.h
#pragma once
#include "SKL/ABIX/Interface/Interface.h"

namespace demo {

using skl::abix::model::TypeId;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::interface_id;
using skl::abix::poly::method_signature;

inline constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");

struct ShapeTraits {
    static constexpr skl::mics::util::array signatures = {
        method_signature("area", i32, nullptr, 0),
        method_signature("perimeter", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
        uint32_t perimeter;
    };
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};

class Shape : public skl::abix::poly::UniqueInterfaceProxy<ShapeTraits> {
    using Base = skl::abix::poly::UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
    int perimeter() const noexcept {
        return this->template call<int (*)(const void *)>(ShapeTraits::slots.perimeter);
    }
};

}  // namespace demo
```

Provider 侧：三个普通实现类，每个方法一个适配器（首参是对象句柄），每个实现一张方法表并注册。

```cpp
// shapes.cpp
// 构建：c++ -std=c++17 -I<abix_root> shapes.cpp -o shapes
#include <cstdio>

#include "shape.h"

using demo::ShapeTraits;
using skl::abix::model::TypeId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::interface_entry;

// 三个实现类：普通 struct，无基类、无虚函数、不包含 ABIX 成员。
struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
    int left;
    int right;
};

// 每个方法一个适配器：首参是类型擦除的对象句柄。
int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int square_perimeter(const void *self) {
    return 4 * static_cast<const Square *>(self)->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int rectangle_perimeter(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return 2 * (r->width + r->height);
}
int triangle_area(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}
int triangle_perimeter(const void *self) {
    const auto *t = static_cast<const Triangle *>(self);
    return t->base + t->left + t->right;
}

// 每个实现一个具体类型身份；真实项目里由 AMC 记录。
constexpr TypeId square_id{0x51, 0x01};
constexpr TypeId rectangle_id{0x51, 0x02};
constexpr TypeId triangle_id{0x51, 0x03};

// 每个实现一张方法表：signature 与 slot 都直接取自共享契约。
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&square_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&square_perimeter),
        ShapeTraits::slots.perimeter, 0},
};
const InterfaceMethod rectangle_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&rectangle_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&rectangle_perimeter),
        ShapeTraits::slots.perimeter, 0},
};
const InterfaceMethod triangle_methods[] = {
    {ShapeTraits::signatures[ShapeTraits::slots.area], interface_entry(&triangle_area), ShapeTraits::slots.area, 0},
    {ShapeTraits::signatures[ShapeTraits::slots.perimeter], interface_entry(&triangle_perimeter),
        ShapeTraits::slots.perimeter, 0},
};

InterfaceStatus register_shape(
    InterfaceRegistry<8> &registry, TypeId type, const InterfaceMethod *methods, uint32_t count) {
    return registry.add({ShapeTraits::id, type, methods, count, ShapeTraits::version, 0});
}

int main() {
    // 注册：每个「实现类 → Shape 接口」一条记录。
    InterfaceRegistry<8> registry;
    if (register_shape(registry, square_id, square_methods, 2) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, rectangle_id, rectangle_methods, 2) != InterfaceStatus::ok) return 1;
    if (register_shape(registry, triangle_id, triangle_methods, 2) != InterfaceStatus::ok) return 1;

    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4, 5, 5};

    struct Instance {
        void *object;
        TypeId type;
        const char *name;
    };
    const Instance instances[] = {
        {&square, square_id, "square"},
        {&rectangle, rectangle_id, "rectangle"},
        {&triangle, triangle_id, "triangle"},
    };

    // 同一段调用代码服务三个实现：绑定一次，之后都是原生间接调用。
    for (const auto &instance : instances) {
        demo::Shape shape;
        if (shape.bind(registry, instance.object, instance.type) != InterfaceStatus::ok) return 1;
        std::printf("%-9s area=%d perimeter=%d\n", instance.name, shape.area(), shape.perimeter());
    }
    return 0;
}
```

输出：

```
square    area=25 perimeter=20
rectangle area=24 perimeter=20
triangle  area=12 perimeter=16
```

要点：

- `Square` / `Rectangle` / `Triangle` 从未继承 `Shape`，也没有被修改；它们仍是普通聚合。
- `signatures` 让 `bind()` 逐槽校验：方法表里的 `signature` 与契约不一致时返回 `signature_mismatch`，不会静默调用。
- 每次 `bind()` 只查表一次；循环里的 `area()` / `perimeter()` 都是普通原生间接调用。
- 调用方只持有 `demo::Shape`，不依赖具体类型；新增实现只需补一张方法表并注册。

### 示例 2 —— 跨模块发现并调用

Provider 与 consumer 共享一份契约常量头（接口身份与版本必须一致）：

```cpp
// canvas_interface.h
#pragma once
#include "SKL/ABIX/Interface/Interface.h"

namespace canvas {
inline constexpr skl::abix::poly::InterfaceId kInterface{0x51, 0x00};
inline constexpr uint32_t kVersion = 1;
using WidthFn = int (*)(const void *, int);
}  // namespace canvas
```

Provider 编译为 `libcanvas.so`，只暴露固定入口与自己的对象句柄，绝不暴露对象布局：

```cpp
// canvas_plugin.cpp
// 构建：c++ -std=c++17 -shared -fPIC -I<abix_root> -o libcanvas.so canvas_plugin.cpp
#include "SKL/ABIX/Interface/Interface.h"
#include "canvas_interface.h"

namespace canvas {

struct Canvas { int width; };
Canvas g_canvas{1920};

int canvas_width(const void *self, int) { return static_cast<const Canvas *>(self)->width; }

const skl::abix::poly::InterfaceMethod kMethods[] = {
    {{0x52, 0x00}, skl::abix::poly::interface_entry(&canvas_width), /*slot*/ 0, 0},
};
const skl::abix::poly::InterfaceManifestEntry kInterfaces[] = {
    {kInterface, kVersion, 0, kMethods, 1, 0},
};
const skl::abix::poly::ModuleManifest kManifest = {
    skl::abix::poly::abix_manifest_version, 1, 0, 1, kInterfaces, 0, 0, nullptr,
};

}  // namespace canvas

// 固定入口：consumer 只为它查询导出表。
SKL_ABIX_DEFINE_MODULE_QUERY(&canvas::kManifest)

// provider 的对象句柄，经普通导出符号给出。
extern "C" SKL_ABIX_DLL_EXPORT const void *canvas_instance(void) { return &canvas::g_canvas; }
```

Consumer 加载、发现、绑定一次并调用，最后卸载：

```cpp
// canvas_host.cpp
// 构建：c++ -std=c++17 -I<abix_root> canvas_host.cpp -ldl -o canvas_host
#include <cstdio>

#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Interface/Interface.h"
#include "canvas_interface.h"

using namespace skl::abix::poly;

int main() {
    skl::abix::dll::Object lib;
    if (!lib.load("./libcanvas.so")) return 1;

    ModuleRegistry<> registry;
    ModuleControl control;   // 由加载方拥有的生命周期令牌
    control.name = "canvas";
    if (!discover_module(lib, control, registry).ok()) return 1;   // 校验清单 + 注册

    const auto *binding = registry.find_first(canvas::kInterface, canvas::kVersion);
    if (binding == nullptr) return 1;

    // provider 的对象句柄来自它自己的导出符号；接口表从不暴露布局。
    using InstanceFn = const void *(*)(void);
    auto instance = reinterpret_cast<InstanceFn>(lib.resolve("canvas_instance"));
    void *object = const_cast<void *>(instance());

    {
        auto bound = bind_interface(*binding, object);   // 取模块租约
        if (!bound.valid()) return 1;
        const auto width = bound.ref.method<canvas::WidthFn>(0);
        std::printf("width=%d\n", width(bound.ref.object(), 0));   // width=1920
    }   // bound 析构：释放租约

    registry.unregister_module(control.id);   // 租约已释放，完成卸载
    lib.unload();
    return 0;
}
```

要点：`discover_module` 只在导出表里查 `abix_module_query_v1` 一个符号；绑定持有租约期间模块无法卸载，因此缓存的 `width` 指针永远不会悬垂。

### 示例 3 —— 一个接口的多个实现与版本共存

不经过动态库时，直接把多个 provider 的 `ModuleManifest` 注册进 `ModuleRegistry`：

```cpp
#include "SKL/ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

// manifest_v1 / manifest_v2 是各自 provider 的清单，都实现 kInterface；
// 每个 manifest 内部持有自己的方法表（构造细节从略）。
ModuleRegistry<> registry;
ModuleControl a;
ModuleControl b;
a.name = "a";
b.name = "b";
registry.register_module(a, manifest_v1);
registry.register_module(b, manifest_v2);

// 取回全部实现（按注册顺序）；version 0 = 任意版本。
const ModuleInterface *all[4];
const size_t count = registry.find_interface(kInterface, 0, all, 4);   // count == 2

// 按版本挑选：精确匹配，或 version 0 取最新。
const auto *v1 = registry.find_first(kInterface, 1);        // 版本 1
const auto *newest = registry.find_first(kInterface, 0);    // 最新，即版本 2
```

规则：请求版本精确匹配，`0` 取最新；同一模块内重复的 `(id, version)` 被拒绝（`duplicate_interface`），不同模块之间允许并存，查询返回全部且从不互相覆盖。

### 示例 4 —— Owner ABI：跨模块创建与释放

Provider 通过自己的入口返回一个 owner handle，销毁入口位于该模块内：

```cpp
// owner_plugin.cpp（节选）
skl::abix::owner::handle owner_counter_create(int base) {
    auto *counter = new Counter{base};
    return skl::abix::owner::make_owned(counter, &destroy_counter, /*module_instance*/ 0);
}
```

Consumer 收养句柄并持有租约，离开作用域时恰好销毁一次（承接示例 2 的 `lib` / `control` / `binding`）：

```cpp
#include <cstdio>

#include "SKL/ABIX/Interface/Owner.h"

using namespace skl::abix::poly;
using AddFn = int (*)(const void *, int);

using CreateFn = skl::abix::owner::handle (*)(int);
auto create = reinterpret_cast<CreateFn>(lib.resolve("owner_counter_create"));
const skl::abix::owner::handle raw = create(40);
if (!skl::abix::owner::valid(raw)) return 1;

{
    // 收养 + 租约：对象存活期间，负责销毁它的模块无法卸载。
    const skl::abix::owner::ref object = skl::abix::owner::ref::adopt(raw, &control);
    if (registry.unregister_module(control.id) == ModuleStatus::busy) { /* 符合预期 */ }

    auto bound = bind_interface(*binding, object.object());
    const auto add = bound.ref.method<AddFn>(0);
    std::printf("add(2)=%d\n", add(bound.ref.object(), 2));   // add(2)=42
}   // object 析构：调用创建方 destroy，然后归还租约

registry.unregister_module(control.id);   // 现在可以卸载
```

## API 参考

以下符号由 `SKL/ABIX/ABIX.h` 引入；除 Owner ABI（`skl::abix::owner`）外，其余都在命名空间 `skl::abix::poly`。要单独引入整层，`#include "SKL/ABIX/Interface/Interface.h"`。

| 头文件                            | 内容                                                                                                      |
| --------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `SKL/ABIX/Interface/Contract.h`       | 接口身份、`InterfaceMethod` / `InterfaceImplementation`、`InterfaceStatus`、`interface_entry`             |
| `SKL/ABIX/Interface/Registry.h`       | `InterfaceRegistry` —— 绑定期实现的注册与解析                                                             |
| `SKL/ABIX/Interface/Ref.h`            | `InterfaceRef` —— 一次解析、按槽位缓存的原生调用句柄                                                      |
| `SKL/ABIX/Interface/Module.h`         | 固定入口 `abix_module_query_v1`、`ModuleManifest`、`validate_manifest`                                    |
| `SKL/ABIX/Interface/Lease.h`          | `ModuleControl` 状态机与 RAII `ModuleLease`                                                               |
| `SKL/ABIX/Interface/ModuleRegistry.h` | `ModuleRegistry` —— 动态发现模块的接口登记与查询                                                          |
| `SKL/ABIX/Interface/Discovery.h`      | `discover_module` —— 加载 → 校验 → 注册                                                                   |
| `SKL/ABIX/Interface/Owner.h`          | Owner ABI —— `handle` / `ref` 的借用/拥有/复制语义                                                        |
| `SKL/ABIX/Interface/Proxy.h`          | C++ 代理 API —— `UniqueInterfaceProxy<Traits>`、`bind`/`call`、签名校验                                   |
| `SKL/ABIX/Interface/Declaration.h`    | 声明糖 —— `SKL_ABIX_INTERFACE_DECLARE`；版本辅助宏 `SKL_ABIX_VERSION_NUMBER` / `SKL_ABIX_VERSION_EXTRACT` |

### `Contract.h` —— 接口身份与方法

| 符号                                                     | 类型                   | 说明                                                                                                                                                                       |
| -------------------------------------------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `InterfaceId`                                            | `Hash128`              | 接口身份：对接口名、版本与有序方法签名计算的内容哈希。不是类名，也不是 vtable 地址；可用 `interface_id(...)` 自动生成                                                      |
| `InterfaceMethodHash`                                    | `model::SignatureHash` | 单个方法的 ABI 签名哈希                                                                                                                                                    |
| `interface_id(name, version, signatures)`                | `InterfaceId`          | 编译期从规范名、版本与按槽位有序的签名算出接口身份；复刻 `AMC::interface_identity`，两端无需手抄常量。参数可用 `mics::util::array` 或变参 `InterfaceMethodHash` 列表      |
| `InterfaceMethodKey`                                     | `{signature, slot}`    | 与槽位配对的签名，供 `interface_id` 的 `mics::util::array` 重载排序后计算身份                                                                                             |
| `method_signature(name, return_type, parameters, count)` | `InterfaceMethodHash`  | 编译期从方法名与返回/参数 TypeID 算出方法签名；复刻 `AMC::signature_hash`（`[[interface]]` 路径，参数名不参与哈希）。TypeID 可由 `model::canonical_type_id`（`ABI.h`）得到 |
| `interface_entry<Fn>(fn)`                                | `uintptr_t`            | 把函数指针拷入不透明的 `entry` 槽位；生成代码与调用方共用同一拼写                                                                                                          |
| `to_string(status)`                                      | `const char*`          | `InterfaceStatus` 的可读名                                                                                                                                                 |

`InterfaceMethod` —— 一个方法的适配器表项：

| 字段        | 类型                  | 说明                                                         |
| ----------- | --------------------- | ------------------------------------------------------------ |
| `signature` | `InterfaceMethodHash` | AMC 记录的方法 ABI 签名哈希                                  |
| `entry`     | `uintptr_t`           | 适配器入口，调用时第一个参数是对象句柄；`0` 表示该槽位未绑定 |
| `slot`      | `uint32_t`            | 稳定分派槽位                                                 |
| `flags`     | `uint32_t`            | 保留                                                         |

`InterfaceImplementation` —— 具体类型对某接口的实现：

| 字段            | 类型                     | 说明                  |
| --------------- | ------------------------ | --------------------- |
| `interface_id`  | `InterfaceId`            |                       |
| `concrete_type` | `model::TypeId`          | 实现该接口的具体类型  |
| `methods`       | `const InterfaceMethod*` | provider 拥有的方法表 |
| `method_count`  | `uint32_t`               | 表项个数              |
| `version`       | `uint32_t`               | 接口版本              |
| `flags`         | `uint32_t`               | 保留                  |

`InterfaceStatus`：`ok`、`invalid_table`、`duplicate_implementation`、`capacity_exceeded`、`interface_not_found`、`version_mismatch`、`method_not_found`、`method_capacity_exceeded`。

**跨工具身份契约**：`InterfaceId` 由 `AMC::interface_identity` 与 `ABIX::poly::interface_id` 共同定义，算法完全一致：

```
hash_text("ABIX-INTERFACE:" + 规范名 + ":" + 版本 + 按槽位排序的签名, 域 = 0x494E544552464143)
```

域标签是 8 字节 ASCII `"INTERFAC"`（`"INTERFACE"` 的大端截断，装不进 `uint64_t` 的是第 9 个字符 `E`），即字节 `49 4E 54 45 52 46 41 43`。它**作为整数混入哈希初始状态**（`lo ^= 域`、`hi += 域`），**不是**字节前缀；因此不存在字节序歧义，其他语言实现必须用同一个整数与同一混合方式，而不是 `reinterpret_cast` 整数的内存表示。

哈希种子也与 ABIX 自身的 `fnv1a` 统一：`lo` 使用标准 **FNV-1a 64 offset basis** `0xCBF29CE484222325`，`hi` 使用 FNV prime `0x100000001B3` + 域，`hi` lane 的乘子是 XXH64 的 `PRIME64_2`。`amc-core-test` 用固定向量钉住这些常量，并校验两端一致。

### `Registry.h` —— 绑定期注册表

`InterfaceRegistry<Capacity = 64>`：固定容量、无分配。注册与解析都是 setup 工作，线性扫描即可 ——它永远不在调用路径上。`add()` 是**注册**，`find()` 是**解析**。

在 64 位目标上，`InterfaceImplementation` 为 56 字节：两个 128 位 ID、方法表指针、`method_count`、`version`、保留的 `flags` 以及填充。因此 `InterfaceRegistry<8>` 为 464 字节（`8 * 56 + count/lookups`）。这是绑定期内存；通常一个共享 registry 比每个 controller 各带一个更合适。自定义紧凑记录可以去掉未使用的 `flags`（每项 48 字节）或减小容量，但会改变公共记录布局。

`InterfaceRegistry` **不做内部同步**。`add()` 必须由调用方串行化，且不能与 `find()` 并发。registry 填充完成并且不再修改之后，并发调用 `find()` / `find_newest()` / `available()` 是安全的：它们只读取条目数组，诊断用的 lookup 计数器是 relaxed atomic。原子计数器并不会让并发 `add()` 变安全；运行时注册 provider 应使用带模块生命周期/lease 管理的 `ModuleRegistry`，并且其注册调用仍需调用方串行化。

| 方法                                              | 返回值                           | 说明                                                                                                                                                                                           |
| ------------------------------------------------- | -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `add(impl)`                                       | `InterfaceStatus`                | 注册一个实现。重复的 `(concrete_type, interface_id, version)` 返回 `duplicate_implementation`（冲突而非静默覆盖）；容量满返回 `capacity_exceeded`；id 为空或声明方法却空表返回 `invalid_table` |
| `find(concrete_type, interface_id, version = 0)`  | `const InterfaceImplementation*` | `version == 0` 取最新注册版本，否则精确匹配；未找到返回 `nullptr`                                                                                                                              |
| `find_newest(concrete_type, interface_id)`        | `const InterfaceImplementation*` | 最新版本，或 `nullptr`                                                                                                                                                                         |
| `available(concrete_type, interface_id, newest&)` | `bool`                           | 是否存在任意版本；`newest` 回填最新版本号——用于区分「未知接口」与「版本不符」，无需二次全扫                                                                                                    |
| `size()`                                          | `size_t`                         | 已注册实现数                                                                                                                                                                                   |
| `at(index)`                                       | `const InterfaceImplementation*` | 按下标访问，越界返回 `nullptr`                                                                                                                                                                 |
| `lookup_count()`                                  | `size_t`                         | 诊断：`find()` 被服务次数；可用来证明 resolve-then-call 只发生一次查找                                                                                                                         |

### `Ref.h` —— 解析句柄

`InterfaceRef<MaxMethods = 32>`：一次解析把绑定的适配器入口拷入按槽位索引的缓存，之后每次调用都是普通原生间接调用——没有逐次查找、名比较或类型检查。

| 方法                                                                          | 返回值            | 说明                                                                                                                                        |
| ----------------------------------------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `resolve<RegCap>(registry, object, concrete_type, interface_id, version = 0)` | `InterfaceStatus` | 恰好一次注册表查找；`object == nullptr` 返回 `invalid_table`；未找到时区分 `version_mismatch`（有该接口但版本不符）与 `interface_not_found` |
| `resolve_methods(interface_id, version, methods, method_count, object)`       | `InterfaceStatus` | 直接从 provider 方法表解析——动态发现模块走这条路径（表来自清单，调用方持有租约）；槽位超过 `MaxMethods` 返回 `method_capacity_exceeded`     |
| `method<Fn>(slot)`                                                            | `Fn`              | 返回缓存入口；槽位未绑定或越界返回 `nullptr`。`Fn` 以对象句柄为首参，传 `object()`                                                          |
| `bound(slot)`                                                                 | `bool`            | 槽位是否已绑定                                                                                                                              |
| `valid()`                                                                     | `bool`            | 是否已解析且对象非空                                                                                                                        |
| `object()`                                                                    | `void*`           | provider 的对象句柄（作为调用的第一个参数）                                                                                                 |
| `interface_id()` / `version()`                                                |                   | 已解析的接口身份 / 版本                                                                                                                     |
| `method_count()`                                                              | `uint32_t`        | 实现跨越的槽位数（最高绑定槽 + 1）                                                                                                          |

`query_interface<MaxMethods = 32, RegCap>(registry, object, concrete_type, interface_id, version = 0)` ——便利函数，按值返回 `InterfaceRef`；检查 `valid()`。

`InterfaceSlotCache<MaxMethods>` 是 `UniqueInterfaceProxy` 使用的紧凑存储：只保留对象句柄、最高已绑定槽和 adapter 入口；期望签名在 resolve 阶段校验后即丢弃。大小约为 `16 + 8 * MaxMethods` 字节，而 `InterfaceRef` 约为 `32 + 24 * MaxMethods`。需要 `interface_id()` / `version()` / `signature()` 诊断或独立裸句柄时用 `InterfaceRef`；每个对象的热路径用 proxy。

`InterfaceSlotTable<MaxMethods>` 是同一份 cache 的可共享形式：用 `build()` / `make_shared_slot_table<Traits>()` 构建一次，然后传给任意多个 `SharedInterfaceProxy`。表本身约为 `8 * MaxMethods + 8` 字节。

provider 对象必须比句柄长寿，且必须正是适配器所针对的对象（句柄创建者拥有正确的指针与任何 `this` 调整）。

### `Module.h` —— 动态发现的清单

| 符号                                                         | 类型                              | 说明                                                            |
| ------------------------------------------------------------ | --------------------------------- | --------------------------------------------------------------- |
| `ModuleInstanceId`                                           | `uint64_t`                        | 每次加载唯一；同一文件加载两次或卸载后重载都得到新 id；`0` 无效 |
| `invalid_module_instance`                                    | 常量                              |                                                                 |
| `abix_manifest_version`                                      | `uint32_t`                        | 清单 ABI 自身版本；未知版本必须拒绝而非猜测                     |
| `abix_module_query_symbol`                                   | `const char*`                     | 固定入口名 `"abix_module_query_v1"`                             |
| `abix_max_manifest_interfaces` / `abix_max_manifest_methods` | 常量                              | 上界，防止畸形清单驱动无界循环                                  |
| `ModuleQueryFn`                                              | `const ModuleManifest* (*)(void)` | 固定入口签名                                                    |
| `SKL_ABIX_DEFINE_MODULE_QUERY(manifest)`                     | 宏                                | 定义 `extern "C"` 的 `abix_module_query_v1`                     |
| `ManifestStatus`                                             | enum                              | `validate_manifest` 的结果；`to_string` 给出名字                |
| `validate_manifest(const ModuleManifest*)`                   | `ManifestStatus`                  | 校验清单                                                        |

`InterfaceManifestEntry`（wire 布局固定为 40 字节）：

| 字段                | 类型                     | 说明                    |
| ------------------- | ------------------------ | ----------------------- |
| `interface_id`      | `InterfaceId`            |                         |
| `interface_version` | `uint32_t`               | 区分兼容的契约修订      |
| `flags`             | `uint32_t`               | 保留                    |
| `methods`           | `const InterfaceMethod*` | provider 拥有的适配器表 |
| `method_count`      | `uint32_t`               | 表项个数                |
| `reserved`          | `uint32_t`               |                         |

`ModuleManifest`（40 字节）：`manifest_version`、`module_abi_version`、`flags`、`interface_count`、`interfaces`、`extension_count`、`reserved`、`extensions`。`extensions` 是不透明、可选、向前兼容的区域：不认识的消费者跳过它，但仍须校验其声明的边界。

`ManifestStatus`：`ok`、`null_manifest`、`unsupported_version`、`invalid_interface`、`invalid_binding`、`too_many_interfaces`、`too_many_methods`。

`validate_manifest` 在**不解引用任意内存**的前提下拒绝：空清单、未知 `manifest_version`、超界的 `interface_count` / `method_count`、空 `interface_id`、声明方法却空方法表、或方法表含重复槽位。绑定槽位 `entry == 0` 是允许的（未绑定）。它无法在一般情况下证明指针安全。

### `Lease.h` —— 模块生命周期与租约

生命周期是一个带引用计数的小状态机：

```text
loading ──activate──▶ active ──begin_unload──▶ draining ──idle──▶ unloaded
```

| 符号                                          | 类型               | 说明                                                                                                                                            |
| --------------------------------------------- | ------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `ModuleState`                                 | enum               | `loading` / `active` / `draining` / `unloaded`；`to_string` 给出名字                                                                            |
| `ModuleControl`                               | struct             | 加载方拥有的生命周期令牌：`id`、`name`、`state`、`leases`，以及平台 `module_handle` 与 `pin`/`unpin` 钩子。留在加载方，使其能与平台模块句柄绑定 |
| `next_module_instance_id()`                   | `ModuleInstanceId` | 单调递增，永不复用                                                                                                                              |
| `module_state(control)`                       | `ModuleState`      | 读取当前状态                                                                                                                                    |
| `activate_module(control)`                    | `bool`             | `loading → active`（CAS）；非 loading 返回 `false`                                                                                              |
| `try_acquire_lease(control)`                  | `bool`             | 仅在 `active` 时租用；递增后**再次**检查状态，避免与并发 `begin_unload` 竞态。成功时取一次 OS 级引用（`pin`），使模块在租约期间保持映射         |
| `release_lease(control)`                      | `void`             | 释放一次租约                                                                                                                                    |
| `begin_unload(control)`                       | `bool`             | 停止接受新租约（进入 `draining`）；已 draining/unloaded 返回 `true`，从未激活返回 `false`                                                       |
| `wait_for_idle(control, max_spins = 1000000)` | `bool`             | 等待所有租约释放；有上界，避免泄漏的租约永久挂起调用方                                                                                          |
| `mark_unloaded(control)`                      | `void`             | 置为 `unloaded`                                                                                                                                 |
| `ModuleLease`                                 | RAII 类            | 可移动、不可拷贝；`valid()`、`release()`；析构释放；释放未持有的租约是无操作                                                                    |
| `acquire_lease(control)`                      | `ModuleLease`      | 模块非 `active` 时返回无效租约（绝不返回半成品）                                                                                                |

租约在活跃调用或存活对象句柄期间持有，这正是缓存方法绑定安全的原因：调用方持有租约的期间，绑定有效。

**OS 级固定（pinning）**：当加载方绑定平台身份（`Discovery.h` → `bind_module_platform`）后，**首个**租约调用 `pin`、**末个**租约调用 `unpin`：Windows 上 `pin` 以 `GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` 解析模块基址、`unpin` 调用 `FreeLibrary`；POSIX 上 `pin` 按加载路径重新 `dlopen(path)`（只增加引用计数、不重跑初始化），`unpin` 调用 `dlclose`。因此持有的租约在 **OS 层直接阻止** `FreeLibrary`/`dlclose`，而不仅靠状态机——即使拥有者在持有租约时卸载，也无法把模块解除映射。该引用在所有租约期间只取一次，依赖加载方持有的 `module_path`，因此加载方需在租约存续期间保持 `Object`（及其路径）存活。

### `ModuleRegistry.h` —— 动态模块接口注册表

`ModuleInterface` —— 一个已发现模块接口：`module`、`interface_id`、`version`、`methods`、`method_count`、`control`（调用前必须持有租约的寿命令牌）。

`ModuleStatus`：`ok`、`already_registered`、`invalid_manifest`、`invalid_state`、`capacity_exceeded`、`interface_capacity_exceeded`、`duplicate_interface`、`not_registered`、`busy`。

`ModuleRegistry<ModuleCapacity = 16, InterfaceCapacity = 128>`：

| 方法                                    | 返回值                   | 说明                                                                                                                                                               |
| --------------------------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `register_module(control, manifest)`    | `ModuleStatus`           | 校验清单、拒绝模块内重复的 `(id, version)`、`activate_module` 后登记；同一 control 重复注册幂等（`already_registered`）；`control` 非 loading 返回 `invalid_state` |
| `unregister_module(id)`                 | `ModuleStatus`           | 进入 unloading：先 `begin_unload`，仅当所有租约释放后才移除；否则返回 `busy`，此时模块已 draining（拒绝新租约），调用方可在在途调用结束后重试                      |
| `registered(id)`                        | `bool`                   |                                                                                                                                                                    |
| `module_count()` / `interface_count()`  | `size_t`                 |                                                                                                                                                                    |
| `interface_at(index)`                   | `const ModuleInterface*` | 按下标访问                                                                                                                                                         |
| `find_interface(id, version, out, max)` | `size_t`                 | 按注册顺序写入**全部**实现（`version == 0` 匹配任意）；返回写入个数（最多 `max`）                                                                                  |
| `find_first(id, version = 0)`           | `const ModuleInterface*` | 最新实现，或 `nullptr`                                                                                                                                             |

多个模块可实现同一接口，查询返回全部且从不互相覆盖；请求版本精确匹配，`0` 取最新。

`BoundInterface<MaxMethods = 32>` —— 已解析绑定 + 保持模块加载的租约；`valid()` 同时要求 `ref.valid()` 与 `lease.valid()`；只要对象存活，经 `ref` 调用就有效。`bind_interface<MaxMethods = 32>(binding, object)` —— 获取租约并解析；模块不再活跃时返回无效绑定（无租约、未解析）。

### `Discovery.h` —— 加载即发现

| 符号                                                                                       | 类型              | 说明                                                                                            |
| ------------------------------------------------------------------------------------------ | ----------------- | ----------------------------------------------------------------------------------------------- |
| `DiscoveryResult`                                                                          | struct            | `manifest`（`ManifestStatus`）、`registration`（`ModuleStatus`）、`queried`（清单指针）、`ok()` |
| `discover_module<ModuleCapacity = 16, InterfaceCapacity = 128>(module, control, registry)` | `DiscoveryResult` | 从已加载模块解析 `abix_module_query_v1`，`validate_manifest` 校验后 `register_module`           |

平台导出表**只为这一个符号**被查询：不解码任何其它导出，也不从符号推断接口含义。缺入口、未知清单版本、结构损坏或重复声明都明确失败。成功后 `control` 变为 `active`。

### `Owner.h` —— Owner ABI

命名空间 `skl::abix::owner`。Interface ABI 说明「怎么调用对象」，Owner ABI 说明「谁拥有、谁释放、释放代码在哪」。

| 符号                                                      | 类型              | 说明                                                              |
| --------------------------------------------------------- | ----------------- | ----------------------------------------------------------------- |
| `abix_owner_abi_version`                                  | `uint32_t`        | Owner ABI 版本                                                    |
| `mode`                                                    | enum              | `borrowed` / `owned` / `copied`；`to_string` 给出名字             |
| `destroy_fn`                                              | `void (*)(void*)` | 创建方的销毁入口，固定 C ABI                                      |
| `handle`                                                  | struct（32 字节） | POD 句柄：`object`、`mode`、`flags`、`destroy`、`module_instance` |
| `valid(handle)`                                           | `bool`            | `object != nullptr`                                               |
| `make_borrowed(object)`                                   | `handle`          | 非拥有引用                                                        |
| `make_owned(object, destroy, module_instance = invalid)`  | `handle`          | 独占所有权，经 `destroy` 释放                                     |
| `make_copied(object, destroy, module_instance = invalid)` | `handle`          | 独立副本，经 `destroy` 释放                                       |

`handle` 字段：`object`（对消费者不透明）、`mode`、`flags`、`destroy`（owned 必填、copied 可选）、`module_instance`（诊断用：哪次加载创建了它）。

`ref` —— 运行时 owner handle：创建方的 POD `handle` 加上一个模块租约，使负责销毁的模块在对象存活期间保持加载。

| 方法                                | 返回值          | 说明                                                                                                            |
| ----------------------------------- | --------------- | --------------------------------------------------------------------------------------------------------------- |
| `adopt(handle, control = nullptr)`  | `ref`           | 收养创建方返回的句柄；owned/copied 需传创建模块的 `control` 以持有租约；borrowed 可省略                         |
| `borrow(object, control = nullptr)` | `ref`           | 便捷：`adopt(make_borrowed(object), control)`                                                                   |
| `valid()`                           | `bool`          | 对象非空                                                                                                        |
| `object()`                          | `void*`         | 对象指针                                                                                                        |
| `ownership()`                       | `mode`          | 所有权模式                                                                                                      |
| `owns()`                            | `bool`          | `owned` 或 `copied`                                                                                             |
| `leased()`                          | `bool`          | 是否持有模块租约                                                                                                |
| `raw()`                             | `const handle&` | 底层 POD 句柄                                                                                                   |
| `release()`                         | `void`          | 恰好释放一次：owned/copied 调用创建方 `destroy`，然后释放租约；borrowed 不销毁。`handle` 被清零，重复释放无操作 |

创建失败返回无效句柄（`object == nullptr`）而非半成品；`destroy` 返回 `void`——释放不会失败，任何失败都必须在创建时报告。移动 handle 即转移所有权。

### `Proxy.h` —— C++ 代理 API

一个接口代理是「对象句柄 + 已解析的适配器」的轻量封装，读起来像 `Shape*`，但不是基类——具体类型不被修改，也不使用编译器 vtable。`UniqueInterfaceProxy<Traits>` 承担绑定/状态/取入口的公共部分，每个接口只补自己的方法；`Traits` 描述接口差异：

| `Traits` 成员 | 说明                                                                                                 |
| ------------- | ---------------------------------------------------------------------------------------------------- |
| `id`          | `InterfaceId`，接口身份（由 AMC 计算）                                                               |
| `version`     | 接口版本                                                                                             |
| `signatures`  | 按 slot 排列的期望签名 `mics::util::array`；其大小同时是 proxy 的槽位容量。零哈希表示该 slot 不校验 |
| `slots`       | 类型化零基槽位表：`Traits::slots.<method>` 就是代理 `call(...)` 使用的 slot 值                       |

`UniqueInterfaceProxy<Traits>`：

| 方法                                                               | 返回值            | 说明                                                                                               |
| ------------------------------------------------------------------ | ----------------- | -------------------------------------------------------------------------------------------------- |
| `bind(registry, object, concrete_type, version = Traits::version)` | `InterfaceStatus` | 绑定并用 `Traits::signatures` 校验 provider 签名，随后丢弃签名；**失败显式可见**，同时存入代理     |
| `bind_methods(interface_id, version, methods, count, object)`      | `InterfaceStatus` | 直接绑定 provider 方法表（动态发现路径）；调用方需持有模块租约，且 id/version 必须与 `Traits` 一致 |
| `status()` / `valid()`                                             |                   | 绑定状态 / 是否可用（`ok` 且已解析）                                                               |
| `object()` / `interface_id()` / `version()` / `method_count()`     |                   | 对象句柄与契约信息（无效 proxy 返回空 id / 0 版本）                                                |
| `verify(slot)`                                                     | `InterfaceStatus` | 绑定后检查该槽位是否绑定；签名比较已在 `bind()` 阶段完成                                           |
| `call<Fn>(slot, args...)`（protected）                             | `Fn` 的返回       | 直调缓存入口（首参自动补对象句柄）；前置条件：`valid()` 且槽位已绑定                               |
| `call_checked<Fn>(out, slot, args...)`（protected）                | `InterfaceStatus` | 状态与结果分离：未绑定返回 `method_not_found`，合法结果绝不会被当成失败                            |

proxy 只保存紧凑的 `InterfaceSlotCache`：对象句柄、最高绑定槽和每槽一个入口指针。provider 签名、接口 id 与版本不会按 proxy 重复存储；它们留在 `Traits` 中，并在绑定阶段校验。

#### 独占式与共享式

ABIX 提供两种 proxy 存储：

| 形式 | 每对象大小 | 额外存储 | 适用 |
|------|-----------:|----------|------|
| `UniqueInterfaceProxy<Traits>` | `24 + 8 * N` | 无 | 一个/少量对象；每对象独立方法表；不需要共享表生命周期 |
| `SharedInterfaceProxy<Traits>` | `24` | 每实现一张 `InterfaceSlotTable`：`8 * N + 8` | 同一实现的大量对象；表构建一次后只读共享 |

**性能不是选择依据。** 在上面的延迟基准里两者都约为 0.75 ns；shared 多出的一次表加载被间接调用成本掩盖。真正需要比较的是 adapter 表的归属、绑定灵活性和总内存。

##### Unique 的优势

- **自包含**：slot 入口就在 proxy 内部，不需要额外对象保活，也不存在 `InterfaceSlotTable` 悬空的问题。
- **每对象独立绑定**：即使 `(concrete_type, interface_id, version)` 相同，不同对象也可以绑定不同的 provider 方法表。适用于多个 module 实例、热重载，或 adapter 表不同的 provider 实例。
- **设置最简单**：`Shape shape(registry, &object, concrete_id)` 就是全部绑定步骤，没有建表步骤和表生命周期约定。
- **单对象总内存更小**：shared 布局要额外保存 `8 * N + 8` 的表，因此只有一个对象时通常 unique 更小。

##### Shared 的优势

- **每对象固定 24 字节**：表随 `N` 增长，而不随对象数量增长。
- **每实现一张表**：adapter 入口可以放在静态/只读存储中，被任意多个对象复用。
- **集合内存密度更好**：controller、device、节点等多实例对象受益最大。

设对象数为 `K`、方法数为 `N`：

```text
Unique 总量 = K * (24 + 8N)
Shared 总量 = K * 24 + (8N + 8)
```

只要表被摊薄，Shared 就更省。`N >= 2` 时，`K = 2` 已经更省；`N = 1` 时 `K = 2` 持平，`K >= 3` 后 Shared 更省。只有单个对象、需要每实例独立方法表，或无法安全管理表生命周期时，Unique 才是更好的选择。

共享式在构建表时仍然校验签名；表只保留 adapter 入口。表需要构建一次，并且必须比所有引用它的 proxy 活得久；`build()` 之后表只读，可安全共享。

用 `SKL_ABIX_INTERFACE_DECLARE_BOTH` 同时生成两种代理类（`Shape` 与 `SharedShape`），或只用 `SKL_ABIX_INTERFACE_DECLARE_SHARED` 生成共享式：

```cpp
SKL_ABIX_INTERFACE_DECLARE_BOTH(Shape, SHAPE_METHODS, 1);

// 每个 (具体类型, 接口, 版本) 一张表。
InterfaceSlotTable<ShapeTraits::signatures.size()> table;
if (make_shared_slot_table<ShapeTraits>(table, registry, box_id) != InterfaceStatus::ok)
    return 1;

Box first{3, 4};
Box second{5, 6};

Shape exclusive(registry, &first, box_id);   // 自包含入口
SharedShape shared_a(table, &first);         // 对象 + 共享表
SharedShape shared_b(table, &second);        // 同一张表，不同对象
```

`SharedShape` 无论方法数多少都是 24 字节；`InterfaceSlotTable<N>` 每实现约 `8 * N + 8` 字节。只有 provider 的 adapter 表变化时才需要重建表（如果来自动态发现 module，重建/使用期间要保持 module lease）。

手写 `Traits` 时连签名与 `id` 都可以由编译期 helper 生成（它们复刻 `AMC::signature_hash` / `AMC::interface_identity` 的字节串与算法，`amc-core-test` 有跨工具一致性检查）：

```cpp
struct ShapeTraits {
    // AMC 的 int32_t TypeID 是 ABI key，不是 C++ 拼写。
    static constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
    // 方法签名也由函数生成；返回/参数 TypeID 必须与 provider 一致。
    static constexpr skl::mics::util::array sigs = {
        method_signature("area", i32, nullptr, 0),
        method_signature("size", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, sigs);   // 自动生成
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
        uint32_t size;
    };
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, sigs.size(), 0>();
};
```

`method_signature` / `canonical_type_id` 只解决**最后一跳**：它们的输入——方法名与返回/参数 TypeID——仍然是 AMC 派生的值。原始类型的 TypeID 是 ABI key（`int32_t` → `prim:i32`，随目标的位宽/符号变化），预处理器无法从 C++ 拼写推出。因此带参数、重载与复杂签名的接口仍应由 `amc generate` 生成签名表，手写 helper 只用于简单且双方约定一致的契约。把 `ShapeTraits` 交给 `UniqueInterfaceProxy<ShapeTraits>` 即可得到 `Shape` 代理，方法为「对象句柄在首位」的直通调用，并提供返回 `InterfaceStatus` 的重载。

#### 注册 `signatures` —— 基础类型与自定义类型的 TypeID

`signatures` 按槽位索引，条目必须按 slot 顺序写。每个条目会与 provider 的 `InterfaceMethod.signature` 比较；`interface_id` 也会哈希同一组值。不符时返回 `signature_mismatch`，绝不会被静默调用。

`method_signature(name, return_type, parameters, count)` 复刻 `AMC::signature_hash`：

```
name + decimal(return_type.lo) + decimal(return_type.hi)
     + 每个参数：decimal(type.lo) + decimal(type.hi)
```

域是 `0x5349474E41545552`（`"SIGNATUR"`），参数名不参与哈希。`name`/`parameters` 是 AMC 记录的**契约函数**的名字与参数，不是随手写的标签。若该函数的首参是对象句柄（常见的适配器约定），句柄就是第 0 个参数；自由函数没有句柄。AMC 生成的头文件里已经有精确的 `signatures` 数组，不确定时直接抄。

参数表有三种等价、零分配的形式：

```cpp
constexpr TypeId params[] = {i32, i32};
method_signature("resize", v, params, 2);                 // 指针 + 数量
method_signature("resize", v, mics::util::array{i32, i32});     // 内联定长数组
method_signature("resize", v, i32, i32);                  // 变参
```

`mics::util::array` 是 `SKL/MICS/Util/Array.h` 里的最小定长类型；其 deduction guide 让 `mics::util::array{i32, i32}` 自己推导类型。三种写法哈希相同的字节，且都不分配内存。

签名里是 **TypeID**，不是 C++ 类型。对基于名字的身份，`model::canonical_type_id(规范名)` 可以复刻 AMC 的值：

| C++ 类型               | 规范名                        | 说明                                                 |
| ---------------------- | ----------------------------- | ---------------------------------------------------- |
| `int` / `int32_t`      | `prim:i32`                    | 整数 key 是 ABI 推导的**位宽**；ABI 相同的拼写会合并 |
| `uint64_t`             | `prim:u64`                    |                                                      |
| `double`               | `prim:fieee64`                | 浮点 key 保留格式（`ieee`/`bfloat`/`x87`/`ppc`）     |
| `bool`                 | `prim:bool`                   |                                                      |
| `void`                 | `prim:void`                   | 仅返回位置                                           |
| `char`                 | `prim:char_s` / `prim:char_u` | 符号性由目标决定                                     |
| `cv::Point`            | `cv::Point`                   | 限定名                                               |
| `std::errc`            | `std::errc`                   | 枚举；`[[expose]]` 会拒绝裸 `std::*` 参数（见下）    |
| `std::string`          | `abix_str_utf8`               | `[[adapt]]` 的 target；`std::string` 本身会被拒绝    |
| `std::vector<int32_t>` | `abix_span_i32`               | 经 `[[adapt]]` / `[[instantiate]]`                   |
| `T*` / `const T*`      | Clang 的指针拼写              | 不适合手写，从 `amc query` / `amc-dump` 读           |

**自定义值类型 —— `cv::Point`**：普通值记录可表示，TypeID 是限定名：

```cpp
constexpr model::TypeId void_id = model::canonical_type_id("prim:void");
constexpr model::TypeId point   = model::canonical_type_id("cv::Point");
constexpr model::TypeId move_params[] = {point};
method_signature("move", void_id, move_params, 1);   // move(cv::Point)
```

如果契约传 `cv::Point*`，参数要用指针的 TypeID，而不是 `cv::Point` 的；从 `amc query` / `amc-dump` 读，不要猜 Clang 的拼写。

**字符串 —— `std::string`**：STL 类型不能裸过边界，必须声明映射：

```toml
[[adapt]]
native = "std::string"
target = "abix_str_utf8"
```

thunk 接收 `abix_str_utf8`（data + len + encoding），签名用这个边界 TypeID：

```cpp
constexpr model::TypeId i32  = model::canonical_type_id("prim:i32");
constexpr model::TypeId text = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId open_params[] = {text};
method_signature("label", text, nullptr, 0);       // 返回借用视图
method_signature("open", i32, open_params, 1);     // open(abix_str_utf8) -> status
```

适配器由 AMC 生成并链接，负责把边界值转成 `std::string`/`std::string_view`。只有参数会被适配；返回的视图遵循 borrow/own/copy 所有权契约，消费者要长期持有就必须复制或接管。

**枚举 —— `std::errc`**：枚举有 TypeID（`canonical_type_id("std::errc")`），但 AMC 的 `[[expose]]` 可表示性检查会拒绝裸 `std::*` 参数。错误状态请传底层整数（`prim:i32`），并用 `[[error]]` （`status`/`success`/`value`）描述，或返回 `abix_result_*`；错误边界用 `Result<T, E>` 承载，不暴露枚举布局。手写 provider/consumer 双方约定一致时可以直接用枚举 TypeID，但它不会匹配 AMC `[[expose]]` 生成的 thunk。

**完整示例**：`draw()`、`move(cv::Point)`、`label() -> abix_str_utf8`、`open(abix_str_utf8) -> int32_t`（`std::errc` 码）：

```cpp
constexpr model::TypeId i32   = model::canonical_type_id("prim:i32");
constexpr model::TypeId v     = model::canonical_type_id("prim:void");
constexpr model::TypeId point = model::canonical_type_id("cv::Point");
constexpr model::TypeId text  = model::canonical_type_id("abix_str_utf8");
constexpr model::TypeId move_params[] = {point};
constexpr model::TypeId open_params[] = {text};

static constexpr mics::util::array sigs = {
    method_signature("draw",  v,    nullptr,     0),
    method_signature("move",  v,    move_params, 1),
    method_signature("label", text, nullptr,     0),
    method_signature("open",  i32,  open_params, 1),
};
static constexpr InterfaceId id = interface_id("Canvas", 1, sigs);
```

若 provider 是 AMC 生成的、契约函数首参是对象句柄，请把句柄 TypeID 加到每个参数表的最前面（或直接抄生成的 `signatures` 数组）。

**拿到精确值**：指针、typedef、模板别名、嵌套记录不要猜规范拼写，从 artifact 读：

```sh
amc query module.abix --type cv::Point     # id=0X... name=cv::Point
amc-dump module.abix                       # 完整 section dump
```

`amc-core-test` 用固定向量把 `method_signature` 对到 `AMC::signature_hash`、`canonical_type_id` 对到 `AMC::hash_text`，helper 不会和生成器漂移。

`amc generate -l cpp` 会为 `[[interface]]` 生成与上面等价的 `Traits`，并在**每个方法首参都是对象句柄**时一并生成 `UniqueInterfaceProxy` 派生类（否则只生成 `Traits`，并注明原因）。

#### 三种写法对比

同一件事——三个形状算面积、按多态方式调用——三种写法，输出一致（`area=25`、`area=24`、`area=12`）。

左：C++ 虚函数；中：ABIX 手动注册（手写适配器 + `InterfaceRegistry` + `InterfaceRef`）；右：ABIX 代理（`Traits` + `UniqueInterfaceProxy`）。

<table>
<tr>
<th align="left">C++ 虚函数</th>
<th align="left">ABIX：手动注册</th>
<th align="left">ABIX：代理（Proxy.h）</th>
</tr>
<tr>
<td valign="top" markdown="1">

```cpp
#include <cstdio>

struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Rectangle : Shape {
    Rectangle(int w, int h) : width(w), height(h) {}
    int area() const override { return width * height; }
    int width, height;
};
struct Triangle : Shape {
    Triangle(int b, int h) : base(b), height(h) {}
    int area() const override { return base * height / 2; }
    int base, height;
};
int main() {
    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4};
    const Shape *shapes[] = {&square, &rectangle, &triangle};
    for (const Shape *shape : shapes) std::printf("area=%d\n", shape->area());
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
#include <cstdio>
#include "SKL/ABIX/Interface/Interface.h"

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");
constexpr auto area_sig = method_signature("area", i32, nullptr, 0);
constexpr InterfaceId kShape = interface_id("Shape", 1, area_sig);
constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId rectangle_id{0x12, 0x00};
constexpr TypeId triangle_id{0x13, 0x00};
using AreaFn = int (*)(const void *);

struct Square {
    int side;
};
struct Rectangle {
    int width;
    int height;
};
struct Triangle {
    int base;
    int height;
};

int square_area(const void *self) {
    const auto *s = static_cast<const Square *>(self);
    return s->side * s->side;
}
int rectangle_area(const void *self) {
    const auto *r = static_cast<const Rectangle *>(self);
    return r->width * r->height;
}
int triangle_area(const void *self) {
     const auto *t = static_cast<const Triangle *>(self);
    return t->base * t->height / 2;
}

int main() {
    const InterfaceMethod square_methods[] = {{area_sig, interface_entry(&square_area), 0, 0}};
    const InterfaceMethod rectangle_methods[] = {{area_sig, interface_entry(&rectangle_area), 0, 0}};
    const InterfaceMethod triangle_methods[] = {{area_sig, interface_entry(&triangle_area), 0, 0}};

    InterfaceRegistry<4> registry;
    if (registry.add({kShape, square_id, square_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;
    if (registry.add({kShape, rectangle_id, rectangle_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;
    if (registry.add({kShape, triangle_id, triangle_methods, 1, 1, 0}) != InterfaceStatus::ok) return 1;

    Square square{5};
    Rectangle rectangle{4, 6};
    Triangle triangle{6, 4};
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        InterfaceRef<2> ref;
        if (ref.resolve(registry, c.object, c.type, kShape, 1) != InterfaceStatus::ok) return 1;
        const auto area = ref.method<AreaFn>(0);
        std::printf("area=%d\n", area(ref.object()));
    }
    return 0;
}
```

</td>
<td valign="top" markdown="1">

```cpp
// 契约、适配器、方法表与注册同中栏
struct ShapeTraits {
    static constexpr skl::mics::util::array signatures = {area_sig};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};
class Shape : public UniqueInterfaceProxy<ShapeTraits> {
    using Base = UniqueInterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::slots.area); }
    InterfaceStatus area(int &out) const noexcept {
        return this->template call_checked<int (*)(const void *)>(out, ShapeTraits::slots.area);
    }
};
int main() {
    // registry / square / rectangle / triangle 同中栏
    struct Case {
        void *object;
        TypeId type;
    };
    const Case cases[] = {{&square, square_id}, {&rectangle, rectangle_id}, {&triangle, triangle_id}};
    for (const Case &c : cases) {
        Shape shape(registry, c.object, c.type);
        std::printf("area=%d\n", shape.valid() ? shape.area() : -1);
    }
    return 0;
}
```

</td>
</tr>
</table>

三者对照：

- **C++ 虚函数**把多态编码进**类型系统**：实现类必须继承 `Shape`，分派靠编译器生成的 vtable。
- **ABIX 手动注册**是最底层的显式步骤（写适配器 → 注册 → 解析 → 调用），无额外抽象；`adapter` 的首参是对象句柄。
- **ABIX 代理**把绑定/状态/调用封装成一个类型：具体类型仍是普通 `struct`（不继承、不含虚函数），绑定一次后 `shape.area()` 即普通原生间接调用；`Traits` 可手写，也可由 `amc generate -l cpp` 生成。

### `Declaration.h` —— 宏声明与注册

`Declaration.h` 是一层可选的宏语法糖。它不引入新的运行时机制，而是把下面这些原本要手写的内容在一次展开中生成出来：

- 每个方法的 `InterfaceMethodHash` 签名表 `signatures`
- 接口身份 `id`
- 零基槽位表 `Slots` / `slots`
- `Traits` 结构
- 继承 `UniqueInterfaceProxy<Traits>` 的代理类，以及每个方法的类型化转发入口

生成出来的 `Traits::id` / `signatures` / `slots` 仍然通过普通的
`InterfaceRegistry`（或 `ModuleRegistry` + `bind_methods`）注册和绑定，和手写 `Traits` 走完全相同的路径。宏只负责“少写声明代码”，不负责运行时注册。

> 宏只能看到方法名、C++ 返回类型、返回类型的 TypeID 和参数 TypeID，看不到 C++ 参数类型。因此它适合简单的值参数契约；复杂签名、重载、引用/记录参数和 `void` 返回仍应由 `amc generate -l cpp` 或手写代理生成。

#### 1. 方法列表

先定义一个有且只有一个参数的 `X` 宏：

```cpp
constexpr skl::abix::model::TypeId i32 =
    skl::abix::model::canonical_type_id("prim:i32");
constexpr skl::abix::model::TypeId v =
    skl::abix::model::canonical_type_id("prim:void");

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})
```

每个条目的形状是：

```text
X(method_name, cxx_return_type, return_type_id, {parameter_type_ids...})
```

| 字段              | 含义                                                                           |
| ----------------- | ------------------------------------------------------------------------------ |
| `method_name`     | 生成的代理方法名，同时也是 `Slots` 里的字段名                                  |
| `cxx_return_type` | 代理方法的 C++ 返回类型，例如 `int32_t`                                        |
| `return_type_id`  | 参与 `method_signature` 的返回类型 `model::TypeId`，必须与 provider / AMC 一致 |
| `{...}`           | 参与签名的参数 TypeID 列表；空 `{}` 表示无参数。顺序必须与契约一致             |

注意：

- `method_name` 所在条目在**宏列表里的顺序**决定 slot：第一条是 slot 0，第二条是 slot 1，以此类推。
- `{...}` 里的元素是 `model::TypeId`，不是 C++ 类型名。若 provider 由 AMC 生成，直接从生成的 `signatures` 或 TypeID 写法抄写。
- 列表宏会被 `TRAITS`、`SLOTS`、`DATA` 多次展开，因此只能包含声明条目，不能有运行时副作用。

#### 2. 一行生成 Traits 和代理

```cpp
#include "SKL/ABIX/Interface/Declaration.h"

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);
```

`SKL_ABIX_INTERFACE_DECLARE(storage, type, list, version, ...)` 等价于：

```cpp
SKL_ABIX_INTERFACE_TRAITS(type, list, version, __VA_ARGS__);
SKL_ABIX_INTERFACE_##storage##_DATA(type, list, __VA_ARGS__);
```

`storage` 可以是 `UNIQUE` 或 `SHARED`。`UNIQUE` 生成内联 `type` 代理；`SHARED` 生成 `Shared##type`。`SKL_ABIX_INTERFACE_DECLARE_SHARED` / `SKL_ABIX_INTERFACE_DECLARE_BOTH` 是便捷写法；`_BOTH` 同时生成内联代理和共享代理。

第四个及之后的参数是可选的**类型属性/导出属性**，会原样插到生成的 `typeTraits` 和 `type` 之前：

```cpp
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1, SHAPE_API);
// 展开为：
// struct SHAPE_API ShapeTraits { ... };
// struct SHAPE_API Shape : public UniqueInterfaceProxy<ShapeTraits> { ... };
```

不传属性时保持原来的写法：

```cpp
SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);
```

它展开后的核心结构如下（省略了宏自身的 `X(...)` 展开细节）：

```cpp
struct Shape;

struct ShapeTraits {
    static constexpr auto signatures = skl::mics::util::array{
        skl::abix::poly::method_signature("area", i32, skl::mics::util::array{}),
        skl::abix::poly::method_signature("resize", i32, skl::mics::util::array{i32, i32}),
    };
    static constexpr auto version = 1;
    static constexpr auto id = skl::abix::poly::interface_id("Shape", version, signatures);

    struct alignas(8) Slots {
        uint32_t area;
        uint32_t resize;
    };
    alignas(8) static constexpr Slots slots =
        skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};

struct Shape : public skl::abix::poly::UniqueInterfaceProxy<ShapeTraits> {
    using UniqueInterfaceProxy<ShapeTraits>::UniqueInterfaceProxy;
    using Traits = ShapeTraits;

    // 每个方法生成两个重载，见下一节
};
```

几个关键点：

- `id` 由 `interface_id(#type, version, signatures)` 计算。`#type` 是宏参数的字符串化结果，所以要把宏调用放在目标命名空间内，并使用**非限定名** `Shape`，这样得到的接口名与 AMC 记录的名字一致。写成 `SKL_ABIX_INTERFACE_DECLARE(UNIQUE, ns::Shape, ...)` 会让 `#type` 变成 `"ns::Shape"`，通常不是想要的接口名。
- `signatures` 的顺序就是 slot 顺序；`slots.area == 0`、`slots.resize == 1`。
- 方法顺序、方法名或版本一旦变化，`signatures`/`id` 都会变化，属于 ABI 不兼容变更。不要为了“整理代码”重排列表。
- `SKL_ABIX_INTERFACE_DECLARE(...)` 末尾需要分号，因为生成的结构体定义没有自带分号。

**Windows DLL 导入/导出属性**

`SKL_ABIX_INTERFACE_DECLARE`、`SKL_ABIX_INTERFACE_TRAITS` 和 `SKL_ABIX_INTERFACE_UNIQUE_DATA` / `SHARED_DATA` 的最后一个可变参数就是为 Windows 这类平台准备的。它会被原样放在生成的类型名之前，因此可以传入 `__declspec(dllimport)`、`__declspec(dllexport)` 或项目自己的 API 宏：

```cpp
// shape_api.h
#if defined(_WIN32)
#  ifdef SHAPE_BUILD_DLL
#    define SHAPE_API __declspec(dllexport)
#  else
#    define SHAPE_API __declspec(dllimport)
#  endif
#else
#  define SHAPE_API
#endif

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1, SHAPE_API);
```

- provider 编译 DLL 时定义 `SHAPE_BUILD_DLL`，得到 `dllexport`。
- 消费方不定义它，得到 `dllimport`。
- 非 Windows 平台上 `SHAPE_API` 为空，宏展开不受影响。

如果分开使用两个半成品宏，属性必须同时传给两边：

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1, SHAPE_API);
SKL_ABIX_INTERFACE_UNIQUE_DATA(Shape, SHAPE_METHODS, SHAPE_API);
```

需要澄清的是：ABIX Interface 本身是 header-only 的，`ShapeTraits` / `Shape` 通常只是编译期类型和 inline 转发方法，**正常情况下不需要 DLL 导入/导出属性**。只有当你确实要把这个生成类型作为 DLL 边界的一部分、并接受 MSVC 对 imported class 的 inline/static 成员规则时才使用它。

另外，`SKL_ABIX_DLL_EXPORT`（见 `SKL/ABIX/Util/Config.h`）用于 provider 的固定 C 入口，例如：

```cpp
extern "C" SKL_ABIX_DLL_EXPORT const void *shape_instance(void);
```

它和这里的接口类型属性是两件事：前者导出 `abi_get_table` / `abix_module_query_v1` 这类按名字查找的 C 符号，后者修饰生成的 `Traits` / proxy 类型。

#### 3. 生成的代理方法

对列表中的每个方法，`SKL_ABIX_INTERFACE_FORWARD` 生成两个模板重载：

```cpp
// 1) 直接返回结果：要求 proxy 已成功绑定，slot 已绑定
template<typename... Args>
auto resize(Args&&... args) const noexcept -> int32_t;

// 2) 显式返回状态：未绑定的 slot 返回 method_not_found，成功时写入 out
template<typename... Args>
auto resize(int32_t& out, Args&&... args) const noexcept
    -> skl::abix::poly::InterfaceStatus;
```

调用方式：

```cpp
Shape shape = /* bind 之后 */;

const int32_t area = shape.area();

int32_t area_out = 0;
if (shape.area(area_out) == skl::abix::poly::InterfaceStatus::ok) {
    // area_out 有效
}

const int32_t width = 800;
const int32_t height = 600;
const int32_t resized = shape.resize(width, height);

int32_t resized_out = 0;
if (shape.resize(resized_out, width, height) == skl::abix::poly::InterfaceStatus::ok) {
    // resized_out 有效
}
```

语义：

- 转发形式直接调用 `UniqueInterfaceProxy::call`，不检查 slot 是否绑定。它适合已经确认 `proxy.valid()` 且契约完整的热路径，性能和手写 `call<Fn>(Traits::slots.fn, ...)` 相同。
- 检查形式调用 `UniqueInterfaceProxy::call_checked`：slot 未绑定时返回 `InterfaceStatus::method_not_found`，不会执行空函数指针；成功时返回 `ok` 并写入 `out`。签名校验已经由 `bind()` / `bind_methods()` 在绑定阶段完成，不会每次调用重新验签。
- `ret_t` 不能是 `void`：检查形式需要 `ret_t& out`。无返回值的方法请使用 AMC 生成的代理或手写代理。
- 代理方法的参数类型是从**调用点实参**经 `std::decay_t<Args>...` 推导出来的，宏本身并不知道契约里的 C++ 参数类型。因此调用时应传入与 adapter 原生签名一致的类型（例如 `int32_t` 变量，而不是 `double` 或大小不同的整数）。类型不一致时不会在宏层面报错，但会构造出错误的函数指针类型。

#### 3.1 从现有类成员函数生成适配器

如果 provider 已经有普通 C++ 类，不想再为每个方法手写 `R fn(const void*, ...)` 适配器，可以用：

```cpp
SKL_ABIX_INTERFACE_MEMBER_ENTRY(Traits, slot_name, member_pointer_expression)
```

它生成一条 `InterfaceMethod`，内部用 `member_adapter<Member>::invoke` 转发到成员函数。例如：

```cpp
struct Box {
    int32_t width;
    int32_t height;

    int32_t area() const noexcept { return width * height; }

    int32_t resize(int32_t w, int32_t h) noexcept {
        width = w;
        height = h;
        return width * height;
    }
};
```

接口契约仍然由 `Traits` 描述，slot 名来自 `Traits::slots`：

```cpp
const InterfaceMethod methods[] = {
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &Box::area),
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, resize, &Box::resize),
};

InterfaceRegistry<4> registry;
if (registry.add({ShapeTraits::id, box_id, methods, 2, ShapeTraits::version, 0})
    != InterfaceStatus::ok)
    return 1;

Box box{3, 4};
Shape shape(registry, &box, box_id);
if (!shape.valid()) return 2;

const int32_t area = shape.area();                 // Box::area()
const int32_t resized = shape.resize(800, 600);   // Box::resize()
```

方法表和注册记录也有更短的写法：

```cpp
#define BOX_METHODS(X)                   \
    X(ShapeTraits, area, &Box::area)     \
    X(ShapeTraits, resize, &Box::resize)

SKL_ABIX_INTERFACE_MEMBER_TABLE(Shape, BOX_METHODS);

registry.add(SKL_ABIX_INTERFACE_IMPLEMENTATION(Shape, box_id, 0));
```

`SKL_ABIX_INTERFACE_MEMBER_TABLE(interface, list)` 把列表展开为具名的
`interface##Methods`（此处为 `ShapeMethods`），类型是
`mics::util::array<InterfaceMethod, N>`。`SKL_ABIX_INTERFACE_IMPLEMENTATION`
组合 `interface##Traits`、该表和具体类型的 `TypeId` 表达式，因此示例实际读取的是
`ShapeTraits::id`、`box_id`、`ShapeMethods` 和 `ShapeTraits::version`。

生成的适配器函数形状是：

```cpp
int32_t invoke(const void *self, int32_t width, int32_t height);
```

规则与限制：

- 支持 `const` / 非 `const` 成员函数，以及它们的 `noexcept` 版本。实现使用单一 `noexcept(Noex)` 特化，避免 Clang 把 `const` 与 `const noexcept` 判成歧义偏特化，因此不需要移除成员上的 `noexcept`。
- 除实例成员外，也支持静态成员函数。静态成员没有隐式对象，生成的 adapter 会接收接口句柄但忽略它：

  ```cpp
  struct StaticBox { static int32_t area() noexcept; };
  SKL_ABIX_INTERFACE_MEMBER_ENTRY(ShapeTraits, area, &StaticBox::area)
  ```

  如果静态函数本身首参就是对象句柄，请直接使用 `interface_entry(&StaticBox::area)`。

- 非 `const` 成员函数会从 `const void*` 句柄内部 `const_cast` 回 `T*`；`const` 成员函数保持 `const T*`。
- 适配器固定使用 `const void*` 首参，正好匹配 `SKL_ABIX_INTERFACE_DECLARE` 生成的 proxy。若手写 proxy 的首参是 `void*`，请手写 wrapper。
- 不处理 `volatile`、引用限定成员（`&` / `&&`）以及重载。重载需要显式成员指针转换，并把整个表达式用括号包住：

    ```cpp
    SKL_ABIX_INTERFACE_MEMBER_ENTRY(
        ShapeTraits, resize,
        (static_cast<int32_t (Box::*)(int32_t, int32_t)>(&Box::resize)))
    ```

- 它只生成 adapter entry，不生成 `Traits`、方法签名或 `id`。这些仍来自 `SKL_ABIX_INTERFACE_DECLARE` / AMC。
- `slot_name` 是接口槽位字段名，可以和成员函数名不同；`member_pointer_expression` 决定实际调用的成员。

#### 4. 注册与绑定：完整示例

宏只生成声明；provider 仍要提供 adapter，并把生成的 `id` / `signatures` / `slots` 注册到 `InterfaceRegistry`：

```cpp
#include "SKL/ABIX/Interface/Declaration.h"

namespace demo {

using namespace skl::abix::poly;
using skl::abix::model::TypeId;

constexpr TypeId i32 = skl::abix::model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X)       \
    X(area, int32_t, i32, {}) \
    X(resize, int32_t, i32, {i32, i32})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, SHAPE_METHODS, 1);

// Provider：普通聚合，没有任何 ABIX 成员，也不继承 Shape。
struct Box {
    int32_t width;
    int32_t height;
};

// adapter 的首参是对象句柄。
int32_t box_area(const void *self) {
    const auto *box = static_cast<const Box *>(self);
    return box->width * box->height;
}

int32_t box_resize(void *self, int32_t width, int32_t height) {
    auto *box = static_cast<Box *>(self);
    box->width = width;
    box->height = height;
    return box->width * box->height;
}

constexpr TypeId box_id{0x51, 0x01};

int run() {
    // slot 值、签名都来自生成的 Traits，避免手写魔法数字。
    const InterfaceMethod methods[] = {
        {ShapeTraits::signatures[ShapeTraits::slots.area],
            interface_entry(&box_area), ShapeTraits::slots.area, 0},
        {ShapeTraits::signatures[ShapeTraits::slots.resize],
            interface_entry(&box_resize), ShapeTraits::slots.resize, 0},
    };

    InterfaceRegistry<4> registry;
    if (registry.add({ShapeTraits::id, box_id, methods, 2, ShapeTraits::version, 0})
        != InterfaceStatus::ok)
        return 1;

    Box box{3, 4};
    Shape shape(registry, &box, box_id);
    if (!shape.valid()) return 2;

    // 热路径：缓存后的原生间接调用。
    const int32_t area = shape.area();

    // 带状态路径：未绑定 slot 返回 method_not_found。
    int32_t checked = 0;
    if (shape.area(checked) != InterfaceStatus::ok) return 3;

    const int32_t width = 8;
    const int32_t height = 6;
    const int32_t resized = shape.resize(width, height);

    int32_t checked_resize = 0;
    if (shape.resize(checked_resize, width, height) != InterfaceStatus::ok) return 4;

    return area == 12 && checked == 12 && resized == 48 && checked_resize == 48 ? 0 : 5;
}

}   // namespace demo
```

对于动态发现的 module，如果 adapter 表来自 manifest，可以绕过 `InterfaceRegistry`：

```cpp
Shape shape;
if (shape.bind_methods(ShapeTraits::id, ShapeTraits::version, methods, 2, &box)
    != InterfaceStatus::ok)
    return 1;
```

`bind_methods` 直接使用 provider 拥有的方法表；调用期间必须持有对应 module 的 lease，否则模块卸载后缓存的函数指针会悬空。

#### 5. 版本与接口身份

`SKL_ABIX_INTERFACE_DECLARE` / `SKL_ABIX_INTERFACE_TRAITS` 的第三个参数是 `version`，它同时用于：

- `Traits::version`
- `interface_id(#type, version, signatures)` 的身份计算
- registry 注册时的版本匹配

小版本迭代可以直接写整数；需要语义版本时使用辅助宏：

```cpp
constexpr unsigned int version = SKL_ABIX_VERSION_NUMBER(1, 2, 3);
// version == 10'200'003

uint32_t major = 0, minor = 0, patch = 0;
SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch);   // 调用方补分号
// major == 1, minor == 2, patch == 3
```

`SKL_ABIX_VERSION_NUMBER(major, minor, patch)` 的打包规则：

```text
major * 10'000'000 + minor * 100'000 + patch
```

`major` / `minor` 只保留两位十进制数字，`patch` 保留五位。`SKL_ABIX_VERSION_EXTRACT` 是它的逆操作，必须传入三个可赋值的变量，且调用方补末尾分号。

接口身份与版本的关系：

- 同名接口、不同版本 → 不同 `id`。
- 同版本但方法列表变化 → 不同 `id`。
- 方法重排 → `signatures` 顺序变化 → 不同 `id`，同时 slot 含义改变，必须视为不兼容变更。
- 如果另一个 TU 用 `amc generate -l cpp` 生成了同一个契约，它生成的 `kNameInterface` 应与宏计算的 `interface_id("Name", version, signatures)` 相等。可以用 `static_assert` 固定这一点。

#### 6. 分步使用：`TRAITS` 与 `DATA`

如果不想一次生成代理类，可以分开使用：

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1);
```

只生成 `ShapeTraits`（以及前置声明 `class Shape;`），不生成 `Shape` 代理。适合：

- 只需要注册，不需要 C++ 代理；
- 想手写一个只暴露部分方法的代理；
- 把契约常量放在公共头，把代理放在另一个头。

```cpp
SKL_ABIX_INTERFACE_TRAITS(Shape, SHAPE_METHODS, 1);
SKL_ABIX_INTERFACE_UNIQUE_DATA(Shape, SHAPE_METHODS);
```

`SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list)` 只生成内联代理类，要求 `type##Traits` 已经存在。`SKL_ABIX_INTERFACE_SHARED_DATA(type, list)` 用同一份 Traits 生成共享式 `Shared##type`。`SKL_ABIX_INTERFACE_DECLARE` / `_SHARED` / `_BOTH` 分别组合对应步骤。

#### 7. 内部展开宏

下面这些宏通常不直接使用，但理解它们有助于排查展开错误：

| 宏                                                          | 作用                                                                                                           |
| ----------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `SKL_ABIX_INTERFACE_METHOD(fn, ret_t, reg_ret_t, ...)`      | 生成 `method_signature(#fn, reg_ret_t, mics::util::array __VA_ARGS__),` 这一项                                |
| `SKL_ABIX_INTERFACE_SLOT(fn, ...)`                          | 在 `Slots` 中生成 `uint32_t fn;`                                                                               |
| `SKL_ABIX_INTERFACE_SLOTS(list)`                            | 生成 `Slots` 和 `slots = make_iota<Slots, signatures.size(), 0>()`                                             |
| `SKL_ABIX_INTERFACE_FORWARD(fn, ret_t, ...)`                | 生成该方法的返回值形式和 `InterfaceStatus` 检查形式                                                            |
| `SKL_ABIX_INTERFACE_MEMBER_ENTRY(Traits, slot, member)`     | 生成一条 `InterfaceMethod`，用 `member_adapter<member>::invoke` 适配现有成员函数                               |
| `SKL_ABIX_INTERFACE_MEMBER_TABLE(interface, list)`          | 由一组 `MEMBER_ENTRY` 条目生成具名的 `interface##Methods`（`mics::util::array<InterfaceMethod, N>`）          |
| `SKL_ABIX_INTERFACE_IMPLEMENTATION(interface, type, flags)` | 由 `interface##Traits`、`interface##Methods`、类型 ID 和 flags 生成 `InterfaceImplementation` 初始化器        |
| `SKL_ABIX_INTERFACE_TRAITS(type, list, ver, ...)`           | 生成 `typeTraits`（含 `signatures`、`version`、`id`、`slots`）；`...` 作为类型属性放在 `typeTraits` 前         |
| `SKL_ABIX_INTERFACE_UNIQUE_DATA(type, list, ...)`           | 生成继承 `UniqueInterfaceProxy<typeTraits>` 的 `type` 代理类；`...` 作为类型属性放在 `type` 前                 |
| `SKL_ABIX_INTERFACE_SHARED_DATA(type, list, ...)`           | 生成继承 `SharedInterfaceProxy<typeTraits>` 的 `Shared##type` 代理类；`...` 作为类型属性放在 `Shared##type` 前 |
| `SKL_ABIX_INTERFACE_DECLARE(storage, type, list, ver, ...)` | `TRAITS` + `SKL_ABIX_INTERFACE_<storage>_DATA`；`storage` 为 `UNIQUE` 或 `SHARED`                              |
| `SKL_ABIX_INTERFACE_DECLARE_UNIQUE(type, list, ver, ...)`   | `TRAITS` + `UNIQUE_DATA`                                                                                       |
| `SKL_ABIX_INTERFACE_DECLARE_SHARED(type, list, ver, ...)`   | `TRAITS` + `SHARED_DATA`                                                                                       |
| `SKL_ABIX_INTERFACE_DECLARE_BOTH(type, list, ver, ...)`     | `TRAITS` + `UNIQUE_DATA` + `SHARED_DATA`                                                                       |

宏展开的常见错误：

- 忘记在 `SKL_ABIX_INTERFACE_DECLARE` / `SKL_ABIX_INTERFACE_UNIQUE_DATA` / `SHARED_DATA` 后写分号。
- 列表条目的参数 TypeID 用了 C++ 类型名（例如 `int32_t`）而不是 `model::TypeId` 变量（例如 `i32`）。
- 在列表宏里使用了带逗号的模板参数，导致宏参数被拆开；必要时用括号保护或改用 AMC。
- 方法名与已有宏/类型冲突；列表里的 `fn` 会直接成为成员名和 `Slots` 字段名。

#### 8. 限制与何时改用 AMC

宏适合：

- 只有值参数、没有重载的简单契约；
- header-only 消费方，不想依赖 `amc generate`；
- 需要快速写一个测试 fixture 或示例。

宏不适合：

- `void` 返回（检查重载需要 `ret_t&`）；
- 引用、数组、函数指针、记录/结构体参数（`std::decay_t` 会改变类型）；
- 重载方法（同名条目会生成重复成员）；
- 需要与编译器 AST 完全一致的复杂 ABI 签名；
- 需要自动发现/生成 manifest 的 provider。

这些情况直接使用 `amc generate -l cpp`，或按 [`Proxy.h`](../../ABIX/Interface/Proxy.h) 的约定手写 `Traits` 和代理类。AMC 具有 Clang AST，能拿到真实的 C++ 参数类型，生成的签名和 adapter typedef 不受宏的文本限制。

## 它不是什么

- 不是共享对象模型，也不是对象生命周期运行时。
- 不是逐次调用查找、名比较或类型检查。
- 不承诺理解任意编译器的私有对象布局：若生产者没有提供适配器，ABIX 不会凭空推断。

## 相关

- [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) —— 契约、扩展目录与 Interface 契约（身份/槽位稳定/签名校验）。
- [`std.md`](std_zh.md) —— ABIX-STD 原语与 borrow/owned/copy 契约。
- [`abic.md`](abic_zh.md) —— `[[interface]]` / `[[implements]]` 构建声明。
- [`without_amc.md`](without_amc_zh.md) —— 只依赖 header-only ABIX、不使用 AMC 工具链。
- [ngcpp/proxy](https://github.com/ngcpp/proxy) —— C++20 facade/指针式运行时多态库（见上文对比）。
