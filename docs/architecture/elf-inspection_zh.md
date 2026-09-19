# 在 ELF 与 Mach-O 二进制文件中检查 ABIX 元数据

<p align="center">
  中文 · <a href="elf-inspection.md">English</a>
</p>

<details>

<summary>目录</summary>

- [元数据如何进入二进制文件](#元数据如何进入二进制文件)
- [节区布局](#节区布局)
- [使用 readelf / objdump（ELF）](#使用-readelf-objdumpelf)
- [使用 otool（macOS）](#使用-otoolmacos)
- [使用 AMC CLI](#使用-amc-cli)
- [Strip 操作](#strip-操作)
- [自动化测试](#自动化测试)
- [支持的格式](#支持的格式)
- [限制](#限制)

</details>

AMC 通过自定义节区将 ABI 元数据嵌入编译后的二进制文件。本文档说明如何使用标准工具（ELF 上的 `readelf`/`objdump`，Mach-O 上的 `otool`）和 AMC CLI 定位、检查和验证这些节区。

## 元数据如何进入二进制文件

AMC C++ 后端（`amc generate -l cpp`）生成的头文件使用 `__attribute__((section(...)))` 将数据放入两个节区，具体属性按目标平台选择：ELF 使用点分隔名称，Mach-O 则要求 `segment,section` 对，且节区名不超过 16 字节。

```cpp
// ELF
__attribute__((used, section(".abix.names")))
inline constexpr const char *amc_type_names[] = { ... };

__attribute__((used, section(".abix.metadata"), aligned(8)))
inline constexpr unsigned char amc_metadata_region[] = { ... };

// Mach-O（macOS）
__attribute__((used, section("__DATA,__abix_names")))
__attribute__((used, section("__DATA,__abix_metadata"), aligned(8)))
```

当用户使用标准编译器（clang++、g++）在 Linux 或 macOS 上编译包含此头文件的代码时，生成的二进制文件包含这两个节区。

```
amc build    →  .abix + .abix.meta
amc generate →  amc_generated.hpp  （section 属性）
clang++/g++  →  ELF 或 Mach-O 二进制文件  （metadata + names 节区）
```

## 节区布局

| 规范名称（ELF） | Mach-O | 内容 | 可 strip？ |
|-----------------|--------|------|-----------|
| `.abix.metadata` | `__DATA,__abix_metadata` | 自描述 Metadata Region：manifest + desc + hash + names（基于偏移，无指针） | 可以，但 `amc metadata --from-elf` 将失败 |
| `.abix.names` | `__DATA,__abix_names` | `const char*` 指针数组，指向类型/字段/函数名称字符串（字符串本体在 `.rodata`/`__TEXT,__cstring`） | 可以，安全 — 运行时不引用它 |

AMC 在调用处统一接受 ELF 规范拼写，并在内部按检测到的容器格式转换为原生拼写。

## 使用 readelf / objdump（ELF）

```bash
# 列出所有 ABIX 节区
readelf -S <binary> | grep abix

# 示例输出：
#   [13] .abix.metadata    PROGBITS   0000000000002048  00002048
#   [25] .abix.names       PROGBITS   0000000000004010  00003010

# 转储 .abix.metadata 原始字节（偏移 0 处应有 ABIX magic）
objdump -s -j .abix.metadata <binary>

# 转储 .abix.names 指针数组
objdump -s -j .abix.names <binary>
```

## 使用 otool（macOS）

```bash
# 列出所有 ABIX 节区（sectname/segname）
otool -l <binary> | grep -A3 -i abix

# 转储 metadata 节区（首字打印为 58494241，即 "ABIX"）
otool -s __DATA __abix_metadata <binary>

# 转储 names 指针数组
otool -s __DATA __abix_names <binary>
```

`.abix.metadata` 节区以 4 字节 magic `ABIX`（十六进制 `41 42 49 58`）开头。

## 使用 AMC CLI

```bash
# 直接从 ELF 或 Mach-O 二进制文件读取元数据（解析 Metadata Region）
amc metadata --from-elf <binary> --format json

# 验证元数据一致性
amc metadata --verify <binary> --format json

# 导出 Metadata Region 为独立的 .abixmeta 文件
amc metadata <file.abix> -o region.abixmeta

# 验证独立的 region 文件
amc metadata --verify region.abixmeta
```

> `--from-elf` 为兼容性保留；输入容器（ELF 或 Mach-O）会被自动检测。

## Strip 操作

```bash
# ELF：移除 .abix.names（安全 — 运行时不使用它）
strip --remove-section=.abix.names <binary>
readelf -S <binary> | grep .abix.names   # 无输出

# Mach-O：移除 __abix_names（安全 — 运行时不使用它）
strip -R __abix_names <binary>
otool -l <binary> | grep __abix_names    # 无输出
```

strip 后，`amc metadata --from-elf` 仍然工作，因为它读取的是 metadata region，而不是 names 节区。

## 自动化测试

项目包含一个测试脚本，在宿主平台上覆盖完整流程（ELF 使用 readelf/objdump，Mach-O 使用 otool）：

```bash
python3 tools/test_amc_elf-pe.py           # 运行并自动清理
python3 tools/test_amc_elf-pe.py --keep    # 保留临时文件供检查
```

脚本运行 9 个阶段：

1. `amc build` — 生成 `.abix` + `.abix.meta`
2. `amc generate` — 生成带 section 属性的 C++ 头文件
3. `clang++` — 编译为原生（ELF/Mach-O）二进制文件
4. `readelf -S` / `otool -l` — 验证 metadata 与 names 节区存在
5. `objdump -s` / `otool -s` — 转储节区内容，验证 ABIX magic
6. `amc metadata --from-elf` — 从二进制解析元数据
7. `amc metadata --verify` — 验证一致性
8. 导出 roundtrip — `.abixmeta` 导出 + 验证
9. 错误路径 — 非二进制拒绝、截断 ELF 拒绝

## 支持的格式

- **Linux / ELF**：ELF64 小端序（x86_64 / aarch64），thin 镜像。
- **macOS / Mach-O**：64 位小端序 Mach-O（arm64），thin 镜像。
- **Windows / PE**：已规划，尚未实现。

## 限制

- **字节序**：目前 ELF 与 Mach-O **仅支持小端序**。大端支持暂时不实现，待未来出现明确的网络 / RPC 需求时再补充。
- **macOS 硬件**：支持 Apple Silicon（arm64）。**暂不支持 Intel Mac（x86_64 macOS）**——没有对应设备用于验证，因此在具备测试设备前不在支持范围内。
- **Mach-O 镜像**必须为 thin（单一架构）；暂不解析 universal / fat 二进制。
- **跨平台目标**：Windows（PE）、macOS（Mach-O）、Linux（ELF）。PE/COFF 解析已规划但尚未实现。
- `.abix.names` 包含 `const char*` 指针；实际字符串在 `.rodata` / `__TEXT,__cstring` 中。使用 `strings <binary> | grep -i amc` 查找。
