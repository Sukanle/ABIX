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

## 它不是什么

- 不是共享对象模型，也不是对象生命周期运行时。
- 不是逐次调用查找、名比较或类型检查。
- 不承诺理解任意编译器的私有对象布局：若生产者没有提供适配器，ABIX 不会凭空推断。

## 相关

- `../../.agents/ABI-SPEC.md` §9.1 / §13.1 —— 契约与扩展目录。
- [`std.md`](std.md) —— ABIX-STD 原语与 borrow/owned/copy 契约。
- `docs/abix/abic.md` —— `[[interface]]` / `[[implements]]` 构建声明。
