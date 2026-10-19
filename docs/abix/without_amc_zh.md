# 只依赖 ABIX、不使用 AMC

<p align="center">
  <a href="without_amc.md">English</a> · 中文
</p>

ABIX 和 AMC 的职责不同：

- **ABIX**（`ABIX/`）是 header-only 运行时层。它没有任何 `.cpp`，也从不 include `AMC/...`。`libabix-runtime` 只是一个 CMake `INTERFACE` 目标，负责加 include 路径。
- **AMC** 是构建期工具链（Clang/LLVM + `toml++`）：提取 C++ 语义、计算规范 ABI 身份、生成 `.abix` 产物，并生成 thunk、adapter 与 C++ 投影。

因此，纯 C++ 项目可以只依赖 ABIX。AMC 从来不是运行时依赖——即使使用它，最终二进制也不链接AMC；而手写的 C++ 契约在构建期同样不需要它。

代价是契约要手写：适配器表和 `Traits` 由你提供，TypeID 也由你给出，而不是让 Clang 自动提取。

## 只用 ABIX 能做什么

| 能力                     | 头文件                                                                           | 需要 AMC？                                                                   |
| ------------------------ | -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Interface 身份与方法签名 | `ABIX/Interface/Contract.h`                                                      | 否——`interface_id`、`method_signature`、`canonical_type_id` 都是 `constexpr` |
| 手写 `Traits` 与代理     | `ABIX/Interface/Proxy.h`                                                         | 否——`InterfaceProxy<Traits>` 是 header-only                                  |
| 进程内注册 / 解析 / 调用 | `Registry.h`、`Ref.h`、`Proxy.h`                                                 | 否                                                                           |
| 动态库发现与租约         | `Module.h`、`ModuleRegistry.h`、`Discovery.h`、`Lease.h`                         | 否——手写 `ModuleManifest` + `SKL_ABIX_DEFINE_MODULE_QUERY`                   |
| Owner ABI                | `Owner.h`                                                                        | 否                                                                           |
| 运行时表 / descriptor    | `ABIX/Runtime/Registry.h`、`ABIX/Metadata/Descriptor.h`、`SKL_ABIX_DEFINE_TABLE` | 否——结构是公开的，可以手写                                                   |
| ABIX 自己的类型/布局摘要 | `ABIX/Model/ABI.h`、`ABIX/Model/Compat.h`                                        | 否——`canonical_type_id`、`canonical_layout_hash`、`abi_signature_hash`       |

一个最小的 C++-only 声明：

```cpp
#include "ABIX/Interface/Interface.h"

using namespace skl::abix;
using namespace skl::abix::poly;

constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");

struct ShapeTraits {
    static constexpr util::StaticArray signatures = {method_signature("area", i32, nullptr, 0)};
    static constexpr InterfaceId id = interface_id("Shape", 1, signatures);
    static constexpr uint32_t version = 1;
    static constexpr size_t slots = 1;
    static constexpr uint32_t area_slot = 0;
};
class Shape : public InterfaceProxy<ShapeTraits> {
    using Base = InterfaceProxy<ShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(ShapeTraits::area_slot); }
};

// Provider 侧：适配器首参是对象句柄。
constexpr model::TypeId square_id{0x51, 0x52};   // 具体类型的 TypeID
int square_area(const void *self);
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[0], interface_entry(&square_area), 0, 0},
};
InterfaceRegistry<4> registry;
registry.add({ShapeTraits::id, square_id, square_methods, 1, 1, 0});
```

全程没有 AMC 生成的文件。若是动态库，用同样的信息手写 `ModuleManifest` 并配合`SKL_ABIX_DEFINE_MODULE_QUERY`；消费端照常使用 `discover_module` 与 `ModuleRegistry`。

## STL 类型：单一 C++ 工具链内部可用，跨边界不可用

Interface 层不检查参数类型：`interface_entry` 只是保存函数指针，`call<Fn>` 负责调用。因此，当整个契约都在同一个 C++ 工具链内部时，适配器可以直接使用 C++ STL 类型——这正是 Interface 作为 vtable 替代品的场景。

只有同时满足以下条件才是安全的：

- provider 与 consumer 都是 C++，且共享编译器与标准库 ABI（`_GLIBCXX_USE_CXX11_ABI`、MSVC runtime、debug/release CRT、iterator debug level、异常/RTTI 设置）；
- 使用相同的 C++ 标准与 allocator；
- 契约不跨越可能由不同工具链构建的插件边界，也不跨越语言边界；
- 双方接受该 STL 类型的生命周期规则（按值或 `const&` 传入的 `std::string` 仅在本次调用内有效；返回的 `std::string` 由调用方持有）。

```cpp
struct Square {
    std::string name;
    // ...
};
using NameFn = const std::string &(*)(const void *);
const std::string &square_name(const void *self) {
    return static_cast<const Square *>(self)->name;
}
// 注册与 `proxy.call<NameFn>(slot)` 照常使用。
```

签名仍然只是契约标签，不是布局校验：两端必须为 `std::string` 使用同一个 TypeID（例如 `canonical_type_id("std::string")`，或你自己固定的值）。ABIX 不校验 STL 的实际布局，调用成立的前提是「同一工具链」这个约定。手写 `Traits` 与代理（或交给 AMC 生成），并把 STL 参数的 TypeID 传给 `method_signature`。

**不要跨越插件、外部语言或外部工具链边界使用 STL 类型。** 不同的 `std::string` / `std::vector` ABI、allocator、异常模型或 RTTI 设置会让调用变成未定义行为，而且在一个模块中创建的对象不能交给另一个使用不同 allocator 的模块销毁。请改用 ABIX-STD 边界类型（`StringView`、`SequenceView<T>`、`Opaque<T>`）或显式 marshalling——这与 `[[adapt]]` 的规则一致：原生 STL 参数需要声明的边界契约。

## 只用 ABIX 会失去什么

这些是 AMC 的自动化部分：

- `[[interface]]` / `[[implements]]` / `[[expose]]` / `[[adapt]]` 驱动的生成；
- Clang AST 对类型、字段、布局、函数签名的提取；
- `.abix` 产物、Metadata Region、`TypeTraits`、布局校验、兼容性报告与 generated adapter；
- Rust / Zig / Lua 投影；
- C++ 拼写 → 规范名（`prim:i32`、指针拼写、模板别名）的映射，需要你自己用`if constexpr`、traits 或手写常量实现。

## 一套引擎，多种身份语义

所有身份与摘要函数共用 `util::canonical_hash`（AMC 里叫 `hash_text` 的同一套引擎）。它们的区别在规范化输入与 domain，而不是算法：

| API                            | 语义                                           | 与 AMC 产物一致？                            |
| ------------------------------ | ---------------------------------------------- | -------------------------------------------- |
| `model::canonical_type_id`     | 类型身份                                       | 是——`TypeID`                                 |
| `poly::method_signature`       | 方法身份                                       | 是——`Function.signature`                     |
| `poly::interface_id`           | 接口身份                                       | 是——`InterfaceRecord.id`                     |
| `model::canonical_layout_hash` | Canonical Layout Encoding v1（跨工具布局契约） | 是——`LayoutHash`                             |
| `model::abi_signature_hash`    | 调用 ABI（返回/参数 + 调用约定/限定符）        | 不是 `method_signature`；方法身份 ≠ 调用 ABI |

不要把这几者压成同一个值。同一个类型可以保持 `TypeID` 不变，而布局随目标平台变化；方法的逻辑身份相同，也不能证明两种调用约定可以安全互调。

`canonical_layout_hash` 实现了 `ABI-SPEC.md` §11.2 的 Canonical Layout Encoding v1：编码与名字无关、带目标 ABI 约束，且 AMC 与 ABIX 逐字节一致。每个 artifact 都携带 `layout_encoding` section，reader 会复算每个 `LayoutHash`；不存在旧的布局规则。

`model::type_hash` 与 `model::signature_hash` 已移除：类型身份用 `canonical_type_id`，调用 ABI 用 `abi_signature_hash`。旧的 `util::fnv1a` / `Value` / `Descriptor` / `ValueSet` 系列也一并移除；它是第二套引擎，且并不用于身份生成。

## 三种落地方式

1. **完全手写**：只依赖 ABIX 头文件，`Traits` 与适配器表都手写。依赖最轻，样板最多。
2. **生成一次、提交产物**：在 CI（或本地）跑一次 AMC，把生成的 `Traits`、thunk 与 `.abix` 提交到仓库，日常构建只依赖 ABIX。开发者不需要 Clang/LLVM，但 ABI 快照可能漂移，源契约变化时必须重新生成。
3. **AMC 作为构建期依赖**（默认路径）：自动化最完整；AMC 在构建机或 CI 上运行，不进入运行时。

如果目标是“C++ 项目不安装 AMC”，选 (1) 或 (2)；如果还需要跨语言互操作、布局校验与自动生成
适配器，选 (3)。

## 相关

- [`interface.md`](interface_zh.md) —— Interface 契约、代理与 `signatures`。
- [`abic.md`](abic_zh.md) —— `[[interface]]` / `[[implements]]` 构建声明。
- [`std.md`](std_zh.md) —— `StringView` 等 ABIX-STD 边界类型。
