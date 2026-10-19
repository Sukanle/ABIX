# ABIX-STD — 跨语言 ABI 原语

<p align="center">
  中文 · <a href="std.md">English</a>
</p>

ABIX-STD 是**跨语言标准 ABI 原语层**。它不是替代 STL，也不会把整个标准库塞进每个 `.abix`。它定义一组小规模的 ABI 类型与契约，让不同语言无需各自重造一套库接口即可共同消费。

规范见 [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) §9.1。

## 三种数据传递语义

| 语义 | 含义 | 谁释放 |
| ---- | ---- | ------ |
| `borrow` | 非拥有视图，仅在生产者保持数据存活期间有效 | 无——接收方不得释放 |
| `owned`  | 接收方取得所有权 | 接收方，通过生产者声明的释放入口 |
| `copy`   | 由显式复制产生的独立数据 | 接收方拥有副本 |

它们是**契约而非布局**：永不改变 `TypeID` / `LayoutHash` / `ABIHash`。它们由所有权契约（`[[owned]]` / `[[contract]]`）承载，并可从 metadata-region 扩展目录在运行期读取。

## 按能力匹配契约，而非按容器

契约按类型能**证明**的能力匹配，而不是维护 `vector ↔ Vec ↔ ArrayList` 这样的名称表：

| 契约              | 能力                  | 典型用途            |
| ----------------- | --------------------- | ------------------- |
| `SequenceView<T>` | 连续读写视图          | 数组、切片、向量借用 |
| `StringView`      | 文本字节 + 长度 + 编码 | 字符串参数、解析    |
| `Owner<T>`        | 所有权 + 释放协议     | 跨 DLL 对象、资源句柄 |
| `Optional<T>`     | 值可能缺失            | 可选参数、查询结果  |
| `Result<T, E>`    | 成功或失败            | 跨语言错误          |
| `Range<T>`        | 可迭代范围            | 非连续容器          |
| `MapView<K, V>`   | 键值查询              | 关联容器            |
| `Opaque<T>`       | 隐藏表示              | 特殊/自定义容器     |

| 优先级 | 契约 |
| ------ | ---- |
| P0 | 连续序列视图、字符串视图、Opaque 句柄、所有权、错误边界 |
| P1 | Optional、Result、回调、通用迭代范围 |
| P2 | Map、Set、Deque、复杂图结构、各语言专属容器 |

P2 类型通过 `Opaque` 句柄 + accessor、能力适配或显式复制暴露——**不是**统一实现。`std::vector<T>` 本身不是独立契约：当元素类型、布局、借用生命周期与失效条件满足时， AMC 通过 `data()`/`size()` 把它 lower 成 `SequenceView<T>`，否则用 `Opaque` 或复制。

## 第一个原语：borrowed span

ABIX-STD 的 span 以源码级模板书写：

```cpp
namespace abix_std {
template <class T> struct abix_span { const T *data; uint64_t len; };
}
typedef abix_std::abix_span<int32_t> abix_span_i32;   // 导出的 ABI 实体
```

ABIX 记录的是**具体实例**，不是模板。前端按请求实例化，AMC 以导出名记录结果：

```toml
[[instantiate]]
template = "abix_std::abix_span"
args = ["int32_t"]
as = "abix_span_i32"
```

具体实体在各语言中投影出相同布局：

```rust
// Rust —— 由 `amc generate -l rust` 生成
#[repr(C)]
pub struct abix_span_i32 { pub data: *const core::ffi::c_int, pub len: core::ffi::c_ulonglong }
```

```zig
// Zig —— 由 `amc generate -l zig` 生成
pub const abix_span_i32 = extern struct { data: [*c]const c_int, len: c_ulonglong };
```

生产者保持底层内存存活并交出视图；消费者读取它，但永不释放。所有权契约把该交互标记为 `borrowed`：

```toml
[[contract]]
function = "demo_numbers"
returns = "borrowed"
```

## P1 契约

P1 增加四个契约，均按能力匹配（形状 + 元素 `TypeID`）：

| 契约       | 布局 / 能力                                          |
| ---------- | ---------------------------------------------------- |
| `optional` | `{ discriminant: bool/u8; payload: T }`              |
| `result`   | `{ status: int; payload: T }`（复用错误边界）        |
| `callback` | `{ 函数入口; opaque context }`                       |
| `range`    | 非连续容器，经长度 + 索引访问 accessor 暴露          |

实例化与契约匹配是两件事：`abix_span<int32_t>` 实例化成功，并不等于它满足 ABIX-STD 的 `SequenceView<i32>` 契约；AMC 需证明兼容，或生成薄适配器。声明契约后，AMC 会对具体类型做 lowering：

```toml
[[instantiate]]
template = "abix_std::abix_span"
args = ["int32_t"]
as = "abix_span_i32"
contract = "sequence-view"     # -> direct
```

同样是两字段但顺序不同的记录会被判为 `needs_adapter`，AMC 生成到规范视图的转换：

```rust
pub fn abix_view_rev_i32_to_abix_span_i32(value: abix_view_rev_i32) -> abix_span_i32 {
    abix_span_i32 { data: value.data, len: value.len as _ }
}
```

泛型参数属于前端职责，见 [`LANGUAGE-PLUGIN.md`](../../.agents/LANGUAGE-PLUGIN.md) 与 [`ABI-SPEC.md`](../../.agents/ABI-SPEC.md) §18。

## 第二个原语：string view

`abix_str_utf8` 是 `{ const char *data; uint64_t len; uint32_t encoding; }`，对 `string-view` 契约做 lowering：

```toml
[[instantiate]]
template = "abix_std::abix_str"
args = ["char"]
as = "abix_str_utf8"
contract = "string-view"
element = "char"
```

code unit 按数据指针 pointee 的 `TypeID` 匹配；encoding 是**运行期值**（构建阶段只检查字段存在与类型）。默认只读、借用。`std::string_view`、Rust `&str` 与 Zig slice 不被视为天然等价——先验证编码、长度单位、布局与借用。

## 范围

范围内：各语言共享的 ABI 类型与契约（span、string view、owned handle、result、callback）。范围外：重写完整 STL、定义通用对象模型、或运行时所有权系统（见 [`AGENTS.md`](../../.agents/AGENTS.md) §1.3）。

端到端示例位于 `AMC/tests/interop/`：一个 C++ 库、一份 `.abix`，以及 Rust 与 Zig 两个消费者读取同一个 borrowed span。
