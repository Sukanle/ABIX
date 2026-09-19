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

本文用一个小的 C++ ABI 走完 ABIX 的核心流程：构建、inspect、query、diff、verify、
generate。

> ABIX 仍在积极开发中；以下命令与当前 CLI 一致。

## 1. 依赖

* C++17 或更高
* CMake 3.20+
* LLVM / Clang 工具链（AMC C++ 前端链接 `clang-cpp`）
* 原生工具链（gcc/clang、GNU make 或 Ninja）

可选：Lua 5.4（Aue 实验模块）；`readelf`/`strip`/`lldb`/`clang++`（集成测试）。

## 2. 构建

```bash
git clone <repository>
cd ABIX
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel
```

产物在 `build/bin`：

| 工具 | 用途 |
|------|------|
| `amc` | ABI 工具链 driver（build/inspect/query/verify/generate/metadata/publish/fetch） |
| `amc-cpp` | C++ 前端/后端 provider（JSON-lines IPC） |
| `amc-dump` | `.abix` 原始 dump（text/JSON） |
| `amc-mcp` | 面向 AI Agent 的 MCP server |
| `abix-conformance` | Aue L0/L1 差分运行器（有 Lua 时构建） |

运行测试：

```bash
ctest --test-dir build/Release --output-on-failure
```

## 3. 描述一个 ABI

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
./build/Release/bin/amc build -c math_api.abic.toml -B build
# -> build/build/math_api.abix
# -> build/build/math_api.abix.meta
```

完整配置见 [`abic_zh.md`](../abix/abic_zh.md)。

## 4. 查看与查询

```bash
./build/Release/bin/amc inspect build/build/math_api.abix
./build/Release/bin/amc query build/build/math_api.abix --type math::Point --layout
./build/Release/bin/amc query build/build/math_api.abix --function math::add --format json
./build/Release/bin/amc context build/build/math_api.abix --format llm
```

所有消费者也能直接吃编译产物（生成头文件把 ABI 内嵌为 `.abix.metadata` 段）：

```bash
./build/Release/bin/amc query libfoo.so --type math::Point --layout
```

## 5. 对比与校验

```bash
./build/Release/bin/amc diff v1.abix v2.abix
./build/Release/bin/amc compatibility v1.abix v2.abix          # 不兼容退出 1
./build/Release/bin/amc verify -c math_api.abic.toml -B build   # 契约 vs 当前实现
./build/Release/bin/amc verify v1.abix v2.abix --format diagnostics
```

`amc verify` 把任何差异（包括新增类型/函数）都视为 drift。

## 6. 代码生成

```bash
./build/Release/bin/amc generate build/build/math_api.abix -l cpp -o generated.hpp
./build/Release/bin/amc generate build/build/math_api.abix -l lua -o aue_contract.hpp
```

## 7. Metadata Region 与 symbol server

```bash
./build/Release/bin/amc metadata build/build/math_api.abix -o math_api.abixmeta
./build/Release/bin/amc metadata --verify math_api.abixmeta
./build/Release/bin/amc metadata --from-elf libfoo.so --format json
./build/Release/bin/amc publish libfoo.so --root ~/.abix/symbols
./build/Release/bin/amc fetch libfoo.so -o libfoo.abixmeta
```

## 8. AI Agent 集成

```bash
./build/Release/bin/amc-mcp build/build/math_api.abix
python3 tools/abix_mcp_compat.py --host host.abix --plugin plugin.abix \
        --amc-mcp ./build/Release/bin/amc-mcp
```

工具目录见 [`MCP_zh.md`](../ai/MCP_zh.md)。

## 下一步

* [`abix_zh.md`](../abix/abix_zh.md) — `.abix` artifact 格式
* [`compatibility_zh.md`](../architecture/compatibility_zh.md) — ABI 身份与兼容性
* [`amc_zh.md`](../amc/amc_zh.md) — AMC 工具链
* [`architecture_zh.md`](../architecture/architecture_zh.md) — 整体架构
