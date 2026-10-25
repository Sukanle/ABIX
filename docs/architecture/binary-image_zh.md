# ABIX BinaryImage 能力层

<p align="center">
  中文 · <a href="binary-image.md">English</a>
</p>

> **状态：已实现（第一版）。** 该层位于 `SKL/ABIX/Binary/`，已接入 AMC；下述能力已支持 ELF、Mach-O 与 PE。

<details>

<summary>目录</summary>

- [原则：统一能力，而非格式](#原则统一能力而非格式)
- [分层](#分层)
- [格式、操作系统与架构是彼此独立的轴](#格式操作系统与架构是彼此独立的轴)
- [第一版接口](#第一版接口)
- [各格式 adapter](#各格式-adapter)
- [原生逃生舱](#原生逃生舱)
- [第一版范围](#第一版范围)
- [消费方](#消费方)

</details>

## 原则：统一能力，而非格式

ABIX **不**试图把 `Elf64_Ehdr`、`mach_header_64` 与 `IMAGE_NT_HEADERS` 塞进同一个 "二进制头"结构。这些容器描述的东西各不相同——ELF 的程序头/节头、Mach-O 的段与加载命令模型、PE 的 COFF 头、节表与数据目录——强行合并只会变成一堆 `#ifdef`。

ABIX 统一的是**"向一个二进制模块提问"的能力**：这是什么格式、它描述什么 target、某个命名 region 在哪里、它导出哪些符号。每个 adapter 把自身的原生结构翻译成这套共享模型；`SKL/ABIX/Binary/` 之上再也看不到任何容器头。

> **格式是实现，能力是接口。**

## 分层

```text
                    BinaryImage (SKL/ABIX/Binary/BinaryImage.h)
                              │
              ┌───────────────┼───────────────┐
             ELF.h          MachO.h          PE.h          （adapter）
              │               │               │
           Elf64_*        Mach-O LC        IMAGE_NT_HEADERS / 节表

  这条线之上，消费方只看到：Target · Section · Export · Region
```

核心 runtime（`SKL/ABIX/Runtime`、`SKL/ABIX/Interface`、`SKL/ABIX/Metadata`）永远不包含 `<elf.h>`、`<mach-o/...>` 或 `<windows.h>`；各 adapter 无依赖，按文档化字节偏移读取。

## 格式、操作系统与架构是彼此独立的轴

PE ≠ Windows，ELF ≠ Linux，Mach-O ≠ macOS。同一容器可以承载多个平台（`ELF + FreeBSD`、`ELF + Android`、`PE + Windows arm64`），因此模型用三个独立的枚举：

```cpp
enum class Format : uint8_t { unknown, elf, macho, pe };
enum class OperatingSystem : uint8_t { unknown, linux, windows, macos, freebsd, android };
enum class Architecture : uint16_t { unknown, x86, x86_64, arm32, arm64, riscv32, riscv64 };
```

## 第一版接口

刻意做小——足以支撑 `.abix` 自动发现、接口注册与原生绑定，仅此而已：

```cpp
class BinaryImage {
public:
    Format format() const noexcept;
    OperatingSystem os() const noexcept;
    Architecture architecture() const noexcept;
    Endianness endian() const noexcept;
    uint8_t pointer_size() const noexcept;
    uint64_t image_base() const noexcept;
    const Target &target() const noexcept;

    const std::vector<Section> &sections() const noexcept;
    const std::vector<Export> &exports() const noexcept;

    const Section *find_section(std::string_view name) const noexcept;
    const Export  *find_export(std::string_view name) const noexcept;

    const Section *find_region(std::string_view canonical) const noexcept;
    bool read_region(std::string_view canonical, std::vector<uint8_t> &out, std::string &error) const;
};

bool parse_binary_image(const uint8_t *data, size_t size, BinaryImage &image, std::string &error);
```

- **`Section`** —— 规范化的链接器节区：`name`、`segment`、`file_offset`、`file_size`、`address`、`size`、`alignment`、`type`、`flags`。
- **`Export`** —— `name`、`address`、`ordinal`。
- **`Target`** —— `format` / `os` / `architecture` / `endian` / `pointer_size` / `image_base` 的组合。
- **`find_region` / `read_region`** —— ABIX region 访问器：规范名（`.abix.metadata`、`.abix.names`）经 `native_section_name` 翻译成容器的原生拼写，`read_region` 返回 Metadata Region（对 PE 还会剥掉私有头，见 [pe-inspection_zh.md](pe-inspection_zh.md)）。

`BinaryImage` 不拥有字节：调用方在镜像使用期间保持缓冲区存活（与旧的 `find_binary_section` 同一契约）。

## 各格式 adapter

| Adapter | 读取 | 导出符号来源 |
|---------|-------|--------------|
| `SKL/ABIX/Binary/ELF.h` | ELF64 小端 | `.dynsym`（GLOBAL/WEAK 且已定义） |
| `SKL/ABIX/Binary/MachO.h` | 32/64 位小端 Mach-O | `LC_SYMTAB`（外部、`N_SECT`） |
| `SKL/ABIX/Binary/PE.h` | PE32 / PE32+ | `IMAGE_EXPORT_DIRECTORY` |

每个 adapter 暴露一个廉价的 `is_*` magic 检查和一个填充 `ParseResult` 的 `detail::parse_*`。PE 另需把 RVA 解析为文件偏移（用于导出名/函数数组），并计算 `address = image_base + RVA`。

## 原生逃生舱

ABIX 层调用方使用规范名，绝不触碰容器头。当调用方确实需要原生结构时，直接从镜像字节读取——`native()` 刻意**不**进入通用 API。若第二个消费方需要某个原生字段，就把该字段提升进 `Section` / `Export` / `Target`，而不是暴露容器头。

## 第一版范围

包含：`format`、`target`、`sections()`、`exports()`、`find_region()`。

刻意**不**包含（避免变成又一个 `llvm::ObjectFile`）：

- **Segment** —— 加载器/内存视图，与链接器节区语义不同。
- **Import** —— PE 导入表、ELF `.rela.plt` 目标、Mach-O bind 信息。
- **Relocation** —— 完整的重定位模型。
- **通用符号解析器** —— `exports()` 是扁平列表，不是解析器。
- **bitcode / fat / universal 镜像** —— 仅 thin 镜像，与此前一致。

Segment、import、relocation 只在出现具体 ABIX 工作流需要时才加入。

## 消费方

- **AMC** —— `amc metadata --from-elf`、`amc inspect`、`amc verify`、`amc publish` 以及 LLDB 插件都经由一层薄兼容垫片（`AMC/include/AMC/Core/ELF.h`），该垫片现在也支持 PE。
- **接口自动发现（规划中）** —— 扫描 `exports()` 找 `abix_module_query_v1`，与内嵌 region 配对，注册接口；runtime 全程不知道容器：

  ```text
  libfoo.so / foo.dll / libfoo.dylib
        │
        ▼
  BinaryImage::parse ── exports()  ── abix_module_query_v1
        │              └ find_region(".abix.metadata") ── ModuleManifest
        ▼
  InterfaceRegistry → InterfaceId → 原生绑定（解析一次）
  ```
