# The Anoptic Logger

The Anoptic logger lets any thread record a line without losing it. Ordinary calls capture typed values into a lock-free MPSC ring; one background thread owns formatting, terminal output, and the output file.

Emission, lifecycle, configuration, and the raw dynamic-format boundary live in `include/anoptic_log.h`.

---

## Public API

### Severity and route

```cpp
enum class ano::Level : std::uint8_t {
    Trace, Debug, Info, Warn, Error, Fatal
};

inline constexpr ano::Route ano::DefaultRoute{};
inline constexpr ano::Route ano::File{1u << 0};
inline constexpr ano::Route ano::Term{1u << 1};
inline constexpr ano::Route ano::Both = File | Term;
inline constexpr ano::Route ano::Now{1u << 2};
```

Severity says how bad. Route says where and when. A route without a sink inherits the level's configured sink; `ano::Now` makes the record synchronous.

Each level carries one reflected `ano::LevelContract`: a five-character display cell, ANSI color, default route, and terminal stream split. Adding or changing a level updates the logger tables from that declaration.

### Emission

```cpp
ano::log(ano::Info, "entity %u spawned at (%d,%d)", id, x, y);
ano::log(ano::Warn, ano::Both, "texture %s missing", name);
ano::log(ano::origin, ano::Error, "bind failed: %d", result);
ano::log(ano::origin, ano::Fatal, ano::Both | ano::Now, "device lost: %s", why);
ANO_DEBUG_LOG(ano::Debug, "loaded %d chunks in %.2f ms", count, elapsedMs);
```

One function name covers default route, explicit route, source attribution, and source attribution plus route. `ano::origin` is the explicit request to capture the call site; it prefixes the message with `file.c:212:`. Calls without it store no source path or line.

The format parameter is a string literal converted through a `consteval` typed wrapper. Compilation parses and validates the format against the argument pack, materializes immutable format/operation tables in static storage, and fails the build at the call site for unsupported formats, wrong argument counts, or incompatible argument types. Runtime producer work contains no format scan and no `va_list` construction.

Pass dynamic text as an argument: `"%s", dynamic`. A dynamic format string belongs at the raw boundary below.

`ANO_DEBUG_LOG(...)` forwards to `ano::log(...)` in Debug and expands to `((void)0)` outside `DEBUG_BUILD`, arguments included. A function cannot preserve that release argument non-evaluation rule because C++ evaluates arguments before entering the function.

Buffered records ride the lock-free ring. `ano::Now` drains the ring, writes through, and syncs an open file before returning.

### Lifecycle

```cpp
int ano::log_init();
int ano::log_cleanup();
```

Call `ano::log_init()` once at startup. It allocates the ring, captures a timestamp anchor, opens `<game-dir>/logs/<session-stamp>_ano.log`, and spawns the drain thread. Before initialization, only `ano::Now` records produce output, to `stderr`.

Call `ano::log_cleanup()` once after all producer threads stop. It joins the drain thread, performs the final drain, syncs, and closes the file.

### Control

```cpp
int  ano::log_output_dir(const char *directory);
void ano::log_set_level(ano::Level minimum);
void ano::log_set_route(ano::Level level, ano::Route route);
void ano::log_flush();
```

`ano::log_output_dir` redirects output to `directory/<session-stamp>_ano.log`. A rejected switch leaves the current file intact and returns `-1`.

`ano::log_set_level` controls admission for buffered records; `ano::Now` bypasses the gate. `ano::log_set_route` requires at least one sink. Without an output file, file-routed records still drain to the terminal.

`ano::log_flush` synchronously drains buffered records. Use it only for a durability point.

### Raw dynamic-format boundary

```cpp
int ano::log_write(ano::Level level, ano::Route route,
                   const char *file, int line, const char *format, ...);
int ano::log_vwrite(ano::Level level, ano::Route route,
                    const char *file, int line, const char *format, va_list arguments);
```

These functions remain for `va_list` wrappers, fuzzers, and genuinely dynamic formats. They preserve the generic parser and eager fallback. Ordinary engine call sites use `ano::log`. The bottom `extern "C"` block in `anoptic_log.h` mirrors the controls and dynamic boundary for foreign ABI callers; engine C++26 code does not use those spellings.

A null `file` stores no origin. The return is `0` for normal write or admission rejection and `1` when a full ring made the dynamic producer wait or write through.

---

## Complete program shape

```cpp
#include <anoptic_log.h>

int main()
{
    if (ano::log_init() != 0)
        return 1;
    ano::log_output_dir("mylogs");
    ano::log_set_route(ano::Warn, ano::Both);

    ano::log(ano::Info, "engine up, build %s", VERSION);
    while (running) {
        tick();
        ano::log_flush();
    }

    // Stop every producer before cleanup.
    return ano::log_cleanup();
}
```

Rules:

1. Initialize before buffered logging and clean up after all producers stop.
2. Any thread may call `ano::log` concurrently. Cleanup is single-owner.
3. Keep ordinary formats literal; pass dynamic text through `%s`.
4. Use `ano::Fatal` or an explicit `ano::Now` for a record that must survive an imminent crash.

Output is wall-clock time, level, optional origin, then message:

```text
14:01:43 INFO  entity 4 spawned at (10,-3)
14:01:44 WARN  world.c:212:  chunk 9 took 40ms to load
```

---

## Underneath: lock-free MPSC ring

MPSC means multi-producer, single-consumer.

The compiler turns each literal call site into static format metadata. A producer checks the severity gate, serializes promoted POD arguments and owned `%s` bytes into a bounded stack record, reserves ring slots with one tail CAS, copies the record, then release-publishes its tag. Filtered records never serialize.

The logger-owned consumer walks claims in order, executes each static plan, batches rendered lines, and writes many lines per syscall. Idle it parks; under load it stays hot.

One ring and one consumer give one claim order. Lines from different threads interleave, each accepted ring record appears once, and `ano::Now` drains prior records before writing out of band.

A full ring applies backpressure. A producer waits for drain room; a persistently wedged consumer makes that producer render and write through instead. Nothing is silently dropped.

What callers can rely on:

- No loss for accepted records.
- Ring issue order, with `ano::Now` draining first.
- No runtime format parsing on the ordinary literal path.
- No allocation or producer-side textual formatting on the ordinary literal path.

What callers must provide:

- Valid lifetime ordering around initialization and cleanup.
- Literal formats for `ano::log`.
- Dynamic formats only through the raw boundary.
