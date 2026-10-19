# ABIX Interface — 非侵入式多态

<p align="center">
  <a href="interface.md">English</a> · 中文
</p>

ABIX Interface 层定义**跨语言接口契约**与**方法分派**机制。它是 ABIX 的一层，而不是任何编译器对象模型
的一部分：接口是一份独立契约，实现方类型不需要为它做任何修改。

它位于 `ABIX/Interface/`（命名空间 `skl::abix::poly`），由 `ABIX/ABIX.h` 暴露。

## 为什么统一 Interface，而不是统一 vtable

C++ 虚调用并不只是从对象取出一个函数指针：它可能涉及多重继承、`this` 调整、thunk、RTTI 以及
编译器私有的 vtable 布局。Rust trait object、Zig 函数表又是另一种表示。即使两边都叫 “vtable”，
表项顺序、指针调整和调用规则也可能不同。

因此 ABIX **不规定任何私有 vtable**，而是在 vtable 之上定义接口：

- **Interface 身份** —— 接口名、版本与有序方法签名的稳定内容哈希。不是类名，也不是 vtable 地址。
- **方法身份** —— `(interface_id, slot)`，方法签名独立校验；名称或形状相同并不等于两个接口兼容。
- **实现绑定** —— 具体类型如何到达某个方法，通过生成的适配器完成；适配器以对象句柄为第一个参数，
  绑定按签名命名，从不暴露 vtable 槽位。

这些是**契约而非布局事实**：它们不会改变 `TypeID` / `LayoutHash` / `ABIHash`。接口也可以写入
Metadata Region（扩展 kind 7–11），供消费端在运行时发现。

## 与 C++ 虚函数对比

同一件事——两个形状各自算面积，再对一组形状做多态调用——两种写法。左栏是 C++ 原生虚函数，右栏是
ABIX Interface；两段程序都输出 `area=9`、`area=12`，都能直接编译运行。

<table>
<tr>
<th align="left">C++ 虚函数（侵入式）</th>
<th align="left">ABIX Interface（非侵入式）</th>
</tr>
<tr>
<td valign="top">
<pre><code class="language-cpp">#include &lt;cstdio&gt;
struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
struct Circle : Shape {
    explicit Circle(int r) : radius(r) {}
    int area() const override { return 3 * radius * radius; }
    int radius;
};
int total(const Shape &amp;s) { return s.area(); }
int main() {
    Square sq{3};
    Circle ci{2};
    const Shape *shapes[] = {&amp;sq, &amp;ci};
    for (const Shape *s : shapes) std::printf("area=%d\n", total(*s));
    return 0;
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">#include &lt;cstdio&gt;
#include "ABIX/Interface/Interface.h"
using namespace skl::abix::poly;
using skl::abix::model::TypeId;
constexpr InterfaceId i_area{0x91, 0x00};
constexpr TypeId square_id{0x11, 0x00};
constexpr TypeId circle_id{0x12, 0x00};
using AreaFn = int (*)(const void *);
struct Square {
    int side;
};
struct Circle {
    int radius;
};
int square_area(const void *self) { 
    int a = static_cast&lt;
    const Square *&gt;(self)-&gt;side; 
    return a * a; 
}
int circle_area(const void *self) { 
    int r = static_cast&lt;
    const Circle *&gt;(self)-&gt;radius;
    return 3 * r * r;
}
int main() {
    const InterfaceMethod square_methods[] = {{{0x01, 0x00}, interface_entry(&amp;square_area), 0, 0}};
    const InterfaceMethod circle_methods[] = {{{0x01, 0x00}, interface_entry(&amp;circle_area), 0, 0}};
    InterfaceRegistry&lt;4&gt; registry;
    registry.add({i_area, square_id, square_methods, 1, 1, 0});
    registry.add({i_area, circle_id, circle_methods, 1, 1, 0});
    Square sq{3};
    Circle ci{2};
    struct Case { void *object; TypeId type; };
    const Case cases[] = {{&amp;sq, square_id}, {&amp;ci, circle_id}};
    for (const Case &amp;k : cases) {
        InterfaceRef&lt;2&gt; ref;
        ref.resolve(registry, k.object, k.type, i_area, 1);
        const auto area = ref.method&lt;AreaFn&gt;(0);
        std::printf("area=%d\n", area(ref.object()));
    }
    return 0;
}</code></pre>
</td>
</tr>
</table>

差别不在「能不能多态」，而在**多态的载体是什么**：

- 左栏把多态编码进**类型系统**：`Shape` 是基类，`Square`/`Circle` 必须继承并 `override`，`area()`
  的分派靠对象里的 vptr 指向编译器私有的 vtable。
- 右栏把多态编码进**接口契约**：`i_area` 是接口身份，方法按稳定 `slot`（此处 0）寻址，`entry` 是
  适配器；`Square`/`Circle` 是普通 `struct`，不继承、不含虚函数。分派靠 `ref.method<AreaFn>(0)`
  取出的原生入口。

### 逐项对照

| 维度 | C++ 虚函数 | ABIX Interface |
|------|-----------|----------------|
| 抽象载体 | 基类 + 纯虚函数（类型系统） | 接口身份 `InterfaceId` + 方法 `slot`（契约） |
| 对具体类型的侵入 | 必须继承、必须 `override` | 零修改；C++ `struct` / Rust / Zig 类型皆可 |
| 分派依据 | 对象内 vptr → 编译器私有 vtable 槽 | 显式解析的适配器入口 `(interface_id, slot)` |
| `this` 调整 / 多重继承 | 编译器生成 thunk，藏进 vtable | provider 的适配器承担，首参即对象句柄 |
| 解析时机 | 每次调用隐式取 vptr | `resolve()` 一次，之后零查找 |
| 异构集合 | 靠基类指针 `const Shape*` | `{ void* object, 适配器 }`，签名统一 |
| 跨语言 | 不可跨（布局/顺序各异） | 同一 `interface_id` 多语言实现 |
| 失败模式 | UB（错调、悬垂 vptr） | `InterfaceStatus`：`interface_not_found` / `version_mismatch` / `method_not_found` |
| ABI 稳定性 | 新增虚函数/改继承即 ABI break | 接口是契约，不参与 `TypeID` / `ABIHash` |

### 何时用哪个

- **纯 C++、编译期同构、无跨边界需求**：虚函数足够，零额外约定。
- **跨 DLL / 跨语言 / 需要「面向接口、不认具体类型」**：用 Interface。消费者代码里可以只出现
  `InterfaceId`（见[示例 1](#示例)的跨模块路径），provider 换了具体类型也无需重编消费者。
- **两者共存**：ABIX 不要求你放弃 C++ 语义——provider 内部照常用虚函数，只在边界处用适配器把某个
  具体类型的方法绑定到接口槽位（AMC 的 `[[interface]]` / `[[implements]]` 声明）。接口可以像 C++
  抽象类一样在头文件里声明：`[[interface]]` 定义契约、`[[implements]]` 建立绑定，`amc generate -l cpp`
  生成 `k<Name>Interface` / `k<Name>Version` / `enum class <Name>Slot` / `<Name>_<method>_fn`，消费者据此
  封装出 `Shape s = Shape::bind(...); s.area();` 这样的用法。

## 快速开始

三步：**注册**（把「某具体类型实现某接口」写入注册表）→ **解析**（一次性查表并缓存适配器入口）
→ **调用**（普通原生间接调用）。下面的完整程序不需要动态库。

```cpp
#include <cstdio>

#include "ABIX/Interface/Interface.h"

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

`resolve()` 是唯一的查表动作——`registry.lookup_count()` 始终是 `1`；之后调用不经过任何查找、
名比较或类型检查。完整的 API 见 [API 参考](#api-参考)，更多场景见[示例](#示例)。

## 绑定期解析，调用期原生调用

运行时设施刻意是*绑定期*的。`InterfaceRegistry` 保存「该具体类型实现了该接口」的记录；
`InterfaceRef` 只解析一次并按 slot 缓存适配器入口，之后每次调用都是普通原生间接调用。runtime
不会变成通用 dispatch loop。

```cpp
#include "ABIX/Interface/Interface.h"

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

`resolve()` 会区分 `interface_not_found`、`version_mismatch`、`method_not_found` 与
`method_capacity_exceeded`，而不是猜测。

## 跨模块的动态发现

生产者（动态库）暴露一个固定入口 `abix_module_query_v1`，返回稳定的 `ModuleManifest`，列出它实现的
接口。平台导出表只用于找到该入口：不解码 mangled name，也不从符号推断接口含义。

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

消费端先 `validate_manifest` 校验清单，再注册与查询。每次加载都得到新的 `ModuleInstanceId`；只要
绑定持有租约，模块就不能卸载。

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

Interface ABI 说明「怎么调用对象」，Owner ABI 说明「谁拥有、谁释放」。只正式化三种语义——
**borrowed / owned / copied**——且释放必须回到创建方的分配域：消费者不得 `free()`/`delete` 指针，也不得
按地址或布局推断析构方式。

```cpp
#include "ABIX/Interface/Owner.h"

// 生产者返回 POD handle；销毁入口位于该模块内。
skl::abix::owner::handle h = make_owned(new Renderer, &destroy_renderer, control.id);

// 消费端 adopt，并在对象存活期间持有模块租约。
skl::abix::owner::ref object = skl::abix::owner::ref::adopt(h, &control);
// ... 通过 Interface binding 使用 object.object() ...
// 释放时恰好调用一次 destroy_renderer，然后归还租约。
```

Owner handle 会在释放前保持其创建模块处于加载状态，因此长期存活的对象不会比负责销毁它的模块更长寿。
移动 handle 即转移所有权；重复释放为无操作。

## 示例

以下示例除标注的共享头文件外都是完整的（示意性的 provider 对象构造从略）。

### 示例 1 —— 跨模块发现并调用

Provider 与 consumer 共享一份契约常量头（接口身份与版本必须一致）：

```cpp
// canvas_interface.h
#pragma once
#include "ABIX/Interface/Interface.h"

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
#include "ABIX/Interface/Interface.h"
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

#include "ABIX/DLL/Object.h"
#include "ABIX/Interface/Interface.h"
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

要点：`discover_module` 只在导出表里查 `abix_module_query_v1` 一个符号；绑定持有租约期间模块无法
卸载，因此缓存的 `width` 指针永远不会悬垂。

### 示例 2 —— 一个接口的多个实现与版本共存

不经过动态库时，直接把多个 provider 的 `ModuleManifest` 注册进 `ModuleRegistry`：

```cpp
#include "ABIX/Interface/Interface.h"

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

规则：请求版本精确匹配，`0` 取最新；同一模块内重复的 `(id, version)` 被拒绝
（`duplicate_interface`），不同模块之间允许并存，查询返回全部且从不互相覆盖。

### 示例 3 —— Owner ABI：跨模块创建与释放

Provider 通过自己的入口返回一个 owner handle，销毁入口位于该模块内：

```cpp
// owner_plugin.cpp（节选）
skl::abix::owner::handle owner_counter_create(int base) {
    auto *counter = new Counter{base};
    return skl::abix::owner::make_owned(counter, &destroy_counter, /*module_instance*/ 0);
}
```

Consumer 收养句柄并持有租约，离开作用域时恰好销毁一次（承接示例 1 的 `lib` / `control` / `binding`）：

```cpp
#include <cstdio>

#include "ABIX/Interface/Owner.h"

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

以下符号由 `ABIX/ABIX.h` 引入；除 Owner ABI（`skl::abix::owner`）外，其余都在命名空间
`skl::abix::poly`。要单独引入整层，`#include "ABIX/Interface/Interface.h"`。

| 头文件 | 内容 |
|------|------|
| `ABIX/Interface/Contract.h` | 接口身份、`InterfaceMethod` / `InterfaceImplementation`、`InterfaceStatus`、`interface_entry` |
| `ABIX/Interface/Registry.h` | `InterfaceRegistry` —— 绑定期实现的注册与解析 |
| `ABIX/Interface/Ref.h` | `InterfaceRef` —— 一次解析、按槽位缓存的原生调用句柄 |
| `ABIX/Interface/Module.h` | 固定入口 `abix_module_query_v1`、`ModuleManifest`、`validate_manifest` |
| `ABIX/Interface/Lease.h` | `ModuleControl` 状态机与 RAII `ModuleLease` |
| `ABIX/Interface/ModuleRegistry.h` | `ModuleRegistry` —— 动态发现模块的接口登记与查询 |
| `ABIX/Interface/Discovery.h` | `discover_module` —— 加载 → 校验 → 注册 |
| `ABIX/Interface/Owner.h` | Owner ABI —— `handle` / `ref` 的借用/拥有/复制语义 |
| `ABIX/Interface/Proxy.h` | C++ 代理 API —— `InterfaceProxy<Traits>`、`bind`/`call`、签名校验 |
| `ABIX/Interface/Declaration.h` | 可选声明宏 —— `ABIX_DECLARE_INTERFACE`（手写签名）与 `ABIX_DECLARE_INTERFACE_AUTO`（由 `method_signature` 计算） |

### `Contract.h` —— 接口身份与方法

| 符号 | 类型 | 说明 |
|------|------|------|
| `InterfaceId` | `Hash128` | 接口身份：对接口名、版本与有序方法签名计算的内容哈希。不是类名，也不是 vtable 地址；可用 `interface_id(...)` 自动生成 |
| `InterfaceMethodHash` | `model::SignatureHash` | 单个方法的 ABI 签名哈希 |
| `interface_id(name, version, signatures, count)` | `InterfaceId` | 编译期从规范名、版本与按槽位有序的签名算出接口身份；复刻 `AMC::interface_identity`，两端无需手抄常量 |
| `InterfaceMethodKey` | `{signature, slot}` | 与槽位配对的签名，供 `interface_id` 的数组重载排序后计算身份 |
| `method_signature(name, return_type, parameters, count)` | `InterfaceMethodHash` | 编译期从方法名与返回/参数 TypeID 算出方法签名；复刻 `AMC::signature_hash`（`[[interface]]` 路径，参数名不参与哈希）。TypeID 可由 `model::canonical_type_id`（`ABI.h`）得到 |
| `interface_entry<Fn>(fn)` | `uintptr_t` | 把函数指针拷入不透明的 `entry` 槽位；生成代码与调用方共用同一拼写 |
| `to_string(status)` | `const char*` | `InterfaceStatus` 的可读名 |

`InterfaceMethod` —— 一个方法的适配器表项：

| 字段 | 类型 | 说明 |
|------|------|------|
| `signature` | `InterfaceMethodHash` | AMC 记录的方法 ABI 签名哈希 |
| `entry` | `uintptr_t` | 适配器入口，调用时第一个参数是对象句柄；`0` 表示该槽位未绑定 |
| `slot` | `uint32_t` | 稳定分派槽位 |
| `flags` | `uint32_t` | 保留 |

`InterfaceImplementation` —— 具体类型对某接口的实现：

| 字段 | 类型 | 说明 |
|------|------|------|
| `interface_id` | `InterfaceId` | |
| `concrete_type` | `model::TypeId` | 实现该接口的具体类型 |
| `methods` | `const InterfaceMethod*` | provider 拥有的方法表 |
| `method_count` | `uint32_t` | 表项个数 |
| `version` | `uint32_t` | 接口版本 |
| `flags` | `uint32_t` | 保留 |

`InterfaceStatus`：`ok`、`invalid_table`、`duplicate_implementation`、`capacity_exceeded`、
`interface_not_found`、`version_mismatch`、`method_not_found`、`method_capacity_exceeded`。

**跨工具身份契约**：`InterfaceId` 由 `AMC::interface_identity` 与
`ABIX::poly::interface_id` 共同定义，算法完全一致：

```
hash_text("ABIX-INTERFACE:" + 规范名 + ":" + 版本 + 按槽位排序的签名, 域 = 0x494E544552464143)
```

域标签是 8 字节 ASCII `"INTERFAC"`（`"INTERFACE"` 的大端截断，装不进 `uint64_t` 的是第 9 个字符 `E`），
即字节 `49 4E 54 45 52 46 41 43`。它**作为整数混入哈希初始状态**（`lo ^= 域`、`hi += 域`），
**不是**字节前缀；因此不存在字节序歧义，其他语言实现必须用同一个整数与同一混合方式，而不是
`reinterpret_cast` 整数的内存表示。

哈希种子也与 ABIX 自身的 `fnv1a` 统一：`lo` 使用标准 **FNV-1a 64 offset basis**
`0xCBF29CE484222325`，`hi` 使用 FNV prime `0x100000001B3` + 域，`hi` lane 的乘子是
XXH64 的 `PRIME64_2`。`amc-core-test` 用固定向量钉住这些常量，并校验两端一致。

### `Registry.h` —— 绑定期注册表

`InterfaceRegistry<Capacity = 64>`：固定容量、无分配。注册与解析都是 setup 工作，线性扫描即可
——它永远不在调用路径上。`add()` 是**注册**，`find()` 是**解析**。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `add(impl)` | `InterfaceStatus` | 注册一个实现。重复的 `(concrete_type, interface_id, version)` 返回 `duplicate_implementation`（冲突而非静默覆盖）；容量满返回 `capacity_exceeded`；id 为空或声明方法却空表返回 `invalid_table` |
| `find(concrete_type, interface_id, version = 0)` | `const InterfaceImplementation*` | `version == 0` 取最新注册版本，否则精确匹配；未找到返回 `nullptr` |
| `find_newest(concrete_type, interface_id)` | `const InterfaceImplementation*` | 最新版本，或 `nullptr` |
| `available(concrete_type, interface_id, newest&)` | `bool` | 是否存在任意版本；`newest` 回填最新版本号——用于区分「未知接口」与「版本不符」，无需二次全扫 |
| `size()` | `size_t` | 已注册实现数 |
| `at(index)` | `const InterfaceImplementation*` | 按下标访问，越界返回 `nullptr` |
| `lookup_count()` | `size_t` | 诊断：`find()` 被服务次数；可用来证明 resolve-then-call 只发生一次查找 |

### `Ref.h` —— 解析句柄

`InterfaceRef<MaxMethods = 32>`：一次解析把绑定的适配器入口拷入按槽位索引的缓存，之后每次调用都是
普通原生间接调用——没有逐次查找、名比较或类型检查。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `resolve<RegCap>(registry, object, concrete_type, interface_id, version = 0)` | `InterfaceStatus` | 恰好一次注册表查找；`object == nullptr` 返回 `invalid_table`；未找到时区分 `version_mismatch`（有该接口但版本不符）与 `interface_not_found` |
| `resolve_methods(interface_id, version, methods, method_count, object)` | `InterfaceStatus` | 直接从 provider 方法表解析——动态发现模块走这条路径（表来自清单，调用方持有租约）；槽位超过 `MaxMethods` 返回 `method_capacity_exceeded` |
| `method<Fn>(slot)` | `Fn` | 返回缓存入口；槽位未绑定或越界返回 `nullptr`。`Fn` 以对象句柄为首参，传 `object()` |
| `bound(slot)` | `bool` | 槽位是否已绑定 |
| `valid()` | `bool` | 是否已解析且对象非空 |
| `object()` | `void*` | provider 的对象句柄（作为调用的第一个参数） |
| `interface_id()` / `version()` | | 已解析的接口身份 / 版本 |
| `method_count()` | `uint32_t` | 实现跨越的槽位数（最高绑定槽 + 1） |

`query_interface<MaxMethods = 32, RegCap>(registry, object, concrete_type, interface_id, version = 0)`
——便利函数，按值返回 `InterfaceRef`；检查 `valid()`。

provider 对象必须比句柄长寿，且必须正是适配器所针对的对象（句柄创建者拥有正确的指针与
任何 `this` 调整）。

### `Module.h` —— 动态发现的清单

| 符号 | 类型 | 说明 |
|------|------|------|
| `ModuleInstanceId` | `uint64_t` | 每次加载唯一；同一文件加载两次或卸载后重载都得到新 id；`0` 无效 |
| `invalid_module_instance` | 常量 | |
| `abix_manifest_version` | `uint32_t` | 清单 ABI 自身版本；未知版本必须拒绝而非猜测 |
| `abix_module_query_symbol` | `const char*` | 固定入口名 `"abix_module_query_v1"` |
| `abix_max_manifest_interfaces` / `abix_max_manifest_methods` | 常量 | 上界，防止畸形清单驱动无界循环 |
| `ModuleQueryFn` | `const ModuleManifest* (*)(void)` | 固定入口签名 |
| `SKL_ABIX_DEFINE_MODULE_QUERY(manifest)` | 宏 | 定义 `extern "C"` 的 `abix_module_query_v1` |
| `ManifestStatus` | enum | `validate_manifest` 的结果；`to_string` 给出名字 |
| `validate_manifest(const ModuleManifest*)` | `ManifestStatus` | 校验清单 |

`InterfaceManifestEntry`（wire 布局固定为 40 字节）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `interface_id` | `InterfaceId` | |
| `interface_version` | `uint32_t` | 区分兼容的契约修订 |
| `flags` | `uint32_t` | 保留 |
| `methods` | `const InterfaceMethod*` | provider 拥有的适配器表 |
| `method_count` | `uint32_t` | 表项个数 |
| `reserved` | `uint32_t` | |

`ModuleManifest`（40 字节）：`manifest_version`、`module_abi_version`、`flags`、
`interface_count`、`interfaces`、`extension_count`、`reserved`、`extensions`。`extensions` 是不透明、
可选、向前兼容的区域：不认识的消费者跳过它，但仍须校验其声明的边界。

`ManifestStatus`：`ok`、`null_manifest`、`unsupported_version`、`invalid_interface`、
`invalid_binding`、`too_many_interfaces`、`too_many_methods`。

`validate_manifest` 在**不解引用任意内存**的前提下拒绝：空清单、未知 `manifest_version`、
超界的 `interface_count` / `method_count`、空 `interface_id`、声明方法却空方法表、或方法表含重复槽位。
绑定槽位 `entry == 0` 是允许的（未绑定）。它无法在一般情况下证明指针安全。

### `Lease.h` —— 模块生命周期与租约

生命周期是一个带引用计数的小状态机：

```text
loading ──activate──▶ active ──begin_unload──▶ draining ──idle──▶ unloaded
```

| 符号 | 类型 | 说明 |
|------|------|------|
| `ModuleState` | enum | `loading` / `active` / `draining` / `unloaded`；`to_string` 给出名字 |
| `ModuleControl` | struct | 加载方拥有的生命周期令牌：`id`、`name`、`state`、`leases`，以及平台 `module_handle` 与 `pin`/`unpin` 钩子。留在加载方，使其能与平台模块句柄绑定 |
| `next_module_instance_id()` | `ModuleInstanceId` | 单调递增，永不复用 |
| `module_state(control)` | `ModuleState` | 读取当前状态 |
| `activate_module(control)` | `bool` | `loading → active`（CAS）；非 loading 返回 `false` |
| `try_acquire_lease(control)` | `bool` | 仅在 `active` 时租用；递增后**再次**检查状态，避免与并发 `begin_unload` 竞态。成功时取一次 OS 级引用（`pin`），使模块在租约期间保持映射 |
| `release_lease(control)` | `void` | 释放一次租约 |
| `begin_unload(control)` | `bool` | 停止接受新租约（进入 `draining`）；已 draining/unloaded 返回 `true`，从未激活返回 `false` |
| `wait_for_idle(control, max_spins = 1000000)` | `bool` | 等待所有租约释放；有上界，避免泄漏的租约永久挂起调用方 |
| `mark_unloaded(control)` | `void` | 置为 `unloaded` |
| `ModuleLease` | RAII 类 | 可移动、不可拷贝；`valid()`、`release()`；析构释放；释放未持有的租约是无操作 |
| `acquire_lease(control)` | `ModuleLease` | 模块非 `active` 时返回无效租约（绝不返回半成品） |

租约在活跃调用或存活对象句柄期间持有，这正是缓存方法绑定安全的原因：调用方持有租约的期间，
绑定有效。

**OS 级固定（pinning）**：当加载方绑定平台身份（`Discovery.h` →
`bind_module_platform`）后，**首个**租约调用 `pin`、**末个**租约调用 `unpin`：Windows 上
`pin` 以 `GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` 解析模块基址、
`unpin` 调用 `FreeLibrary`；POSIX 上 `pin` 按加载路径重新 `dlopen(path)`（只增加引用计数、
不重跑初始化），`unpin` 调用 `dlclose`。因此持有的租约在 **OS 层直接阻止**
`FreeLibrary`/`dlclose`，而不仅靠状态机——即使拥有者在持有租约时卸载，也无法把模块解除
映射。该引用在所有租约期间只取一次，依赖加载方持有的 `module_path`，因此加载方需在租约
存续期间保持 `Object`（及其路径）存活。

### `ModuleRegistry.h` —— 动态模块接口注册表

`ModuleInterface` —— 一个已发现模块接口：`module`、`interface_id`、`version`、`methods`、
`method_count`、`control`（调用前必须持有租约的寿命令牌）。

`ModuleStatus`：`ok`、`already_registered`、`invalid_manifest`、`invalid_state`、
`capacity_exceeded`、`interface_capacity_exceeded`、`duplicate_interface`、`not_registered`、`busy`。

`ModuleRegistry<ModuleCapacity = 16, InterfaceCapacity = 128>`：

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `register_module(control, manifest)` | `ModuleStatus` | 校验清单、拒绝模块内重复的 `(id, version)`、`activate_module` 后登记；同一 control 重复注册幂等（`already_registered`）；`control` 非 loading 返回 `invalid_state` |
| `unregister_module(id)` | `ModuleStatus` | 进入 unloading：先 `begin_unload`，仅当所有租约释放后才移除；否则返回 `busy`，此时模块已 draining（拒绝新租约），调用方可在在途调用结束后重试 |
| `registered(id)` | `bool` | |
| `module_count()` / `interface_count()` | `size_t` | |
| `interface_at(index)` | `const ModuleInterface*` | 按下标访问 |
| `find_interface(id, version, out, max)` | `size_t` | 按注册顺序写入**全部**实现（`version == 0` 匹配任意）；返回写入个数（最多 `max`） |
| `find_first(id, version = 0)` | `const ModuleInterface*` | 最新实现，或 `nullptr` |

多个模块可实现同一接口，查询返回全部且从不互相覆盖；请求版本精确匹配，`0` 取最新。

`BoundInterface<MaxMethods = 32>` —— 已解析绑定 + 保持模块加载的租约；`valid()` 同时要求
`ref.valid()` 与 `lease.valid()`；只要对象存活，经 `ref` 调用就有效。
`bind_interface<MaxMethods = 32>(binding, object)` —— 获取租约并解析；模块不再活跃时返回无效绑定
（无租约、未解析）。

### `Discovery.h` —— 加载即发现

| 符号 | 类型 | 说明 |
|------|------|------|
| `DiscoveryResult` | struct | `manifest`（`ManifestStatus`）、`registration`（`ModuleStatus`）、`queried`（清单指针）、`ok()` |
| `discover_module<ModuleCapacity = 16, InterfaceCapacity = 128>(module, control, registry)` | `DiscoveryResult` | 从已加载模块解析 `abix_module_query_v1`，`validate_manifest` 校验后 `register_module` |

平台导出表**只为这一个符号**被查询：不解码任何其它导出，也不从符号推断接口含义。缺入口、
未知清单版本、结构损坏或重复声明都明确失败。成功后 `control` 变为 `active`。

### `Owner.h` —— Owner ABI

命名空间 `skl::abix::owner`。Interface ABI 说明「怎么调用对象」，Owner ABI 说明「谁拥有、谁释放、
释放代码在哪」。

| 符号 | 类型 | 说明 |
|------|------|------|
| `abix_owner_abi_version` | `uint32_t` | Owner ABI 版本 |
| `mode` | enum | `borrowed` / `owned` / `copied`；`to_string` 给出名字 |
| `destroy_fn` | `void (*)(void*)` | 创建方的销毁入口，固定 C ABI |
| `handle` | struct（32 字节）| POD 句柄：`object`、`mode`、`flags`、`destroy`、`module_instance` |
| `valid(handle)` | `bool` | `object != nullptr` |
| `make_borrowed(object)` | `handle` | 非拥有引用 |
| `make_owned(object, destroy, module_instance = invalid)` | `handle` | 独占所有权，经 `destroy` 释放 |
| `make_copied(object, destroy, module_instance = invalid)` | `handle` | 独立副本，经 `destroy` 释放 |

`handle` 字段：`object`（对消费者不透明）、`mode`、`flags`、`destroy`（owned 必填、copied 可选）、
`module_instance`（诊断用：哪次加载创建了它）。

`ref` —— 运行时 owner handle：创建方的 POD `handle` 加上一个模块租约，使负责销毁的模块在对象存活
期间保持加载。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `adopt(handle, control = nullptr)` | `ref` | 收养创建方返回的句柄；owned/copied 需传创建模块的 `control` 以持有租约；borrowed 可省略 |
| `borrow(object, control = nullptr)` | `ref` | 便捷：`adopt(make_borrowed(object), control)` |
| `valid()` | `bool` | 对象非空 |
| `object()` | `void*` | 对象指针 |
| `ownership()` | `mode` | 所有权模式 |
| `owns()` | `bool` | `owned` 或 `copied` |
| `leased()` | `bool` | 是否持有模块租约 |
| `raw()` | `const handle&` | 底层 POD 句柄 |
| `release()` | `void` | 恰好释放一次：owned/copied 调用创建方 `destroy`，然后释放租约；borrowed 不销毁。`handle` 被清零，重复释放无操作 |

创建失败返回无效句柄（`object == nullptr`）而非半成品；`destroy` 返回 `void`——释放不会失败，
任何失败都必须在创建时报告。移动 handle 即转移所有权。

### `Proxy.h` / `Declaration.h` —— C++ 代理 API

一个接口代理是「对象句柄 + 已解析的适配器」的轻量封装，读起来像 `Shape*`，但不是基类——具体类型
不被修改，也不使用编译器 vtable。`InterfaceProxy<Traits>` 承担绑定/状态/取入口的公共部分，每个接口
只补自己的方法；`Traits` 描述接口差异：

| `Traits` 成员 | 说明 |
|------|------|
| `id` | `InterfaceId`，接口身份（由 AMC 计算） |
| `version` | 接口版本 |
| `slots` | **槽位容量**（≥ 最高 slot + 1），据此确定 `InterfaceRef` 的缓存大小，不是魔法数字 |
| `signatures[]`（可选） | 每个 slot 期望的方法签名；提供时 `bind()` 会逐槽校验（`0` 表示该槽不校验） |

`InterfaceProxy<Traits>`：

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `bind(registry, object, concrete_type, version = Traits::version)` | `InterfaceStatus` | 绑定并（若有 `signatures`）校验；**失败显式可见**，同时存入代理 |
| `bind_methods(interface_id, version, methods, count, object)` | `InterfaceStatus` | 直接绑定 provider 方法表（动态发现路径）；调用方需持有模块租约 |
| `status()` / `valid()` | | 绑定状态 / 是否可用（`ok` 且已解析） |
| `object()` / `interface_id()` / `version()` / `method_count()` | | 对象句柄与契约信息 |
| `verify(slot)` | `InterfaceStatus` | 校验单个槽位：未绑定 `method_not_found`，签名不符 `signature_mismatch` |
| `call<Fn>(slot, args...)`（protected） | `Fn` 的返回 | 直调缓存入口（首参自动补对象句柄）；前置条件：`valid()` 且槽位已绑定 |
| `call_checked<Fn>(out, slot, args...)`（protected） | `InterfaceStatus` | 状态与结果分离：未绑定返回 `method_not_found`，合法结果绝不会被当成失败 |

`ABIX_DECLARE_INTERFACE`（`Declaration.h`，可选语法糖）用 X-macro 去掉样板，**不推断签名**——
签名哈希与槽位显式给出，正是预处理器能可靠完成的部分。接口身份不再手写：宏用
`interface_id("Shape", version, 方法表)` 从**规范名 + 版本 + 按槽位排序的签名**算出，与 AMC 完全一致：

```cpp
#define SHAPE_METHODS(X)                 \
    X(area, 0x1234, 0x5678, int32_t, 0)  \
    X(size, 0x9abc, 0xdef0, int32_t, 1)

ABIX_DECLARE_INTERFACE(Shape, 1, SHAPE_METHODS)
```

另一个版本用编译期函数生成签名：`ABIX_DECLARE_INTERFACE_AUTO` 的条目是
「方法名、返回 TypeID、参数 TypeID 数组、参数个数、返回类型、槽位」，签名由
`method_signature(...)` 算出，代理还会转发方法参数：

```cpp
constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
constexpr model::TypeId resize_params[] = {i32, i32};

#define SHAPE_AUTO(X)                             \
    X(area,   i32, nullptr,      0, int32_t, 0)   \
    X(resize, i32, resize_params, 2, int32_t, 1)

ABIX_DECLARE_INTERFACE_AUTO(Shape, 1, SHAPE_AUTO)

// shape.area()             对象句柄在首位，0 参数
// shape.resize(3, 4)       转发两个参数
// shape.resize(out, 3, 4)  显式状态重载
```

两个宏只差签名来源：`ABIX_DECLARE_INTERFACE` 手写 `{lo, hi}`，`ABIX_DECLARE_INTERFACE_AUTO`
用 `method_signature` 从 TypeID 计算；`id`、槽位与 `slots` 的生成方式相同。参数数组必须
是作用域内已有的名字（预处理器无法按 `{i32, i32}` 生成数组）。

手写 `Traits` 时连签名与 `id` 都可以由编译期 helper 生成（它们复刻 `AMC::signature_hash` /
`AMC::interface_identity` 的字节串与算法，`amc-core-test` 有跨工具一致性检查）：

```cpp
struct ShapeTraits {
    // AMC 的 int32_t TypeID 是 ABI key，不是 C++ 拼写。
    static constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");
    // 方法签名也由函数生成；返回/参数 TypeID 必须与 provider 一致。
    static constexpr InterfaceMethodHash sigs[] = {
        method_signature("area", i32, nullptr, 0),
        method_signature("size", i32, nullptr, 0),
    };
    static constexpr InterfaceId id = interface_id("Shape", 1, sigs, 2);   // 自动生成
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 2;
    static constexpr uint32_t area_slot = 0;
    static constexpr uint32_t size_slot = 1;
};
```

`method_signature` / `canonical_type_id` 只解决**最后一跳**：它们的输入——方法名与返回/参数
TypeID——仍然是 AMC 派生的值。原始类型的 TypeID 是 ABI key（`int32_t` → `prim:i32`，随目标的
位宽/符号变化），预处理器无法从 C++ 拼写推出。因此带参数、重载与复杂签名的接口仍应由
`amc generate` 生成签名表，手写 helper 只用于简单且双方约定一致的契约。它生成 `ShapeTraits`
（`id`/`version`/`slots`/`signatures`/`*_slot`）与一个 `Shape` 代理，方法为「对象句柄在首位」的
直通调用，并提供返回 `InterfaceStatus` 的重载。

`amc generate -l cpp` 会为 `[[interface]]` 生成与上面等价的 `Traits`，并在**每个方法首参都是对象
句柄**时一并生成 `InterfaceProxy` 派生类（否则只生成 `Traits`，并注明原因）。

#### 三种写法对比

同一件事——一个形状算面积、按多态方式调用——三种写法，输出一致（`area=25`）。

左：C++ 虚函数；中：ABIX 手动注册（手写适配器 + `InterfaceRegistry` + `InterfaceRef`）；右：ABIX 代理（`Traits` + `InterfaceProxy`）。

<table>
<tr>
<th align="left">C++ 虚函数</th>
<th align="left">ABIX：手动注册</th>
<th align="left">ABIX：代理（Proxy.h）</th>
</tr>
<tr>
<td valign="top">
<pre><code class="language-cpp">struct Shape {
    virtual ~Shape() = default;
    virtual int area() const = 0;
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int area() const override { return side * side; }
    int side;
};
int main() {
    Square sq{5};
    const Shape &amp;s = sq;                    // 多态
    std::printf("area=%d\n", s.area());     // 25
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">struct Square { int side; };
int square_area(const void *self) {
    int s = static_cast&lt;const Square *&gt;(self)-&gt;side;
    return s * s;
}
using AreaFn = int (*)(const void *);
int main() {
    const InterfaceMethod m[] = {
        {{0x1111, 0x2222}, interface_entry(&amp;square_area), 0, 0}};
    InterfaceRegistry&lt;4&gt; registry;
    registry.add({kShapeInterface, square_id, m, 1, 1, 0});
    Square sq{5};
    InterfaceRef&lt;4&gt; ref;
    ref.resolve(registry, &amp;sq, square_id, kShapeInterface, 1);
    std::printf("area=%d\n", ref.method&lt;AreaFn&gt;(0)(ref.object()));
}</code></pre>
</td>
<td valign="top">
<pre><code class="language-cpp">// Traits 可手写，或由 ABIX_DECLARE_INTERFACE / amc generate -l cpp 生成
struct ShapeTraits {
    static constexpr InterfaceId id = kShapeInterface;
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 1;
    static constexpr uint32_t area_slot = 0;
};
class Shape : public InterfaceProxy&lt;ShapeTraits&gt; {
    using Base = InterfaceProxy&lt;ShapeTraits&gt;;
public:
    using Base::Base;
    int area() const noexcept {
        return this-&gt;template call&lt;int (*)(const void *)&gt;(ShapeTraits::area_slot);
    }
    InterfaceStatus area(int &amp;out) const noexcept {
        return this-&gt;template call_checked&lt;int (*)(const void *)&gt;(out, ShapeTraits::area_slot);
    }
};
int main() {
    /* 注册同中栏 */
    Shape shape(registry, &amp;sq, square_id);
    std::printf("area=%d\n", shape.valid() ? shape.area() : -1);
}</code></pre>
</td>
</tr>
</table>

三者对照：

- **C++ 虚函数**把多态编码进**类型系统**：实现类必须继承 `Shape`，分派靠编译器生成的 vtable。
- **ABIX 手动注册**是最底层的显式步骤（写适配器 → 注册 → 解析 → 调用），无额外抽象；`adapter`
  的首参是对象句柄。
- **ABIX 代理**把绑定/状态/调用封装成一个类型：具体类型仍是普通 `struct`（不继承、不含虚函数），
  绑定一次后 `shape.area()` 即普通原生间接调用；`Traits` 可手写，也可由 `ABIX_DECLARE_INTERFACE` 或
  `amc generate -l cpp` 生成。

## 它不是什么

- 不是共享对象模型，也不是对象生命周期运行时。
- 不是逐次调用查找、名比较或类型检查。
- 不承诺理解任意编译器的私有对象布局：若生产者没有提供适配器，ABIX 不会凭空推断。

## 相关

- [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) —— 契约、扩展目录与 Interface 契约（身份/槽位稳定/签名校验）。
- [`std.md`](std_zh.md) —— ABIX-STD 原语与 borrow/owned/copy 契约。
- [`abic.md`](docs/abix/abic_zh.md) —— `[[interface]]` / `[[implements]]` 构建声明。
