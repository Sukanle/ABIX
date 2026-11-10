# 跨语言 Lease C ABI（v0.3.x 契约）

<p align="center">
  中文 · <a href="lease.md">English</a>
</p>

> **状态：ABIX v0.3.x 契约冻结；ABIX 头文件与 mock Host 测试已实现；AMC 参考 Host 与 C++/Rust/Zig interop 已在 Linux、macOS、Windows 验证（测试矩阵 1–10）。** 本文定义 ABIX 侧的跨语言模块 lease C ABI 契约。ABIX 在 `include/SKL/ABIX/Interface/LeaseABI.h` 提供 C 类型和状态映射，在 `test/lease_abi_test.cpp` 提供 mock Host 契约测试。Host（例如 AMC）提供 function table 实现和 registry。ABIX 不导出全局 lease 符号，也不依赖 AMC。

## 目的

Rust 或 Zig 消费者可能持有 ABIX 模块创建的对象。对象的 `destroy` 入口位于该模块中。如果消费者仍持有对象时模块被卸载，调用 `destroy` 就会变成 use-after-unload。

Lease C ABI 为每个跨语言 owned object 提供模块生命周期凭证。它不是第二套所有权系统，而是现有 `ModuleControl` / `ModuleLease` 生命周期的 C ABI 入口。

## 不变量

- Registry 由 Host 持有，不属于 ABIX，也不属于插件。
- ABIX 定义 C 类型和契约；Host 提供 function table。
- Lease 是 registry 作用域内的不透明 `uint64_t` token；`0` 为 invalid。
- Token 不是指针，绝不能按指针解释。
- Lease 复用现有 `ModuleControl` lease 计数；ABIX 不新增第二套引用计数。
- 模块的 `destroy` 代码必须在 lease release 前保持映射有效。
- 生命周期顺序是 `acquire -> create -> destroy -> release`；禁止先 release 再 destroy。
- 复制 token 不创建新 lease。Rust wrapper 不得实现 `Clone`；Zig wrapper 使用带 `deinit()` 的结构体。
- ABIX Runtime 不向 Rust/Zig 暴露 `ModuleControl` 或 `ModuleLease` 内存布局。
- 所有回调使用 C ABI 且不得 unwind：C++ 异常或 Rust panic 不得穿越边界。

## 类型

```c
typedef struct abix_lease_registry abix_lease_registry;
typedef uint64_t abix_module_instance;
typedef uint64_t abix_lease;

#define ABIX_LEASE_INVALID UINT64_C(0)

typedef enum abix_status {
    ABIX_SUCCESS = 0,
    ABIX_ERROR_INVALID = 1,
    ABIX_ERROR_NOT_FOUND = 2,
    ABIX_ERROR_STALE_HANDLE = 3,
    ABIX_ERROR_BUSY = 4,
    ABIX_ERROR_UNLOADING = 5
} abix_status;
```

数值属于 C ABI，不得重编号。

## Host function table

```c
typedef struct abix_lease_api {
    uint32_t abi_version;   // currently 1
    uint32_t struct_size;   // sizeof(abix_lease_api)

    void *host_context;

    abix_status (*registry_create)(
        void *host_context,
        abix_lease_registry **out_registry);

    abix_status (*registry_destroy)(
        void *host_context,
        abix_lease_registry *registry);

    abix_status (*acquire)(
        void *host_context,
        abix_lease_registry *registry,
        abix_module_instance module,
        abix_lease *out_lease);

    void (*release)(
        void *host_context,
        abix_lease_registry *registry,
        abix_lease lease);
} abix_lease_api;
```

规则：

- ABIX 头文件定义该结构；Host 填充并拥有实现。
- Rust/Zig 从 Host 接收 `abix_lease_api *` 和 `abix_lease_registry *`；它们不创建或销毁 Host 的 registry。
- 必须检查 `abi_version`；不支持的版本必须拒绝。
- `struct_size` 允许后续 ABI 版本增量增加字段；更小的表必须拒绝，不能部分读取。
- 有效表中的每个函数指针都必须非空。
- Host API 对象的生命周期必须长于所有 registry 和所有通过它创建的 owner。
- `registry_create` / `registry_destroy` 是 Host 操作；Rust/Zig 只使用 `acquire` / `release`。

## 状态映射

`abix_status` 是 C ABI 结果，显式映射到现有 ABIX 错误身份。它不是新的错误域。

| C 状态                    | ABIX 映射                                         |
| ------------------------- | ------------------------------------------------- |
| `ABIX_SUCCESS`            | 规范 `success()`                                  |
| `ABIX_ERROR_INVALID`      | `CallError::invalid`（`call_domain`）             |
| `ABIX_ERROR_NOT_FOUND`    | `ModuleStatus::not_registered`（`module_domain`） |
| `ABIX_ERROR_STALE_HANDLE` | `CallError::stale_handle`（`call_domain`）        |
| `ABIX_ERROR_BUSY`         | `ModuleStatus::busy`（`module_domain`）           |
| `ABIX_ERROR_UNLOADING`    | `CallError::unloading`（`call_domain`）           |

未知 C 状态值一律视为失败。映射辅助函数必须保留数值并带有效失败 domain，不得把未知状态折叠成成功。

C ABI 状态值稳定。新增状态需要新的 `abi_version`，或记录为旧 reader 必须按失败处理的增量值。

安装包还通过 `ABIX_LEASE_API_JSON`（`share/ABIX/lease_api.json`）暴露 v1 function table 布局。它记录 `abi_version`、`struct_size`、字段 offset 和稳定状态值，使 AMC 等消费者可以生成或校验自己的 lease 声明，而不必重复维护布局。

## Registry 生命周期

```text
ACTIVE
  |
  | destroy
  v
CLOSING
  +-- outstanding leases > 0  -> ABIX_ERROR_BUSY；registry 保持 CLOSING
  +-- outstanding leases == 0 -> DESTROYED
```

规则：

- `registry_create` 返回不透明 registry handle；`0`/null 为 invalid。
- `acquire` 和 `release` 线程安全。
- `CLOSING` 拒绝新的 `acquire`，返回 `ABIX_ERROR_UNLOADING` 或 `ABIX_ERROR_BUSY`；已有 lease 仍可 release。
- 仍有 lease 时 `registry_destroy` 返回 `ABIX_ERROR_BUSY`；此时不得释放 registry。
- destroy 成功后 registry handle 失效，不得再使用。
- 不同 registry 中可以有相同数值的 token；registry 是 lease 身份的一部分。

## Acquire 与 release

`acquire`：

1. 校验 registry 和 module instance。
2. 确认模块仍允许新 lease。
3. 通过现有 lease 获取路径增加 `ModuleControl` lease 计数。
4. 为该 registry 分配新 token。
5. 保存 `token -> lease record`。
6. 把 token 写入 `out_lease`，返回 `ABIX_SUCCESS`。

失败时 `out_lease` 为 `ABIX_LEASE_INVALID`，不留下部分 lease。

`release`：

- `ABIX_LEASE_INVALID`、未知 token 和已释放 token 都是 no-op。
- 同一 token 并发 release 最多减计数一次。
- registry 生命周期内 token 不复用；token 溢出时拒绝新 acquire，不回绕。
- 来自其他 registry 的 token 是 no-op。
- registry 必须有效；在 `registry_destroy` 之后调用 `release` 是契约违规。

## 模块生命周期与 unregister

保留现有状态机：

```text
REGISTERED
  |
  | unregister_module()
  v
DRAINING
  +-- new lease: rejected
  +-- existing lease: valid
  +-- existing owner: destroy allowed
  |
  | outstanding leases > 0
  v
BUSY（模块保持 DRAINING）
  |
  | destroy object, release lease
  v
DRAINING
  |
  | unregister_module() retry
  v
UNLOADING -> UNLOADED
```

规则：

- `unregister_module` 在返回 `busy` 前进入 draining；这是 ABIX 现有行为，刻意保留。
- `busy` 不是无副作用的：它关闭新 lease 入口，并让已有 lease 排空。
- `unregister_module` 失败后，必须在最后一个 lease 释放后重试。
- acquire 与 unregister 必须使用同一同步协议。模块状态检查与 lease 计数增加必须是一次原子决定；成功的 acquire 之后绝不能发生卸载。
- DRAINING 不阻止已有 owner 的 `destroy`；析构属于 draining 过程，不是重新获取 lease 的新调用。

## Owner 生命周期

```text
Acquire Lease
  -> Create Object
  -> Rust / Zig owns object
  -> Destroy Object
  -> Release Lease
```

- acquire 必须早于对象交付；不能出现 create/acquire 窗口。
- 创建失败时立即释放 lease。
- `owner::handle` 仍是模块/所有权身份，用于诊断和关联；其规范 C 拼写是 `abix_owner_handle`。
- `owner::ref`（C++）内部持有同语义 lease。
- Rust/Zig wrapper 持有 `object + destroy + lease`，在 `Drop` / `deinit()` 中先 destroy 后 release。
- `destroy` 必须有 no-unwind 契约。

## 责任边界

| 层               | 责任                                                            |
| ---------------- | --------------------------------------------------------------- |
| ABIX             | C ABI 类型、状态映射、C++ owner 集成、契约测试骨架              |
| Host（AMC）      | `abix_lease_api` 实现、registry、module table、Rust/Zig fixture |
| Plugin           | 提供 `destroy`；绝不实现 lease release                          |
| Rust/Zig wrapper | 持有 object 和 lease；destroy 后 release；lease 不实现 `Clone`  |

ABIX 不得依赖 AMC 实现细节。AMC 不得向 Rust/Zig 暴露 `ModuleControl` 或 `ModuleLease` 布局。

## 测试矩阵

1. Rust 创建对象，Host `unregister_module` 返回 `busy`。
2. draining 后新的 `acquire` 失败。
3. Rust `Drop` 在 draining 下仍可 `destroy`。
4. `Drop` 释放 lease 后，`unregister_module` 成功。
5. Zig 创建的对象可由 Rust 接管，destroy/release 顺序正确。
6. 两个 DSO 通过同一 registry acquire/release。
7. 两个 registry 出现相同 token 数值时互不影响。
8. 同一 token 并发重复 release 最多减计数一次。
9. registry 有存活 lease 时 `registry_destroy` 返回 `busy`。
10. acquire/unregister 压力测试中，成功的 acquire 之后绝不出现已卸载模块。

## 实现状态

| 层                             | 状态                                                                             |
| ------------------------------ | -------------------------------------------------------------------------------- |
| ABIX C ABI 头文件              | `include/SKL/ABIX/Interface/LeaseABI.h`                                          |
| ABIX 状态映射                  | `abix::to_error_code(abix_status)`                                               |
| ABIX 契约测试                  | `test/lease_abi_test.cpp`，使用 mock Host registry                               |
| Host registry / function table | AMC 参考实现位于 `tests/interop/lease/`；interop 已在 Linux、macOS、Windows 验证 |
| Rust/Zig owned-object wrapper  | 已实现；interop 已在 Linux、macOS、Windows 验证                                  |
| Plugin `destroy` 集成          | 参考 fixture 已实现；沿用现有 owner ABI                                          |
| 跨平台验证                     | 已在 Linux、macOS、Windows 验证                                                  |
| Zig→Rust 接管 fixture          | 已实现；interop 已在 Linux、macOS、Windows 验证                                  |

ABIX 测试使用 mock Host 验证契约，不会成为生产 registry，也不导出全局符号。

## 版本

- 该契约在 v0.3.x 中是 additive，不改变现有 STD 布局。
- 初始 function table 的 `abi_version = 1`；`struct_size` 保护增量字段。
- 未知状态值失败关闭。
- ABIX v0.4.0 的 STD 布局变更与本契约无关。

## 非目标

- ABIX 自有的进程级全局 lease registry。
- 向其他语言暴露 `ModuleControl` / `ModuleLease` 布局。
- 第二套引用计数系统。
- 与旧 STD 类型的跨布局兼容。
- 在即将卸载的插件中实现 lease release。
