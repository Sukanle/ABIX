# 测试 ABIX 与 AMC

<p align="center">
  中文 · <a href="testing.md">English</a>
</p>

本文是完整的测试流程：如何构建与运行、每一层测什么、为什么存在、健康的运行应是什么样。它是 [`CONTRIBUTING.md`](../../CONTRIBUTING.md) 的实操配套。改动行为时，至少运行所属的层（最简是 `ctest`）。

> 仓库规则：**没有被实际运行过的测试，不得声称通过。** 下面列出的预期字符串就是你要核对的证据。

<details>

<summary>目录</summary>

- [构建与运行](#构建与运行)
- [预期结果](#预期结果)
- [测试分层](#测试分层)
    - [A. 运行时与单元（Catch2，`test/`）](#a-运行时与单元catch2test)
    - [B. AMC 单元（`amc-core-test`）](#b-amc-单元amc-core-test)
    - [C. AMC 端到端（`amc-e2e-test`）](#c-amc-端到端amc-e2e-test)
    - [D. AMC CLI 集成（`amc-integration-test`）](#d-amc-cli-集成amc-integration-test)
    - [E. AMC 功能集（`amc-feature-test`）](#e-amc-功能集amc-feature-test)
    - [F. Rust/Zig interop（`amc-interop-test`）](#f-rustzig-interopamc-interop-test)
    - [G. Rust provider 一致性（`amc-rust-test`）](#g-rust-provider-一致性amc-rust-test)
    - [H. cmdly interop（`amc-cmdly-interop-test`）](#h-cmdly-interopamc-cmdly-interop-test)
    - [I. expose interop（`amc-expose-interop-test`）](#i-expose-interopamc-expose-interop-test)
    - [J. CMake 集成（`amc-cmake-integration-test`）](#j-cmake-集成amc-cmake-integration-test)
- [手动功能走查与生成文件](#手动功能走查与生成文件)
- [原生绑定（Rust）走查](#原生绑定rust走查)
- [Sanitizer 与平台说明](#sanitizer-与平台说明)
- [如何解读失败](#如何解读失败)

</details>

## 构建与运行

```bash
# Release 构建
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

# 全部测试
ctest --test-dir build/Release --output-on-failure

# 只跑某一层
ctest --test-dir build/Release -R amc-feature-test --output-on-failure
```

Windows 使用 MSYS2 UCRT64 工具链（见 [`CONTRIBUTING.md#windows-msys2-ucrt64`](../../CONTRIBUTING.md#windows-msys2-ucrt64)）。

可执行文件为 `*.exe`，测试脚本会自动解析 `amc.exe`，公共插件是 exe 同目录下的 `*.dll`。

macOS 上 Homebrew 的 LLVM/Clang 是 keg-only 公式，不在 CMake 的默认搜索路径中，需要显式指定前缀：

```bash
# 使用已安装的版本（见 `brew list --versions llvm`）
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S . \
    -DCMAKE_PREFIX_PATH="$(brew --prefix llvm)"
```

把 `llvm` 换成你实际安装的版本化公式。缺少它时 CMake 会提示 `LLVM / Clang not found — AMC ... will not be built`，AMC 相关目标不会构建，多数命名层不会出现在 `ctest` 中。

## 预期结果

健康的检出应通过**全部 138 个已注册测试**。各命名层与其摘要行：

| #   | 测试                         | 摘要行                                            |
| --- | ---------------------------- | ------------------------------------------------- |
| 1   | `amc-core-test`              | `amc-core-test: all checks passed`                |
| 2   | `amc-e2e-test`               | `=== Results: N passed, 0 failed ===`             |
| 3   | `amc-integration-test`       | `=== Results: N passed, 0 failed ===`             |
| 4   | `amc-feature-test`           | `AMC feature set: N passed, 0 failed, 0 skipped`  |
| 5   | `amc-cmake-integration-test` | `PASS`（或带原因的 `[SKIP]`）                     |
| 6   | `amc-rust-test`              | `N passed, 0 failed`                              |
| 7   | `amc-interop-test`           | `interop: N passed, 0 failed`                     |
| 8   | `amc-cmdly-interop-test`     | `PASS`（或 `[SKIP]`）                             |
| 9   | `amc-expose-interop-test`    | `PASS`（或 `[SKIP]`）                             |
| 10+ | Catch2 用例（`test/`）       | `All tests passed (N assertions in M test cases)` |

第 5/8/9 层在其可选工具链缺失（`rustc`、`fmt`/`cmdly`）时打印 `[SKIP]`（仍算通过），**其余必须全绿**。

## 测试分层

每一层说明：**功能**（测什么）、**目的**（为什么存在）、**预期现象**（可观察的成功）。

### A. 运行时与单元（Catch2，`test/`）

- **功能** —— 纯头文件的 ABI 运行时，对真实动态库进行验证：
    - 经 name+sig 表加载/解析/调用 DLL；
    - 热重载 RCU 卸载/超时/zombie 生命周期、日志；
    - 边界/调用约定；
    - 跨编译器与跨 CRT 变体；
    - MICS 集成；
    - 非侵入式 **Interface** 层；
    - `atomic`，以及 `BinaryImage` 能力层（ELF/Mach-O/PE 的 region 与导出解析）。
- **目的** —— 证明运行时保持原生执行（无通用分派器）、生命周期规则成立（租约/owner 句柄使模块保持映射），且布局/哈希/分派不会静默回归。
- **预期现象** —— 每个套件输出 `All tests passed (N assertions in M test cases)`。
    - 重点套件：
        - `interface_dll_test` 含租约固定用例（“a lease keeps the module mapped across unload”）；
        - `interface_module_test` 含多线程注册/注销与租约/卸载竞争用例。

### B. AMC 单元（`amc-core-test`）

- **功能** —— 直接测 AMC 核心库：
    - 类型/符号哈希、布局；
    - `validate`、`.abix` 读写往返；
    - Metadata Region；
    - 实例化缓存；
    - 字符串/JSON 辅助；
    - 二进制读取器（ELF/Mach-O/PE 节区与 CodeView build id）。
- **目的** —— 让格式/校验核心独立于 CLI 与任何宿主工具链而保持正确。
- **预期现象** —— `amc-core-test: all checks passed`。

### C. AMC 端到端（`amc-e2e-test`）

- **功能** —— `.abix` 生成 → 序列化 → 反序列化 → 比较 → 兼容性，v4 节区目录（含 interface 节区）、符号投影与后端投影生成。
- **目的** —— 证明 IR 字节级往返一致，且可选节区前后兼容。
- **预期现象** —— `=== Results: N passed, 0 failed ===`（当前 366 passed）。

### D. AMC CLI 集成（`amc-integration-test`）

- **功能** —— 端到端驱动真实 CLI：
    - `frontend`、`validate`、`inspect`、`generate`（cpp/lua）；
    - 生成头文件的编译检查；
    - `build`、`dump`、`diff`、`compatibility`；
    - 自描述 bootstrap；
    - provider IPC 握手与能力发现；
    - `context`、`verify`（一致 + 漂移）；
    - 统一 JSON 错误信封；
    - metadata region（发射/校验/篡改）；
    - `query`、符号服务器；
    - region → module；
    - adapter / ABI-check / shim 流程。
- **目的** —— CLI 契约是用户与 CI 依赖的东西；这是最接近“构建服务器实际运行”的测试。
- **预期现象** —— `=== Results: N passed, 0 failed ===`（当前 106）。需要 ELF/Mach-O 专属工具的步骤（`readelf`、LLDB、`dlopen` shim）在 Windows 上打印 `[INFO] ... skipping` 而不计失败。

### E. AMC 功能集（`amc-feature-test`）

跨平台测试（`AMC/tests/cli/feature_test.py`），直接断言 AMC 的五大能力：

| 方面         | 检查                                                                                                                                                                                                 |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 前后端分离   | `--list-languages`（cpp/lua/rust/zig）；`--describe-language`（`cpp`/`rust`=frontend+backend，`zig`=backend，均 `protocol=jsonl-v1`）；`amc-cpp`/`amc-rust` 的 `--ipc` **READY + CAPABILITIES** 握手 |
| 代码解析     | `amc build` 解析 **C++**（`amc-cpp`）与 **Rust**（`amc-rust`，内置前端，无需 `rustc`）→ `.abix`                                                                                                      |
| 生成 IR      | `validate`、`inspect`、`query --layout`、`context --format json`                                                                                                                                     |
| 原生语言绑定 | 同一 `.abix` → `generate -l cpp\|zig\|rust\|lua`；并**编译** C++ 与 **Rust** 绑定                                                                                                                    |
| 复用 IR      | 同一 `.abix` 驱动全部四个后端                                                                                                                                                                        |

- **目的** —— 用一次快速、轻工具链的断言覆盖 AMC 的价值主张：一份 IR、多种语言、干净的前后端分离。
- **预期现象** —— `AMC feature set: 19 passed, 0 failed, 0 skipped`（无 `rustc` 时 Rust 编译检查会 `[SKIP]`，此时 `skipped` 可能为 1）。

### F. Rust/Zig interop（`amc-interop-test`）

- **功能** —— 完整跨语言闭环：构建原生 C-ABI 动态库（`demo_lib.cpp`），`amc build` → `.abix`，`amc generate -l rust` 与 `-l zig`，然后编译并**运行** Rust 与 Zig 消费端， 它们链接原生库并经生成的绑定调用。
- **目的** —— 证明 `.abix` 是可复用的 IR，且投影是可用的原生绑定，而非普通文本。
- **预期现象** —— `interop: 13 passed, 0 failed`，消费端打印 `interop ok: ...` / `zig interop ok`。缺少 `rustc`/`clang++` 或平台不支持时打印 `[SKIP]`。

### G. Rust provider 一致性（`amc-rust-test`）

- **功能** —— Rust 前端（源码级 `#[repr(C)]` / `extern "C"` 提取）与后端（ABIX → Rust `repr(C)` 投影）：广告的 JSONL 协议、产物生成、投影编译，以及**确定性**（相同输入 → 字节一致产物）。
- **目的** —— 语言插件必须可复现且自洽。
- **预期现象** —— `16 passed, 0 failed`。

### H. cmdly interop（`amc-cmdly-interop-test`）

- **功能** —— 一个真实 C++ 库（`cmdly`）经 ABIX 门面 + Rust 消费端；以非平凡类型跑通 expose → thunk → manifest → registry → binding 闭环。
- **目的** —— 在真实库（而非 fixture）上验证端到端叙事。
- **预期现象** —— `PASS`；`rustc`/`fmt`/`cmdly` 缺失时为 `[SKIP]`。

### I. expose interop（`amc-expose-interop-test`）

- **功能** —— 自动能力生成：`amc build` 为被暴露的 C++ 类发射 thunk、发现清单与 Rust 绑定，随后 Rust 消费端跨模块边界创建/调用/销毁对象。
- **目的** —— 证明“无需手写适配器”的路径（`.abic.toml` → 能力）可用。
- **预期现象** —— `PASS`；无 `rustc` 时为 `[SKIP]`。

### J. CMake 集成（`amc-cmake-integration-test`）

- **功能** —— 经 CMake（真实消费工程）驱动 `amc build`，验证 CLI 契约与生成产物。
- **目的** —— 工具链与用户真正使用的构建系统集成。
- **预期现象** —— `PASS`，或带原因的 `[SKIP]`。

## 手动功能走查与生成文件

各套件在临时目录生成并在结束时清理。要查看产物，手动执行：

```bash
AMC=build/Release/bin/amc.exe           # Linux/macOS 去掉 .exe

# 解析源码 -> IR
"$AMC" build -c AMC/tests/fixtures/amc_test.abic.toml -B build/amc-demo

# 一份 IR -> 四种语言绑定
ABIX=build/amc-demo/build/amc_test.abix
"$AMC" generate "$ABIX" -l cpp  -o build/amc-demo/bindings/cpp
"$AMC" generate "$ABIX" -l rust -o build/amc-demo/bindings/rust
"$AMC" generate "$ABIX" -l zig  -o build/amc-demo/bindings/zig
"$AMC" generate "$ABIX" -l lua  -o build/amc-demo/bindings/lua
```

| 后端 | `-l`        | 产物文件                                                           |
| ---- | ----------- | ------------------------------------------------------------------ |
| IR   | `amc build` | `build/amc-demo/build/amc_test.abix`（+ `.abix.meta`）             |
| C++  | `cpp`       | `bindings/cpp/amc_generated.hpp`、`bindings/cpp/amc_abi_check.hpp` |
| Rust | `rust`      | `bindings/rust/amc_generated.rs`                                   |
| Zig  | `zig`       | `bindings/zig/amc_generated.zig`                                   |
| Lua  | `lua`       | `bindings/lua/amc_lua_contract.hpp`                                |

`-o` 可给目录（文件名固定如上）或显式文件路径。

fixture：

- C++ `AMC/tests/fixtures/amc_test.abic.toml`；
- Rust 前端 `AMC/tests/fixtures/rust_demo.abic.toml`。

## 原生绑定（Rust）走查

interop 测试在临时目录正是这么做的：

```bash
# 原生 C-ABI 动态库（Windows 需同时产出 rustc 链接用的导入库）
clang++ -std=c++17 -shared -O2 AMC/tests/interop/demo_lib.cpp \
    -o /tmp/interop_demo.dll -Wl,--out-implib,/tmp/libinterop_demo.dll.a
# Linux：clang++ -std=c++17 -shared -fPIC -O2 ... -o /tmp/libinterop_demo.so

amc build -c AMC/tests/interop/interop_demo.abic.toml -B /tmp/abix     # -> /tmp/abix/interop_demo.abix
amc generate /tmp/abix/interop_demo.abix -l rust -o /tmp/gen           # -> /tmp/gen/amc_generated.rs

# consumer.rs 以 #[path = "gen/amc_generated.rs"] 引入投影
rustc --edition 2021 -O /tmp/consumer.rs --cfg abix_link -L /tmp -o /tmp/consumer
/tmp/consumer
```

`--cfg abix_link` 打开投影里的 `#[link(name = "interop_demo")]`，符号即绑定到原生库。预期最后一行：`interop ok: add=5 scale=13 div=5 err=1 types=31`。消费端的 `assert_eq!` 才是真正的断言（布局、所有权、错误边界、ABIX-STD、Interface）。

## Sanitizer 与平台说明

- **Sanitizer** —— Debug 构建可 `-DSANITIZER=asan` / `-DSANITIZER=tsan`（仅 Linux/macOS；Windows 上禁用）。并发套件（`interface_module_test`、 `concurrency_test`）建议在 TSan 下运行以捕获数据竞争。
- **Windows** —— MSYS2 UCRT64（GCC 或 Clang）。测试为 `*.exe`，工作目录在 `build/<type>/bin`，因此插件（`*.dll`）与已暂存的跨工具链变体（`bin/variants/...`）能被解析。跨 CRT 覆盖需先跑 `tools/build_variants.py --with-msvc`（构建 MSVC 的 `*_dll` 变体）；随后 `13.cross_crt_msvc_resource` 与 `13b.owner and interface across a cross-CRT MSVC DLL` 会真正运行而非跳过。
- **ELF/Mach-O 专属步骤**（`readelf`/`objdump`/`otool`、LLDB、`dlopen` shim）在 Windows 上跳过，由 Linux/macOS 覆盖。

## 如何解读失败

- 用 `--output-on-failure` 单跑该层，或直接运行脚本/exe（见[构建与运行](#构建与运行)）。
- 分层顺序使失败能定位到一个层：`amc-core-test`（核心）→ `amc-e2e-test`（IR 往返）→ `amc-feature-test`（解析/IR/绑定/复用/前后端）→ `amc-interop-test`（跨语言）→ `amc-integration-test`（CLI）。
- `[SKIP]` 不是偶然的通过：它表示可选工具链缺失。装上它（`rustc`、`zig`、`lldb`）即可真正跑该层。
