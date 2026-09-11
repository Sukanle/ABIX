# .abic — ABI Configuration

## 定位

`.abic` 是**声明式 ABI 配置文件**，描述"如何构建 ABI"的意图与策略，而非 ABI 事实本身。

```
.abic = Intent / Policy
.abix = Fact / Artifact
```

`.abic` 更接近 CMakeLists.txt / Cargo.toml / protobuf options / compiler configuration，而不是最终 ABI 数据库。

---

## 核心职责

| 职责 | 说明 |
|------|------|
| 声明源文件路径 | 指定需要扫描的源文件，避免全项目扫描 |
| 声明导出范围 | 哪些类型、函数需要导出 |
| 声明目标平台 | arch / OS |
| 声明 ABI 约定 | 编译器 / 调用约定 |
| 声明归一化策略 | 类型名如何规范化 |
| 声明兼容性策略 | 是否生成兼容关系、兼容规则 |
| 声明映射策略 | 是否生成 Map、映射方向 |
| 声明生成目标 | 目标语言、输出格式 |
| 声明运行模式 | 静态/动态/混合 |

---

## 格式：TOML

`.abic` 面向**人**，因此使用 TOML（或 YAML）这种可读性高的格式。

---

## 完整示例

```toml
[package]
name = "mylib"
version = "2.0"
abi_version = 1

[source]
files = [
    "src/foo.hpp",
    "src/bar.hpp",
    "src/baz.cpp"
]
include_dirs = ["include", "src"]
exclude = ["src/internal/*"]

[target]
arch = "x86_64"
os = "linux"

[abi]
compiler = "gcc"
calling_convention = "sysv_abi"

[export]
types = ["Foo", "Bar", "Baz", "FooKind", "BarFlags"]
functions = ["foo_create", "foo_destroy", "bar_process"]

[normalization]
integer = "fixed-width"       # int → i32, long → i64, ...
pointer = "opaque"             # T* → Ptr<T>
string = "slice"               # std::string → StringSlice
container = "abi_stable"       # std::vector → AbiVector

[compatibility]
enable = true
mode = "layout_hash"           # layout_hash | type_id | strict
max_minor_version = 4

[map]
Foo_v1 = "Foo_v2"              # 版本映射
Bar_old = "Bar_new"            # 重命名映射

[generator]
language = ["cpp", "rust"]
hash_algorithm = "fnv1a64"     # fnv1a64 | xxh3 | blake3
emit_map_ir = true             # 在 .abix 中生成 Map IR
emit_static_map = true         # 生成 MapPrivate 静态转换

[runtime]
mode = "hybrid"                # static | dynamic | hybrid
lazy_load = true               # Runtime 延迟加载

[output]
format = "abix"                # abix | json | none
compact = true                 # 二进制紧凑布局
debug_info = false             # 嵌入调试字符串（与 compact 互斥，debug_info 优先）
```

---

## 配置节详解

### `[package]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 包名 |
| `version` | string | 语义版本号 |
| `abi_version` | uint | ABI 格式版本（与包版本解耦） |

### `[source]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `files` | string[] | 需要扫描的源文件路径（相对 .abic 所在目录） |
| `include_dirs` | string[] | 头文件搜索路径 |
| `exclude` | string[] | 排除的文件模式（glob） |

**设计意图**：AMC 仅扫描 `[source]` 指定的文件，而非整个项目。这显著加快编译速度，尤其在大型项目中避免不必要的 AST 解析。

如果 `[source]` 缺失，AMC 将扫描 .abic 同目录下所有 `.hpp` / `.h` / `.cpp` 文件（向后兼容，但会发出警告）。

### `[target]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `arch` | string | 目标架构：`x86_64` / `aarch64` / `riscv64` / ... |
| `os` | string | 目标 OS：`linux` / `windows` / `macos` / ... |

`[target]` 仅描述**目标平台**，不包含编译器/调用约定（后者属于 `[abi]`）。

### `[abi]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `compiler` | string | 编译器：`gcc` / `clang` / `msvc` / ... |
| `calling_convention` | string | 调用约定：`sysv_abi` / `ms_abi` / `aapcs` / ... |

`[abi]` 描述**编译器与 ABI 约定**，与 `[target]`（平台）职责分离。

### `[export]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `types` | string[] | 需要导出 ABI 的类型名（含枚举） |
| `functions` | string[] | 需要导出 ABI 的函数名 |

枚举本质上是 Type，统一列入 `types`。不再单独设置 `enums` 字段，避免语义重叠。

### `[normalization]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `integer` | enum | 整数归一化策略：`fixed-width` / `native` / `none` |
| `pointer` | enum | 指针归一化策略：`opaque` / `typed` / `none` |
| `string` | enum | 字符串归一化策略：`slice` / `ptr` / `none` |
| `container` | enum | 容器归一化策略：`abi_stable` / `none` |

归一化的目的是让不同编译器/语言产生的类型名映射到同一规范表示：

```
C++ int        → i32
C++ long       → i64 (on LP64)
Rust i32       → i32
Zig i32        → i32
```

### `[compatibility]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `enable` | bool | 是否生成兼容性信息 |
| `mode` | enum | 兼容检查模式：`layout_hash` / `type_id` / `strict` |
| `max_minor_version` | uint | 最大兼容的 minor 版本号 |

### `[map]`

键值对形式，声明类型映射关系：

- 版本映射：`Foo_v1 = "Foo_v2"` — 从旧版本映射到新版本
- 重命名映射：`Bar_old = "Bar_new"` — 从旧名称映射到新名称

### `[generator]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `language` | string[] | 目标语言：`cpp` / `rust` / `zig` / ... |
| `hash_algorithm` | enum | Hash 算法：`fnv1a64` / `xxh3` / `blake3` |
| `emit_map_ir` | bool | 是否在 .abix 中生成 Map IR |
| `emit_static_map` | bool | 是否生成 MapPrivate 静态转换 |

`emit_map_ir` 和 `emit_static_map` 是独立的生成开关，与 `[runtime].mode` 无耦合：
- `emit_map_ir = true, emit_static_map = false`：仅在 .abix 中保存 Map IR，运行时动态执行
- `emit_map_ir = true, emit_static_map = true`：同时生成运行时 IR 和编译期 MapPrivate
- `emit_map_ir = false, emit_static_map = true`：仅生成 MapPrivate（.abix 中无 Map IR）

`hash_algorithm` 是**生成策略**（输入），.abix Header 中的 `hash_algorithm` 是**实际使用的算法**（输出），两者是合理的配置→产物关系。

### `[runtime]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `mode` | enum | 运行模式：`static` / `dynamic` / `hybrid` |
| `lazy_load` | bool | Runtime 是否延迟加载 |

### `[output]`

| 字段 | 类型 | 说明 |
|------|------|------|
| `format` | enum | 输出格式：`abix` / `json` / `none` |
| `compact` | bool | 二进制是否紧凑布局 |
| `debug_info` | bool | 是否嵌入调试字符串 |

`debug_info` 与 `compact` 互斥：当 `debug_info = true` 时，忽略 `compact`（调试信息优先）。

---

## 运行模式

### `static`

所有 ABI 信息在编译期确定，生成 `constexpr` 元数据和 `MapPrivate`。

```
.abic → AMC → .abix → AMC → .abix.hpp → Compiler → inline
```

### `dynamic`

所有 ABI 信息在运行时加载，通过 `mmap` + Registry 动态查询。

```
.abic → AMC → .abix → Runtime → mmap → Registry → 动态查询
```

### `hybrid`（推荐）

已知 ABI 走静态路径，未知 ABI 走动态路径。

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

---

## 与 .abix 的关系

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

- `.abic` 是输入：描述意图
- `.abix` 是输出：记录事实
- AMC 是编译器：从意图产生事实

**Source of Truth 永远是 `.abix`**，而非 `.abic`。`.abic` 只在构建时使用，运行时不参与。

---

## 最小示例

仅导出类型，使用默认配置：

```toml
[package]
name = "minimal"
version = "1.0"

[source]
files = ["src/foo.hpp"]

[export]
types = ["Foo"]
```

AMC 将使用所有字段的默认值补全配置。

---

## 设计原则

1. **声明式**：描述"要什么"，而非"怎么做"
2. **人可读**：TOML 格式，便于手写和版本控制
3. **可补全**：所有字段都有合理默认值
4. **与版本解耦**：`abi_version` 独立于 `version`
5. **仅构建时使用**：运行时不依赖 `.abic`
6. **无语义重叠**：每个配置项有唯一职责，不同节之间不产生冲突组合
