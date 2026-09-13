# 运行时概览

ABIX 运行时消费 ABI metadata，并保持执行路径为原生调用，不会解释兼容调用。

## 职责

```text
discover → verify → identify → bind → adapt → native call
```

运行时可以参与以上全部环节，但绑定完成后，兼容调用直接走原生 ABI，不再有逐调用的
ABI 机制。

## 注册表

`RuntimeRegistry` 持有所有已加载模块的 ABI 事实，并在加载期校验：

* canonical `TypeDesc` / `TypeLayout` 是唯一 ABI 事实；
* 共享 TypeID 必须携带相同 `LayoutHash`（兼容重复去重，冲突拒绝）；
* 查找基于 `TypeID`（`find_by_id`、`type_of<T>()`）；名字查找仅为诊断用途，
  紧凑构建下返回 `nullptr`。

## Metadata 三种模式

```text
.abix（完整 artifact，含名字）
   │ 投影
   ▼
Metadata Region（内嵌、pointer-free、可 mmap）
   │ materialize
   ▼
Runtime Descriptor（pointer-rich，热路径）
```

* 工具链把 Region 写入生成头文件的 `.abix.metadata` 段；离线工具无需加载程序即可扫描。
* `MaterializedModule` 可把 Region 重新投影为 pointer-rich 的 `ModuleDescriptor`，
  使注册表完全由 metadata image 驱动，无需编译期 descriptor 数组。

设计细节见 [`metadata_modes_zh.md`](metadata_modes_zh.md)。

## DLL 函数表

稳定的公开调用 ABI 是 DLL 导出表：一张扁平、带版本的函数 entry 表。metadata 是其上
额外的一层显式描述。

## 快速开始

### DLL 端（提供方）

```cpp
#include "abix/abix.hpp"

extern "C" int add(int a, int b) { return a + b; }
extern "C" double multiply(double a, double b) { return a * b; }

SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("add", add),
    SKL_ABIX_ENTRY("multiply", multiply),
)
```

### 宿主端（使用方）

```cpp
#include "abix/abix.hpp"

using namespace skl::abix;

dll_object lib;
lib.load("math_dll.dll");

auto add = dll_func<int(int, int)>(lib, "add");
if (add.valid()) {
    int result = add(2, 3);  // 5
}

auto mul = dll_func<double(double, double)>(lib, "multiply");
if (mul.valid()) {
    double result = mul(1.5, 4.0);  // 6.0
}
```

运行时 API 见 [`api_zh.md`](api_zh.md)。

## API 参考

完整运行时 API（`entry`、`table`、`dll_object`、`call_error`、DLL 资源智能指针、
日志、RCU 超时策略、类型化函数句柄与签名 hash）见 [`api_zh.md`](api_zh.md)。

## 性能

绑定与 metadata 投影发生在初始化阶段；边界开销与查找基准见
[`benchmark_ZH.md`](benchmark_ZH.md)。
