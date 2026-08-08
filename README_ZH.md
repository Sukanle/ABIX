<div align="center">

# ABIX

## 一个跨 DLL 的 SKL_ABIX 安全函数调用库，支持签名校验、版本管理、热重载与查找加速。

![License](https://img.shields.io/badge/License-Apache_2.0-blue)
![Language](https://img.shields.io/badge/Language-C/C++-red)

[English](README.md) | 中文
</div>

## 概述

ABIX（SKL_ABIX 接口）是一个轻量级 C++ 库，支持跨 DLL/共享库边界的稳健、类型安全的函数调用。它通过引入稳定的函数表、编译期签名哈希和可选的版本管理，解决了 `GetProcAddress`/`dlsym` 的根本脆弱性——确保 DLL 的使用方与提供方永远不会发生静默不匹配。

核心设计目标：

- **SKL_ABIX 稳定表** — 纯 POD 的 `entry` 和 `table` 结构体，带有固定的魔数和格式版本号，可跨编译器版本、CRT 变体和调用约定安全使用。
- **编译期签名哈希** — 每个函数签名在编译期通过 FNV-1a 进行哈希；签名不匹配在解析时即被检测到，而非调用时。
- **版本演进** — 同一函数名的多个版本可在单张表中共存，支持向前兼容的 API 演进。
- **热重载** — 整数句柄 ID 在卸载/重载周期中保持稳定，支持零停机 DLL 升级。
- **查找加速** — 三种查找策略（线性、静态热点、自适应热点）适配不同的访问模式，自适应热点缓存可基于运行时调用频率自动学习。

## 特性

- **稳定函数表** — `SKL_ABIX_DEFINE_TABLE(...)` 宏生成一个 POD 导出表，入口点为 `abi_get_table()`
- **签名安全** — `fn_sig<T>` 为每个函数类型生成唯一的编译期哈希，包含调用约定信息
- **类型安全智能指针** — `unique_dll_ptr`、`ref_dll_ptr`、`shared_dll_ptr`、`weak_dll_ptr`、`view_dll_ptr`，用于管理 DLL 分配的资源，确保通过正确的 DLL 端释放函数进行释放
- **跨边界回调** — `function_dll<R(Args...)>` 是一个 8 字节的闭包，可捕获 lambda 并跨 DLL 边界调用
- **版本令牌** — `SKL_ABIX_VERSION("1.0")` 允许多个同名函数的实现共存
- **调用约定感知** — `dll_func_cc<C, Sig>` 和 `dll_func<Sig, C>` 模板支持 `__cdecl`、`__stdcall`、`__fastcall` 和 `__vectorcall`
- **动态反射集成** — 基于 Reflection 库构建，支持运行时类型查询和通过 `make_pod_type_info` / `make_offset_field` 访问 POD 字段
- **查找策略** — 三种函数表查找策略，自适应热点缓存可自动提升频繁调用的条目

## 快速开始

### DLL 端（提供方）

```cpp
#include "abix/abix.hpp"

extern "C" int add(int a, int b) { return a + b; }
extern "C" double multiply(double a, double b) { return a * b; }

SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("add", add),
    SKL_ABIX_ENTRY("multiply", multiply),
)
```

### 宿主端（使用方）

```cpp
#include "abix/abix.hpp"

using namespace skl::abix;

dll_object lib;
lib.load("math_dll.dll");

auto add = dll_func<int(int, int)>(lib, "add");
if (add.valid()) {
    int result = add(2, 3);  // 5
}

auto mul = dll_func<double(double, double)>(lib, "multiply");
if (mul.valid()) {
    double result = mul(1.5, 4.0);  // 6.0
}
```
> [!NOTE]
> - [API.md](doc/api_zh_CN.md)

## 注册宏

### 导出表定义

| 宏 | 说明 |
|---|------|
| `SKL_ABIX_DEFINE_TABLE(...)` | 用条目列表定义导出表 |
| `SKL_ABIX_ENTRY(name, func)` | 以默认调用约定（Cdecl）和版本 0 注册函数 |
| `SKL_ABIX_ENTRY_CC(name, func, cctype)` | 以指定调用约定注册函数 |
| `SKL_ABIX_ENTRY_FULL(name, func, cctype, ver, flg)` | 以完整参数注册函数（调用约定、版本、标志） |
| `SKL_ABIX_VERSION("1.0")` | 为版本化函数条目创建版本令牌 |

### 类型标签注册

| 宏 | 说明 |
|-------|-------------|
| `SKL_ABIX_TYPE_TAG(T, tag)` | 为用户类型 `T` 注册自定义哈希标签（委托给 `STATIC_TYPE_TAG`） |

### 调用约定选择器

| 宏 | 说明 |
|-------|-------------|
| `SKL_ABIX_CCPICK(Cdecl)` | 选择 `Cdecl` 调用约定 |
| `SKL_ABIX_CCPICK(Stdcall)` | 选择 `Stdcall` 调用约定 |

## 核心类型

### `entry` — 函数表条目

```cpp
struct entry {
    const char *name;       // 函数名称
    sig_t sig;              // 签名哈希（编译期 FNV-1a）
    version_t version;      // 版本令牌（0 = 无版本）
    uintptr_t fnptr;        // 函数指针
    name_hash_t name_hash;  // 名称哈希（FNV-1a 32 位）
    uint32_t flags;         // 标志（SKL_ABIX_ENTRY_HOT = 0x1）
};
```

### `table` — 导出表

```cpp
struct table {
    uint32_t count;          // 条目数量
    uint32_t magic;          // SKL_ABIX_TABLE_MAGIC (0xAB1E7A81)
    uint32_t format_version; // SKL_ABIX_TABLE_FORMAT_VERSION (1)
    uint32_t reserved;       // 预留
    const entry *entries;    // 指向条目数组的指针
};
```

### `dll_object` — DLL 模块

| 方法 | 返回值 | 说明 |
|--------|---------|-------------|
| `load(path)` | `bool` | 加载 DLL/SO 并校验其导出表 |
| `unload()` | `bool` | 若无活跃句柄（引用计数 = 0）则卸载 |
| `force_unload()` | `void` | 无条件卸载，忽略引用计数 |
| `reload(path)` | `bool` | 卸载并重新加载新 DLL |
| `is_loaded()` | `bool` | 模块是否已加载 |
| `get_table()` | `const table*` | 获取导出表指针 |
| `add_ref()` | `void` | 增加引用计数 |
| `release_ref()` | `void` | 减少引用计数 |
| `ref_count()` | `uint32_t` | 当前引用计数 |

### `call_error` — 错误码

| 值 | 说明 |
|-------|-------------|
| `none` | 无错误 |
| `not_loaded` | DLL 未加载 |
| `not_found` | 表中未找到函数名 |
| `sig_mismatch` | 签名哈希不匹配 |
| `version_mismatch` | 版本令牌不匹配 |
| `stale_handle` | 存在活跃句柄时无法卸载 |
| `table_changed` | 解析后表已变更 |
| `invalid` | 无效句柄 |
| `load_failed` | DLL 加载失败 |

## DLL 资源智能指针

| 类型 | 语义 | 说明 |
|------|------|------|
| `unique_dll_ptr<T>` | 独占所有权 | 单一持有者，析构时调用 DLL 端释放函数 |
| `ref_dll_ptr<T>` | 引用计数（非原子） | 单线程共享所有权 |
| `shared_dll_ptr<T>` | 原子引用计数 | 线程安全共享所有权 |
| `weak_dll_ptr<T>` | 弱引用 | `shared_dll_ptr` 的非拥有观察者 |
| `view_dll_ptr<T>` | 视图引用 | `ref_dll_ptr` 的非拥有观察者 |
| `fn_deleter<T>` | 自定义删除器 | 包装 DLL 释放函数，供 `std::unique_ptr` 使用 |

## 查找策略

ABIX 支持三种查找策略，可通过 `dll_func` 模板参数在编译期选择：

| 策略 | 说明 | 适用场景 |
|--------|-------------|----------|
| `Linear` | 全表线性扫描（默认） | 小表、冷启动 |
| `StaticHot` | 预注册的热点缓存 | 编译期已知的固定热点条目 |
| `AdaptiveHot` | 自学习热点缓存 | 动态工作负载、热点偏移场景 |

`AdaptiveHot` 策略自动采样调用频率，并将超过可配置阈值的条目提升至热点缓存。

## 目录结构

```
ABIX/
├── abix/                      # 核心库头文件
│   ├── abix.hpp               # 主入口头文件（包含全部）
│   ├── config.h               # 平台检测、宏、AbiLookupPolicy
│   ├── type.h                 # 核心类型：entry、table、类型别名
│   ├── register.h             # SKL_ABIX_DEFINE_TABLE、SKL_ABIX_ENTRY 宏
│   ├── obj_dll.h              # dll_object：DLL 加载/卸载/引用计数
│   ├── fn_dll.h               # dll_func / dll_func_cc：类型化函数句柄
│   ├── fn_sig.h               # fn_sig<T>：编译期签名哈希
│   ├── type_sig.h             # type_sig<T>：编译期类型哈希
│   ├── search.h               # find_index、lookup_linear：表查找
│   ├── cache.h                # static_hot_cache、adaptive_hot_cache：查找加速
│   ├── function.h             # function_dll：8 字节跨边界闭包
│   ├── dll.h                  # 聚合器：obj_dll + fn_dll
│   ├── dll_ptr.h              # 聚合器：全部智能指针类型
│   ├── refl.h                 # 动态反射集成辅助工具
│   └── dll_ptr/               # 智能指针实现
│       ├── unique_ptr.h       # unique_dll_ptr<T>、fn_deleter<T>
│       ├── ref_ptr.h          # ref_dll_ptr<T>、view_dll_ptr<T>
│       ├── shared_ptr.h       # shared_dll_ptr<T>
│       ├── weak_ptr.h         # weak_dll_ptr<T>
│       └── view_ptr.h         # view_dll_ptr<T>
├── dlls/                      # 示例/测试 DLL 实现
│   ├── plugin_types.h         # 跨 DLL 类型的共享类型标签
│   ├── math_dll.cpp           # 基础数学函数
│   ├── version_dll.cpp        # 带版本号的 log 函数（v1.0/v2.0）
│   ├── sigcheck_dll.cpp       # 签名不匹配测试
│   ├── resource_dll.cpp       # 资源生命周期（创建/销毁）
│   ├── callback_dll.cpp       # 跨边界回调测试
│   ├── reload_dll_a.cpp       # 热重载变体 A
│   ├── reload_dll_b.cpp       # 热重载变体 B
│   ├── edge_dll.cpp           # 边界情况测试
│   ├── edge_stdcall_dll.cpp   # __stdcall 调用约定测试
│   ├── hotcache_dll.cpp       # 查找策略基准测试 DLL
│   ├── closed_dll.cpp         # 闭源模拟
│   └── closed_dll_v2.cpp      # 闭源模拟 v2
├── tools/                     # 构建与代码生成脚本
│   ├── build.py               # 主构建脚本
│   ├── build_variants.py      # 跨编译器变体构建
│   ├── build_msvc_variants.ps1 # MSVC 变体构建
│   └── gen_hotcache_dll.py    # hotcache_dll.cpp 代码生成器
├── main.cpp                   # 测试套件（Catch2）
└── CMakeLists.txt             # 构建配置
```

## 支持的平台与工具链

| 平台 | 编译器 | 最低版本 | 状态 |
|----------|----------|-----------------|--------|
| Windows  | MSVC     | VS 2022 (17.0+) | ✓ |
| Windows  | MinGW-w64 (GCC) | 13.0+ | ✓ |
| Windows  | Clang-cl | 17.0+ | ✓ |
| Linux    | GCC      | 13.0+ | ✓ |
| Linux    | Clang    | 17.0+ | ✓ |

**要求：** C++17 或更高版本（推荐 C++20 以获得 `consteval` 支持）。Reflection 库作为 git 子模块包含在内。

## 测试

测试使用 [Catch2](https://github.com/catchorg/Catch2)，入口文件 `main.cpp` 覆盖以下场景：

| 测试 | 标签 | 覆盖内容 |
|------|------|----------|
| 基础数学线性扫描 | `[basic]` | 加载 DLL，通过线性扫描解析函数，通过整数句柄调用 |
| 跨编译器变体 | `[cross]` | 同一函数名在 g++/clang/MSVC 构建中结果一致 |
| 签名哈希校验 | `[typesafe]` | 签名不匹配在查找时被拒绝，不发生静默类型转换 |
| 独占资源接管 | `[resource]` | `unique_dll_ptr`/`unique_ptr` 析构时调用 DLL 释放函数 |
| ref_dll_ptr 引用计数 | `[resource]` | 非原子引用计数共享资源，仅在最后一个析构时释放 |
| ABI 函数回调 | `[callback]` | `function_dll` 捕获 lambda 并跨边界调用 |
| 版本演进 | `[version]` | 同一 log 接口的 v1.0/v2.0 版本令牌共存 |
| 查找策略基准测试 | `[perf]` | 线性/静态热点/自适应热点策略在真实分布下的表现 |
| 热重载 | `[reload]` | 卸载 A 后加载 B，句柄 ID 不变，返回值更新 |
| 边界情况处理 | `[edge]` | 未找到、卸载后调用、引用计数阻止卸载、调用约定不匹配 |

```bash
# 构建并运行测试
cd tools
python build.py                    # GCC/Clang（Ninja 或 MinGW Makefiles）
python build.py --with-msvc        # 同时构建 MSVC 跨编译器变体
python build.py --run-only         # 仅运行测试，跳过构建
```

## 未来计划

- **AMC 集成** — 结合计划中的元对象编译器，从 C++ 属性自动生成 `SKL_ABIX_DEFINE_TABLE` 条目
- **序列化支持** — 扩展 `type_sig` 和类型标签，支持跨 DLL 边界的复杂类型序列化
- **网络传输** — 通过相同的稳定表格式实现远程函数调用

## 许可证

Apache License, Version 2.0。详见 [LICENSE](https://www.apache.org/licenses/LICENSE-2.0)。

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)