# Zig

<p align="center">
  <a href="zig.md">English</a> · 中文
</p>

<details>
<summary>目录</summary>

- [角色](#角色)
- [生成的投影](#生成的投影)
- [布局检查](#布局检查)
- [示例](#示例)
- [限制与说明](#限制与说明)

</details>

Zig 是 **backend-only** 的 ABIX 目标：ABIX 消费已有的 `.abix` artifact，再把它投影成 Zig。没有 Zig 前端，AMC 中也没有 Zig 专属模型。

## 角色

- `amc --describe-language zig` 报告 `backend protocol=jsonl-v1`。
- `amc-zig` 是独立的 provider 进程；它消费与 Rust、C++ 后端相同的 ABIX IR。
- Zig 是投影目标，而不是 import 语言。先从 C++ 或 Rust 源码构建 artifact，再运行 `amc generate -l zig`。

## 生成的投影

```bash
amc generate build/package.abix -l zig -o gen/
```

`amc_generated.zig` 包含：

- 布局兼容记录的 `extern struct` 声明；
- 导出函数的 `extern fn` 声明；
- `[*c]` 带类型指针；不透明 pointee 使用可空指针；
- 类型身份、大小、对齐与字段 offset 的 `ABIX_*` 常量；
- 带 `@sizeOf`、`@alignOf` 与 `@offsetOf` 断言的 `comptime` 块。

驱动 Rust 投影的同一份 artifact 也驱动 Zig 投影。`AMC/tests/interop/` 会构建同一个原生库，并让两个消费者都对它运行；AMC 中没有加入任何 Zig 专属信息。

## 布局检查

`comptime` 块会把布局不匹配变成构建失败：

```zig
comptime {
    if (@sizeOf(DemoVec2) != 16) @compileError("DemoVec2 size mismatch");
    if (@alignOf(DemoVec2) != 8) @compileError("DemoVec2 align mismatch");
    if (@offsetOf(DemoVec2, "y") != 8) @compileError("DemoVec2::y offset mismatch");
}
```

这是 C++ `static_assert` 与 Rust `const assert!` 在 Zig 中的对应物：记录的 ABI 在编译期被验证，原生调用还来不及出错。

## 示例

```zig
const std = @import("std");
const abi = @import("gen/amc_generated.zig");

pub fn main() void {
    const sum = abi.demo_add(2, 3);
    if (sum != 5) @panic("demo_add");

    var v = abi.DemoVec2{ .x = 4, .y = 2.5 };
    const scaled = abi.demo_scale(&v, 2.0);
    if (scaled != 13.0) @panic("demo_scale");

    var out: c_int = 0;
    const rc = abi.demo_div(10, 2, &out);
    if (rc != 0 or out != 5) @panic("demo_div");
}
```

Zig 消费方链接与 C++、Rust 消费方相同的原生库；它不需要 C 头文件，也不需要生成的 C shim。

## 限制与说明

- 目前 Zig 是 backend-only：没有 Zig 源码提取器，因此 Zig provider 必须通过其他前端或生成的 artifact 暴露 ABI 兼容边界。
- 导出面仍必须能用原生 ABI 表达；Zig 内部类型与 allocator 留在模块内部。
- 记录的 ABI 是 target 相关的；Zig 消费方必须按与 provider 相同的 target 与 C ABI 构建。
- `[*c]` 指针保留 C 指针语义，`extern struct` 与 `comptime` 检查则把布局钉死。
- 错误边界尚未投影为 Zig error set / 错误联合；当前只保留普通 status/payload 参数。
- ABIX IR 中没有加入任何 Zig 专属概念；投影完全由 artifact 推导。
