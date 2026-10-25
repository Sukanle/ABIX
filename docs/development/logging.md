# Logging and diagnostics

<p align="center">
  English · <a href="logging_zh.md">中文</a>
</p>

ABIX logging is a lightweight diagnostic side channel. It is deliberately not a
logging framework: ABIX owns event emission and the callback contract; AMC or the
host owns collection, formatting, storage, rotation, asynchrony and
internationalisation.

## Responsibility boundary

| Layer | Responsibility | Required? |
| --- | --- | --- |
| `Error` / `CallError` | The program-visible reason an operation failed; callers branch on it | Yes |
| Structured context | Module/path/stage/error detail; can accompany an error | Optional |
| Formatted log | Human-readable diagnostic event | Optional |

Logging must never replace an error return. A module load failure still returns
`CallError::load_failed` when every sink is disabled. Do not log the same error
unconditionally at every layer: the lower layer supplies the precise error code,
and the application boundary decides whether to record a final `Error` event.

## Layers

```text
ABIX Core
  error codes + ABIX_LOG_* diagnostic call sites
        |
Diagnostic interface (SKL/ABIX/Util/Log.h)
  fixed callback + level + message lifetime
        |
Host / AMC
  fmtlog, Quill, custom backend, files, async queue, I18N
```

ABIX does not link fmt, fmtlog or Quill. A sink is an observer and cannot change
the result of an operation.

## API contract

`Log.h` provides:

- `LogLevel` — `Debug`, `Info`, `Warning`, `Error`, `Off`.
- `log_sink_t` — legacy `(LogLevel, const char *)` callback.
- `LogRecord` / `log_record_sink_t` — length-aware callback with
  `message`, `length` and `truncated`.
- `LogState` — an explicit sink/record-sink/level owner.
- `default_log_state()` — convenience state for one image.
- `set_log_sink`, `set_log_record_sink`, `set_log_level` and getters.
- `log` plus the `ABIX_LOG_*` and `ABIX_LOG_*_TO` macros.

The sink is set to `nullptr` by default, so ABIX emits nothing until the host
installs one. The default runtime threshold is `Warning`; the host can lower it
to `Debug`, raise it to `Error`, or disable logging with `Off`.

## Threading and callback rules

- With the default `ABIX_LOG_THREAD_SAFE=1`, sink, record sink and threshold
  use ABIX atomic load/store. Setting them while other threads log is supported.
- With `ABIX_LOG_THREAD_SAFE=0`, the fields are plain storage; the host must
  establish the fixed-sink configuration before worker threads start (or provide
  its own synchronization).
- The callback runs synchronously on the logging thread. ABIX holds no logging
  lock while calling it.
- `message` points into a stack buffer and is valid only for the duration of the
  callback. An asynchronous backend must copy it before returning.
- The callback must not throw. `log()` is `noexcept`; an escaping exception
  terminates the process.
- A callback may call back into the logging API, but must not create unbounded
  recursive logging.
- The legacy callback cannot observe truncation. Use `log_record_sink_t` when
  the consumer needs `length` / `truncated`.
- The buffer is 1 KiB. `length` excludes the NUL; `truncated` is true when the
  formatted output did not fit.
- Filtering happens before `vsnprintf`; compile-time level disabling removes the
  call entirely.

## Configuration and dispatch policy

ABIX separates three concerns:

| Layer | Owner | Responsibility |
| --- | --- | --- |
| Configuration storage | ABIX | Sink/record-sink/threshold storage and the read/write strategy |
| Diagnostic dispatch | ABIX | Formatting, filtering and synchronous callback invocation |
| Backend concurrency | Host / external logger | Queueing, locking, async worker, file output |

`ABIX_LOG_THREAD_SAFE` selects the configuration strategy at compile time:

- `1` (default) — `ThreadSafeLogState`: ABIX atomic load/store over plain
  function-pointer/`uint8_t` storage. Concurrent reads and configuration
  changes are supported.
- `0` — `SingleThreadedLogState`: plain storage, no atomic operations on the log
  path. The host must guarantee that sink and level changes do not race with
  logging. This is the intended fixed-sink mode: install the sink before worker
  threads start, then do not modify it.

The selected type is exposed as `LogState`; the two concrete types remain
available as `ThreadSafeLogState` and `SingleThreadedLogState`. The switch is
compile-time, not a per-call branch. Because it is a header-only policy, every
module that shares a `LogState` must use the same value; a mixed set of DLLs
must instead share one host-owned configuration or an explicit runtime state.

The atomic path uses `SKL/ABIX/Util/Atomic.h` free functions over caller-owned
plain storage. It does not store a `std::atomic` object in `LogState`, avoiding
standard-library object layout across DLL/CRT boundaries. Atomic sink
configuration does not make the whole log operation lock-free: formatting and
the external sink can still block. External loggers such as Quill/fmtlog remain
responsible for their own queue and backpressure.

## Cross-DLL ownership

ABIX is header-only, so the convenience `default_log_state()` is inline and has
one copy per DSO/image. It is **not** a process-global singleton. Setting a sink
in the host does not automatically configure an ABIX copy compiled into a plugin.

Choose one of these policies:

1. **Per-module configuration** — each ABIX-bearing module installs its sink in
   its own initialisation path.
2. **Explicit state** — the host/runtime owns a `LogState` and code under its
   control uses the explicit overloads or `ABIX_LOG_*_TO`.
3. **Unique runtime** — if a future build adds a single ABIX runtime object, that
   runtime owns the `LogState` and all modules use it.

Do not assume that an inline static variable is shared across DLLs. The
callback ABI itself stays a plain function pointer and `LogRecord`, so replacing
the host logger does not change ABIX core interfaces. An explicit `LogState` is
a plain-storage configuration object: share it only between modules built with
compatible C++ ABI and ABIX headers, or keep it inside the host runtime and
expose only the callback boundary.

## Deliberately out of scope

ABIX does not provide:

- asynchronous logging threads or queues;
- file rotation, console colour or timestamps;
- thread IDs, source file/line formatting;
- log I18N translation;
- sink chains or dynamic filtering configuration.

Those belong to AMC or the host logger. If structured diagnostics are added
later, they should be a versioned event structure with IDs, module identity and
error codes, not an ever-growing `printf` callback signature.
