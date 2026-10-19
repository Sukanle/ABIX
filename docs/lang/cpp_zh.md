# C++

<p align="center">
  <a href="cpp.md">English</a> · 中文
</p>

<details>
<summary>目录</summary>

- [角色](#角色)
- [前端](#前端)
- [生成的投影](#生成的投影)
- [接口层](#接口层)
- [示例](#示例)
- [限制与说明](#限制与说明)

</details>

C++ 是 ABIX 的第一个宿主语言，也是参考 provider。它使用 Clang/LLVM 从 AST 提取 ABI 事实，并生成带编译期布局检查的 C++17 投影。

## 角色

- `amc-cpp` 同时是 frontend 与 backend provider (`amc --describe-language cpp` 报告 `frontend backend`)。
- 前端用 Clang 解析配置的翻译单元；AMC 把类型、字段、函数、符号、布局、target 事实、所有权与错误契约记录进 artifact。
- 后端生成 `amc_generated.hpp` 与 `amc_abi_check.hpp`。
- C++ 同时也是 ABIX 自身与 AMC core 的实现语言。

## 前端

`.abic.toml` 选择 `language = "cpp"`，并列出文件与导出符号：

```toml
[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["demo_api.hpp"]
symbols = ["DemoVec2", "demo_add", "demo_div"]
```

前端与 AMC 共同处理模板、`std::` 类型、属性、`[[interface]]` / `[[implements]]`、`[[owned]]` / `[[contract]]`、`[[instantiate]]`、`[[view]]`、`[[expose]]` 与错误边界。详见 [`abic_zh.md`](../abix/abic_zh.md)。

## 生成的投影

```bash
amc generate build/package.abix -l cpp -o gen/
```

`amc_generated.hpp` 包含布局兼容的 C++ 类型与 typedef、ABI 常量、所有权与错误边界 helper，以及 metadata region 访问器。对接口契约，它还会生成 `k<Name>Interface`、`k<Name>Version`、`enum class <Name>Slot`、`<Name>_<method>_fn` typedef、`Traits` struct，以及当每个方法首参都是对象句柄时的 `InterfaceProxy` 派生代理。

`amc_abi_check.hpp` 是编译期布局检查。请把它放在它所引用的原生声明之后：

```cpp
#include "demo_api.hpp"
#include "amc_abi_check.hpp"
```

它断言 `sizeof`、`alignof`、逐字段 `offsetof` 与逐字段宽度。位域与隐式命名的类型会被跳过，因为 `offsetof` / `sizeof` 对它们没有定义。

## 接口层

C++ 接口层是 header-only 的，位于 [`ABIX/Interface/`](../../ABIX/Interface/)：

- `Contract.h`：`InterfaceId`、`method_signature`、`interface_id`、`InterfaceMethod` 与 `InterfaceImplementation`。
- `Proxy.h`：`InterfaceProxy<Traits>`；生成或手写的代理提供 `shape.area()`。
- `Registry.h` / `Ref.h`：进程内注册与一次性解析。
- `Module.h` / `ModuleRegistry.h` / `Lease.h` / `Discovery.h`：动态 module manifest 与租约。
- `Owner.h`：跨模块所有权。
- `Version.h`：版本打包辅助宏。

provider 保留自己的 class、虚函数与布局。适配器是唯一的桥；接口从不参与 `TypeID` / `LayoutHash` / `ABIHash`。

## 示例

构建 interop demo 并生成 C++ 投影：

```bash
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

./build/Release/bin/amc build -c AMC/tests/interop/interop_demo.abic.toml -B build/demo
./build/Release/bin/amc generate build/demo/interop_demo.abix -l cpp -o build/demo/gen
```

生成的 header 可以被 C++ 消费方直接 include；如果 provider 布局发生漂移，`amc_abi_check.hpp` 会让构建失败。

## 限制与说明

- C++ STL 类型不能原样跨越边界；需要声明 `[[view]]`、`[[instantiate]]` 或 `[[expose]]` 映射，或使用 ABIX-STD 契约。
- 记录的 ABI 是 target 相关的。两端应使用相同的 target、编译器 ABI 与调用约定。
- 接口层不要求继承或虚函数；普通 `struct` 即可。
- 生成的 C++ header 面向 C++17。
- Clang 前端需要 LLVM / Clang 开发库；Rust 与 Zig provider 不需要。
