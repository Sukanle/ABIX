# ABIX 元数据三档模式：内嵌可读 / 全 Hash / 剥离文件

## 设计背景

ABIX 的运行时类型元数据需要在**诊断能力**和**生产体积**之间做选择。

在早期实现中，`TypeDescriptor` 携带 `name` 指针（72 字节），字符串常量落在 `.rdata`，
每条类型元数据 ~120 字节。这对于开发调试是便利的，但对于生产发布，这个开销累积到
数百类型后就会变成可感知的空间占用。

DWARF/PDB 的思路是：调试信息是一个独立 section，`strip` 可以移除它，`.text` 和 `.data`
不受影响。ABIX 借鉴了这一模式，但做了更深层的布局优化——**不仅分离 section，还根据
档位调整 descriptor 的自身布局**。

## 核心理念

```
┌──────────────────────────────────────────────────────────────┐
│  名字是符号，不是数据                                          │
│                                                              │
│  type_id / layout_hash = 数据（运行时契约校验必需）              │
│  name / 字段名字符串   = 符号（只有诊断需要）                    │
└──────────────────────────────────────────────────────────────┘
```

这对应 ELF 里 `.symtab` 和 `.strtab` 的关系——运行时只需要地址，名字只给调试器看。
ABIX 做的是同一件事，只是把它提升到了 ABI 元数据层。

## 三档总览

| 档位 | TypeDescriptor | 字符串位置 | .abix 文件 | 典型场景 |
|------|---------------|-----------|-----------|---------|
| **Debug** | 72 B（含 `name`） | 内嵌 `.abix.names` section | 可选生成 | 开发 / 单步调试 |
| **RelWithDebInfo** | 56 B（无 `name`） | 独立 `.abix` 归档 | 必须归档 | 灰度 / 崩溃分析 |
| **Release** | 56 B（无 `name`） | 无 | 建议归档 | 线上发布 |

关键性质：
- **RelWithDebInfo 和 Release 的二进制完全一致**，区别仅在 `.abix` 文件是否归档
- **同一份二进制**，strip 前是 debug，strip 后是 release
- **编译一次，两种形态**，可做 differential testing

---

## Debug 模式：内嵌可读

### TypeDescriptor 布局（72 字节）

```cpp
struct TypeDescriptor {           // sizeof = 72
    const char *name;             //  8 B  ← 指向 .abix.names section
    Hash128 type_id;              // 16 B
    LayoutHash layout_hash;       // 16 B
    uint32_t flags;               //  4 B
    uint32_t size;                //  4 B
    uint32_t align;               //  4 B
    const FieldDescriptor *fields; //  8 B
    uint32_t field_count;         //  4 B
    // padding                    //  8 B
};
```

### .abix.names section

AMC C++ backend 生成一个独立的 section，包含所有类型/字段/函数名的字符串常量：

```cpp
#ifdef __GNUC__
__attribute__((section(".abix.names")))
#endif
inline constexpr const char *amc_type_names[] = {
    "MyClass",
    "MyStruct",
    "MyEnum",
    // ...
};
```

这个 section：
- 在 ELF 上落入 `.abix.names`（自定义 section）
- **不被任何运行时代码引用** —— 运行时路径只使用 `type_id`/`layout_hash`
- 可以被 `strip` 安全移除，不影响程序行为
- 供 `amc-dump`、`ABIX symbol server` 等外部工具消费

### 适用场景

- 本地开发，需要单步调试中看到类型名
- 单元测试，断言失败时可读的类型信息
- 不需要考虑二进制体积的环境

---

## Release 模式：全 Hash

### TypeDescriptor 布局（56 字节）

```cpp
struct TypeDescriptor {           // sizeof = 56
    const FieldDescriptor *fields; //  8 B
    Hash128 type_id;              // 16 B
    LayoutHash layout_hash;       // 16 B
    uint32_t flags;               //  4 B
    uint32_t size;                //  4 B
    uint32_t align;               //  4 B
    uint32_t field_count;         //  4 B
};                                // 56 B，无 name，无 padding
```

所有其他 descriptor 同样移除 `name` 字段：

| 结构体 | Debug 大小 | Release 大小 | 节省 |
|--------|-----------|-------------|------|
| `TypeDescriptor` | 72 B | 56 B | **22%** |
| `FieldDescriptor` | 32 B | 24 B | **25%** |
| `FunctionDescriptor` | 56 B | 48 B | **14%** |
| `ParameterDescriptor` | 32 B | 24 B | **25%** |
| `SymbolDescriptor` | 24 B | 8 B | **67%** |

> `ModuleDescriptor` 保留 `name` 和 `version`，因为它们不是诊断名字，而是逻辑包标识符，
> 模块加载和验证路径需要它们。

### 运行时契约：完全基于 Hash

Release 模式下，所有运行时查询只使用 hash：

```cpp
// Debug + Release 都支持
const auto *entry = registry.find_by_id(type_id);

// 仅 Debug 支持，Release 返回 nullptr
const auto *entry = registry.find_by_name("MyClass");
```

两个构建模式的 `type_id` 完全相同，因为它是 **canonical type identity 的哈希** ——
不依赖名字、不依赖布局、不依赖编译器版本。

### 约束：`TypeDescriptor` 不进 ABI 契约

由于布局随构建类型变化，`TypeDescriptor` 不能是跨 DLL/so 传递的类型。
它只是进程内结构。如果未来需要跨边界传递 descriptor，需要固定布局。

---

## RelWithDebInfo：剥离文件

### 核心思想

RelWithDebInfo 构建的二进制**完全等同于 Release**（56 字节 descriptor，无名字），
但会归档一份 `.abix` 文件作为同伴产物。

```
release binary         relwithdebinfo binary         debug binary
     │                       │                            │
     │                  56 B descriptor              72 B descriptor
     │                  no .abix.names                .abix.names section
     │                       │                            │
     ▼                       ▼                            ▼
 线上运行               灰度验证 / 崩溃分析              本地开发
                             │
                             ▼
                    companion .abix artifact
                    (包含所有类型名/字段名)
```

这样：
- **同一份二进制**，strip 前是 debug，strip 后是 release
- 灰度环境和线上环境运行完全相同的二进制
- 灰度环境归档了 `.abix`，可以事后诊断
- 线上环境不归档 `.abix`，就是最小发布

### 版本配对：Hash 就是配对键

```
崩溃栈 → 提取 type_id (Hash128)
       → 在归档的 .abix 文件里查这个 hash
       → 得到 name / layout / field 信息
```

关键：`type_id` 是 canonical type identity 的哈希，它在 debug 和 release 下完全相同。
所以 debug 和 release 二进制可以互相校验，`.abix` 文件和二进制可以双向验证。
如果 hash 不匹配，说明版本错配——这是一个**加载期可检测的错误**，不是运行时才暴露。

这比 DWARF 更安全。DWARF 的 `DW_AT_name` 是字符串，没有内建校验；
ABIX 的 hash 是强校验。

---

## Section 分离机制

### 生成

AMC C++ backend 在生成 descriptor 数组后，额外生成一个 `.abix.names` section：

```cpp
// 由 AMC 自动生成，不被任何运行时代码引用
#ifdef __GNUC__
__attribute__((section(".abix.names")))
#endif
inline constexpr const char *amc_type_names[] = {
    "MyClass", "MyStruct", "MyEnum", ...
};
```

### Strip

```bash
# Debug → Release（移除 .abix.names section）
strip --strip-section=.abix.names <binary>

# 验证 section 已被移除
readelf -S <binary> | grep .abix.names  # 无输出
```

### 在二进制中查看

```bash
# 列出所有自定义 section
readelf -S <binary> | grep abix

# 查看 .abix.names 内容
objcopy --dump-section .abix.names=/dev/stdout <binary> | strings
```

### 非 GCC 兼容处理

在不支持 `__attribute__((section))` 的编译器上，`.abix.names` 内容退化到常规 `.rodata`，
但运行时仍然不引用它们：

```cpp
// 非 GCC 编译器：名字字符串仍在二进制中，但位于普通 .rodata
// 诊断工具仍可通过符号表找到它们
inline constexpr const char *amc_type_names[] = {
    "MyClass", ...
};
```

---

## ABIX Symbol Server

这个设计打开了一个自然的能力：**ABIX 符号服务器**。

```
线上崩溃 → 提取 type_id hash 列表
         → 查询 ABIX symbol server
         → 拉取对应的 .abix 片段
         → 还原类型名 / 字段名 / 布局
```

这和微软的 symbol server、Mozilla 的 Tecken 是同一个模式。ABIX 有天然优势：
**hash 是内容寻址的**，同一个 `.abix` artifact 可以在多个项目间共享，不需要按构建版本存储。

### .abix.meta 元文件

建议在 AMC 生成时，除了 `.abix`，再生成一个 `.abix.meta` 元文件，记录：

```ini
build_id = "20260315-abcdef"
timestamp = 2026-03-15T10:30:00Z
compiler = "Clang 22.0"
hash_algorithm = "xxh3_128"
type_count = 61
```

这样 symbol server 能按构建 ID 检索，不依赖文件名约定。

---

## 量化收益

### 500 类型估算

| 档位 | 总元数据 |
|------|---------|
| Debug（内嵌字符串） | ~123 KB |
| Release（descriptor + hash） | ~28 KB |
| **相对节省** | **77%** |

### 实际测量（ABIX runtime self-description，61 类型）

| 指标 | Debug | Release |
|------|-------|---------|
| TypeDescriptor 数组 | 4,392 B (72 B × 61) | 3,416 B (56 B × 61) |
| 名字字符串 | ~3 KB | 0 B |
| .data.rel.ro 段 | 11.4 KB | ~8 KB |
| **二进制增量** | ~100 KB | ~50 KB（含 descriptor 数组） |

### 节省来源分析

```
Debug:   72 B (TypeDescriptor含name) + 变长字符串常量 ≈ 120 B/type
Release: 56 B (TypeDescriptor无name) + 无字符串常量   ≈ 56 B/type
```

Release 节省的 22% 来自 `TypeDescriptor` 自身的 `name` 指针移除（连带 padding 减少），
而字符串常量的节省（~20-50 B/type）是额外收益。

---

## 操作指南

### 场景一：本地开发（Debug）

```bash
amc build -c my_project.abic.toml -B build/debug
# AMC 生成包含 .abix.names 的 Debug 模式 C++ projection
# 直接编译即可，名字字符串可用
```

### 场景二：线上发布（Release）

```bash
amc build -c my_project.abic.toml -B build/release
# AMC 生成不包含名字字符串的 Release 模式 C++ projection
# 编译后 strip 确认无 .abix.names section
strip --strip-section=.abix.names bin/my_app
```

### 场景三：灰度/崩溃分析（RelWithDebInfo）

```bash
amc build -c my_project.abic.toml -B build/relwithdebinfo

# 归档 .abix 文件到符号服务器
cp build/relwithdebinfo/build/my_project.abix /symbol-server/releases/v1.2.3/

# 编译、发布与 Release 完全相同的二进制
# 线上崩溃时，用崩溃栈中的 type_id 查询符号服务器
```

### 验证：strip 前后一致性

```bash
# 1. 编译 Debug 版本
# 2. 运行测试，确认行为正确
./bin/test_all

# 3. strip 移除 .abix.names
strip --strip-section=.abix.names bin/test_all

# 4. 再次运行同一组测试，行为完全一致
./bin/test_all

# 5. 验证 .abix.names 已被移除
readelf -S bin/test_all | grep -c .abix.names  # 输出 0
```

---

## 必须守住的边界

### 边界一：`.abix` 文件必须和二进制版本精确对应

Hash 校验能防错配，但前提是**校验真的执行了**。加载期必须做一次 `type_id` 集合比对，
不能只信文件存在。

### 边界二：Release 下不能有"依赖名字"的运行时逻辑

一旦某个热路径依赖 `name` 字符串（比如日志、错误信息），Release 就会出现空指针或占位符。
规则：**名字只在诊断路径使用，诊断路径必须能容忍名字缺失。**

### 边界三：`TypeDescriptor` 布局随档位变化，不能进 ABI 契约

如果需要跨 DLL/so 传递 descriptor，就需要固定布局——那时候 `name` 字段要么永远存在
（用 56 B + 8 B 的取舍），要么用 handle 间接。

### 边界四：`.abix` 的归档策略要明确

建议在 AMC 生成时，除了 `.abix`，再生成一个 `.abix.meta` 元文件，记录构建 ID、时间戳、
编译器版本、hash 算法版本。这样 symbol server 能按构建 ID 检索，不依赖文件名约定。

---

## 总结

这个设计把"元数据膨胀"从**结构性代价**降级为**可配置的调试开销**。
默认路径为生产优化，诊断能力按需附加。

```
Debug:       72 B descriptor + .abix.names section  → 完整可读
Release:     56 B descriptor + 无名字字符串          → 最小体积
RelWithDeb:  56 B descriptor + 同伴 .abix 文件       → 事后诊断
                                                    ↑
                                     同一二进制，仅 .abix 文件是否归档
```