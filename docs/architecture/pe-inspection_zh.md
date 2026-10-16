# Windows PE 镜像的 ABIX 私有头设计

<p align="center">
  中文 · <a href="pe-inspection.md">English</a>
</p>

> **状态：已实现。** `amc generate -l cpp` 会为 `_WIN32` 发射 `.abix` / `.abix.n`
> 节区（GNU/clang 属性，以及 MSVC 的 `#pragma section` + `__declspec(allocate)`）；
> AMC 再经能力层（[binary-image_zh.md](binary-image_zh.md)，`ABIX/Binary/PE.h`）
> 读回 region。已用 clang++ 与 cl.exe 验证，覆盖 `amc metadata --from-elf` 与
> `amc publish`（CodeView GUID + Age）。ABIX 可用 MSYS2 UCRT64 工具链在 Windows 上
> 构建，PE 镜像已被端到端处理。

<details>

<summary>目录</summary>

- [为什么 PE 需要单独设计](#为什么-pe-需要单独设计)
- [ABIX 私有头](#abix-私有头)
- [节区布局与命名](#节区布局与命名)
- [为什么不使用 `.abix$m` / `.abix$d` 分片](#为什么不使用-abixm--abixd-分片)
- [如何把节区放进镜像](#如何把节区放进镜像)
- [读取 PE 镜像](#读取-pe-镜像)
- [用标准工具检查](#用标准工具检查)
- [Strip 操作](#strip-操作)
- [AMC CLI](#amc-cli)
- [限制](#限制)
- [测试](#测试)

</details>

## 为什么 PE 需要单独设计

AMC 通过自定义节区嵌入 Metadata Region：ELF 上是 `.abix.metadata`，Mach-O 上是
`__DATA` 段里的 `__abix_metadata`。两者之所以这么命名，是因为规范化的 ELF 拼写在
这两种容器里合法。Windows PE 有一个本质约束，迫使它单独设计：

- **PE 节名恰好是 8 字节。** `IMAGE_SECTION_HEADER.Name` 是 `BYTE Name[8]`，且当名字
  占满 8 字节时**不带 NUL 结尾**。规范名 `.abix.metadata`（14 字节）放不下。

  这不仅是"不好看"：截断是静默的，而且会**撞名**。在本工具链上编译探测显示链接器会
  截断规范名：

  ```text
  # clang++ -shared probe.cpp  （probe 使用 __attribute__((section(".abix.metadata"))))
  Name: .abix.me (2E 61 62 69 78 2E 6D 65)   # ".abix.me"
  Name: .abix.na (2E 61 62 69 78 2E 6E 61)   # ".abix.na"
  ```

  `.abix.metadata` 与 `.abix.meta` 都会截断成 `.abix.me`，被静默合并成同一个节区。

- **不同工具链行为不一致。** MSVC 与 MinGW/GNU-ld 并不以相同方式处理节名（见下文
  [为什么不使用 `.abix$m`](#为什么不使用-abixm--abixd-分片)）。

- **节名不是契约。** 节名是工具链可以截断、重排、合并、改写的字符串。

因此 ABIX **不靠节名识别载荷**：它写入一个自描述的**私有头**，读取器按**内容**
（`ABIX` magic）定位载荷，节名只作为快速路径。

## ABIX 私有头

私有头是一个小的、无指针、8 字节对齐的结构，位于 `.abix` 节区偏移 0 处，紧邻
Metadata Region 之前。它是 ABIX 自己的结构（不是 PE 结构），故称"私有"。所有字段
为小端。

```c
// 位于 `.abix` 节区偏移 0。无指针；可整块拷贝、mmap 或重定位。
struct AbixPeHeader {              // 24 字节
    uint32_t magic;                // 'A''B''I''X'  = 0x58494241
    uint16_t header_version;       // 1
    uint16_t header_size;          // 24（8 字节对齐）
    uint32_t flags;                // bit0 = NAMES_SECTION_PRESENT
    uint32_t region_offset;        // 相对节区起点；24
    uint32_t region_size;          // Metadata Region 字节长度
    uint32_t reserved;             // 0（保持 region_offset 8 字节对齐）
};
```

于是 `.abix` 节的载荷为：

```text
.abix 节区
├── AbixPeHeader        (24 B，私有头 —— 本文)
├── Metadata Region     (region_size B：manifest + desc + hash，…)
└── padding             (对齐到 8 B)
```

读取器校验 `magic`、`header_version`、`header_size` 后，把
`region_offset … region_offset + region_size` 当作一个普通的 **Metadata Region**，
交给既有、与容器无关的路径（`read_metadata_header` / `verify_metadata_region`）。
**不引入新的 Region 格式**；私有头只负责*定位* Region，并让它在节名被工具链做任何
处理之后依然可被找到。

`AbixPeHeader` 刻意不携带指针或绝对地址：Metadata Region 本身已经是自包含、无指针
的镜像，保持头部无指针意味着"私有头 + Region"这一对可以被整块 dump、哈希、逐字节
比对。

## 节区布局与命名

| 规范名（ELF） | PE 节区 | 内容 | 可 strip？ |
|-----------------|------------|---------|-------------|
| `.abix.metadata` | `.abix` | `AbixPeHeader` + Metadata Region（无指针） | 否 —— 必需 |
| `.abix.names` | `.abix.n` | `const char *` 指针数组，指向名字字符串 | 是 —— 运行时不使用 |

两个名字都 ≤ 8 字节，且关键的是**不共享 `$` 分组约定**（见下一节）。
`native_section_name(BinaryFormat::pe, …)` 负责规范名 → PE 的翻译；未知名字原样
返回，与 ELF/Mach-O 一致。

名字使用显式的点号（`.abix.n`），而不是依赖 `.abix.names` 的截断（那会静默变成
`.abix.na`）。显式的短名字让映射稳定且可读：

```cpp
// AMC C++ 后端，Windows PE 目标
__attribute__((used, section(".abix"), aligned(8)))
inline constexpr unsigned char amc_abix_region[] = { /* AbixPeHeader + Region */ };

__attribute__((used, section(".abix.n")))          // 仅 Debug
inline constexpr const char *amc_type_names[] = { "MyClass", /* ... */ };
```

## 为什么不使用 `.abix$m` / `.abix$d` 分片

COFF/PE 给 `name$suffix` 赋予了特殊语义：链接器会把各分片**合并成单个 `name` 节区，
并按 `suffix` 排序**（即 `.CRT$XCU` 惯用法）。因此在
[`metadata_modes_zh.md`](../abix/metadata_modes_zh.md) 里曾有一版
`.abix$m` / `.abix$d` / `.abix$h` / `.abix$n` 的设想。该方案经不起链接器的考验：

1. **分片偏移是链接期的，不是编译期的。** 自描述 Region 用"相对 Region 起点的字节
   偏移"引用各部件。一旦拆成独立的 `$` 分片，它们在 `.abix` 里的最终位置由链接器
   决定，编译器就无法给出正确的 `AbixPeHeader.region_offset`。
2. **合并顺序由后缀决定，不由我们决定。** `.abix$d` 排在 `.abix$m` 之前，合并后
   manifest 不会落在最前面。
3. **行为随工具链而异。** MSVC 会合并 `$` 分片；MinGW GNU-ld 默认不合并。同一份源码
   在 MSVC 下产出合并后的 `.abix`，在 MinGW 下却是一堆散落的 `.abix$*` 节区。

对名字还有第四个问题：`.abix$n` 与 `.abix` 共享基名，MSVC 会把它**合并进**`.abix`，
名字便再也无法独立 strip —— 而 Debug/Release 模型正依赖这一点。

因此采用：**单个自包含的 `.abix` 节区**（私有头 + Region），外加一个独立的、非 `$`
的 `.abix.n` 承载可剥离的名字。`$` 分片仅作为**已否决的备选**记录在此。

## 如何把节区放进镜像

**MinGW / Clang**（`SKL_ABIX_BUILD_DLL_VARIANTS` 跨工具链路径使用 clang）：用节属性，
名字不超过 8 字节，并保证即使未被引用也保留：

```cpp
__attribute__((used, section(".abix"), aligned(8)))
inline constexpr unsigned char amc_abix_region[] = { /* ... */ };
```

**MSVC**：节区必须先用 `#pragma section` 声明，用 `__declspec(allocate(...))` 放置，
并用链接器指令赋予属性：

```cpp
#pragma section(".abix", read)
#pragma section(".abix.n", read)                    // 仅 Debug
__declspec(allocate(".abix")) alignas(8)
extern const unsigned char amc_abix_region[];
#pragma comment(linker, "/SECTION:.abix,R")
```

Region 必须放进只读节区，且不能参与 `--gc-sections` 之类的裁剪
（`__attribute__((used))`，或在链接脚本里显式 `KEEP`，或 MSVC 的 `/INCLUDE:`）。

## 读取 PE 镜像

`find_binary_section` 增加 PE 分支。单个镜像的算法如下：

1. 读 DOS 头；要求 `e_magic == 'MZ'`；取 `e_lfanew`（偏移 `0x3C`）为 NT 头偏移。
2. 要求 `Signature == 'PE\0\0'`。
3. 读 COFF 头（20 字节）：`NumberOfSections`（偏移 +2）与 `SizeOfOptionalHeader`
   （偏移 +16）。
4. 读可选头 magic（`0x10B` = PE32，`0x20B` = PE32+）与 `ImageBase`（后续解析名字需要）。
5. 遍历节表：位置 `nt + 24 + SizeOfOptionalHeader`，共
   `NumberOfSections` × 40 字节条目，每条含 `Name[8]`、`VirtualSize`（+8）、
   `VirtualAddress`（+12）、`SizeOfRawData`（+16）、`PointerToRawData`（+20）。
6. 定位载荷：
   - 快速路径：`Name` 为 `.abix` 的节区；否则
   - 回退：扫描每个节区的原始字节，在节区起点寻找 `AbixPeHeader` 的 magic 并校验
     `header_version` / `header_size`。
   回退正是私有头存在的意义：让读取器**不依赖节名**。
7. 拷贝 `[PointerToRawData, +SizeOfRawData)`（按文件长度截断）并校验 `AbixPeHeader`；
   带边界检查地取出 `region_offset` / `region_size`，把 Region 交给共享的元数据路径。

**名字。** `.abix.n` 存的是 `const char *` 指针而非字符串本身（字符串在 `.rdata`）。
与 ELF 节区不同，这些指针是**绝对虚拟地址**，离线读取器必须映射回去：

```text
指针 (VA) → RVA = VA − ImageBase → 经节表得到文件偏移
```

这一步 VA → RVA → 文件偏移 是 PE 特有的，也是 PE 读取器唯一比 ELF/Mach-O 读取器
多做的工作。它只对可选的名字节区必需；`amc metadata`（只读 Region）不需要。

## 用标准工具检查

```bash
# 节区头（binutils / LLVM / MSVC）
objdump -h app.exe | grep -i abix
llvm-readobj --sections app.exe | grep -A2 -i abix
dumpbin /HEADERS app.exe | findstr /i abix

# 映像节区偏移 0 处带 ABIX magic
objcopy --dump-section .abix=region.bin app.exe
llvm-objcopy --dump-section .abix=region.bin app.exe
# region.bin 现在以 41 42 49 58（"ABIX"）开头
```

合并之后，`dumpbin /HEADERS app.exe` 会列出映像里的 `.abix` 节区及其
`PointerToRawData` / `SizeOfRawData`，这正是第 7 步所读取的内容。

## Strip 操作

PE 没有按节区的 `strip`；用 `objcopy` 移除名字节区：

```bash
objcopy --remove-section=.abix.n app.exe
llvm-objcopy --remove-section=.abix.n app.exe app.stripped.exe

# Region 节区不受影响：
objcopy --dump-section .abix=region.bin app.stripped.exe
```

移除 `.abix.n` 不得改变程序行为 —— 运行时不引用名字（与 ELF 的 `.abix.names` 同一
不变量）。

## AMC CLI

既有 CLI 与容器无关，无需新参数：

```bash
amc metadata --from-elf app.exe --format json   # 自动识别 PE
amc metadata --verify app.exe --format json
amc metadata app.abix -o region.abixmeta
```

> `--from-elf` 是历史名称；容器（ELF、Mach-O、PE）由文件的 magic 字节识别，因此在
> 读取器落地后 PE 镜像即可被接受。

## 限制

- **PE 已实现。** `amc generate -l cpp` 会发射 `.abix` / `.abix.n`，
  `detect_binary_format` / `find_binary_section` / `amc metadata` / `amc publish`
  可读回（见 [BinaryImage 层](binary-image_zh.md)）。
- **先做 PE32+。** 64 位 PE（`0x20B`）是主目标；PE32（`0x10B`）在结构上被支持，因为
  Region 无指针且与字长无关。
- **仅小端**，与 ELF/Mach-O 一致。
- **名字需要 RVA 映射。** 离线读取 `.abix.n` 需要 `VA → RVA → 文件偏移`；
  `amc metadata`（只读 Region）不需要。
- **刻意不使用 `$` 分片**（节区合并与链接期偏移）；Region 是单个 `.abix` 块。
- **已签名镜像。** Authenticode 签名会哈希节表与节区字节；签名后再加 `.abix` 会使签名
  失效。应在**签名之前**嵌入 ABIX（与添加任何自定义节区同样的规则）。

## 测试

既有测试脚本 `tools/test_amc_elf-pe.py` 的名字里本就包含 PE。实现时应为它增加
Windows 分支：

1. `amc build` → `.abix` + `.abix.meta`
2. `amc generate -l cpp` → 带 PE 节区属性的头文件
3. `clang++`/`cl.exe` → 含 `.abix`（与 `.abix.n`）节区的 PE 镜像
4. `llvm-readobj --sections` / `dumpbin /HEADERS` —— 确认节区存在
5. `llvm-objcopy --dump-section` / `objcopy` —— 确认 `ABIX` magic
6. `amc metadata --from-elf` / `--verify` —— 解析并校验 Region
7. `llvm-objcopy --remove-section=.abix.n` —— strip 往返
8. 错误用例 —— 非 PE 拒绝、截断 PE 拒绝
