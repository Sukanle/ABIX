# .abix — Canonical ABI Artifact

## 定位

`.abix` 是 ABIX 体系的核心产物——**与具体语言、编译器、C++ ABI 无关的规范化 ABI 元数据容器**。

```
.abic = Intent / Policy    （配置/意图）
.abix = Fact / Artifact    （事实/产物）
```

`.abix` 可以理解为：

- **ABI Object File** — 类比 `.o` / `.obj`，但描述的是 ABI 而非机器码
- **ABI IR** — 类比 LLVM IR，但描述的是 ABI 布局/兼容/映射而非控制流
- **ABI Bytecode** — 可被 Runtime 直接 `mmap` 消费，也可被 AMC 降级为编译期代码

---

## 核心职责

| 职责 | 说明 |
|------|------|
| 记录 ABI 事实 | 类型、布局、字段、函数、符号的真实状态 |
| 记录 ABI IR | Map 操作的中间表示（语言无关） |
| 记录兼容性 | LayoutHash / SignatureHash / Compatibility |
| 支持 mmap | 二进制格式，零反序列化，直接访问 |
| 支持投影 | 可被 AMC 投影为 C++ / Rust / Zig 等目标代码 |

---

## 格式：Binary

`.abix` 面向 **Runtime / AMC / Compiler**，因此使用二进制格式。

### 设计原则

| 原则 | 说明 |
|------|------|
| 固定宽度 | `u8` / `u16` / `u32` / `u64` / `i8` / `i16` / ...，不使用 `sizeof(long)` / `sizeof(void*)` |
| 不保存裸指针 | 使用 `TypeId` / `Offset` / `Index` 引用，而非指针 |
| Offset-based | 所有表通过偏移量/索引访问 |
| Little endian | 统一字节序 |
| Compact | 针对 cache locality / compactness / sequential access / random access 优化 |
| mmap-friendly | 可直接 `mmap("foo.abix")` 后访问 Header → Section → Record |
| 无冗余 | 不保存可从其他表推导的信息 |

### "无需反序列化" ≠ "零运行时成本"

`mmap` 后仍可能存在：page fault / cache miss / bounds check / hash lookup / pointer chasing。因此 `.abix` 针对访问模式优化布局。

---

## 整体结构

```
.abix
├── Header
├── Section Directory
├── ABI Identity
├── String Table
├── Type Table
├── Layout Table
├── Field Table
├── Function Table
├── Parameter Table
├── Symbol Table
├── Map Table
├── Map Operation Table
├── Dependency Table
└── Extensions
```

**精简说明**（相比初始设计）：

| 移除/变更 | 原因 |
|-----------|------|
| `Target` 节 | 合并入 `ABI Identity`，职责统一为"此 artifact 的编译环境" |
| `Hash Table` | 当前 AMC v4 required section，保存 HashDescriptor 与 canonical hash records |
| `Compatibility Table` 中的 `source/target_layout_hash` | 由 source/target TypeId 查 Type Table → Layout Table 推导 |
| Type Table 中的 `field_begin` / `field_count` | 通过 `layout_index` 从 Layout Table 获取 |
| Symbol Table 中的 `target_hash` | 改为 `(target_kind, target_index)` typed index |
| Parameter Table 中的 `position` | 由数组顺序推导 |
| Field Table 中的 `size` | 默认从字段 TypeId 对应的 Layout 推导（仅数组/opaque 等特殊情况保留） |

---

## 各节详解

### Header

```
Offset  Size  Field
0x00    u32   magic              = 0x58494241  ("ABIX")
0x04    u16   format_version     = 3
0x06    u16   hash_algorithm     = 0 (AMC Core development Hash128)
                                      | 1 (xxh3) | 2 (blake3)
0x08    u32   flags
0x0C    u32   section_count      ← Section Directory 条目数
0x10    u32   section_dir_offset ← Section Directory 偏移
```

**Hash 算法明确记录在 Header 中**，而非隐含在实现里。这是 `.abix` 作为稳定 ABI artifact 的关键设计。

新增 Section Directory 替代固定的 `*_offset` 字段，避免新增 section 时修改 Header 布局。

### 当前 AMC v4 实现 Profile

当前开发版本的 `amc-core` 已实现并且只读取 `format_version = 4`。早期 v1/v2/v3 artifact
在 devel 阶段被主动废弃，不提供兼容读取。v4 使用本节 Header 与
Section Directory，当前 required sections 的 ID 为：`1 Strings`、`2 Identity`、`13 Target`、`3 Types`、
`4 Fields`、`5 Functions`、`6 Parameters`、`7 Symbols`、`8 HashDescriptor`、`9 HashTable`。

已实现的 string 引用一律为 `(offset, length)`；Identity record 还携带 package name 和
package version 的该引用。读者会拒绝错误目录范围、错误 required entry size、越界字符串
引用、非法 TypeKind/SymbolKind，以及不满足 Core layout/type 引用不变量的 artifact。
Hash Table 已作为当前 required profile 实现；Compatibility、Map IR、Dependency 和
目录项为 `{section_id, offset, byte_length, count, entry_size, flags}`。`flags.required`
要求 reader 认识该 section；未知 optional section 会被安全跳过。Compatibility、Map 和
MapOperation 当前是 optional section。Hash Cache sections 仍为后续扩展。

运行时集成使用 `abix/runtime_descriptor.h` 的固定布局 descriptor。MICS Runtime 的
`TypeId` 与 ABIX `model::TypeId` 均为完整 Hash128；跨命名空间转换必须通过显式的
`abix/mics_bridge.h` 函数完成，禁止将 ABI 身份截断为单个 `uint64_t`。

`abix/runtime_registry.h` 提供 `RuntimeRegistry<Capacity>` bridge。它接收生成的
`ModuleDescriptor`，在写入前完成 module 级完整性检查、重复 TypeId 检查和字段/函数类型
引用检查；失败时不会留下部分注册结果。成功后同时保留 runtime descriptor 指针和
canonical `MetadataRegistry` entry，可按 Hash128 或名称查询。

### Section Directory

```
Offset  Size  Field             (per entry)
0x00    u32   section_id        = 1 (string_table) | 2 (abi_identity) | 3 (type_table)
                            | 4 (field_table) | 5 (function_table) | 6 (parameter_table)
                            | 7 (symbol_table) | 8 (map_table) | 9 (map_op_table)
                            | 10 (dependency_table) | 11 (hash_cache) | ...
0x04    u32   offset            ← 该 section 在文件中的偏移
0x08    u32   byte_length       ← section 的完整字节长度
0x0C    u32   count             ← 该 section 的记录数
0x10    u32   entry_size        ← 固定记录大小；0 表示变长 section
0x14    u32   flags             ← bit 0: required
0x0C    u32   entry_size        ← 每条记录的字节数（0 = 变长）
```

### ABI Identity

合并原 `Target` 节，统一描述此 artifact 的编译环境：

```
Offset  Size  Field
0x00    u32   arch               = 0 (x86_64) | 1 (aarch64) | 2 (riscv64) | ...
0x04    u32   os                 = 0 (linux) | 1 (windows) | 2 (macos) | ...
0x08    u32   compiler           = 0 (gcc) | 1 (clang) | 2 (msvc) | ...
0x0C    u32   calling_convention = 0 (sysv_abi) | 1 (ms_abi) | 2 (aapcs) | ...
0x10    u32   abi_flags
0x14    u64   artifact_identity_hash_lo  ← 当前为 package identity hash，Canonical ABIHash 后续实现
0x1C    u64   artifact_identity_hash_hi
0x24    u32   package_name_offset
0x28    u32   package_name_length
0x2C    u32   package_version_offset
0x30    u32   package_version_length
```

后续 Canonical ABIHash 将定义为 `hash(全部 ABI Identity 字段 + 所有 Type/Layout/Function/Symbol)`，
使用 Hash128。当前 v2 profile 的该字段保存完整 canonical ABIHash。

### String Table

```
Offset  Size  Field
0x00    u32   count
0x04    u32   total_bytes
0x08    u8[]  data               ← 连续字符串，以 \0 分隔
```

所有名称通过 `(offset, length)` 引用 String Table 中的子串，避免重复存储。

### Type Table

```
Offset  Size  Field             (per record)
0x00    u64   type_hash_lo      ← TypeId 低 64 位
0x08    u64   type_hash_hi      ← TypeId 高 64 位
0x10    u64   layout_hash_lo    ← LayoutHash 低 64 位
0x18    u64   layout_hash_hi    ← LayoutHash 高 64 位
0x20    u32   name_offset       ← String Table 偏移
0x24    u32   name_length
0x28    u32   kind
0x2C    u32   flags
0x30    u32   size
0x34    u32   align
0x38    u32   field_begin
0x3C    u32   field_count
0x40    u32   array_count
```

当前 AMC v2 profile 的 Type record 为 68 字节，并直接携带 LayoutHash、布局尺寸和字段范围。
独立 Layout Table 及多继承 Base Table 保留为后续 schema 扩展。

当前 AMC devel profile 为支持成员级 projection，在 Field record 前增加 Hash128
`owner_type`，Field record 为 48 字节；在 Function record 前增加 Hash128 `owner_type`，
Function record 为 72 字节。free function 使用零值 owner。

**多继承支持**：`base_type_index` 改为 `base_begin` + `base_count`，指向 Base Table（或内联在 Type Table 末尾的基类索引数组）。

### Layout Table

```
Offset  Size  Field             (per record)
0x00    u64   layout_hash_lo    ← LayoutHash 低 64 位
0x08    u64   layout_hash_hi    ← LayoutHash 高 64 位
0x10    u32   size              ← sizeof
0x14    u32   alignment         ← alignof
0x18    u32   field_begin       ← Field Table 起始索引
0x1C    u32   field_count
0x20    u32   padding           ← 内部填充字节数
0x24    u32   vtable_offset     ← vtable 偏移（UINT32_MAX = 无 vtable）
0x28    u64   base_layout_hash_lo ← 主基类 LayoutHash 低 64 位（0 = 无基类）
0x30    u64   base_layout_hash_hi ← 主基类 LayoutHash 高 64 位
```

字段范围信息**仅在 Layout Table 中保存一份**，Type Table 通过 `layout_index` 引用。

### Field Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   type_hash_lo      ← 字段类型 TypeId 低 64 位
0x10    u64   type_hash_hi      ← 字段类型 TypeId 高 64 位
0x18    u32   offset            ← offsetof
0x1C    u32   flags             ← static / mutable / bitfield / array / opaque / ...
0x20    u32   bitfield_width    ← bitfield 宽度（0 = 非 bitfield）
0x24    u32   bitfield_offset   ← bitfield 偏移
0x28    u32   array_count       ← 数组元素数（0 = 非数组）
0x2C    u32   explicit_size     ← 仅当 flags 含 opaque/array 时有效，否则为 0
```

**精简**：移除 `size` 字段。常规字段大小由 TypeId → Layout Table 推导。仅 `opaque` / `array` 等特殊情况通过 `explicit_size` 保存。

AMC 的 `owner_type` 使 `Foo::field` 选择不依赖成员名称猜测。

### Function Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   signature_hash_lo ← SignatureHash 低 64 位
0x10    u64   signature_hash_hi ← SignatureHash 高 64 位
0x18    u64   return_type_hash_lo ← 返回类型 TypeId 低 64 位
0x20    u64   return_type_hash_hi ← 返回类型 TypeId 高 64 位
0x28    u32   param_begin       ← Parameter Table 起始索引
0x2C    u32   param_count
0x30    u32   calling_convention
0x34    u32   flags             ← static / virtual / const / noexcept / ...
0x38    u32   vtable_index      ← vtable 索引（UINT32_MAX = 非 virtual）
```

AMC 当前 profile 在上述字段前增加 Hash128 `owner_type`。零值表示 namespace/free
function；选择 `Foo` 保留全部成员，选择 `Foo::method` 只保留指定方法。

AMC-M8 frontend 目前将 namespace 和 typedef/alias 作为独立 Type record（kind
`namespace` / `alias`）。Field flags 使用 `field_bitfield`、`field_base`、visibility
位域以及 bit offset/width 位域记录 bitfield、继承和访问级别；具体模板特化按其实例化
类型记录，依赖模板主体不作为可布局 ABI 输出。Function flags 记录访问级别，
`calling_convention` 使用稳定的 AMC 编号（C、stdcall、fastcall、thiscall、aarch64 SVE）。

### Parameter Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset
0x04    u32   name_length
0x08    u64   type_hash_lo      ← 参数类型 TypeId 低 64 位
0x10    u64   type_hash_hi      ← 参数类型 TypeId 高 64 位
0x18    u32   flags             ← in / out / inout / default / ...
```

**精简**：移除 `position`，由数组顺序推导参数位置。

### Symbol Table

```
Offset  Size  Field             (per record)
0x00    u32   name_offset       ← linker symbol 名
0x04    u32   name_length
0x08    u32   mangled_offset    ← mangled 名
0x0C    u32   mangled_length
0x10    u32   target_kind       = 0 (type) | 1 (field) | 2 (function)
0x14    u32   target_index      ← 对应表中的索引
0x18    u32   visibility        = 0 (public) | 1 (protected) | 2 (private)
0x1C    u32   binding           = 0 (local) | 1 (global) | 2 (weak)
```

**精简**：`target_hash` 改为 `(target_kind, target_index)` typed index。Hash 和 index 同时保存没有必要——index 更快且无碰撞风险。

### Compatibility Table

```
Offset  Size  Field             (per record)
0x00    u64   source_type_hash_lo
0x08    u64   source_type_hash_hi
0x10    u64   target_type_hash_lo
0x18    u64   target_type_hash_hi
0x20    u32   compatibility     = 0 (identical) | 1 (layout_compatible)
                            | 2 (map_compatible) | 3 (incompatible)
0x24    u32   map_index         ← 指向 Map Table 的索引（UINT32_MAX = 无映射）
0x28    u32   flags
```

**精简**：移除 `source_layout_hash` / `target_layout_hash`。LayoutHash 由 source/target TypeId → Type Table → Layout Table 推导，无需冗余存储。

### Map Table

```
Offset  Size  Field             (per record)
0x00    u64   source_type_hash_lo
0x08    u64   source_type_hash_hi
0x10    u64   target_type_hash_lo
0x18    u64   target_type_hash_hi
0x20    u32   source_name_offset
0x24    u32   source_name_length
0x28    u32   target_name_offset
0x2C    u32   target_name_length
0x30    u32   operation_begin   ← Map Operation Table 起始索引
0x34    u32   operation_count
0x38    u32   flags             ← bidirectional / lossy / ...
```

当前 AMC v2 Map record 为 60 字节。兼容性报告可跨两个 artifact 保存，因此
Map record 同时保存 source/target 的稳定名称，避免 source TypeId 必须存在于
target artifact 的 Type Table。

### Map Operation Table (ABI IR)

`.abix` 中保存的是 **Map IR**（语言无关的操作码），而非 C++ 代码：

| Opcode | 名称 | source_idx | target_idx | auxiliary | 说明 |
|--------|------|:----------:|:----------:|:---------:|------|
| 0x00 | `COPY_FIELD` | ✓ | ✓ | — | 按偏移拷贝字段 |
| 0x01 | `CONVERT_INT` | ✓ | ✓ | ✓ | 整数类型转换，aux = 目标类型 TypeId |
| 0x02 | `CONVERT_FLOAT` | ✓ | ✓ | ✓ | 浮点类型转换，aux = 目标类型 TypeId |
| 0x03 | `DEFAULT_FIELD` | — | ✓ | ✓ | 用默认值填充，aux = 默认值编码 |
| 0x04 | `RENAME_FIELD` | ✓ | ✓ | — | 字段重命名 |
| 0x05 | `PTR_REINTERPRET` | ✓ | ✓ | ✓ | 指针重新解释，aux = 目标 TypeId |
| 0x06 | `CALL_CONVERTER` | ✓ | ✓ | ✓ | 调用转换器，aux = 转换函数 SignatureHash |
| 0x07 | `SKIP_FIELD` | ✓ | — | — | 跳过字段（源有目标无） |
| 0x08 | `ADD_DEFAULT` | — | ✓ | ✓ | 添加默认字段，aux = 默认值编码 |
| 0x09 | `NESTED_MAP` | ✓ | ✓ | ✓ | 递归映射，aux = 嵌套 Map Table 索引 |

**opcode-specific payload**：不同 opcode 实际需要的字段不同。无效字段设为 `UINT32_MAX`（sentinel），而非浪费空间存储无用数据。

每条 Map Operation 的编码：

```
Offset  Size  Field
0x00    u8    opcode
0x01    u8    flags             ← 保留
0x02    u16   reserved
0x04    u32   source_field_index ← UINT32_MAX = 不使用
0x08    u32   target_field_index ← UINT32_MAX = 不使用
0x0C    u64   auxiliary_lo      ← 附加数据低 64 位
0x14    u64   auxiliary_hi      ← 附加数据高 64 位
```

Runtime 执行 Map 时必须先验证 source/target Layout、字段索引以及
`offset + copy_size <= layout.size`，验证失败不得执行部分写入。当前核心
执行器支持 `COPY_FIELD`、`RENAME_FIELD`、`DEFAULT_FIELD`、`ADD_DEFAULT`
和 `SKIP_FIELD`；`CONVERT_INT`、`CONVERT_FLOAT`、`CALL_CONVERTER` 通过
已注册的无异常 Converter 回调执行。RuntimeKey 或任何单独的 hash 不能
替代这些边界和类型检查。

### Dependency Table

```
Offset  Size  Field             (per record)
0x00    u64   dependency_hash_lo ← 依赖的 .abix 的 ABIHash 低 64 位
0x08    u64   dependency_hash_hi ← 依赖的 .abix 的 ABIHash 高 64 位
0x10    u32   name_offset
0x14    u32   name_length
0x18    u32   version_min
0x1C    u32   version_max
```

### Hash Cache（可选）

```
Offset  Size  Field             (per record)
0x00    u64   hash_lo           ← TypeId / SignatureHash 低 64 位
0x08    u64   hash_hi           ← TypeId / SignatureHash 高 64 位
0x10    u32   table_kind        = 0 (type) | 1 (function) | 2 (symbol)
0x14    u32   index             ← 对应表中的索引
```

**非 canonical 数据**。这是主表可重建的索引，用于 Runtime 快速查找。可由 Runtime 按需构建，或在 .abix 中作为可选 cache section 预生成。

### Extensions

保留区域，用于未来扩展。Section Directory 中 `section_id = 255` 指向此区域。

---

## Type / Layout / Function / Symbol 分离

这是 `.abix` 设计的重要原则：

| 概念 | 回答的问题 | 举例 |
|------|-----------|------|
| **Type** | 这是什么类型？ | `Foo` |
| **Layout** | 它在 ABI 上长什么样？ | `size=32, align=8, field[a@0, b@8, c@24]` |
| **Function** | 逻辑函数签名是什么？ | `Foo* foo_create(i32, u64)` |
| **Symbol** | 对应哪个 linker symbol？ | `_Z11foo_createim` |

**Type ≠ Layout**：同一 Type 在不同平台可有不同 Layout。

**Function ≠ Symbol**：同一 Function 在不同编译器可有不同 mangled Symbol。

这样避免把 C++/Rust/编译器具体实现混入 `.abix` 核心模型。

---

## Hash 体系

### Hash 宽度与兼容层

```cpp
struct Hash128 {
    uint64_t lo;
    uint64_t hi;
};
```

`.abix` 的 canonical 元数据使用 Hash128；现有 ABIX DLL 表和 MICS
运行时接口继续使用 64 位哈希，以保持既有二进制兼容性：

| 哈希类型 | 宽度 | 说明 |
|----------|------|------|
| `TypeId` | Hash128 | `.abix` 中的规范类型标识 |
| `LayoutHash` | Hash128 | 完整布局描述 |
| `SignatureHash` | Hash128 | `.abix` 中的规范函数签名；现有 DLL 函数表仍使用 Hash64 |
| `ABIHash` | Hash128 | 整个 ABI artifact |

现有运行时接口中的 `type_hash` / `sig_t` 保持 `uint64_t`。第一版
`.abix` 使用 Hash128 表示 TypeId、LayoutHash、SignatureHash 和 ABIHash；算法可替换为
XXH3 / BLAKE3 / SHA-256 truncated，**不改变 `.abix` 模型**。Hash 算法由
Header 中的 `hash_algorithm` 字段标识。

实现必须使用算法的规范定义。当前 C++ 参考代码中的双 lane FNV 值仅用于
测试和过渡，并使用独立的 algorithm ID；它不能被标记为标准 FNV1A-128。

哈希只用于快速身份定位，不是内容正确性的证明。加载器发现相同 ID 时，
应继续验证 canonical name、layout 或 signature，避免碰撞导致静默错误。

### HashDescriptor 与 Hash Domain

AMC 当前 v2 使用固定的 HashDescriptor section（ID `8`，16 字节）：

```text
u16 algorithm              = 0 (AMC Hash128)
u16 algorithm_version      = 1
u32 canonical_version      = 1
u32 flags
u32 reserved
```

Hash Table section（ID `9`）使用 32 字节记录：

```text
u32 hash_kind               = 0 artifact | 1 type_id | 2 layout | 3 signature
u32 target_kind             = 0 module | 1 type | 2 function
u32 target_index
u32 flags
u64 hash_lo
u64 hash_hi
```

Identity 中的 artifact hash 是 `ABIHash`。它来自独立的 canonical encoding，不包含
section offset、directory offset、padding 或 String Table 的物理排列。canonical version
和 hash algorithm 变化都会使 ABIHash 变化；package name/version 不参与 ABIHash。

每个 canonical hash value 都必须携带以下描述信息：

```cpp
struct HashDescriptor {
    Algorithm algorithm;
    uint16_t algorithm_version;
    uint16_t digest_bits;
    HashDomain domain;
};
```

`TypeId`、`LayoutHash`、`SignatureHash` 和 `ABIHash` 使用不同的 domain，
即使输入字节相同也不能互相当作同一个 hash。算法、版本、位宽和 domain
共同决定 hash 的语义。

一个 `.abix` 可以携带多个 hash representation，例如 legacy FNV1A-64
和 modern BLAKE3-128。Runtime 选择自己支持的 representation；canonical
value 一旦写入 artifact 就不能在运行时改变。

Runtime 可以从完整 Hash128 生成 64 位 `RuntimeKey` 用于 HashIndex，但
`RuntimeKey` 仅用于定位候选项，最终必须比较完整 digest 和 descriptor。

用户自定义算法使用保留的 algorithm ID，并且只有 Runtime 显式注册并支持
该算法时才能验证。未知算法的 artifact 应报告 unsupported，而不能静默
降级为另一种算法。

### TypeId（类型标识）

```cpp
TypeId = hash(canonical type identity)
```

回答：**是不是同一个类型？**

### LayoutHash（布局哈希）

```cpp
LayoutHash = hash(
    TypeId,
    size,
    alignment,
    field_count,
    field TypeId[],
    field offset[],
    field bitfield[],
    base_class LayoutHash[],
    vtable ABI,
    ...
)
```

回答：**内存布局是否兼容？**

### SignatureHash（签名哈希）

```cpp
SignatureHash = hash(
    return TypeId,
    parameter TypeId[],
    calling_convention,
    qualifiers
)
```

回答：**函数调用是否兼容？**

### ABIHash（整体哈希）

```cpp
ABIHash = hash(
    ABI Identity,
    all TypeId[],
    all LayoutHash[],
    all SignatureHash[],
    all Symbol[]
)
```

回答：**整个 ABI 是否相同？**

### TypeHash ≠ LayoutHash

```cpp
struct A { int a; double b; };   // TypeHash_A, LayoutHash_A
struct B { double b; int a; };   // TypeHash_B, LayoutHash_B

// TypeHash_A ≠ TypeHash_B  （不同类型）
// LayoutHash_A ≠ LayoutHash_B  （不同布局）
// 但 sizeof(A) == sizeof(B) 且 alignof(A) == alignof(B)  （仅靠 size/align 不够！）
```

**区分 TypeHash 和 LayoutHash 是 ABIX 兼容性系统的基础。**

---

## 三种消费方式

### Mode A：Runtime（动态）

```
.abix
  ↓
mmap
  ↓
ABIX Runtime
  ↓
Registry
  ↓
动态查询
```

适用场景：动态库、插件、动态 ABI、未知类型、运行时兼容检查。

支持两种加载模式：

| 模式 | 说明 |
|------|------|
| **Lazy mmap View** | 延迟加载，按需 page fault |
| **Eager Runtime Registry** | 一次性加载到 Registry，全量可用 |

### Mode B：Static（编译期）

```
.abix
  ↓
AMC
  ↓
foo.abix.hpp
  ↓
constexpr
  ↓
Compiler
  ↓
inline
```

生成结果示例：

```cpp
constexpr auto Foo_TypeId    = TypeId{0x...};
constexpr auto Foo_Layout    = TypeLayout{.size=32, .align=8, ...};
constexpr auto Foo_FieldA    = 0;   // offsetof(Foo, a)
constexpr auto Foo_FieldB    = 8;   // offsetof(Foo, b)

// MapPrivate 可被编译器完全优化掉
template<>
struct MapPrivate<FooV1, FooV2> {
    static FooV2 map(const FooV1& src) {
        FooV2 dst;
        dst.a = src.a;              // COPY_FIELD
        dst.b = static_cast<i64>(src.b);  // CONVERT_INT
        dst.c = 0;                  // DEFAULT_FIELD
        return dst;
    }
};
```

最终：`ABI metadata → compile-time → direct offset/load/store`。

### Mode C：Hybrid（混合，推荐）

```
                  .abix
                   │
        ┌──────────┴──────────┐
        ↓                     ↓
 Known ABI              Unknown ABI
        ↓                     ↓
 Compile-time            Runtime
 MapPrivate              Registry
        ↓                     ↓
 zero/low overhead       dynamic compatibility
```

**Static when possible, Dynamic when necessary.**

---

## 与 .abic 的关系

```
                 ┌───────────────┐
                 │    .abic      │
                 │ Intent/Policy │
                 └───────┬───────┘
                         │
                        AMC
                         │
                         ▼
                 ┌───────────────┐
                 │    .abix      │
                 │ ABI Artifact  │
                 │   + ABI IR    │
                 └───────────────┘
```

| 属性 | `.abic` | `.abix` |
|------|---------|---------|
| 定位 | 配置/意图 | 事实/产物 |
| 格式 | TOML（人可读） | Binary（机器可读） |
| 消费者 | AMC（构建时） | Runtime / AMC / Compiler |
| 运行时依赖 | 无 | 有（Runtime 模式） |
| Source of Truth | 否 | **是** |

---

## 投影体系

`.abix` 是 Source of Truth，所有目标代码都是它的投影：

```
foo.abix
  ↓
  ├── Runtime projection  → mmap / Registry → Dynamic ABI
  ├── C++ projection      → foo.abix.hpp    → Static ABI (constexpr)
  ├── Rust projection     → foo.abix.rs     → Static ABI
  ├── Zig projection      → foo.abix.zig    → Static ABI
  └── Debug projection    → foo.abix.txt    → 人类可读
```

**`.abix.hpp` 不应该成为新的"ABI 源文件"**，而是 `.abix` 的编译期投影。

---

## 与传统方案的区别

| 方案 | 本质 | ABIX 的区别 |
|------|------|------------|
| Reflection | 运行时类型内省 | ABIX 包含兼容性/映射 IR，不仅是类型信息 |
| Serialization | 数据编解码 | ABIX 关注 ABI 布局兼容，不关注数据格式 |
| RPC | 远程过程调用 | ABIX 关注本地跨 DLL 边界，不涉及网络 |
| IDL | 接口描述语言 | ABIX 从实际编译产物提取，非手写接口 |
| Protobuf/FlatBuffers | 数据 schema | ABIX 描述内存布局 ABI，非序列化 schema |

ABIX 是一个**位于源码/编译器与 ABI Runtime 之间的 ABI 中间层**。

---

## 核心理念

| 关键词 | 说明 |
|--------|------|
| **Canonical** | 不同编译器/语言/平台的 ABI 信息首先被归一化 |
| **Declarative** | `.abix` 描述"是什么"，而非"怎么写 C++ 代码" |
| **Dynamic** | Runtime 可动态加载，不要求双方 ABI 在编译时完全固定 |
| **Static** | 已知 ABI 可被 AMC 降级为 constexpr / MapPrivate / inline |
| **Self-hosting** | ABIX 自己也使用 ABIX：`abix.abix` → ABIX Runtime → 读取自己的 ABI |

---

## 完整文件关系

```
                      Project
                         │
                         ▼
                      xxx.abic
                    Configuration
                         │
                         │ AMC
                         ▼
                      xxx.abix
               Canonical ABI Artifact
                         │
           ┌─────────────┼─────────────┐
           │             │             │
           ▼             ▼             ▼
        Runtime        C++          Rust/Zig
         mmap        .abix.hpp       .abix.rs
           │             │             │
           ▼             ▼             ▼
       Dynamic ABI   Static ABI     Static ABI
```

**一句话定义：**

> `.abix` 是"ABI 实际是什么"的规范化二进制事实与 ABI IR；同一份 `.abix` 既可以成为 Runtime 的动态 ABI 数据库，也可以被 AMC 降级为几乎零开销的编译期 ABI 代码，从而把"动态 ABI"与"静态性能"统一起来。
