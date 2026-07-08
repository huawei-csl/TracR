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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tracr/marker_management_engine.hpp>

namespace fs = std::filesystem;

constexpr const char PARAVER_HEADER[] = "DEFAULT_OPTIONS\n\n"
                                        "LEVEL               THREAD\n"
                                        "UNITS               NANOSEC\n"
                                        "LOOK_BACK           100\n"
                                        "SPEED               1\n"
                                        "FLAG_ICONS          ENABLED\n"
                                        "NUM_OF_STATE_COLORS 1000\n"
                                        "YMAX_SCALE          37\n\n"

                                        "DEFAULT_SEMANTIC\n\n"
                                        "THREAD_FUNC         State As Is\n\n"

                                        "STATES_COLOR\n"
                                        "0   {  0,   0,   0}\n"
                                        "1   {  0, 130, 200}\n"
                                        "2   {217, 217, 217}\n"
                                        "3   {230,  25,  75}\n"
                                        "4   { 60, 180,  75}\n"
                                        "5   {255, 225,  25}\n"
                                        "6   {245, 130,  48}\n"
                                        "7   {145,  30, 180}\n"
                                        "8   { 70, 240, 240}\n"
                                        "9   {240,  50, 230}\n"
                                        "10  {210, 245,  60}\n"
                                        "11  {250, 190, 212}\n"
                                        "12  {  0, 128, 128}\n"
                                        "13  {128, 128, 128}\n"
                                        "14  {220, 190, 255}\n"
                                        "15  {170, 110,  40}\n"
                                        "16  {255, 250, 200}\n"
                                        "17  {128,   0,   0}\n"
                                        "18  {170, 255, 195}\n"
                                        "19  {128, 128,   0}\n"
                                        "20  {255, 215, 180}\n"
                                        "21  {  0,   0, 128}\n"
                                        "22  {  0,   0, 255}\n\n"

                                        "EVENT_TYPE\n"
                                        "0 90         TraCR\n"
                                        "VALUES\n";

/**
 * Type of tracr file processing format
 */
enum class Format { PARAVER, DUMP, PERFETTO };

/**
 * string to enum format for switch case
 */
Format parseFormat(const std::string &format) {
  if (format == "paraver")
    return Format::PARAVER;
  if (format == "dump")
    return Format::DUMP;
  return Format::PERFETTO; // default
}

/**
 * Properly quotes and escapes a string as a JSON string literal.
 */
static std::string json_str(const std::string &s) {
  return nlohmann::json(s).dump();
}

/**
 * Formats a nanosecond duration as a fixed-point microsecond string with
 * exactly 3 decimal places (e.g. 1017990020 ns -> "1017990.020").
 * Pure integer arithmetic avoids scientific notation and float precision loss.
 */
static std::string fmt_us(uint64_t ns) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu.%03llu",
                (unsigned long long)(ns / 1000),
                (unsigned long long)(ns % 1000));
  return buf;
}

/**
 * Min-heap based k-way merge over a collection of pre-sorted Payload vectors.
 * Replaces the O(N*K) linear-scan approach with O(N*log K).
 */
class PayloadMerger {
  struct Entry {
    uint64_t timestamp;
    size_t file_idx;
    bool operator>(const Entry &o) const { return timestamp > o.timestamp; }
  };

  const std::vector<std::vector<TraCR::Payload>> &_files;
  std::vector<size_t> _ptrs;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> _heap;

public:
  explicit PayloadMerger(const std::vector<std::vector<TraCR::Payload>> &files)
      : _files(files), _ptrs(files.size(), 0) {
    for (size_t i = 0; i < files.size(); ++i)
      if (!files[i].empty())
        _heap.push({files[i][0].timestamp, i});
  }

  bool empty() const { return _heap.empty(); }

  // Returns the next payload in timestamp order and the index of its source
  // file.
  std::pair<TraCR::Payload, size_t> next() {
    auto top = _heap.top();
    _heap.pop();
    TraCR::Payload p = _files[top.file_idx][_ptrs[top.file_idx]];
    if (++_ptrs[top.file_idx] < _files[top.file_idx].size())
      _heap.push(
          {_files[top.file_idx][_ptrs[top.file_idx]].timestamp, top.file_idx});
    return {p, top.file_idx};
  }
};

/**
 * All trace data belonging to one proc folder (one process / MPI rank / NPU).
 */
struct ProcData {
  // process id parsed from the "proc.<id>" folder name
  int pid = -1;

  nlohmann::json metadata;

  // One timestamp-sorted payload vector per thread folder
  std::vector<std::vector<TraCR::Payload>> bts_files;
  std::vector<pid_t> bts_tids;

  // Synchronization anchors in the proc-local clock domain. Every payload
  // timestamp of this proc is normalized to sync_start, assuming all procs
  // pass INSTRUMENTATION_START() at the same real instant (e.g. right after
  // a blocking collective such as MPI_Init). sync_end enables interpolation
  // once intermediate sync stations (INSTRUMENTATION_SYNC) exist.
  uint64_t sync_start = 0;
  uint64_t sync_end = 0;
};

/**
 * A function to load a bts file into a std::vector<Payload>
 */
bool load_bts_file(const fs::path &filepath,
                   std::vector<TraCR::Payload> &traces) {
  std::ifstream ifs(filepath, std::ios::binary);
  if (!ifs) {
    std::cerr << "Failed to open file: " << filepath << "\n";
    return false;
  }

  ifs.seekg(0, std::ios::end);
  std::streamsize filesize = ifs.tellg();
  ifs.seekg(0, std::ios::beg);

  size_t count = filesize / sizeof(TraCR::Payload);
  traces.resize(count);

  ifs.read(reinterpret_cast<char *>(traces.data()),
           count * sizeof(TraCR::Payload));
  if (!ifs) {
    std::cerr << "Failed to read all data from file: " << filepath << "\n";
    return false;
  }

  return true;
}

/**
 * Goes through all the thread folder and loads all the bts files from
 * the given proc folder
 */
int load_thread_traces(const fs::path &proc_path,
                       std::vector<std::vector<TraCR::Payload>> &bts_files,
                       std::vector<pid_t> &bts_tids) {
  size_t tot_num_traces = 0;
  for (const auto &thread_entry : fs::directory_iterator(proc_path)) {

    if (!thread_entry.is_directory())
      continue;

    const std::string folder_name = thread_entry.path().filename().string();

    if (folder_name.find("thread.") != 0)
      continue;

    fs::path trace_file = thread_entry.path() / "traces.bts";

    if (!fs::exists(trace_file)) {
      std::cerr << "  No trace file in: " << thread_entry.path() << "\n";
      return 1;
    }

    std::cout << "  Found trace file: " << trace_file << "\n";

    std::vector<TraCR::Payload> traces;

    if (!load_bts_file(trace_file, traces)) {
      std::cerr << "  Failed to load bts file: " << trace_file << "\n";
      return 1;
    }

    tot_num_traces += traces.size();

    std::cout << "Loaded " << traces.size() << " traces from " << trace_file
              << "\n";

    std::size_t dot_pos = folder_name.find('.');
    if (dot_pos == std::string::npos) {
      std::cerr << "  Invalid thread folder name: " << folder_name << "\n";
      return 1;
    }

    pid_t tid;
    try {
      tid = std::stoi(folder_name.substr(dot_pos + 1));
    } catch (const std::exception &) {
      std::cerr << "  Error parsing TID in folder: " << folder_name << "\n";
      return 1;
    }

    bts_files.push_back(std::move(traces));
    bts_tids.push_back(tid);
  }

  std::cout << "Total number of traces: " << tot_num_traces << "\n";

  return 0;
}

/**
 *
 */
int load_metadata_json(const fs::path &proc_path, nlohmann::json &metadata) {
  fs::path json_file = proc_path / "metadata.json";

  if (!fs::exists(json_file)) {
    std::cerr << "  No metadata.json found\n";
    return 1;
  }

  std::ifstream ifs(json_file);
  if (!ifs.is_open()) {
    std::cerr << "  Failed to open metadata.json\n";
    return 1;
  }

  try {
    ifs >> metadata;
    std::cout << "  Loaded metadata.json:\n";
  } catch (const std::exception &e) {
    std::cerr << "  Failed to parse JSON: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

/**
 * Determines the per-proc synchronization anchors. Prefers the start_time /
 * end_time pair written to metadata.json; falls back to the earliest / latest
 * payload timestamp for traces recorded by older library versions.
 */
void extract_sync_anchors(ProcData &proc) {
  uint64_t first = UINT64_MAX;
  uint64_t last = 0;
  for (const auto &traces : proc.bts_files) {
    if (traces.empty())
      continue;
    first = std::min(first, traces.front().timestamp);
    last = std::max(last, traces.back().timestamp);
  }

  if (proc.metadata.contains("start_time") &&
      !proc.metadata["start_time"].is_null())
    first = std::min(proc.metadata["start_time"].get<uint64_t>(), first);

  if (proc.metadata.contains("end_time") &&
      !proc.metadata["end_time"].is_null())
    last = std::max(proc.metadata["end_time"].get<uint64_t>(), last);

  proc.sync_start = (first == UINT64_MAX) ? 0 : first;
  proc.sync_end = last;
}

/**
 * A function for extracting the bts and metadata of every proc folder
 */
int extract_bts_metadata(std::vector<ProcData> &procs,
                         const fs::path base_path) {

  for (const auto &proc_entry : fs::directory_iterator(base_path)) {
    if (!proc_entry.is_directory() ||
        proc_entry.path().filename().string().find("proc.") != 0)
      continue;

    std::cout << "Found proc folder: " << proc_entry.path() << "\n";

    ProcData proc;

    std::string folder_name = proc_entry.path().filename().string();
    std::size_t dot_pos = folder_name.find('.');
    if (dot_pos != std::string::npos) {
      std::string pid_str = folder_name.substr(dot_pos + 1);
      try {
        proc.pid = std::stoi(pid_str);
      } catch (const std::exception &e) {
        std::cerr << "  Error parsing PID in folder: " << folder_name << "\n";
        return 1;
      }
    }

    if (load_metadata_json(proc_entry.path(), proc.metadata) != 0) {
      return 1;
    }

    if (load_thread_traces(proc_entry.path(), proc.bts_files, proc.bts_tids) !=
        0) {
      std::cerr << "Error: load_thread_traces() failed.\n";
      return 1;
    }

    extract_sync_anchors(proc);

    std::cout << "  Sync anchors: start=" << proc.sync_start
              << " end=" << proc.sync_end << " (duration "
              << (proc.sync_end - proc.sync_start) << " ns)\n";

    procs.push_back(std::move(proc));
  }

  if (procs.empty()) {
    std::cerr << "Error: No proc folder found.\n";
    return 1;
  }

  // Deterministic proc/task numbering regardless of directory iteration order
  std::sort(procs.begin(), procs.end(),
            [](const ProcData &a, const ProcData &b) { return a.pid < b.pid; });

  std::cout << "Found " << procs.size() << " proc folder(s)\n";

  return 0;
}

/**
 * Store the state.cfg in the given tracr folder
 */
int copy_state_cfg(const fs::path &base_path) {
  fs::path source = "state.cfg";

  try {
    fs::copy_file(source, base_path / source.filename(),
                  fs::copy_options::overwrite_existing);

    std::cout << "File 'state.cfg' copied successfully.\n";
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Error copying file: " << e.what() << '\n';
    return 1;
  }

  return 0;
}

/**
 * Create the tracr.pcf file
 *
 * The colorId -> label mapping is merged over all procs, as every proc
 * registers its own markers and they do not have to match.
 */
int create_tracr_pcf(const fs::path &base_path,
                     const std::vector<ProcData> &procs) {
  std::ofstream out(base_path / "tracr.pcf");
  if (!out) {
    std::cerr << "Error opening tracr.pcf for writing\n";
    return 1;
  }

  out << PARAVER_HEADER;

  std::map<std::string, nlohmann::json> merged;
  for (const auto &proc : procs) {
    if (!proc.metadata.contains("markerTypes") ||
        proc.metadata["markerTypes"].is_null())
      continue;

    for (auto it = proc.metadata["markerTypes"].begin();
         it != proc.metadata["markerTypes"].end(); ++it) {
      auto [pos, inserted] = merged.emplace(it.key(), it.value());
      if (!inserted && pos->second != it.value())
        std::cout << "WARNING: colorId " << it.key()
                  << " maps to different labels across procs: " << pos->second
                  << " vs " << it.value() << " (keeping the first)\n";
    }
  }

  for (const auto &[key, value] : merged) {
    out << key << "   " << value << "\n";
  }

  out.close();
  std::cout << "tracr.pcf written successfully.\n";

  return 0;
}

/**
 * Extracts the channel names of one proc (if given) and returns its number of
 * channels.
 */
size_t extract_channel_info(const nlohmann::json &metadata,
                            std::vector<std::string> &channel_names) {
  size_t num_channels = 1; // default

  if (metadata.contains("channel_names") &&
      !metadata["channel_names"].is_null()) {

    num_channels = metadata["channel_names"].size();
    for (auto &channel_name : metadata["channel_names"])
      channel_names.push_back(channel_name);

  } else if (metadata.contains("num_channels") &&
             !metadata["num_channels"].is_null()) {

    num_channels = metadata["num_channels"];
  }

  return num_channels;
}

/**
 * Returns colorId strings indexed by eventId for writing to the Paraver PRV
 * file. Reads the authoritative markerColorIds array when available, with
 * fallback to the legacy markerTypes key iteration for old traces.
 */
std::vector<std::string>
extract_marker_color_ids(const nlohmann::json &metadata) {
  std::vector<std::string> color_ids;

  if (metadata.contains("markerColorIds") &&
      !metadata["markerColorIds"].is_null()) {
    for (auto &id : metadata["markerColorIds"])
      color_ids.push_back(std::to_string(uint16_t(id)));
  } else if (metadata.contains("markerTypes") &&
             !metadata["markerTypes"].is_null()) {
    for (auto &[key, value] : metadata["markerTypes"].items())
      color_ids.push_back(key);
  }

  return color_ids;
}

/**
 * Create the tracr.prv file for Paraver format.
 *
 * Every proc becomes one Paraver task (application 1), every channel one
 * thread within its task. Timestamps are normalized per proc to its
 * sync_start anchor and the records of all procs are emitted in global time
 * order.
 */
int create_tracr_prv(const fs::path &base_path,
                     const std::vector<ProcData> &procs,
                     const std::vector<size_t> &proc_channels) {
  std::ofstream out(base_path / "tracr.prv");
  if (!out) {
    std::cerr << "Error opening tracr.prv for writing\n";
    return 1;
  }

  // Total visualized time (ftime): the Paraver timeline spans the whole
  // profiling session, from INSTRUMENTATION_START() (= t0 of every proc) to
  // the latest INSTRUMENTATION_END() across all procs. For traces recorded
  // without the end_time anchor, sync_end fell back to the last payload, so
  // the timeline then ends at the last recorded marker instead.
  uint64_t total_time = 0;
  for (const auto &proc : procs)
    if (proc.sync_end > proc.sync_start)
      total_time = std::max(total_time, proc.sync_end - proc.sync_start);

  auto now = std::chrono::system_clock::now();
  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm *local_tm = std::localtime(&now_time);

  out << "#Paraver (" << std::setw(2) << std::setfill('0') << local_tm->tm_mday
      << "/" << std::setw(2) << std::setfill('0') << (local_tm->tm_mon + 1)
      << "/" << std::setw(2) << std::setfill('0') << (local_tm->tm_year % 100)
      << " at " << std::setw(2) << std::setfill('0') << local_tm->tm_hour << ":"
      << std::setw(2) << std::setfill('0') << local_tm->tm_min
      << "):" << total_time << "_ns:0:1:" << procs.size() << "(";

  for (size_t p = 0; p < procs.size(); ++p)
    out << proc_channels[p] << ":1" << (p + 1 < procs.size() ? "," : "");

  out << ")\n";

  struct PrvRecord {
    uint64_t timestamp; // normalized to the proc's sync_start
    uint32_t task;      // 1-based proc index
    uint32_t thread;    // 1-based channel
    std::string colorId;
  };

  // One side of a flow (task/thread + normalized time), keyed by flow id
  struct FlowEndpoint {
    uint64_t timestamp;
    uint32_t task;
    uint32_t thread;
  };

  size_t tot_num_traces = 0;
  for (const auto &proc : procs)
    for (const auto &traces : proc.bts_files)
      tot_num_traces += traces.size();

  std::vector<PrvRecord> records;
  records.reserve(tot_num_traces);

  std::unordered_map<uint32_t, std::vector<FlowEndpoint>> flow_starts;
  std::unordered_map<uint32_t, std::vector<FlowEndpoint>> flow_ends;

  for (size_t p = 0; p < procs.size(); ++p) {
    const ProcData &proc = procs[p];

    std::vector<std::string> markerColorIds =
        extract_marker_color_ids(proc.metadata);

    PayloadMerger merger(proc.bts_files);
    while (!merger.empty()) {
      auto [payload, index] = merger.next();

      // Flow payloads become communication records, not event records
      if (payload.eventId == TraCR::EVENTID_FLOW_START ||
          payload.eventId == TraCR::EVENTID_FLOW_END) {
        auto &endpoints = (payload.eventId == TraCR::EVENTID_FLOW_START)
                              ? flow_starts[payload.extraId]
                              : flow_ends[payload.extraId];
        endpoints.push_back({payload.timestamp - proc.sync_start,
                             static_cast<uint32_t>(p + 1),
                             static_cast<uint32_t>(payload.channelId) + 1});
        continue;
      }

      std::string colorId;

      if (!markerColorIds.empty()) {
        colorId = (payload.eventId == TraCR::EVENTID_RESET)
                      ? "0"
                      : markerColorIds[payload.eventId];
      } else {
        colorId = (payload.eventId == TraCR::EVENTID_RESET)
                      ? "0"
                      : std::to_string(payload.eventId);
      }

      records.push_back(
          {payload.timestamp - proc.sync_start, static_cast<uint32_t>(p + 1),
           static_cast<uint32_t>(payload.channelId) + 1, std::move(colorId)});
    }
  }

  // Paraver expects the records of all tasks in global time order
  std::stable_sort(records.begin(), records.end(),
                   [](const PrvRecord &a, const PrvRecord &b) {
                     return a.timestamp < b.timestamp;
                   });

  // Pair flow starts with flow ends (per flow id, in time order) into Paraver
  // communication records: 3:obj_send:lsend:psend:obj_recv:lrecv:precv:size:tag
  struct CommRecord {
    FlowEndpoint send;
    FlowEndpoint recv;
    uint32_t flowId;
  };
  std::vector<CommRecord> comms;
  size_t unmatched = 0;

  for (auto &[flowId, starts] : flow_starts) {
    auto ends_it = flow_ends.find(flowId);
    std::vector<FlowEndpoint> *ends =
        (ends_it != flow_ends.end()) ? &ends_it->second : nullptr;

    auto by_time = [](const FlowEndpoint &a, const FlowEndpoint &b) {
      return a.timestamp < b.timestamp;
    };
    std::sort(starts.begin(), starts.end(), by_time);
    size_t num_pairs = 0;
    if (ends) {
      std::sort(ends->begin(), ends->end(), by_time);
      num_pairs = std::min(starts.size(), ends->size());
      for (size_t i = 0; i < num_pairs; ++i)
        comms.push_back({starts[i], (*ends)[i], flowId});
      unmatched += ends->size() - num_pairs;
    }
    unmatched += starts.size() - num_pairs;
  }
  for (const auto &[flowId, ends] : flow_ends)
    if (!flow_starts.count(flowId))
      unmatched += ends.size();

  if (unmatched > 0)
    std::cout << "WARNING: " << unmatched
              << " flow endpoint(s) without a matching counterpart were "
                 "dropped.\n";

  std::sort(comms.begin(), comms.end(),
            [](const CommRecord &a, const CommRecord &b) {
              return a.send.timestamp < b.send.timestamp;
            });

  // Interleave event and communication records by time
  size_t c = 0;
  for (const auto &record : records) {
    while (c < comms.size() && comms[c].send.timestamp <= record.timestamp) {
      const CommRecord &comm = comms[c++];
      out << "3:0:1:" << comm.send.task << ":" << comm.send.thread << ":"
          << comm.send.timestamp << ":" << comm.send.timestamp
          << ":0:1:" << comm.recv.task << ":" << comm.recv.thread << ":"
          << comm.recv.timestamp << ":" << comm.recv.timestamp
          << ":1:" << comm.flowId << "\n";
    }
    out << "2:0:1:" << record.task << ":" << record.thread << ":"
        << record.timestamp << ":90:" << record.colorId << "\n";
  }
  for (; c < comms.size(); ++c) {
    const CommRecord &comm = comms[c];
    out << "3:0:1:" << comm.send.task << ":" << comm.send.thread << ":"
        << comm.send.timestamp << ":" << comm.send.timestamp
        << ":0:1:" << comm.recv.task << ":" << comm.recv.thread << ":"
        << comm.recv.timestamp << ":" << comm.recv.timestamp
        << ":1:" << comm.flowId << "\n";
  }

  if (!comms.empty())
    std::cout << "Wrote " << comms.size() << " communication record(s) from "
              << "flow events.\n";

  out.close();
  std::cout << "tracr.prv written successfully.\n";

  return 0;
}

/**
 *
 */
int create_tracr_row(const fs::path &base_path,
                     const std::vector<std::string> &row_names) {
  std::ofstream out(base_path / "tracr.row");
  if (!out) {
    std::cerr << "Error opening tracr.row for writing\n";
    return 1;
  }

  out << "LEVEL NODE SIZE 1\n"
         "hostname\n\n"
         "LEVEL THREAD SIZE "
      << row_names.size() << "\n";

  for (const auto &name : row_names)
    out << name << "\n";

  out.close();
  std::cout << "tracr.row written successfully.\n";

  return 0;
}

/**
 * Store in Paraver format
 */
int paraver(const std::vector<ProcData> &procs, const fs::path base_path) {
  if (copy_state_cfg(base_path) != 0) {
    return 1;
  }

  if (create_tracr_pcf(base_path, procs) != 0) {
    return 1;
  }

  // Channel counts and flat (task-major) thread names for the row file
  std::vector<size_t> proc_channels(procs.size());
  std::vector<std::string> row_names;

  for (size_t p = 0; p < procs.size(); ++p) {
    std::vector<std::string> channel_names;
    proc_channels[p] = extract_channel_info(procs[p].metadata, channel_names);

    for (size_t c = 0; c < proc_channels[p]; ++c) {
      std::string name = (c < channel_names.size())
                             ? channel_names[c]
                             : ("Channel_" + std::to_string(c));
      if (procs.size() > 1)
        name = "proc." + std::to_string(procs[p].pid) + ":" + name;
      row_names.push_back(std::move(name));
    }
  }

  if (create_tracr_prv(base_path, procs, proc_channels) != 0) {
    return 1;
  }

  if (create_tracr_row(base_path, row_names) != 0) {
    return 1;
  }

  return 0;
}

/**
 *
 */
void validate_last_events_for_perfetto(
    const std::vector<std::vector<TraCR::Payload>> &bts_files,
    const std::vector<pid_t> &bts_tids) {
  for (size_t i = 0; i < bts_files.size(); ++i) {

    // Trailing flow payloads don't open events; look for the last payload
    // that changes the channel state (a set or a reset).
    size_t last = bts_files[i].size();
    while (last > 0 &&
           (bts_files[i][last - 1].eventId == TraCR::EVENTID_FLOW_START ||
            bts_files[i][last - 1].eventId == TraCR::EVENTID_FLOW_END))
      --last;

    if (last == 0)
      continue;

    const TraCR::Payload &payload = bts_files[i][last - 1];

    if (payload.eventId != TraCR::EVENTID_RESET) {
      std::cout
          << "WARNING: the last event got lost of this thread: " << bts_tids[i]
          << ". To not loose this last event add INSTRUMENTATION_MARK_RESET() "
             "as the last event for Perfetto format.\n";
    }
  }
}

/**
 * Store in Perfetto format.
 *
 * Writes a JSON object with a "traceEvents" array in nanoseconds. Events are
 * streamed directly to disk — no in-memory JSON array is built. Every proc
 * shows up as its own process (pid), with its channels as threads (tid).
 * Timestamps are normalized per proc to its sync_start anchor.
 */
int perfetto(const std::vector<ProcData> &procs, const fs::path base_path) {

  std::ofstream out(base_path / "perfetto.json");
  if (!out.is_open()) {
    std::cerr << "Failed to open 'perfetto.json' for writing!\n";
    return 1;
  }

  // Wrapper object: traceEvents array + ns time unit
  out << "{\"traceEvents\":[\n";

  bool first_event = true;
  auto emit_comma = [&]() {
    if (!first_event)
      out << ",\n";
    first_event = false;
  };

  for (size_t p = 0; p < procs.size(); ++p) {
    const ProcData &proc = procs[p];
    const int pid = (proc.pid == -1) ? 0 : proc.pid;

    // Extract channel count and names
    uint32_t num_channels = 1;
    const nlohmann::json *channels_json = nullptr;
    if (proc.metadata.contains("channel_names") &&
        !proc.metadata["channel_names"].is_null())
      channels_json = &proc.metadata["channel_names"];
    if (channels_json)
      num_channels = channels_json->size();
    else if (proc.metadata.contains("num_channels") &&
             !proc.metadata["num_channels"].is_null())
      num_channels = proc.metadata["num_channels"];

    // Extract marker labels indexed by eventId.
    // Prefer markerLabels array (correct insertion order); fall back to
    // markerTypes key iteration for traces produced by older library versions.
    std::vector<std::string> markerLabels;
    if (proc.metadata.contains("markerLabels") &&
        !proc.metadata["markerLabels"].is_null())
      for (auto &label : proc.metadata["markerLabels"])
        markerLabels.push_back(label);
    else if (proc.metadata.contains("markerTypes") &&
             !proc.metadata["markerTypes"].is_null())
      for (auto &[key, value] : proc.metadata["markerTypes"].items())
        markerLabels.push_back(value);

    // Optional extraId -> human-readable label map (e.g. func_id -> kernel
    // name), keyed by the stringified extraId. When present, each slice whose
    // extraId has an entry gets an "extra_label" arg alongside "extra_id".
    // Absent for traces whose producer registered no labels — those keep
    // emitting extra_id only.
    const nlohmann::json *extraIdLabels = nullptr;
    if (proc.metadata.contains("extraIdLabels") &&
        !proc.metadata["extraIdLabels"].is_null())
      extraIdLabels = &proc.metadata["extraIdLabels"];

    // Process name + stable UI ordering metadata events
    emit_comma();
    out << "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":" << pid
        << ",\"args\":{\"name\":"
        << json_str("TraCR proc." + std::to_string(pid)) << "}}";
    out << ",\n{\"name\":\"process_sort_index\",\"ph\":\"M\",\"pid\":" << pid
        << ",\"args\":{\"sort_index\":" << p << "}}";

    // Thread name metadata events (one per channel)
    for (uint32_t i = 0; i < num_channels; ++i) {
      std::string name = channels_json ? std::string((*channels_json)[i])
                                       : ("Channel_" + std::to_string(i + 1));
      out << ",\n{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":" << pid
          << ",\"tid\":" << (i + 1) << ",\"args\":{\"name\":" << json_str(name)
          << "}}";
    }

    // Merge the proc's thread buffers in timestamp order and emit complete
    // events (ph=X)
    std::vector<TraCR::Payload> prev_payloads(
        num_channels, TraCR::Payload{0, TraCR::EVENTID_RESET, UINT32_MAX, 0});

    PayloadMerger merger(proc.bts_files);
    while (!merger.empty()) {
      auto [payload, index] = merger.next();

      uint16_t channelId = payload.channelId;
      if (channelId >= num_channels) {
        std::cerr << "Payload channelId " << channelId
                  << " is out of bounds!\n";
        return 1;
      }

      // Flow payloads draw an arrow between the enclosing slices of both
      // endpoints (ph=s at the source, ph=f at the destination). They do not
      // open or close events, so the slice state machine is left untouched.
      if (payload.eventId == TraCR::EVENTID_FLOW_START ||
          payload.eventId == TraCR::EVENTID_FLOW_END) {
        const bool is_start = (payload.eventId == TraCR::EVENTID_FLOW_START);
        out << ",\n{\"name\":\"flow\",\"cat\":\"flow\",\"ph\":"
            << (is_start ? "\"s\"" : "\"f\",\"bp\":\"e\"")
            << ",\"id\":" << payload.extraId
            << ",\"ts\":" << fmt_us(payload.timestamp - proc.sync_start)
            << ",\"pid\":" << pid << ",\"tid\":" << (channelId + 1) << "}";
        continue;
      }

      if (prev_payloads[channelId].eventId != TraCR::EVENTID_RESET) {
        const TraCR::Payload &prev = prev_payloads[channelId];
        std::string mType = (!markerLabels.empty())
                                ? markerLabels[prev.eventId]
                                : std::to_string(prev.eventId);

        out << ",\n{\"name\":" << json_str(mType)
            << ",\"cat\":\"tracr\",\"ph\":\"X\""
            << ",\"ts\":" << fmt_us(prev.timestamp - proc.sync_start)
            << ",\"dur\":" << fmt_us(payload.timestamp - prev.timestamp)
            << ",\"pid\":" << pid << ",\"tid\":" << (prev.channelId + 1);
        if (prev.extraId != UINT32_MAX) {
          out << ",\"args\":{\"extra_id\":" << prev.extraId;
          if (extraIdLabels) {
            auto label_it = extraIdLabels->find(std::to_string(prev.extraId));
            if (label_it != extraIdLabels->end())
              out << ",\"extra_label\":"
                  << json_str(label_it.value().get<std::string>());
          }
          out << "}";
        }
        out << "}";
      }

      prev_payloads[channelId] = payload;
    }

    validate_last_events_for_perfetto(proc.bts_files, proc.bts_tids);
  }

  out << "\n]}\n";
  out.close();

  std::cout << "perfetto.json written successfully.\n";
  return 0;
}

/**
 * Dump trace info to terminal
 */
int dump_info(const std::vector<ProcData> &procs) {

  for (const ProcData &proc : procs) {
    std::cout << "\n===== proc." << proc.pid << " =====\n";
    std::cout << "Sync anchors: start=" << proc.sync_start
              << " end=" << proc.sync_end << " (duration "
              << (proc.sync_end - proc.sync_start) << " ns)\n";

    std::unordered_map<uint16_t, int32_t> channelIds_check;
    std::unordered_map<uint16_t, std::unordered_set<uint32_t>> extraIds_check;

    std::cout << "Thread[x]: [channelId, eventId, extraId, timestamp]\n";

    PayloadMerger merger(proc.bts_files);
    while (!merger.empty()) {
      auto [payload, index] = merger.next();

      // Flow payloads don't open/close events: print and skip the checks
      if (payload.eventId == TraCR::EVENTID_FLOW_START ||
          payload.eventId == TraCR::EVENTID_FLOW_END) {
        std::cout << "Thread[" << proc.bts_tids[index] << "]: ["
                  << payload.channelId << ", "
                  << ((payload.eventId == TraCR::EVENTID_FLOW_START)
                          ? "FLOW_START"
                          : "FLOW_END")
                  << ", id=" << payload.extraId << ", " << payload.timestamp
                  << "]\n";
        continue;
      }

      std::cout << "Thread[" << proc.bts_tids[index] << "]: ["
                << payload.channelId << ", " << payload.eventId << ", "
                << payload.extraId << ", " << payload.timestamp << "]\n";

      int32_t &counter = channelIds_check[payload.channelId];
      if (payload.eventId == TraCR::EVENTID_RESET) {
        --counter;
      } else {
        ++counter;
      }

      auto &inner_set = extraIds_check[payload.channelId];
      auto it_inner = inner_set.find(payload.extraId);
      if (it_inner == inner_set.end()) {
        inner_set.insert(payload.extraId);
      } else {
        inner_set.erase(it_inner);
      }
    }

    std::cout
        << "\nChannels which do not follow Push/Pop methology: {channelId, "
           "count}\n";
    for (const auto &[key, value] : channelIds_check) {
      if (value != 0) {
        std::cout << "{" << key << ", " << value << "}\n";
      }
    }

    std::cout << "\nChannels which do not follow Push/Pop methology for the "
                 "extraIds: channelId: {extraIds...}\n";
    for (const auto &[channelId, inner_set] : extraIds_check) {
      if (inner_set.size() > 0) {
        std::cout << channelId << ": {";
        for (const auto &value : inner_set) {
          std::cout << value << ", ";
        }
        std::cout << "}\n";
      }
    }
    std::cout << "\n";
  }

  return 0;
}

/**
 *  The main function to transform bts files into readable files
 *
 *  Use:
 *  1. Create a perfetto format file:
 *    - ./tracr_process <path-to-tracr/>
 *    - ./tracr_process <path-to-tracr/> perfetto
 *
 *  2. Create a paraver format file:
 *    - ./tracr_process <path-to-tracr/> paraver
 *
 *  3. Dump traces and informations directly in the terminal (for debugging):
 *    - ./tracr_process <path-to-tracr/> dump
 */
int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <folder_path>\n OR " << argv[0]
              << " <folder_path>" << "<'perfetto'|'paraver'|'dump'>\n";
    return 1;
  }

  const fs::path base_path = argv[1];

  if (!fs::exists(base_path) || !fs::is_directory(base_path)) {
    std::cerr << "Error: Folder does not exist or is not a directory.\n";
    return 1;
  }

  std::string format = "perfetto";
  if (argc == 3) {
    format = argv[2];
  }

  std::vector<ProcData> procs;

  if (extract_bts_metadata(procs, base_path) != 0) {
    std::cerr << "extract_bts_metadata() failed\n";
    return 1;
  }

  switch (parseFormat(format)) {
  case Format::PARAVER:
    if (paraver(procs, base_path) != 0) {
      std::cerr << "paraver() failed\n";
      return 1;
    }
    break;
  case Format::DUMP:
    if (dump_info(procs) != 0) {
      std::cerr << "dump_info() failed\n";
      return 1;
    }
    break;
  case Format::PERFETTO:
    if (perfetto(procs, base_path) != 0) {
      std::cerr << "perfetto() failed\n";
      return 1;
    }
    break;
  }

  std::cout << "TraCR Process finished successfully\n";

  return 0;
}
