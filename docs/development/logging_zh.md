# 日志与诊断

<p align="center">
  <a href="logging.md">English</a> · 中文
</p>

ABIX 的日志是轻量的诊断副作用，而不是日志框架。ABIX 负责事件出口和回调契约；AMC 或宿主负责采集、格式化、存储、轮转、异步和国际化。

## 职责边界

| 层 | 职责 | 是否必须 |
| --- | --- | --- |
| `Error` / `CallError` | 程序可判断的失败原因；调用者据此分支 | 是 |
| 结构化上下文 | 模块、路径、阶段、错误细节；可伴随错误 | 可选 |
| 格式化日志 | 人类可读的诊断事件 | 可选 |

日志不能替代错误返回值。即使所有 sink 关闭，模块加载失败仍然必须返回 `CallError::load_failed`。不要在每一层都无条件记录同一个 Error：底层产生准确的错误码，应用边界决定是否记录最终 Error。

## 分层

```text
ABIX Core
  错误码 + ABIX_LOG_* 诊断调用点
        |
诊断接口（SKL/ABIX/Util/Log.h）
  固定回调 + 级别 + 消息生命周期
        |
宿主 / AMC
  fmtlog、Quill、自定义后端、文件、异步队列、I18N
```

ABIX 不链接 fmt、fmtlog 或 Quill。sink 只观察诊断事件，不能改变操作结果。

## API 契约

`Log.h` 提供：

- `LogLevel` —— `Debug`、`Info`、`Warning`、`Error`、`Off`。
- `log_sink_t` —— 兼容的 `(LogLevel, const char *)` 回调。
- `LogRecord` / `log_record_sink_t` —— 带 `message`、`length`、`truncated` 的长度感知回调。
- `LogState` —— 显式的 sink、record sink 和级别所有者。
- `default_log_state()` —— 单个镜像内的便利状态。
- `set_log_sink`、`set_log_record_sink`、`set_log_level` 及读取函数。
- `log` 以及 `ABIX_LOG_*`、`ABIX_LOG_*_TO` 宏。

默认 sink 为 `nullptr`，宿主安装前 ABIX 不产生输出。默认运行时阈值为 `Warning`；宿主可以降到 `Debug`、升到 `Error`，或用 `Off` 完全关闭。

## 线程与回调规则

- 默认 `ABIX_LOG_THREAD_SAFE=1` 时，sink、record sink 和阈值使用 ABIX 原子 load/store。其他线程记录日志时设置这些状态是支持的。
- `ABIX_LOG_THREAD_SAFE=0` 时字段为普通存储；宿主必须在工作线程启动前完成固定 sink 配置，或自行提供同步。
- 回调在记录日志的线程同步执行；ABIX 调用回调时不持有日志锁。
- `message` 指向栈缓冲区，只在回调执行期间有效。异步后端必须在返回前复制。
- 回调不得抛出异常。`log()` 是 `noexcept`；异常逸出会终止进程。
- 回调可以重入日志 API，但不得形成无界递归。
- 兼容回调无法获知截断信息；需要 `length` / `truncated` 时使用 `log_record_sink_t`。
- 缓冲区为 1 KiB。`length` 不包含 NUL；格式化结果放不下时 `truncated` 为 true。
- 运行时过滤在 `vsnprintf` 之前执行；编译期关闭级别会完全移除调用。

## 配置与分派策略

ABIX 将职责分为三层：

| 层 | 所有者 | 职责 |
| --- | --- | --- |
| 配置存储 | ABIX | sink、record sink、阈值的存储和读写策略 |
| 诊断分发 | ABIX | 过滤、格式化并同步调用回调 |
| 后端并发 | 宿主 / 外部日志库 | 队列、锁、异步线程、文件输出 |

`ABIX_LOG_THREAD_SAFE` 在编译期选择配置策略：

- `1`（默认）—— `ThreadSafeLogState`：对普通函数指针/`uint8_t` 存储使用 ABIX 原子 load/store。支持并发读取和配置变更。
- `0` —— `SingleThreadedLogState`：普通存储，日志路径不执行原子操作。宿主必须保证 sink/级别变更与日志调用不并发。这适合固定 sink 模式：工作线程启动前安装 sink，之后不再修改。

`LogState` 是当前策略的别名；两个具体类型分别为 `ThreadSafeLogState` 和 `SingleThreadedLogState`。这是编译期开关，不是每次调用的分支。由于 ABIX 是 header-only，共享同一个 `LogState` 的所有模块必须使用相同取值；混合 DLL 应由宿主统一持有一个配置，或使用显式 runtime 状态。

原子路径使用 `SKL/ABIX/Util/Atomic.h` 对调用方持有的 plain storage 进行操作，不在 `LogState` 中存放 `std::atomic` 对象，从而避免标准库对象布局在 DLL/CRT 边界上的差异。原子 sink 配置不代表整个日志操作无锁：格式化和外部 sink 仍可能阻塞。Quill/fmtlog 等外部日志库继续负责自己的队列和背压。

## 跨 DLL 所有权

ABIX 是 header-only，便利的 `default_log_state()` 是 inline 的，每个 DSO/镜像一份，**不是进程级单例**。宿主设置 sink 不会自动配置插件中编译进去的那一份 ABIX。

可选择：

1. **逐模块配置** —— 每个包含 ABIX 的模块在自己的初始化路径中安装 sink。
2. **显式状态** —— 宿主/运行时持有 `LogState`，其控制的代码使用显式重载或 `ABIX_LOG_*_TO`。
3. **唯一 Runtime** —— 如果未来构建引入单一 ABIX Runtime，由该 Runtime 持有 `LogState`，所有模块使用它。

不要把 inline 静态变量误认为跨 DLL 共享。回调 ABI 本身保持普通函数指针和 `LogRecord`，因此替换宿主日志库不需要改动 ABIX 核心接口。显式 `LogState` 是 plain-storage 配置对象：只应在使用兼容 C++ ABI 和 ABIX 头的模块之间共享，或保留在宿主 runtime 内、只暴露回调边界。

## 明确不做

ABIX 不提供：

- 异步日志线程或队列；
- 文件轮转、控制台彩色输出、时间戳；
- 线程 ID、源文件/行号格式化；
- 日志 I18N 翻译；
- sink 链或动态过滤配置。

这些属于 AMC 或宿主日志库。若未来需要结构化诊断，应增加带版本号的事件结构，包含事件 ID、模块标识和错误码，而不是持续扩展 `printf` 风格回调。
