# TraCR

**TraCR** (pronounced *tracer*) is a lightweight, nanoscale instrumentation library for tracing multi-threaded C++ applications. It is designed for minimal overhead on the hot path and can be toggled entirely at compile time, leaving zero-cost stubs when disabled.

## How it works

Instrumentation calls store fixed-size `Payload` records (16 bytes each) into a per-thread ring buffer:

```
struct Payload {
    uint16_t channelId;   // logical channel / "lane" to visualize on
    uint16_t eventId;     // type of event (maps to a label/color)
    uint32_t extraId;     // user-defined extra tag (e.g. task ID)
    uint64_t timestamp;   // nanosecond timestamp
};
```

The model is **SET / RESET**: `MARK_SET` starts an event on a channel, `MARK_RESET` closes it. The postprocessor reconstructs durations from consecutive pairs.

At program exit (`INSTRUMENTATION_END` / `INSTRUMENTATION_THREAD_FINALIZE`), each thread flushes its buffer as a raw binary `.bts` file. A separate `tracr_process` tool converts these files into a visualization format of your choice.

### Output directory layout

```
tracr/
  proc.<pid>/              # one folder per process (e.g. per MPI rank)
    metadata.json          # marker labels, channel names, start/end sync timestamps
    thread.<tid>/
      traces.bts           # raw Payload array
```

---

## Build

TraCR uses [Meson](https://mesonbuild.com/) and requires C++17.

```bash
meson setup build
cd build && ninja
```

### Options

| Option | Default | Description |
|---|---|---|
| `buildExamples` | `false` | Build the example programs |
| `buildTests` | `false` | Build the test suite |
| `compileWarningsAsErrors` | `false` | Treat warnings as errors (for CI) |

```bash
meson setup build -DbuildExamples=true -DbuildTests=true
```

### Using TraCR as a subproject

Add this repository under `subprojects/tracr` and in your `meson.build`:

```meson
tracr_dep = dependency('TraCR', fallback: ['tracr', 'InstrumentationBuildDep'])
```

---

## Enabling TraCR

TraCR is **disabled by default** (all macros are no-ops). Enable it at compile time:

```bash
# with Meson
meson setup build -Dcpp_args="-DENABLE_TRACR"
# or directly
g++ -DENABLE_TRACR ...
```

---

## API

### Lifecycle

```cpp
INSTRUMENTATION_START()              // initialize proc + main thread
INSTRUMENTATION_END()                // flush all data and tear down

INSTRUMENTATION_THREAD_INIT()        // call at the start of each non-main thread
INSTRUMENTATION_THREAD_FINALIZE()    // call at the end of each non-main thread
```

### Defining event types

Event types (markers) must be registered before use. Returns the `eventId` to pass to `MARK_SET`.

```cpp
// Assign a specific color from the Paraver palette
uint16_t id = INSTRUMENTATION_MARK_W_COLOR_ADD("label", MARK_COLOR_TEAL);

// Auto-assign the next available color
uint16_t id = INSTRUMENTATION_MARK_ADD("label");
```

Available colors: `MARK_COLOR_BLUE`, `MARK_COLOR_RED`, `MARK_COLOR_GREEN`, `MARK_COLOR_YELLOW`, `MARK_COLOR_ORANGE`, `MARK_COLOR_PURPLE`, `MARK_COLOR_CYAN`, `MARK_COLOR_MAGENTA`, `MARK_COLOR_TEAL`, `MARK_COLOR_MINT`, `MARK_COLOR_PEACH`, `MARK_COLOR_LAVENDER`, and more — see `tracr.hpp`.

### Recording events

```cpp
INSTRUMENTATION_MARK_SET(channelId, eventId, extraId)   // start an event
INSTRUMENTATION_MARK_RESET(channelId)                   // end the event on this channel
```

`channelId` is the visualization lane (0-based). `extraId` is an optional user tag (e.g. task index); use `UINT32_MAX` for none.

### Flow events (arrows between events)

```cpp
INSTRUMENTATION_FLOW_START(channelId, flowId)   // arrow leaves the currently open event
INSTRUMENTATION_FLOW_END(channelId, flowId)     // arrow arrives at the currently open event
```

A flow connects two traced events with an arrow — across channels, threads, or procs. The classic use case is message passing: `FLOW_START` next to an `MPI_Send()` and `FLOW_END` next to the matching `MPI_Recv()` draws the message path in the merged multi-proc view.

Rules:

- Both endpoints must use the **same `flowId`**, and a `flowId` must be **unique within the whole trace**. For MPI, derive it from values both sides know — e.g. `(src_rank, tag, sequence number)` — or transmit it inside the message itself (see `examples/tracr/flow.cpp`).
- Call flow macros while an event is **open** on that channel (between `MARK_SET` and the closing reset/set): the arrow attaches to the enclosing event.
- In Perfetto these become flow events (`ph:"s"` / `ph:"f"`); in Paraver they become communication records (the yellow send/recv lines).

### Channel metadata

```cpp
// Provide human-readable names for each channel
nlohmann::json names = {"worker_0", "worker_1", "worker_2"};
INSTRUMENTATION_ADD_CHANNEL_NAMES(names);

// Or just declare the count (channels get generic names)
INSTRUMENTATION_ADD_NUM_CHANNELS(4);
```

### Runtime control

```cpp
INSTRUMENTATION_ON()                  // re-enable tracing at runtime
INSTRUMENTATION_OFF()                 // pause tracing at runtime (no lock, best-effort)
INSTRUMENTATION_TRACE_PATH("./out/")  // set output directory (call before START)
```

---

## Minimal example

```cpp
#include <tracr/tracr.hpp>

int main() {
    INSTRUMENTATION_START();

    uint16_t compute_id = INSTRUMENTATION_MARK_ADD("compute");
    uint16_t io_id      = INSTRUMENTATION_MARK_ADD("io");

    INSTRUMENTATION_MARK_SET(0, compute_id, 0);
    // ... do work ...
    INSTRUMENTATION_MARK_RESET(0);

    INSTRUMENTATION_MARK_SET(0, io_id, 0);
    // ... do I/O ...
    INSTRUMENTATION_MARK_RESET(0);

    INSTRUMENTATION_ADD_NUM_CHANNELS(1);
    INSTRUMENTATION_END();
}
```

### Multi-threaded (pthreads)

```cpp
void* worker(void* arg) {
    INSTRUMENTATION_THREAD_INIT();

    INSTRUMENTATION_MARK_SET(thread_id, task_id, task_index);
    // ... work ...
    INSTRUMENTATION_MARK_RESET(thread_id);

    INSTRUMENTATION_THREAD_FINALIZE();
    return nullptr;
}
```

See [examples/tracr/](examples/tracr/) for complete examples including pthreads and a performance benchmark.

---

## Post-processing

After running your instrumented binary, convert the `.bts` files with `tracr_process`:

```bash
# Perfetto format (default) — open in https://ui.perfetto.dev
./tracr_process <path-to-tracr/> perfetto

# Paraver format — open .prv/.pcf/.row in Paraver
./tracr_process <path-to-tracr/> paraver

# Dump to terminal (for debugging)
./tracr_process <path-to-tracr/> dump
```

### Perfetto

Produces `perfetto.json`. Load it at [ui.perfetto.dev](https://ui.perfetto.dev). Each proc becomes its own process, each channel a named track within it; event durations are reconstructed from SET/RESET pairs. Timestamps are written as floating-point microseconds (e.g. a 1500 ns event → `1.5 µs`) to preserve sub-microsecond precision within Perfetto's native time unit.

### Paraver

Produces `tracr.prv`, `tracr.pcf`, and `tracr.row`. Each proc becomes one Paraver task, each channel one thread within its task. The trace duration in the header spans the full profiling session — from `INSTRUMENTATION_START()` (t=0) to the latest `INSTRUMENTATION_END()` across all procs; traces recorded by older library versions without the `end_time` anchor end at the last recorded marker instead. Requires a `state.cfg` in the working directory (copied automatically from `postprocessing/paraver/state.cfg` during build).

### Dump

Prints all payloads chronologically (per proc) and reports any channels with mismatched SET/RESET counts.

### Multi-proc traces and synchronization

`tracr_process` accepts a `tracr/` folder containing any number of `proc.<pid>/` subfolders — for example one per MPI rank writing to the same trace path. TraCR itself has no MPI dependency: a multiproc trace is simply produced by several instrumented processes running with the same trace path. You can simulate this without MPI via `examples/bash_scripts/multiproc_run.sh <num_procs> <executable>`, which launches N concurrent instances of a binary (the test suite does this for every example). `metadata.json` stores two synchronization anchors, `start_time` (taken at `INSTRUMENTATION_START()`) and `end_time` (taken when the trace is dumped at `INSTRUMENTATION_END()`), in the same clock domain as the payload timestamps. The postprocessor normalizes every proc's timestamps to its own `start_time`, which aligns procs under the assumption that they all pass `INSTRUMENTATION_START()` at the same real instant (e.g. right after a blocking collective such as `MPI_Init`). Traces recorded by older library versions fall back to the earliest/latest payload timestamp per proc.

---

## Compile-time configuration

| Flag | Default | Description |
|---|---|---|
| `ENABLE_TRACR` | off | Enable all instrumentation (otherwise no-ops) |
| `TRACR_CAPACITY` | `1<<20` (≈1M) | Per-thread trace buffer size (in number of events) |
| `USE_HW_COUNTER` | off | Use hardware timer (TSC on x86, `cntvct_el0` on AArch64) instead of `clock_gettime` |
| `TRACR_POLICY_PERIODIC` | off | Wrap around (overwrite oldest) when buffer is full |
| `TRACR_POLICY_IGNORE_IF_FULL` | off | Silently drop events when buffer is full |
| *(default)* | — | Abort with error when buffer is full |
| `TRACR_DISABLE_FLUSH` | off | Skip writing `.bts` files (for in-memory-only use) |
| `ENABLE_DEBUG` | off | Enable internal debug prints |

Buffer memory per thread: `TRACR_CAPACITY × 16 bytes` (default ≈ 17 MB).

---

## Supported architectures

- **x86_64** — `clock_gettime(CLOCK_MONOTONIC_RAW)` or TSC (`USE_HW_COUNTER`)
- **AArch64** — `clock_gettime(CLOCK_MONOTONIC_RAW)` or `cntvct_el0` (`USE_HW_COUNTER`)

---

## License

Copyright 2026 Huawei Technologies Co., Ltd.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
