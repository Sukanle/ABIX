# 在 ELF 二进制文件中检查 ABIX 元数据

AMC 通过自定义节区将 ABI 元数据嵌入编译后的 ELF 二进制文件。本文档说明如何使用标准工具（`readelf`、`objdump`）和 AMC CLI 定位、检查和验证这些节区。

## 元数据如何进入二进制文件

AMC C++ 后端（`amc generate -l cpp`）生成的头文件使用 `__attribute__((section(...)))` 将数据放入两个 ELF 节区：

```cpp
// 名称表 — 仅用于诊断，可安全 strip
__attribute__((used, section(".abix.names")))
inline constexpr const char *amc_type_names[] = { ... };

// 自描述 Metadata Region（manifest + desc + hash + names）
__attribute__((used, section(".abix.metadata"), aligned(8)))
inline constexpr unsigned char amc_metadata_region[] = { ... };
```

当用户使用标准编译器（clang++、g++）编译包含此头文件的代码时，生成的 ELF 二进制文件包含两个节区。

```
amc build    →  .abix + .abix.meta
amc generate →  amc_generated.hpp  （section 属性）
clang++/g++  →  ELF 二进制文件  （.abix.metadata + .abix.names）
```

## 节区布局

| 节区 | 内容 | 可 strip？ |
|------|------|-----------|
| `.abix.metadata` | 自描述 Metadata Region：manifest + desc + hash + names（基于偏移，无指针） | 可以，但 `amc metadata --from-elf` 将失败 |
| `.abix.names` | `const char*` 指针数组，指向类型/字段/函数名称字符串（字符串本体在 `.rodata`） | 可以，安全 — 运行时不引用它 |

## 使用 readelf

```bash
# 列出所有 ABIX 节区
readelf -S <binary> | grep abix

# 示例输出：
#   [13] .abix.metadata    PROGBITS   0000000000002048  00002048
#   [25] .abix.names       PROGBITS   0000000000004010  00003010

# 详细节区信息（大小、偏移、标志）
readelf -S --wide <binary> | grep -A1 "\.abix"
```

## 使用 objdump

```bash
# 转储 .abix.metadata 原始字节（偏移 0 处应有 ABIX magic）
objdump -s -j .abix.metadata <binary>

# 转储 .abix.names 指针数组
objdump -s -j .abix.names <binary>
```

`.abix.metadata` 节区以 4 字节 magic `ABIX`（十六进制 `41 42 49 58`）开头。

## 使用 AMC CLI

```bash
# 直接从 ELF 二进制文件读取元数据（解析 .abix.metadata 节区）
amc metadata --from-elf <binary> --format json

# 验证元数据一致性
amc metadata --verify <binary> --format json

# 导出 Metadata Region 为独立的 .abixmeta 文件
amc metadata <file.abix> -o region.abixmeta

# 验证独立的 region 文件
amc metadata --verify region.abixmeta
```

## Strip 操作

```bash
# 移除 .abix.names（安全 — 运行时不使用它）
strip --remove-section=.abix.names <binary>

# 验证已移除
readelf -S <binary> | grep .abix.names   # 无输出
```

strip 后，`amc metadata --from-elf` 仍然工作，因为它读取的是 `.abix.metadata`，不是 `.abix.names`。

## 自动化测试

项目包含一个测试脚本，覆盖完整流程：

```bash
python3 tools/test_amc_elf-pe.py           # 运行并自动清理
python3 tools/test_amc_elf-pe.py --keep    # 保留临时文件供检查
```

脚本运行 9 个阶段：

1. `amc build` — 生成 `.abix` + `.abix.meta`
2. `amc generate` — 生成带 section 属性的 C++ 头文件
3. `clang++` — 编译为 ELF 二进制文件
4. `readelf -S` — 验证 `.abix.metadata` 和 `.abix.names` 存在
5. `objdump -s` — 转储节区内容，验证 ABIX magic
6. `amc metadata --from-elf` — 从 ELF 解析元数据
7. `amc metadata --verify` — 验证一致性
8. 导出 roundtrip — `.abixmeta` 导出 + 验证
9. 错误路径 — 非 ELF 拒绝、截断 ELF 拒绝

## 注意事项

- 仅支持 **ELF64 小端序**（x86_64 / aarch64 Linux）。
- PE/COFF 支持已规划但尚未实现。
- `.abix.names` 包含 `const char*` 指针；实际字符串在 `.rodata` 中。使用 `strings <binary> | grep -i amc` 查找。
