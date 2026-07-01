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
 * @file marker_management_engine.hpp
 * @brief Marker collection and storing mechanism
 * @author Noah Andrés Baumann
 * @date 08/01/2026
 */

#pragma once

#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sched.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace TraCR {

/**
 * Compiler hint macros for skewed branch prediction.
 * Prefixed TRACR_ to avoid collisions with system headers (GLib, Linux kernel).
 */
#define TRACR_LIKELY(x) __builtin_expect(!!(x), 1)
#define TRACR_UNLIKELY(x) __builtin_expect(!!(x), 0)

/**
 * The maximum capacity of one tracr thread for capturing the traces.
 * Currently, we fix it here. Might be definable by the user.
 *
 * capatity = 2**16 = 65'536     -> ~1MB tracr thread size
 * capacity = 2**20 = 1'048'576  -> ~17MB tracr thread size (default)
 * capacity = 2**24 = 16'777'216 -> ~268MB tracr thread size
 */
#ifndef TRACR_CAPACITY
constexpr size_t CAPACITY = 1 << 20;
#else
constexpr size_t CAPACITY = TRACR_CAPACITY;
#endif

/**
 * Debug printing method. Can be enabled with the ENABLE_DEBUG flag included.
 * TODO: not yet working
 */
#ifdef ENABLE_DEBUG
#define debug_print(fmt, ...) printf("[TraCR DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define debug_print(fmt, ...)
#endif

/**
 * Our nanosecond timer
 *
 * This timer can also be changed by the chrono (or PyPTO get_cycle()) method
 */
class NanoTimer {
public:
  // Always returns nanoseconds
  static uint64_t now() {
#ifdef USE_HW_COUNTER
    return ticks_to_ns(raw());
#else
    return clock_ns();
#endif
  }

#ifdef USE_HW_COUNTER

  // Raw hardware counter ticks
  static inline uint64_t raw() {
#if defined(__aarch64__)
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif defined(__x86_64__)
    unsigned hi, lo;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
#error "Unsupported architecture for HW counter"
#endif
  }

  // Counter frequency (ticks per second)
  static inline uint64_t frequency() {
#if defined(__aarch64__)
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#elif defined(__x86_64__)
    return tsc_frequency();
#endif
  }

#endif

private:
  static inline uint64_t clock_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }

#ifdef USE_HW_COUNTER
  static inline uint64_t ticks_to_ns(uint64_t ticks) {
    static const uint64_t freq = frequency();
    // Split to avoid overflow: (ticks * 1e9) / freq overflows for runs > ~6s
    // at 3 GHz. Divide first, then handle the remainder separately.
    return (ticks / freq) * 1'000'000'000ULL +
           (ticks % freq) * 1'000'000'000ULL / freq;
  }
#endif

#if defined(__x86_64__) && defined(USE_HW_COUNTER)
  // Warning: Simplified TSC calibration (see note below)
  static uint64_t tsc_frequency() {
    // Fallback to clock-based calibration
    // Runs once (static)
    static uint64_t freq = [] {
      uint64_t t0 = raw();
      uint64_t n0 = clock_ns();

      // wait ~50ms
      struct timespec req = {0, 50'000'000};
      nanosleep(&req, nullptr);

      uint64_t t1 = raw();
      uint64_t n1 = clock_ns();

      return (t1 - t0) * 1'000'000'000ULL / (n1 - n0);
    }();
    return freq;
  }
#endif
};

/**
 * Marker payload
 */
struct Payload {
  // channelId defines in which channel this payload has to be set [0, 65535]
  uint16_t channelId;

  // eventId defines the type of event (i.e. the color) [0, 65535]
  uint16_t eventId;

  // extraId consists of an extra information that has been added to be stored
  // as well (i.e. The type of task label of the event type)
  uint32_t extraId;

  // Chrono nanosecond timestamp
  uint64_t timestamp;
};
static_assert(sizeof(Payload) == 16, "Payload must be exactly 16 bytes");

/**
 * TraCR Thread class. One MPI instance has at least 1
 */
class TraCRThread {
public:
  /**
   * Constructor
   */
  TraCRThread(long tid) : _tid(tid){};

  /**
   * No default constructor allowed.
   */
  TraCRThread() = delete;

  /**
   * Default Destructor as we obey RAII
   */
  ~TraCRThread() = default;

  /**
   *
   */
  inline void store_trace(const Payload &payload) {
#ifdef TRACR_POLICY_PERIODIC
    if (TRACR_UNLIKELY(_traceIdx == CAPACITY)) {
      debug_print("WARNING: TID[%lu] is full, this thread will now overwrite "
                  "from the beginning.",
                  _tid);
    }

    _traces[_traceIdx % CAPACITY] = payload;
    ++_traceIdx;

#elif defined(TRACR_POLICY_IGNORE_IF_FULL)
    if (TRACR_UNLIKELY(_traceIdx >= CAPACITY)) {
      debug_print("WARNING: TID[%lu] is full, this thread will now ignore "
                  "incoming traces.",
                  _tid);
    } else {
      _traces[_traceIdx] = payload;
      ++_traceIdx;
    }
#else /* Abort if full */
    if (TRACR_UNLIKELY(_traceIdx >= CAPACITY)) {
      std::cerr << "Warning: TID[" << _tid
                << "] is full, terminating with a Runtime Error.\n";
      std::exit(EXIT_FAILURE);
    }

    _traces[_traceIdx] = payload;
    ++_traceIdx;
#endif
  }

  /**
   * Flushes the traces into a file at the given path
   */
#ifndef TRACR_DISABLE_FLUSH
  inline void flush_traces(const std::string &path) {
    // Don't create a folder if this TraCR thread is empty
    if (_traceIdx == 0)
      return;

    std::string thread_folder = path + "thread." + std::to_string(_tid) + "/";

    // Create the thread ID folder
    if (mkdir(thread_folder.c_str(), 0755) != 0) {
      if (errno != EEXIST) { // ignore "already exists"
        std::cerr << "mkdir failed for: " << thread_folder << " errno=" << errno
                  << " (" << std::strerror(errno) << ")\n";
        std::exit(EXIT_FAILURE);
      }
    }

    std::string filepath = thread_folder + "traces.bts";

    debug_print("The filepath of this TraCR thread[%lu] is: %s", _tid,
                filepath.c_str());

    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) {
      std::cerr << "Failed to open file: " << filepath << "\n";
      std::exit(EXIT_FAILURE);
    }

#ifdef TRACR_POLICY_PERIODIC
    if (_traceIdx > CAPACITY) {
      // Ring buffer has wrapped: write in temporal order (oldest entry first)
      size_t oldest = _traceIdx % CAPACITY;
      ofs.write(reinterpret_cast<const char *>(_traces.data() + oldest),
                sizeof(Payload) * (CAPACITY - oldest));
      ofs.write(reinterpret_cast<const char *>(_traces.data()),
                sizeof(Payload) * oldest);
    } else {
      ofs.write(reinterpret_cast<const char *>(_traces.data()),
                sizeof(Payload) * _traceIdx);
    }
#else
    ofs.write(reinterpret_cast<const char *>(_traces.data()),
              sizeof(Payload) * _traceIdx);
#endif

    if (!ofs.good()) {
      std::cerr << "Failed to write into file: " << filepath << "\n";
      std::exit(EXIT_FAILURE);
    }

    // Closing file
    ofs.close();

    if (ofs.fail()) {
      std::cerr << "Failed to close file: " << filepath << "\n";
      std::exit(EXIT_FAILURE);
    }
  }
#endif

  /**
   *
   */
  inline long getTID() { return _tid; }

  // The array to keep track of the traces
  std::array<Payload, CAPACITY> _traces;

  // The index at which point to add the next marker
  size_t _traceIdx = 0;

private:
  // kernel thread ID
  long _tid;
};

/**
 * TraCR Proc class, each MPI instance can have one.
 */
class TraCRProc {
public:
  /**
   * Constructor
   */
  TraCRProc(long tid)
      : _tracr_init_time(NanoTimer::now()), _tid(tid) {
    // sched_getcpu() is a Linux/glibc extension, unavailable on macOS/BSD (where
    // the sim build is compiled, incl. arm64 which has no CPU-id API at all).
    // _lCPUid only labels the per-process trace folder ("proc.<id>/") and a
    // diagnostic "pid" field, so off-Linux we fall back to getpid(): still unique
    // per process, so concurrent MPI ranks keep separate folders instead of all
    // colliding in "proc.-1/". On Linux the behaviour is unchanged.
#ifdef __linux__
    _lCPUid = sched_getcpu();
#else
    _lCPUid = static_cast<int>(getpid());
#endif

    _proc_folder_name = "proc." + std::to_string(_lCPUid) + "/";

    debug_print("_proc_folder_name: %s", _proc_folder_name.c_str());
  };

  /**
   * No default constructor allowed.
   */
  TraCRProc() = delete;

  /**
   * Default Destructor as we obey RAII
   */
  ~TraCRProc() = default;

  /**
   *
   */
#ifndef TRACR_DISABLE_FLUSH
  inline bool create_folder_recursive(const std::string &path = "") {
    _proc_folder_name = path + "tracr/" + _proc_folder_name;

    std::istringstream iss(_proc_folder_name);
    std::string token;
    std::string current;

    // Handle leading slash
    if (!_proc_folder_name.empty() && _proc_folder_name[0] == '/') {
      current = "/";
    }

    while (std::getline(iss, token, '/')) {
      if (token.empty())
        continue;
      current += token + "/";

      if (mkdir(current.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
          std::cerr << "mkdir failed for: " << current << " errno=" << errno
                    << " (" << std::strerror(errno) << ")\n";
          return false;
        }
      }
    }

    return true;
  }

  /**
   *
   */
  inline const std::string &getFolderPath() const { return _proc_folder_name; }

  /**
   *
   */
  inline void dump_JSON() {
    if (!json_is_ready) {
      write_JSON();
    }

    // Create and open the metadata.json file
    std::string filename = _proc_folder_name + "metadata.json";
    std::ofstream file(filename);

    if (!file.is_open()) {
      std::cerr << "Failed to open file: " << filename << " for writing!\n";
      std::exit(EXIT_FAILURE);
    }

    // Dump JSON into file (pretty-printed with 4 spaces)
    file << _json_file.dump(4);

    // Close the file
    file.close();

    debug_print("'%s' successfully written!", filename.c_str());
  }
#endif

  /**
   *
   */
  inline void addCustomChannelNames(const nlohmann::json &channel_names) {
    _json_file["channel_names"] = channel_names;
    _json_file["num_channels"] = channel_names.size();
  }

  /**
   *
   */
  inline void addNumberOfChannels(uint16_t num_channels) {
    _json_file["num_channels"] = num_channels;
  }

  /**
   *
   */
  inline void write_JSON() {
    _json_file["pid"] = _lCPUid;
    _json_file["start_time"] = _tracr_init_time;

    // Labels and colorIds indexed by eventId (insertion order).
    // These are the authoritative source for postprocessor label lookup.
    _json_file["markerLabels"] = _markerLabels;
    _json_file["markerColorIds"] = _markerColorIds;

    // colorId -> label mapping kept for Paraver PCF generation
    for (const auto &[key, value] : _markerTypes) {
      _json_file["markerTypes"][std::to_string(key)] = value;
    }

    json_is_ready = true;
  }

  /**
   *
   */
  inline long getTID() { return _tid; }

  // colorId -> label, used for Paraver PCF
  std::unordered_map<uint16_t, std::string> _markerTypes;

  // Labels indexed by eventId (insertion order) - for Perfetto and Paraver PRV
  std::vector<std::string> _markerLabels;

  // Paraver colorIds indexed by eventId - maps eventId -> colorId for PRV file
  std::vector<uint16_t> _markerColorIds;

  // Metadata and channel information of this system
  nlohmann::json _json_file;

private:
  // The name of the proc folder
  std::string _proc_folder_name;

  //
  bool json_is_ready = false;

  // TraCR start time
  uint64_t _tracr_init_time;

  // kernel thread ID
  long _tid;

  // logical CPU ID
  int _lCPUid;
};

} // namespace TraCR
