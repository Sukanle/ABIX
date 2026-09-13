# AMC — ABI Meta Compiler

AMC 是 ABIX 工具链入口：从语言 AST 提取 ABI、投影为 ABIX IR，并提供查看、比较、
校验、生成与分发工具。

```text
                   AMC
                    │
       ┌────────────┼────────────┐
       ▼            ▼            ▼
     解析         生成          分析
       │            │            │
       └────────────┼────────────┘
                    ▼
                 ABIX IR
```

AMC 是可扩展工具链而非语言专用编译器：前端提取 ABI，ABIX IR 始终是统一表示。

## 配置

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

完整参考见 [`abic_zh.md`](abic_zh.md)。

## 命令

### `amc build`

```bash
amc build -c package.abic.toml [-B <build_dir>]
```

按配置运行前端、投影导出符号，写出 `.abix` 及 `.abix.meta` 索引 sidecar。

### inspect / validate

```bash
amc inspect  file.abix      # 一行摘要
amc validate file.abix      # 结构与 hash 校验
amc inspect  libfoo.so      # 解码内嵌 Metadata Region
```

### query

```bash
amc query file.abix --type Foo --layout
amc query file.abix --function Foo::bar
amc query file.abix --compatible other.abix
amc query file.abix --type Foo --format json
```

`--type`/`--function` 支持精确名、`Owner::name`、子串或 `0x` TypeID。

### context

```bash
amc context file.abix --format llm
amc context file.abix --format json --no-names
```

类型/字段/函数以 `T0/F0/P0` 索引，交叉引用不再重复 128 位 hash。

### diff / compatibility

```bash
amc diff v1.abix v2.abix [-o report.abix]
amc compatibility v1.abix v2.abix        # 不兼容退出 1
```

### verify

```bash
amc verify -c package.abic.toml [-B <build_dir>]
amc verify contract.abix implementation.abix
amc verify contract.abix implementation.abix --format diagnostics
```

`--format diagnostics` 输出 `file:line:column: error: ABI <kind> ...`（使用契约的
Source Origin），供编辑器/CI 使用。

### generate

```bash
amc generate file.abix -l cpp -o generated.hpp
amc generate file.abix -l lua -o aue_contract.hpp
amc generate file.abix -l lua -o conformance.lua
```

### metadata

```bash
amc metadata file.abix -o region.abixmeta [--no-names] [--build-id 0x...]
amc metadata --verify region.abixmeta
amc metadata --from-elf libfoo.so --format json
```

### publish / fetch

以 ELF GNU BuildID 为 key 的本地 symbol server：

```bash
amc publish libfoo.so --root ~/.abix/symbols
amc publish file.abix --root ~/.abix/symbols --build-id 0x...
amc fetch libfoo.so -o libfoo.abixmeta
amc fetch --build-id 0x... -o libfoo.abix
```

目录：`{root}/{build_id[0:2]}/{build_id[2:]}.{abix|abixmeta}` 及 `.meta`。
远端 fallback 尚未实现。

## 结构化错误

```bash
amc validate missing.abix --error-format json
```

```json
{"schema":"abix.error/1","error":{"code":"AMC-IO","category":"io",
 "stage":"read_abix","message":"cannot open input: missing.abix",
 "file":"missing.abix","detail":null}}
```

命令带 `--format json` 时错误也切到 JSON。

## 其他工具

| 工具 | 用途 |
|------|------|
| `amc-cpp` | C++ 前端/后端 provider（JSON-lines IPC） |
| `amc-dump` | `.abix` 原始 dump（text/JSON） |
| `amc-mcp` | MCP server（见 [MCP.md](MCP.md)） |
| `abix-conformance` | Aue L0/L1 差分运行器（有 Lua 时构建） |
