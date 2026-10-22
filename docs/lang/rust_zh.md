# Rust

<p align="center">
  <a href="rust.md">English</a> · 中文
</p>

<details>
<summary>目录</summary>

- [角色](#角色)
- [前端](#前端)
- [生成的投影](#生成的投影)
- [所有权与错误](#所有权与错误)
- [示例](#示例)
- [限制与说明](#限制与说明)

</details>

Rust 是 ABIX 的第一个非 C++ provider。`amc-rust` 包含源码级前端与 Rust 后端，不链接 Clang/LLVM。

## 角色

- `amc --describe-language rust` 报告 `frontend backend protocol=jsonl-v1`。
- 前端从 `.rs` 文件中读取 `#[repr(C)]` struct/enum 与 `extern "C"` 函数，并自行计算 C 布局。
- 后端从已有的 `.abix` artifact 生成 `amc_generated.rs`。
- `amc-rust` 是独立的 provider 进程；它从不修改 ABIX IR。

## 前端

`.abic.toml` 选择 `language = "rust"`，并列出源码与导出符号：

```toml
[[import]]
language = "rust"
files = ["src/lib.rs"]
symbols = ["DemoVec2", "demo_add"]

[[export]]
output = "demo.abix"
```

前端刻意是一个初步提取器：它理解跨越 FFI 边界的那部分 Rust —— `#[repr(C)]` struct/enum 与 `extern "C"` 函数 —— 并计算与 C++ 前端从 Clang 得到的相同布局。泛型、trait、生命周期与位域不在范围内；不支持的构造会被忽略，而不是猜测。

一个 provider 源码如下：

```rust
#[repr(C)]
pub struct DemoVec2 {
    pub x: i32,
    pub y: f64,
}

#[no_mangle]
pub extern "C" fn demo_add(a: i32, b: i32) -> i32 {
    a + b
}
```

## 生成的投影

```bash
amc generate build/package.abix -l rust -o gen/
```

`amc_generated.rs` 包含：

- `#[repr(C)]` 记录与枚举；
- 导出函数的 `unsafe extern "C"` 声明；
- 类型身份、大小、对齐与字段 offset 的 `ABIX_*` 常量；
- `const _: () = assert!(core::mem::size_of::<T>() == ...)` / `align_of` / `offset_of!` 检查；
- 可选的 metadata region 模块，用于运行时检查。

C ABI 标量会投影为 `core::ffi::c_*` 别名 (`c_int`、`c_double` 等)，这些别名由 ABI 归一化后的 primitive 身份推导，而不是固定宽度拼写，因此别名本身编码了目标平台的 C ABI。

## 所有权与错误

后端把记录的契约转换成 Rust 原生包装：

- `owned` 工厂变成带 `Drop`、`as_ref`、`as_ptr`、`into_raw` 的 `Owned*` 包装；释放入口就是 artifact 中记录的那个，因此 Rust 不会自行释放外部内存；
- 错误边界变成带类型的 `Result<T, E>` 包装，例如 `demo_div_result`；
- ABIX-STD 的 span/string/optional/callback 类型按记录的布局投影为带类型的记录。

原始 `extern "C"` 调用仍然是 `unsafe` 的；包装缩小了 unsafe 表面积，但没有掩盖 ABI。

## 示例

```rust
#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;

fn main() {
    unsafe {
        assert_eq!(demo_add(2, 3), 5);

        let v = DemoVec2 { x: 4, y: 2.5 };
        assert_eq!(demo_scale(&v, 2.0), 13.0);

        let owned = OwnedDemoVec2::create(7, 1.5).expect("create");
        assert_eq!(owned.as_ref().x, 7);
        assert_eq!(demo_div_result(10, 2), Ok(5));
    }
}
```

Rust 文件仍需要链接原生库；当启用 `abix_link` 选项时，投影会通过 `ABIX_LIBRARY_NAME` 提供 `#[link]` 支持。

## 限制与说明

- 前端不是完整的 `rustc` AST：只提取 `#[repr(C)]` / `extern "C"` 表面；泛型、trait、生命周期与位域留在模块内部。
- 记录的 ABI 是 target 相关的；Rust 消费方必须按与 provider 相同的 target 与 C ABI 编译。
- panic 与异常不得穿越边界；错误边界契约是受支持的路径。
- 生成文件是普通 Rust，可以在外层再封装成安全的 crate API。
- `amc-rust` 不链接 Clang/LLVM；它的前端自行计算布局。
