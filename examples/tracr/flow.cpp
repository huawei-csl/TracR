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
#include <sys/wait.h>
#include <unistd.h>

#include <tracr/tracr.hpp>

#define NMSGS 5

/**
 * This example shows flow events: an arrow drawn between two traced events,
 * here from a "send" on one proc to the matching "recv" on another — exactly
 * the MPI_Send() -> MPI_Recv() pattern, but simulated with fork() + a pipe so
 * no MPI is needed. It produces a two-proc trace from a single binary.
 *
 * Both endpoints of a flow must use the same flow id, which has to be unique
 * within the whole trace. Like an MPI message tag, the id here is transmitted
 * inside the message itself; with real MPI it can also be derived from values
 * both sides know, e.g. (src_rank, tag, sequence number).
 */
int main(void) {
  int data_pipe[2]; // sender -> receiver messages
  int sync_pipe[2]; // receiver -> sender "ready" signal

  if (pipe(data_pipe) != 0 || pipe(sync_pipe) != 0) {
    perror("pipe");
    return 1;
  }

  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    return 1;
  }

  if (child == 0) {
    /* ----------------- receiver proc (the MPI_Recv() side) ----------------
     * Signal readiness first, so that both procs pass INSTRUMENTATION_START()
     * at almost the same real instant — the sync anchors of the two procs
     * then line up, like starting right after MPI_Init(). */
    close(data_pipe[1]);
    close(sync_pipe[0]);

    char token = 'r';
    if (write(sync_pipe[1], &token, 1) != 1)
      return 1;
    close(sync_pipe[1]);

    INSTRUMENTATION_START();
    const auto recv_id =
        INSTRUMENTATION_MARK_W_COLOR_ADD("recv message", MARK_COLOR_GREEN);

    for (int i = 0; i < NMSGS; ++i) {
      uint32_t flow_id;
      if (read(data_pipe[0], &flow_id, sizeof(flow_id)) != sizeof(flow_id))
        return 1;

      INSTRUMENTATION_MARK_SET(0, recv_id, flow_id);
      // The arrow ends inside this open "recv message" event
      INSTRUMENTATION_FLOW_END(0, flow_id);
      usleep(300); // simulate some work on the received message
      INSTRUMENTATION_MARK_RESET(0);
    }

    close(data_pipe[0]);
    INSTRUMENTATION_END();
    return 0;
  }

  /* ------------------ sender proc (the MPI_Send() side) ------------------ */
  close(data_pipe[0]);
  close(sync_pipe[1]);

  char token;
  if (read(sync_pipe[0], &token, 1) != 1)
    return 1;
  close(sync_pipe[0]);

  INSTRUMENTATION_START();
  const auto send_id =
      INSTRUMENTATION_MARK_W_COLOR_ADD("send message", MARK_COLOR_ORANGE);

  for (int i = 0; i < NMSGS; ++i) {
    // Namespacing the flow id with our pid keeps ids unique even when several
    // instances of this example run concurrently (e.g. via multiproc_run.sh)
    uint32_t flow_id = (uint32_t(getpid() & 0xFFFF) << 16) | uint32_t(i);

    INSTRUMENTATION_MARK_SET(0, send_id, flow_id);
    // The arrow starts inside this open "send message" event
    INSTRUMENTATION_FLOW_START(0, flow_id);
    usleep(100); // simulated transmission delay, makes the arrow visible
    if (write(data_pipe[1], &flow_id, sizeof(flow_id)) != sizeof(flow_id))
      return 1;
    INSTRUMENTATION_MARK_RESET(0);
    usleep(500); // pause between messages
  }

  close(data_pipe[1]);
  INSTRUMENTATION_END();

  int status = 0;
  waitpid(child, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "receiver proc failed\n");
    return 1;
  }

  printf("Sent and received %d messages connected by flow events\n", NMSGS);
  return 0;
}
