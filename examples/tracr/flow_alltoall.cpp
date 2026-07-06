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

#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include <tracr/tracr.hpp>

/**
 * Simulated MPI_Alltoall using flow events. Every rank sends one block to
 * every rank (including itself), so there are N*N messages, each drawn as its
 * own arrow. Here the N ranks are modelled as N channels of a single proc,
 * which is enough to show the crossing arrow topology; in a real MPI run each
 * rank would be its own proc and the exact same flow-id scheme would draw the
 * arrows cross-process.
 *
 * The key rule for flows: a flowId must be unique per message and shared by
 * both endpoints. Message from src -> dst uses the id  src * N + dst, so the
 * FLOW_START on src's send matches the FLOW_END on dst's recv.
 */
int main(void) {
  // Number of simulated MPI ranks
  constexpr uint16_t N = 4;

  // Initialize TraCR
  INSTRUMENTATION_START();

  // Each INSTRUMENTATION_MARK_ADD costs around (~3us)
  // Should be done at the beginning or at the ending of the code
  const uint16_t send_id =
      INSTRUMENTATION_MARK_W_COLOR_ADD("Sending Info", MARK_COLOR_LAVENDER);
  const uint16_t recv_id = INSTRUMENTATION_MARK_ADD("Receiving Info");

  // Namespacing the flow id with our pid keeps ids unique even when several
  // instances of this example run concurrently (e.g. via multiproc_run.sh)
  const uint32_t id_base = uint32_t(getpid() & 0xFFFF) << 16;

  // Alltoall send phase: rank i sends one block to every rank j
  for (uint16_t i = 0; i < N; ++i) {
    INSTRUMENTATION_MARK_SET(i, send_id, i);
    for (uint16_t j = 0; j < N; ++j) {
      printf("Rank[%u] Send: to %u\n", i, j);
      // Message i -> j; the arrow leaves rank i's send event
      INSTRUMENTATION_FLOW_START(i, id_base | uint32_t(i * N + j));
    }
    INSTRUMENTATION_MARK_RESET(i);
  }

  // Alltoall recv phase: rank i receives one block from every rank j
  for (uint16_t i = 0; i < N; ++i) {
    INSTRUMENTATION_MARK_SET(i, recv_id, i);
    for (uint16_t j = 0; j < N; ++j) {
      printf("Rank[%u] Recv: from %u\n", i, j);
      // Message j -> i; the arrow arrives on rank i's recv event
      INSTRUMENTATION_FLOW_END(i, id_base | uint32_t(j * N + i));
    }
    INSTRUMENTATION_MARK_RESET(i);
  }

  // Declare the channels so the postprocessor accepts channelIds 0..N-1 and
  // labels the lanes in the viewer
  nlohmann::json names = nlohmann::json::array();
  for (uint16_t i = 0; i < N; ++i)
    names.push_back("Rank_" + std::to_string(i));
  INSTRUMENTATION_ADD_CHANNEL_NAMES(names);

  INSTRUMENTATION_END();

  return 0;
}
