/*
 *   Copyright 2026 Huawei Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file pytracr.cpp
 * @brief PyTraCR: pybind11 bindings exposing the TraCR API to Python
 * @author Noah Andrés Baumann
 * @date 22/07/2026
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <tracr/tracr.hpp>

namespace py = pybind11;

static bool instrumentation_active() { return INSTRUMENTATION_ACTIVE; }

/**
 * Proc methods
 */
static void py_instrumentation_start() { INSTRUMENTATION_START(); }

static void py_instrumentation_end() { INSTRUMENTATION_END(); }

/**
 * Thread methods
 */
static void py_instrumentation_thread_init() { INSTRUMENTATION_THREAD_INIT(); }

static void py_instrumentation_thread_finalize() {
  INSTRUMENTATION_THREAD_FINALIZE();
}

static std::string py_instrumentation_get_thread_trace_str() {
  return INSTRUMENTATION_GET_THREAD_TRACE_STR();
}

/**
 * Marker methods
 */
static uint16_t py_instrumentation_mark_w_color_add(const std::string &label,
                                                    uint16_t colorId) {
  return INSTRUMENTATION_MARK_W_COLOR_ADD(label, colorId);
}

static uint16_t py_instrumentation_mark_add(const std::string &label) {
  return INSTRUMENTATION_MARK_ADD(label);
}

static void py_instrumentation_mark_set(uint16_t channelId, uint16_t eventId,
                                        uint32_t extraId) {
  INSTRUMENTATION_MARK_SET(channelId, eventId, extraId);
}

static void py_instrumentation_mark_reset(uint16_t channelId) {
  INSTRUMENTATION_MARK_RESET(channelId);
}

/**
 * Flow methods (draw an arrow between two events, e.g. send -> recv)
 */
static void py_instrumentation_flow_start(uint16_t channelId, uint32_t flowId) {
  INSTRUMENTATION_FLOW_START(channelId, flowId);
}

static void py_instrumentation_flow_end(uint16_t channelId, uint32_t flowId) {
  INSTRUMENTATION_FLOW_END(channelId, flowId);
}

/**
 * Channel metadata methods
 */
static void py_instrumentation_add_channel_names(
    const std::vector<std::string> &channel_names) {
  INSTRUMENTATION_ADD_CHANNEL_NAMES(nlohmann::json(channel_names));
}

static void py_instrumentation_add_num_channels(uint16_t num_channels) {
  INSTRUMENTATION_ADD_NUM_CHANNELS(num_channels);
}

/**
 * Other methods
 */
static void py_instrumentation_on() { INSTRUMENTATION_ON(); }

static void py_instrumentation_off() { INSTRUMENTATION_OFF(); }

static void py_instrumentation_trace_path(const std::string &path) {
  INSTRUMENTATION_TRACE_PATH(path);
}

static bool py_instrumentation_is_proc_ready() {
  return INSTRUMENTATION_IS_PROC_READY();
}

static int py_instrumentation_num_tracr_threads() {
  return INSTRUMENTATION_NUM_TRACR_THREADS();
}

static bool py_instrumentation_proc_exists() {
  return INSTRUMENTATION_PROC_EXISTS();
}

static bool py_instrumentation_thread_exists() {
  return INSTRUMENTATION_THREAD_EXISTS();
}

static std::string py_instrumentation_get_json_str() {
  return INSTRUMENTATION_GET_JSON_STR();
}

PYBIND11_MODULE(tracr, m) {
  m.doc() = "PyTraCR: pybind11 bindings for the TraCR profiling library";

  m.def("INSTRUMENTATION_ACTIVE", &instrumentation_active,
        "Returns True if TraCR instrumentation is compiled in");

  /**
   * Proc methods
   */
  m.def("INSTRUMENTATION_START", &py_instrumentation_start,
        "Initialize the TraCR proc and the calling (main) thread");
  m.def("INSTRUMENTATION_END", &py_instrumentation_end,
        "Flush all traces, write metadata.json and tear TraCR down");

  /**
   * Thread methods
   */
  m.def("INSTRUMENTATION_THREAD_INIT", &py_instrumentation_thread_init,
        "Initialize TraCR for the calling thread (not the main thread)");
  m.def("INSTRUMENTATION_THREAD_FINALIZE", &py_instrumentation_thread_finalize,
        "Flush and finalize TraCR for the calling thread");
  m.def("INSTRUMENTATION_GET_THREAD_TRACE_STR",
        &py_instrumentation_get_thread_trace_str,
        "Hex dump of the calling thread's trace buffer (debugging)");

  /**
   * Marker methods
   */
  m.def("INSTRUMENTATION_MARK_W_COLOR_ADD",
        &py_instrumentation_mark_w_color_add, py::arg("label"),
        py::arg("colorId"),
        "Register a marker type with a Paraver color. Returns its eventId");
  m.def("INSTRUMENTATION_MARK_ADD", &py_instrumentation_mark_add,
        py::arg("label"),
        "Register a marker type (color auto-assigned). Returns its eventId");
  m.def("INSTRUMENTATION_MARK_SET", &py_instrumentation_mark_set,
        py::arg("channelId"), py::arg("eventId"),
        py::arg("extraId") = UINT32_MAX,
        "Start an event on a channel (closes the previous one)");
  m.def("INSTRUMENTATION_MARK_RESET", &py_instrumentation_mark_reset,
        py::arg("channelId"), "End the currently open event on a channel");

  /**
   * Flow methods
   */
  m.def("INSTRUMENTATION_FLOW_START", &py_instrumentation_flow_start,
        py::arg("channelId"), py::arg("flowId"),
        "Flow arrow source, attached to the open event on this channel");
  m.def("INSTRUMENTATION_FLOW_END", &py_instrumentation_flow_end,
        py::arg("channelId"), py::arg("flowId"),
        "Flow arrow destination, attached to the open event on this channel");

  /**
   * Channel metadata methods
   */
  m.def("INSTRUMENTATION_ADD_CHANNEL_NAMES",
        &py_instrumentation_add_channel_names, py::arg("channel_names"),
        "Set human-readable channel names (list of str)");
  m.def("INSTRUMENTATION_ADD_NUM_CHANNELS",
        &py_instrumentation_add_num_channels, py::arg("num_channels"),
        "Declare the number of channels (generic names)");

  /**
   * Other methods
   */
  m.def("INSTRUMENTATION_ON", &py_instrumentation_on,
        "Re-enable tracing at runtime");
  m.def("INSTRUMENTATION_OFF", &py_instrumentation_off,
        "Pause tracing at runtime (best-effort, no lock)");
  m.def("INSTRUMENTATION_TRACE_PATH", &py_instrumentation_trace_path,
        py::arg("path"),
        "Set the output directory (call before INSTRUMENTATION_START)");
  m.def("INSTRUMENTATION_IS_PROC_READY", &py_instrumentation_is_proc_ready,
        "Returns True if the TraCR proc is initialized");
  m.def("INSTRUMENTATION_NUM_TRACR_THREADS",
        &py_instrumentation_num_tracr_threads,
        "Number of currently registered TraCR threads");
  m.def("INSTRUMENTATION_PROC_EXISTS", &py_instrumentation_proc_exists,
        "Returns True if the TraCR proc exists");
  m.def("INSTRUMENTATION_THREAD_EXISTS", &py_instrumentation_thread_exists,
        "Returns True if the calling thread is TraCR-initialized");
  m.def("INSTRUMENTATION_GET_JSON_STR", &py_instrumentation_get_json_str,
        "The metadata.json content as a string (debugging)");

  py::enum_<mark_color>(m, "mark_color", py::arithmetic())
      .value("MARK_COLOR_NONE", MARK_COLOR_NONE)
      .value("MARK_COLOR_BLUE", MARK_COLOR_BLUE)
      .value("MARK_COLOR_LIGHT_GRAY", MARK_COLOR_LIGHT_GRAY)
      .value("MARK_COLOR_RED", MARK_COLOR_RED)
      .value("MARK_COLOR_GREEN", MARK_COLOR_GREEN)
      .value("MARK_COLOR_YELLOW", MARK_COLOR_YELLOW)
      .value("MARK_COLOR_ORANGE", MARK_COLOR_ORANGE)
      .value("MARK_COLOR_PURPLE", MARK_COLOR_PURPLE)
      .value("MARK_COLOR_CYAN", MARK_COLOR_CYAN)
      .value("MARK_COLOR_MAGENTA", MARK_COLOR_MAGENTA)
      .value("MARK_COLOR_LIGHT_GREEN", MARK_COLOR_LIGHT_GREEN)
      .value("MARK_COLOR_PINK", MARK_COLOR_PINK)
      .value("MARK_COLOR_TEAL", MARK_COLOR_TEAL)
      .value("MARK_COLOR_GRAY", MARK_COLOR_GRAY)
      .value("MARK_COLOR_LAVENDER", MARK_COLOR_LAVENDER)
      .value("MARK_COLOR_BROWN", MARK_COLOR_BROWN)
      .value("MARK_COLOR_LIGHT_YELLOW", MARK_COLOR_LIGHT_YELLOW)
      .value("MARK_COLOR_MAROON", MARK_COLOR_MAROON)
      .value("MARK_COLOR_MINT", MARK_COLOR_MINT)
      .value("MARK_COLOR_OLIVE", MARK_COLOR_OLIVE)
      .value("MARK_COLOR_PEACH", MARK_COLOR_PEACH)
      .value("MARK_COLOR_NAVY", MARK_COLOR_NAVY)
      .value("MARK_COLOR_BRIGHT_BLUE", MARK_COLOR_BRIGHT_BLUE)
      .export_values();
}
