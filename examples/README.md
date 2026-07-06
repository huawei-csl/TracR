# TraCR examples

The following examples are provided in `tracr/`. Each one is built twice by
meson: once with instrumentation enabled (`<name>_tracr_enabled`) and once with
the zero-cost stubs (`<name>_tracr_disabled`).

- `simple.cpp`: The best starting point. A single-threaded matrix
multiplication where every phase (allocate, fill, MMM, print, free) is wrapped
in a marker registered with `INSTRUMENTATION_MARK_W_COLOR_ADD()`. All events
live on one channel of the main thread; the `extraId` field is used to attach
extra information (the matrix size) to an event.

- `pthread.cpp`: Multi-threaded tracing with Pthreads. Every worker thread
calls `INSTRUMENTATION_THREAD_INIT()` / `INSTRUMENTATION_THREAD_FINALIZE()`
itself and marks its tasks on its own channel (channel id = thread id), so
each thread shows up as its own lane in the viewer. Also shows lazy marker
registration (`INSTRUMENTATION_MARK_ADD()`, no explicit color) and custom
channel names via `INSTRUMENTATION_ADD_CHANNEL_NAMES()`.

- `performance_test.cpp`: Measures TraCR's hot-path overhead by issuing 1000
`INSTRUMENTATION_MARK_SET()` / `INSTRUMENTATION_MARK_RESET()` pairs in a tight
loop and printing the average cost per marker.

- `flow.cpp`: Flow events across two procs — the `MPI_Send()` -> `MPI_Recv()`
pattern simulated with `fork()` and a pipe, so no MPI is needed. The sender
wraps each message in a marker and calls `INSTRUMENTATION_FLOW_START()`, the
receiver answers with `INSTRUMENTATION_FLOW_END()`; the merged trace shows an
arrow per message from the send slice to the recv slice (flow events in
Perfetto, communication records in Paraver). The flow id travels inside the
message itself, like an MPI tag.

- `flow_alltoall.cpp`: A simulated `MPI_Alltoall` built from flow events. N
ranks are modelled as N channels of one proc; every rank sends a block to every
rank, giving N*N arrows in the full crossing pattern. Shows the essential flow
rule — a unique id per message shared by both endpoints — by deriving the id of
message `src -> dst` as `src * N + dst`, so the send's `FLOW_START` matches the
recv's `FLOW_END`.

## Simulated multiproc runs (no MPI required)

Any instrumented example can be run in "multiproc mode" by simply launching it
several times concurrently from the same working directory — each process
writes its own `proc.<pid>/` folder into the shared `tracr/` directory, exactly
like N MPI ranks on one node would. The `multiproc_run.sh` helper does this:

```bash
cd <some-workdir>
../bash_scripts/multiproc_run.sh 4 ./simple_tracr_enabled
tracr_process tracr/ perfetto   # one process per pid in the merged view
```

The meson test suite runs every example this way (4 concurrent instances) and
postprocesses the merged trace in all formats.
