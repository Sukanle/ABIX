<div align="center">

# ABIX

## 一个跨 DLL 的 SKL_ABIX 安全函数调用库，支持签名校验、版本管理、热重载与自动查找加速。

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
- **RCU 非阻塞卸载** — 全局 `rcu_domain`（基于 Epoch-Based Reclamation），`dll_object` 通过 `enter_read()`/`exit_read()` 委托至全局域，配合编译器内建原子操作，实现安全的并发 DLL 卸载，不阻塞活跃的调用者。
- **查找加速** — 自动查找策略：小表（< 64 条目）线性扫描，大表（≥ 64 条目）HashIndex，零 ABI 格式变更。
- **ABI 元数据运行时** — 版本化 `.abix` v4 artifact、生成的静态 descriptor 与有界 `RuntimeRegistry` 提供原生类型元数据，不改变 DLL 函数表 ABI。
- **AMC 管线** — `amc` 提取选定的 C++ ABI 布局、校验/检查 artifact、生成 C++ 投影，并产出 Compatibility / Map IR 报告。

> [!IMPORTANT]
> ABIX 的 Hash Container、Micro-RCU、RCU batching 等性能优化均**针对 ABIX 自身的 read-mostly 场景特化**，并非通用并发容器或通用 RCU 实现。

## 特性

- **稳定函数表** — `SKL_ABIX_DEFINE_TABLE(...)` 宏生成一个 POD 导出表，入口点为 `abi_get_table()`
- **签名安全** — `fn_sig<T>` 为每个函数类型生成唯一的编译期哈希，包含调用约定信息
- **类型安全智能指针** — `unique_dll_ptr`、`ref_dll_ptr`、`shared_dll_ptr`、`weak_dll_ptr`、`view_dll_ptr`，用于管理 DLL 分配的资源，确保通过正确的 DLL 端释放函数进行释放
- **跨边界回调** — `function_dll<R(Args...)>` 是一个 8 字节的闭包，可捕获 lambda 并跨 DLL 边界调用
- **版本令牌** — `SKL_ABIX_VERSION("1.0")` 允许多个同名函数的实现共存
- **调用约定感知** — `dll_func_cc<C, Sig>` 和 `dll_func<Sig, C>` 模板支持 `__cdecl`、`__stdcall`、`__fastcall` 和 `__vectorcall`
- **RCU 非阻塞卸载** — 线程安全的 DLL 卸载，通过全局 `rcu_domain` 的读侧临界区（`enter_read()`/`exit_read()`）和编译器内建原子操作（`_Interlocked*`/`__atomic_*`），零 `std::atomic` ABI 风险
- **RCU 超时策略** — 当 EBR 宽限期超过 `ABIX_RCU_TIMEOUT_MS` 时的三种策略：Safe（僵尸+泄漏）、ForceUnload（绕过 EBR）和 ForceLeak（摘除+泄漏，需宏显式开启）
- **超时检查模式** — 三种零/低 CPU 检查模式：Lazy（入口点检查）、Tick（宿主驱动）和 OS Timer（内核级等待）
- **可插拔日志** — 编译期可移除的日志系统，支持 C 回调接收器（`ABIX_LOG_*` 宏）、按级别禁用和 ABI 安全的 `set_log_sink()`，可对接生产级日志平台
- **动态反射集成** — 基于 mics 库构建，支持运行时类型查询和通过 `make_pod_type_info` / `make_offset_field` 访问 POD 字段
- **查找策略** — 自动：小表线性扫描，大表 HashIndex。加载时构建，零 ABI 格式变更。
- **生成的运行时元数据** — `RuntimeRegistry::register_module()` 校验生成的 `ModuleDescriptor`；可选生成的 `TypeTraits<T>` 支持 `type_of<T>()`。
- **ABI 元数据编译器（AMC）** — C++ frontend、C++17 投影 backend、兼容性分析、`MapPrivate<A, B>` 生成和 JSON-lines provider IPC。

## ABI 元数据与 AMC

稳定 DLL 函数表仍是公共调用 ABI。元数据是额外且显式的一层：AMC 读取选定的
C++ 接口，写出包含 type/layout、field、function、symbol、hash、compatibility 和
map record 的 `.abix` v4 artifact，随后可生成供运行时 Registry 注册的 C++17
descriptor 投影。

```sh
amc build -c package.abic.toml -B build
amc validate build/build/package.abix
amc generate build/build/package.abix -l cpp -o package_metadata.hpp
amc diff v1.abix v2.abix -o compatibility.abix
```

生成的 native traits 是显式启用的：在包含生成头文件前定义
`AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS`，先注册其
`amc_generated::amc_module` descriptor，再调用
`RuntimeRegistry::type_of<T>()`。参见 [API 参考](docs/api_zh.md) 和
[`.abix` 格式说明](docs/abix.md)。

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
> - [API.md](docs/api_zh.md)

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
| `unload()` | `bool` | 标记卸载 → 等待读者 → 若无活跃句柄则卸载（引用计数 = 0） |
| `force_unload()` | `void` | 无条件卸载，忽略引用计数（绕过 RCU，调用者需自行保证安全） |
| `reload(path)` | `bool` | 卸载并重新加载新 DLL |
| `is_loaded()` | `bool` | 模块是否已加载且未处于卸载中 |
| `get_table()` | `const table*` | 获取导出表指针 |
| `add_ref()` | `void` | 增加引用计数 |
| `release_ref()` | `void` | 减少引用计数 |
| `ref_count()` | `uint32_t` | 当前引用计数 |
| `enter_read()` | `const table*` | 进入 RCU 读侧临界区（委托至全局 `rcu_domain`），返回已验证的导出表指针；若模块未加载或已僵尸则返回 `nullptr` |
| `exit_read()` | `void` | 退出 RCU 读侧临界区（委托至全局 `rcu_domain`） |
| `set_timeout_policy(p)` | `void` | 设置 RCU 超时策略（`Safe` / `ForceUnload` / `ForceLeak`） |
| `timeout_policy()` | `RCUTimeoutPolicy` | 获取当前 RCU 超时策略 |

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
| `unloading` | DLL 正在卸载中 |

## DLL 资源智能指针

| 类型 | 语义 | 说明 |
|------|------|------|
| `unique_dll_ptr<T>` | 独占所有权 | 单一持有者，析构时调用 DLL 端释放函数 |
| `ref_dll_ptr<T>` | 引用计数（非原子） | 单线程共享所有权 |
| `shared_dll_ptr<T>` | 原子引用计数 | 线程安全共享所有权 |
| `weak_dll_ptr<T>` | 弱引用 | `shared_dll_ptr` 的非拥有观察者 |
| `view_dll_ptr<T>` | 视图引用 | `ref_dll_ptr` 的非拥有观察者 |
| `fn_deleter<T>` | 自定义删除器 | 包装 DLL 释放函数，供 `std::unique_ptr` 使用 |

## 日志系统

ABIX 提供可插拔、编译期可移除的日志系统，零 ABI 风险。

### 日志级别

| 级别 | 宏 | 说明 |
|-------|-------|-------------|
| `Debug` | `ABIX_LOG_DEBUG(...)` | 详细诊断信息 |
| `Info` | `ABIX_LOG_INFO(...)` | 一般操作消息 |
| `Warning` | `ABIX_LOG_WARNING(...)` | 可恢复的问题，降级行为 |
| `Error` | `ABIX_LOG_ERROR(...)` | 严重故障，不可恢复的错误 |

### 运行时重定向

```cpp
void my_sink(skl::abix::LogLevel level, const char *message) {
    // 转发到 spdlog、fmt、ELK、Splunk 等
    spdlog::log(static_cast<spdlog::level::level_enum>(level), message);
}
skl::abix::set_log_sink(my_sink);
```

### 编译期控制

```cpp
#define ABIX_DISABLE_LOGGING               // 零开销：所有日志代码被移除
#define ABIX_DISABLE_LOG_LEVEL_DEBUG      // 仅禁用 Debug 级别
#define ABIX_DISABLE_LOG_LEVEL_INFO       // 仅禁用 Info 级别
```

## RCU 超时策略

当 `ABIX_RCU_TIMEOUT_ENABLE` 开启（默认）且 EBR 宽限期超过 `ABIX_RCU_TIMEOUT_MS`（默认：5000ms）时，应用以下三种策略之一：

| 策略 | 行为 | 可用性 | 默认 |
|------|------|--------|------|
| `Safe` | 标记僵尸，放弃卸载，DLL 泄漏但**绝不崩溃** | 始终可用 | 默认 |
| `ForceUnload` | 绕过 EBR，强制 `FreeLibrary`/`dlclose`——活跃调用者**将崩溃** | 始终可用 | — |
| `ForceLeak` | 摘除模块，不卸载 DLL，旧对象安全泄漏 | 需 `#define ABIX_ENABLE_FORCE_LEAK_POLICY` | — |

**僵尸状态：** 在 Safe/ForceLeak 策略下，`dll_object` 变为僵尸：
- `is_loaded()` 返回 `false`
- `enter_read()` 返回 `nullptr`（设置 `call_error::unloading`）
- `load()` 先强制卸载僵尸，再加载新 DLL

## 配置速查

```cpp
// ==================== 1. 日志 ====================
// #define ABIX_DISABLE_LOGGING
// #define ABIX_DISABLE_LOG_LEVEL_DEBUG

// ==================== 2. 超时策略 ====================
#define ABIX_RCU_TIMEOUT_ENABLE     1
#define ABIX_RCU_TIMEOUT_MS         5000   // 编译期回退默认值
#define ABIX_RCU_TIMEOUT_FRAMES_DEFAULT 0  // 编译期回退默认值（0 = 禁用）
// #define ABIX_ENABLE_FORCE_LEAK_POLICY

// ==================== 3. 惰性饥饿防护 ====================
#define ABIX_LAZY_STARVATION_GUARD  ABIX_LAZY_STARVATION_GUARD_TICK  // 0=关闭 | 1=Tick（默认） | 2=空闲线程
// #define ABIX_ENABLE_IDLE_BACKGROUND_THREAD   // 等级 2 必需

// 运行时配置：
// dll_object lib(RCUTimeoutConfig{5000, 300});  // 5秒 或 300帧，谁先到谁触发
// lib.set_timeout_policy(RCUTimeoutPolicy::ForceUnload);
```

## 查找策略

ABIX 根据表大小自动选择最优查找策略：

| 表大小 | 策略 | 说明 |
|------------|----------|-------------|
| < 64 条目 | **Linear** | 全表线性扫描，零额外开销，~15 ns |
| ≥ 64 条目 | **HashIndex** | 开放寻址哈希索引，~13-17 ns，O(1) 查找 |

**设计：**
- **HashIndex** 在 DLL 加载时一次性构建——无运行时初始化竞争
- 使用开放寻址 + 线性探测，容量为 2 的幂次方
- 负载率 ~50%（容量 = `next_pow2(count * 2)`）
- **零 ABI 格式变更**：`table` 和 `entry` 结构体保持不变，`hash_index` 为纯运行时元数据
- HashIndex 仅存储 `{name_hash, entry_index}`——从不复制 `entry` 数据

**性能数据：**
| 条目数 | Linear | HashIndex | 加速比 |
|---------|--------|-----------|---------|
| 16 | 14.8 ns | 13.2 ns | 1.1× |
| 64 | 21.8 ns | 13.4 ns | 1.6× |
| 256 | 190 ns | 13.9 ns | 13.7× |
| 1024 | 743 ns | 14.8 ns | 50.2× |
| 4096 | 1220 ns | 15.6 ns | 78.2× |
| 16384 | 2451 ns | 17.0 ns | 144.2× |

> **核心思路**：不要优化小表——优化大表。小表已经足够快（~15 ns）。大表通过 HashIndex 可获得 2-3 个数量级的提升。

## 目录结构

```
ABIX/
├── abix/                      # 核心库头文件
│   ├── abix.hpp               # 主入口头文件（包含全部）
│   ├── config.h               # 平台检测、宏
│   ├── type.h                 # 核心类型：entry、table、类型别名
│   ├── register.h             # SKL_ABIX_DEFINE_TABLE、SKL_ABIX_ENTRY 宏
│   ├── obj_dll.h              # dll_object：DLL 加载/卸载/引用计数、RCU 读/写侧、超时策略
│   ├── rcu_domain.h           # rcu_domain：全局 EBR 域，enter/exit/retire/synchronize
│   ├── fn_dll.h               # dll_func / dll_func_cc：类型化函数句柄
│   ├── fn_sig.h               # fn_sig<T>：编译期签名哈希
│   ├── type_sig.h             # type_sig<T>：编译期类型哈希
│   ├── search.h               # find_index、hash_index、find_linear、find_hash：表查找与自动策略选择
│   ├── log.h                  # 日志：可插拔 C 回调接收器、按级别编译期禁用
│   ├── rcu_config.h           # RCUTimeoutConfig：运行时超时设置（毫秒 + 帧数）
│   ├── rcu_timeout.h          # RCU 超时：时间源、OS 定时器、饥饿防护、平台抽象
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
│   ├── hotcache_dll.cpp       # HashIndex 查找基准测试 DLL
│   ├── closed_dll.cpp         # 闭源模拟
│   └── closed_dll_v2.cpp      # 闭源模拟 v2
├── tools/                     # 构建与代码生成脚本
│   ├── build.py               # 主构建脚本
│   ├── build_variants.py      # 跨编译器变体构建
│   ├── build_msvc_variants.ps1 # MSVC 变体构建
│   └── gen_hotcache_dll.py    # hotcache_dll.cpp 代码生成器（大表基准测试）
├── bench/                     # 性能基准测试（Google Benchmark）
│   ├── CMakeLists.txt
│   ├── ebr/                   # EBR 基准（enter/exit、sync、混合负载、拓扑）
│   ├── workloads/             # 工作负载 runner（阈值扫描、workload 混合）
│   └── abix/                  # ABIX 集成基准（resolve、跨策略查找）
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

**要求：** C++17 或更高版本（推荐 C++20 以获得 `consteval` 支持）。mics 库作为 git 子模块包含在内。

## 测试

测试使用 [Catch2](https://github.com/catchorg/Catch2) 框架，入口文件为 `main.cpp`。**全部 31 个测试用例均通过**，覆盖核心功能、边界条件、资源管理、跨编译器兼容性、闭源契约、超时策略及性能基准。

### 测试分类

| 类别 | 测试编号 | 标签 | 覆盖内容 |
|------|----------|------|----------|
| **基础功能** | 1, 3, 6, 7, 9 | `[basic]`, `[typesafe]`, `[callback]`, `[version]`, `[reload]` | 线性扫描、签名哈希、`function_dll` 回调、版本令牌共存、热重载 |
| **资源管理** | 4, 5, 11, 12 | `[resource]` | `unique_dll_ptr`/`ref_dll_ptr`/Socket/字符串跨边界生命周期 |
| **跨编译器/CRT** | 2, 13, 17 | `[cross]` | GCC/Clang/MSVC 混编；MinGW 宿主 + MSVC DLL 无堆冲突 |
| **闭源商业分发** | 15, 16, 18 | `[closed]` | 私有字段隐藏、偏移量双校验、破坏性版本变更拦截 |
| **日志与配置** | 19, 20, 21, 22, 23, 25, 27, 28 | `[log]`, `[config]`, `[tick]` | 日志重定向、缓冲区截断、`RCUTimeoutConfig`、策略切换、`tick()` 注入 |
| **RCU 超时与僵尸** | 24, 26, 29, 30, 31 | `[rcu]`, `[zombie]`, `[policy]`, `[timeout]`, `[tick]` | Safe/ForceUnload/ForceLeak 超时触发、僵尸恢复、帧驱动超时 |
| **反射集成** | 14 | `[refl]` | 静态/动态反射（FP/Any/Registry/TypeInfo/StaticRefl）集成 |
| **边界情况** | 10 | `[edge]` | 未找到、卸载后调用、引用计数阻止卸载、调用约定不匹配 |
| **性能基准** | **8, bench/** | `[perf]`, `[bench]` | **Atomic / EBR / 查找（Linear + HashIndex）/ 调用 / 并发 / 可扩展性** 全矩阵性能基准（Google Benchmark） |

### 构建与运行

```bash
# 构建所有测试（默认 GCC/Clang + Ninja 或 MinGW Makefiles）
cd tools
python build.py

# 同时构建 MSVC 跨编译器变体（用于 Test 2/13/17）
python build.py --with-msvc

# 仅运行已有构建的测试（不重新编译）
python build.py --run-only

# 性能基准（需 Google Benchmark）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench_all
./build/bin/bench_all.exe

# 或运行所有测试+基准（Catch2 + Google Benchmark）
cmake --build build --target test_all bench_all
```

### 预期结果与日志解读

执行 `python build.py --run-only` 后，控制台输出类似以下摘要：

```
All tests passed (31 assertions in 31 test cases)
```

每个测试用例均输出带有 `[log]` 前缀的详细步骤，例如：

- **Test 8**：打印各策略耗时和加速比，自动检查是否满足阈值（若硬件波动可输出 `WARN` 而非失败）。
- **Test 15~18**：打印闭源契约的哈希、偏移量校验结果，若破坏性变更被拦截，明确输出 `[PASS]` 和哈希不匹配信息。
- **Test 24/26/29/30**：模拟 RCU 超时，打印僵尸生成和 `load()` 恢复过程，验证 Safe/ForceUnload/ForceLeak 三种策略的超时触发链路。
- **Test 31**：帧驱动超时，多线程推进 `tick()` 并验证帧数截止触发。

所有测试均不依赖外部网络，DLL 文件位于 `plugins/` 或 `variants/` 目录，若缺少某些跨编译器变体，对应测试自动跳过并输出 `WARN`（不导致整体失败）。

### 调试建议

- Debug 构建下性能测试数据偏大，建议 Release 构建以获取真实性能数据。
- 跨编译器测试需提前运行 `tools/build_msvc_variants.py` 生成 MSVC 变体 DLL，否则相关测试跳过。
- 若某测试失败，日志会明确指出失败位置（`REQUIRE` 表达式及行号），可结合 `build/test.log` 定位问题。

## 性能

ABIX 针对多读少写（read-mostly）场景优化，支持大量并发读者和相对较少的写者。内嵌的 Micro-RCU 通过 cache-line 感知的状态布局和批量 epoch 推进来降低共享 cache-line 竞争。

### Role-Based 工作负载（B8 vs B16 Epoch Batch）

Role-Based 是 ABIX 最重要的性能指标——它模拟真实场景：N 个 reader 线程 + 1 个 writer 线程。下表对比了 `SKL_ABIX_RCU_EPOCH_BATCH = 8`（默认）与 `16` 在 Intel Core i7-14700K（20 P-cores，SMT 关闭）上的表现：

| Workload    | Threads |            B8 |           B16 |
| ----------- | ------: | ------------: | ------------: |
| read-heavy  |     20T |      9.44 G/s | **11.67 G/s** |
| balanced    |     20T |     11.66 G/s | **11.90 G/s** |
| write-heavy |     20T | **13.94 G/s** |     13.53 G/s |

Batching 在 ABIX 的目标 read-mostly 工作负载下能够显著降低高并发开销。完整的方法论、硬件配置、优化分析和全部结果请参见 [Benchmark & Performance](docs/benchmark_ZH.md)。

## 行业方案对比

> [!WARNING]
> 数据来源：ABIX 实测 + 公开基准测试与行业文档。
> 如有错误，请提交 Pull Request，会在第一时间修复。

### 核心指标总览

| 方案 | 核心开销 | 延迟 | 备注 |
|------|---------|------|------|
| **原始函数指针（基线）** | 直接调用 | ~0.095 ns | 编译器可内联 |
| **ABIX（跨 DLL 函数调用）** | 查表 + 间接调用 | ~6.7 ns | 含完整安全校验 |
| **ABIX（哈希索引查找）** | 查表 + 类型安全校验 | ~14 ns | 含完整安全校验 |
| **GetProcAddress / dlsym** | PE/ELF 导出表遍历 + 字符串哈希 | ~27 μs (27,000 ns) | 每次查表 |
| **GetProcAddress（缓存后）** | 仅函数指针调用 | ~0.1 μs (100 ns) | 无类型安全 |
| **std::function 调用** | 类型擦除 + 间接调用 | 1.6 ~ 2.8 ns | SBO 命中 ~1.6 ns |
| **std::function 构造** | SBO 或堆分配 | 2.3 ~ 42 ns | 超 SBO 堆分配 19.6 ns |
| **C++ 虚函数调用** | vtable 查表 + 间接跳转 | 0.55 ~ 2.1 ns | 去虚化后 ~0.23 ns |
| **COM QueryInterface** | 运行时接口查询 + 引用计数 | 显著高于虚函数 | 每次接口切换均需调用 |
| **Qt 信号槽（同线程）** | 元对象查找 + 槽函数调用 | ~42.7 ns | Qt 6.5.1 实测 |
| **Qt 信号槽（跨线程）** | 事件队列 + 参数序列化 | ~128 ns | 跨线程开销显著增加 |
| **Unreal Engine（蓝图 Tick）** | 脚本上下文 + 反射调用 | ~100 - 200 ns | 空蓝图 Tick |
| **Unity（托管→原生回调）** | 托管/原生域切换 | 显著高于原生调用 | 大规模回调时瓶颈明显 |

### 深度解读

#### 1. 与 GetProcAddress / dlsym 对比：快 3 个数量级

`GetProcAddress` 每次调用需要遍历 DLL 的导出表并进行字符串比较，单次开销高达 **27 μs**。虽然缓存函数指针后后续调用接近零开销，但这要求开发者手动维护缓存，且完全放弃了类型安全。

ABIX 的查找机制（HashIndex ~14 ns）将查找开销降低了 **1,900 倍**，同时提供了编译期类型安全——这是手动缓存 `GetProcAddress` 永远无法做到的。

#### 2. 与 std::function 对比：更轻量，更安全

`std::function` 的调用开销约 1.6~2.8 ns，看起来比 ABIX 的 6.7 ns 更快。但请注意：

- `std::function` **不能跨 DLL 边界安全传递**（会触发跨 CRT 堆问题）
- `std::function` 的对象大小为 **32 字节**（vs `function_dll` 的 **8 字节**）
- `std::function` 构造时可能触发堆分配（超 SBO 时 19.6 ns）

ABIX 的 `function_dll` 以 **8 字节** 的固定大小、**零堆分配** 的设计，安全跨越 DLL 边界——这是 `std::function` 完全无法胜任的场景。

#### 3. 与 Qt 信号槽对比：快 6 倍

Qt 同线程信号槽耗时 **42.7 ns**，是 ABIX 完整跨 DLL 调用（6.7 ns）的 **6 倍**。Qt 的开销主要来自元对象系统的连接查找和参数编组——这些正是 ABIX 通过编译期哈希绕过的成本。

#### 4. 与游戏引擎对比：数量级的优势

Unreal 的空蓝图 Tick 约 **100-200 ns**，Unity 的托管→原生回调切换在大规模场景下是已知性能瓶颈。ABIX 的 **6.7 ns** 调用开销意味着：在同样的帧预算下，ABIX 可支撑 **15-30 倍** 于游戏引擎原生回调的调用量。

#### 5. COM QueryInterface：运行时类型安全的代价

COM 的 `QueryInterface` 每次接口切换都需要运行时查询，开销显著高于虚函数调用。ABIX 的类型安全在编译期完成，运行时只需一次 ~14 ns 的哈希查表——无需像 COM 那样在热路径上反复调用 `QueryInterface`。

## 未来计划

- **AMC wrapper 扩展** — 生成原生 wrapper class、显式转换 adapter 与热重载 projection；当前 metadata 不会自动生成 DLL 函数表导出条目。
- **序列化支持** — 扩展 `type_sig` 和类型标签，支持跨 DLL 边界的复杂类型序列化
- **网络传输** — 通过相同的稳定表格式实现远程函数调用

### ABIX Runtime 自举状态

ABIX Runtime 元数据现已完成自描述：干净构建可以使用
`abix/self.abic.toml` 重新生成 model、registry、map 和 RCU/EBR 类型的 metadata，
将其注册后用 `type_of<T>()` 查询原生类型。手写 Bootstrap Kernel 仍是受信任的
启动组件。

AMC 也通过 `amc/self.abic.toml` 描述自身 core IR；其生成的 metadata projection 已
被编译并由 `RuntimeRegistry` 消费。这是 metadata 自举验证，不是编译器源码自举：
`amc-cpp` 的 C++ 语义提取仍依赖 Clang/LLVM，且不会由生成的 descriptor 反向构建。

运行时的后续演进目标是以同一语义替换实现：

```
Header-only RCU（参考实现）
       │
       ▼
ABI Metadata + AMC / ABI 生成器
       │
       ▼
Bootstrap ABI（自动生成胶水代码）
       │
       ▼
libabix_rcu（可替换的 Runtime）
       │
       ▼
ABIX 构建自己的 Runtime
```

最终架构清晰分离关注点：

```
                 Public ABIX Header
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
       Header backend          Runtime backend
             │                       │
        inline reader            ABI Runtime
             │                       │
             └───────────┬───────────┘
                         ▼
                    相同语义 ABI
```

这样 Runtime 可以自由通过布局策略（padded、dense、NUMA、hierarchical）演进，而 `rcu_domain`、`rcu_guard` 和 ABI 契约保持稳定。

## 许可证

Apache License, Version 2.0。详见 [LICENSE](https://www.apache.org/licenses/LICENSE-2.0)。

---

Copyright 2026 [Sukanle](https://github.com/Sukanle)
