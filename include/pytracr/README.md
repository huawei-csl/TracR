# PyTraCR

PyTraCR exposes the TraCR instrumentation API to Python as the `tracr`
module, built with [pybind11](https://github.com/pybind/pybind11).

## Requirements

pybind11 must be discoverable, either via pip:

```bash
pip install pybind11
```

or as a system package:

```bash
sudo apt install pybind11-dev
```

## Build

Enable the `buildPyTraCR` option:

```bash
meson setup build -DbuildPyTraCR=true
ninja -C build
```

This produces `build/include/pytracr/tracr.cpython-<ver>-<arch>.so`. Either
install it system-wide with `meson install -C build`, or point Python at the
build directory:

```bash
PYTHONPATH=build/include/pytracr python3 my_script.py
```

## Usage

The Python API mirrors the C++ macros one-to-one. The module is always
compiled with instrumentation enabled (`INSTRUMENTATION_ACTIVE()` returns
`True`).

```python
from tracr import *

INSTRUMENTATION_START()

work_id = INSTRUMENTATION_MARK_W_COLOR_ADD("work", mark_color.MARK_COLOR_TEAL)
idle_id = INSTRUMENTATION_MARK_ADD("idle")  # color auto-assigned

INSTRUMENTATION_MARK_SET(0, work_id)            # start "work" on channel 0
INSTRUMENTATION_MARK_SET(0, idle_id, 42)        # switch to "idle", extraId=42
INSTRUMENTATION_MARK_RESET(0)                   # close the channel

INSTRUMENTATION_ADD_CHANNEL_NAMES(["main"])     # plain list of str

INSTRUMENTATION_END()
```

Python threads (`threading.Thread`) are real OS threads, so multithreaded
tracing works exactly like in C++: call `INSTRUMENTATION_THREAD_INIT()` at
the start and `INSTRUMENTATION_THREAD_FINALIZE()` at the end of each
non-main thread.

See [examples/pytracr/](../../examples/pytracr/) for complete examples
(single-threaded, multithreaded, and flow arrows) and the top-level
[README](../../README.md) for the full API reference and the
`tracr_process` post-processing workflow.
