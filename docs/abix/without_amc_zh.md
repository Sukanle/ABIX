# 只依赖 ABIX、不使用 AMC

<p align="center">
  <a href="without_amc.md">English</a> · 中文
</p>

ABIX 和 AMC 的职责不同：

- **ABIX**（`ABIX/`）是 header-only 运行时层。它没有任何 `.cpp`，也从不 include
  `AMC/...`。`libabix-runtime` 只是一个 CMake `INTERFACE` 目标，负责加 include 路径。
- **AMC** 是构建期工具链（Clang/LLVM + `toml++`）：提取 C++ 语义、计算规范 ABI 身份、
  生成 `.abix` 产物，并生成 thunk、adapter 与 C++ 投影。

因此，纯 C++ 项目可以只依赖 ABIX。AMC 从来不是运行时依赖——即使使用它，最终二进制也不链接
AMC；而手写的 C++ 契约在构建期同样不需要它。

代价是契约要手写：适配器表和 `Traits` 由你提供，TypeID 也由你给出，而不是让 Clang 自动提取。

## 只用 ABIX 能做什么

| 能力 | 头文件 | 需要 AMC？ |
|---|---|---|
| Interface 身份与方法签名 | `ABIX/Interface/Contract.h` | 否——`interface_id`、`method_signature`、`canonical_type_id` 都是 `constexpr` |
| 手写 `Traits` 与代理 | `ABIX/Interface/Declaration.h` | 否——`ABIX_DECLARE_INTERFACE` / `ABIX_DECLARE_INTERFACE_AUTO` |
| 进程内注册 / 解析 / 调用 | `Registry.h`、`Ref.h`、`Proxy.h` | 否 |
| 动态库发现与租约 | `Module.h`、`ModuleRegistry.h`、`Discovery.h`、`Lease.h` | 否——手写 `ModuleManifest` + `SKL_ABIX_DEFINE_MODULE_QUERY` |
| Owner ABI | `Owner.h` | 否 |
| 运行时表 / descriptor | `ABIX/Runtime/Registry.h`、`ABIX/Metadata/Descriptor.h`、`SKL_ABIX_DEFINE_TABLE` | 否——结构是公开的，可以手写 |
| ABIX 自己的类型/布局哈希 | `ABIX/Model/Compat.h` | 否——`type_hash`、`layout_hash`、`signature_hash` |

一个最小的 C++-only 声明：

```cpp
#include "ABIX/Interface/Declaration.h"
#include "ABIX/Interface/Interface.h"

using namespace skl::abix::poly;

constexpr model::TypeId i32 = model::canonical_type_id("prim:i32");

#define SHAPE_METHODS(X) X(area, i32, int32_t, 0)
ABIX_DECLARE_INTERFACE_AUTO(Shape, 1, SHAPE_METHODS)

// Provider 侧：适配器首参是对象句柄。
constexpr model::TypeId square_id{0x51, 0x52};   // 具体类型的 TypeID
int square_area(const void *self);
const InterfaceMethod square_methods[] = {
    {ShapeTraits::signatures[0], interface_entry(&square_area), 0, 0},
};
InterfaceRegistry<4> registry;
registry.add({ShapeTraits::id, square_id, square_methods, 1, 1, 0});
```

全程没有 AMC 生成的文件。若是动态库，用同样的信息手写 `ModuleManifest` 并配合
`SKL_ABIX_DEFINE_MODULE_QUERY`；消费端照常使用 `discover_module` 与 `ModuleRegistry`。

## 只用 ABIX 会失去什么

这些是 AMC 的自动化部分：

- `[[interface]]` / `[[implements]]` / `[[expose]]` / `[[adapt]]` 驱动的生成；
- Clang AST 对类型、字段、布局、函数签名的提取；
- `.abix` 产物、Metadata Region、`TypeTraits`、布局校验、兼容性报告与 generated adapter；
- Rust / Zig / Lua 投影；
- C++ 拼写 → 规范名（`prim:i32`、指针拼写、模板别名）的映射，需要你自己用
  `if constexpr`、traits 或手写常量实现。

## 两套哈希——要有意识地选一套

ABIX 目前有两组身份函数：

| 组 | 函数 | 与 AMC 产物一致？ |
|---|---|---|
| ABIX 内部 | `model::type_hash`、`model::layout_hash`、`model::signature_hash`（`Compat.h`，`util::fnv1a`） | 否 |
| AMC 兼容 | `model::canonical_type_id`、`poly::method_signature`、`poly::interface_id`（`util::canonical_hash`） | 是 |

纯 C++、永远不用 AMC 的项目可以任选一套，只要前后一致。若以后可能接 AMC 或其他语言，请使用
AMC 兼容组；不要把 `model::type_hash` 的值喂给 `method_signature` 再指望它与 AMC 产物匹配。

## 三种落地方式

1. **完全手写**：只依赖 ABIX 头文件，`Traits` 与适配器表都手写。依赖最轻，样板最多。
2. **生成一次、提交产物**：在 CI（或本地）跑一次 AMC，把生成的 `Traits`、thunk 与 `.abix`
   提交到仓库，日常构建只依赖 ABIX。开发者不需要 Clang/LLVM，但 ABI 快照可能漂移，
   源契约变化时必须重新生成。
3. **AMC 作为构建期依赖**（默认路径）：自动化最完整；AMC 在构建机或 CI 上运行，不进入运行时。

如果目标是“C++ 项目不安装 AMC”，选 (1) 或 (2)；如果还需要跨语言互操作、布局校验与自动生成
适配器，选 (3)。

## 相关

- [`interface.md`](interface_zh.md) —— Interface 契约、代理与 `signatures`。
- [`abic.md`](abic_zh.md) —— `[[interface]]` / `[[implements]]` 构建声明。
- [`std.md`](std_zh.md) —— `StringView` 等 ABIX-STD 边界类型。
