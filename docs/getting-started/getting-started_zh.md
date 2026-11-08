# 快速上手

<p align="center">
  中文 · <a href="getting-started.md">English</a>
</p>

<details>

<summary>目录</summary>

- [1. 依赖](#1-依赖)
- [2. 构建](#2-构建)
- [3. 描述一个 ABI](#3-描述一个-abi)
- [4. 查看与查询](#4-查看与查询)
- [5. 对比与校验](#5-对比与校验)
- [6. 代码生成](#6-代码生成)
- [7. Metadata Region 与 symbol server](#7-metadata-region-与-symbol-server)
- [8. AI Agent 集成](#8-ai-agent-集成)
- [下一步](#下一步)

</details>

本文用一个小的 C++ ABI 走完 ABIX 的核心流程：构建、inspect、query、diff、verify、 generate。

> ABIX 仍在积极开发中；以下命令与当前 CLI 一致。

## 1. 依赖

> **平台支持：** 支持 Linux、macOS 与 Windows；Windows 上使用 MSYS2 UCRT64 工具链（GCC 或 Clang）。见 [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md#windows-msys2-ucrt64)。

- C++17 或更高
- CMake 3.20+
- 仅在同时构建 AMC 时需要 LLVM / Clang 工具链（AMC C++ 前端链接 `clang-cpp`）
- 原生工具链（gcc/clang、GNU make 或 Ninja）

可选：Lua 5.4（AMC 仓库中的 Aue 实验模块）、`readelf`/`strip`/`lldb`/`clang++`（集成测试）。

## 2. 构建

```bash
git clone <repository>
cd ABIX
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel
```

ABIX 构建 header-only runtime 及其测试。AMC 是独立仓库；其产物位于 AMC 构建目录的 `bin/`：

| 工具               | 用途                                                                            |
| ------------------ | ------------------------------------------------------------------------------- |
| `amc`              | ABI 工具链 driver（build/inspect/query/verify/generate/metadata/publish/fetch） |
| `amc-cpp`          | C++ 前端/后端 provider（JSON-lines IPC）                                        |
| `amc-rust`         | Rust 前端/后端 provider（JSON-lines IPC）；不依赖 Clang/LLVM                    |
| `amc-dump`         | `.abix` 原始 dump（text/JSON）                                                  |
| `amc-mcp`          | 面向 AI Agent 的 MCP server                                                     |
| `abix-conformance` | standalone AMC 构建中的 Aue L0/L1 差分运行器（有 Lua 时构建）                   |

运行测试：

```bash
ctest --test-dir build/Release --output-on-failure
```

## 3. 描述一个 ABI

AMC 是独立仓库：<https://github.com/Sukanle/AMC>。请在 AMC 仓库中构建并安装；下面的命令假定 `amc` 已在 `PATH` 中。

ABI 提取由 `.abic.toml` 驱动：

```toml
[package]
name = "math_api"
version = "1.0"

[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["math_api.hpp"]
symbols = ["math::add", "math::Point"]

[[export]]
output = "build/math_api.abix"
```

```cpp
// math_api.hpp
namespace math {
struct Point { int x; int y; };
int add(int a, int b);
}
```

```bash
amc build -c math_api.abic.toml -B build
# -> build/build/math_api.abix
# -> build/build/math_api.abix.meta
```

完整配置见 [`abic_zh.md`](https://github.com/Sukanle/AMC/blob/main/docs/abic/abic_zh.md)。

## 4. 查看与查询

```bash
amc inspect build/build/math_api.abix
amc query build/build/math_api.abix --type math::Point --layout
amc query build/build/math_api.abix --function math::add --format json
amc context build/build/math_api.abix --format llm
```

所有消费者也能直接吃编译产物（生成头文件把 ABI 内嵌为 `.abix.metadata` 段）：

```bash
amc query libfoo.so --type math::Point --layout
```

## 5. 对比与校验

```bash
amc diff v1.abix v2.abix
amc compatibility v1.abix v2.abix          # 不兼容退出 1
amc verify -c math_api.abic.toml -B build   # 契约 vs 当前实现
amc verify v1.abix v2.abix --format diagnostics
```

`amc verify` 把任何差异（包括新增类型/函数）都视为 drift。

## 6. 代码生成

```bash
amc generate build/build/math_api.abix -l cpp -o generated.hpp
amc generate build/build/math_api.abix -l lua -o aue_contract.hpp
```

## 7. Metadata Region 与 symbol server

```bash
amc metadata build/build/math_api.abix -o math_api.abixmeta
amc metadata --verify math_api.abixmeta
amc metadata --from-elf libfoo.so --format json
amc publish libfoo.so --root ~/.abix/symbols
amc fetch libfoo.so -o libfoo.abixmeta
```

## 8. AI Agent 集成

```bash
amc-mcp build/build/math_api.abix
python3 path/to/AMC/tests/cli/mcp_compat_test.py --host host.abix --plugin plugin.abix \
        --amc-mcp amc-mcp
```

工具目录见 AMC 仓库的 [`MCP_zh.md`](https://github.com/Sukanle/AMC/blob/main/docs/ai/MCP_zh.md)。

## 下一步

- [`abix_zh.md`](../abix/abix_zh.md) — `.abix` artifact 格式
- [`compatibility_zh.md`](../architecture/compatibility_zh.md) — ABI 身份与兼容性
- [`amc_zh.md`](https://github.com/Sukanle/AMC/blob/main/docs/amc/amc_zh.md) — AMC 工具链
- [`architecture_zh.md`](../architecture/architecture_zh.md) — 整体架构
