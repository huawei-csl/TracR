"""
    Copyright 2026 Huawei Technologies Co., Ltd.

  Licensed under the Apache License, Version 2.0 (the "License")
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
"""

import os
import threading

from tracr import *

NRANKS = 4  # number of threads
NTASKS = 2  # number of tasks per thread

# Lock for printing
print_lock = threading.Lock()


def thread_function(id, task_running_id, task_finished_id):
    # TraCR init thread (each Python thread is a real OS thread)
    INSTRUMENTATION_THREAD_INIT()

    with print_lock:
        print(f"Thread {id} is running. PID: {os.getpid()}, "
              f"native TID: {threading.get_native_id()}")

    # running tasks
    for i in range(NTASKS):
        taskid = id * NTASKS + i

        INSTRUMENTATION_MARK_SET(id, task_running_id, taskid)

        with print_lock:
            print(f"Thread {id} is running task: {taskid}")

        INSTRUMENTATION_MARK_SET(id, task_finished_id, taskid)

    INSTRUMENTATION_MARK_RESET(id)

    # TraCR free thread
    INSTRUMENTATION_THREAD_FINALIZE()


def main():
    """This is an example of using the thread markers with Python threads"""
    # Initialize TraCR
    INSTRUMENTATION_START()

    task_running_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "task running", mark_color.MARK_COLOR_MINT)
    task_finished_id = INSTRUMENTATION_MARK_ADD("task finishing")

    # Create NRANKS number of threads
    threads = [
        threading.Thread(target=thread_function,
                         args=(i, task_running_id, task_finished_id))
        for i in range(NRANKS)
    ]

    for thread in threads:
        thread.start()

    # Wait for all threads to finish
    for thread in threads:
        thread.join()

    print("All threads have finished.")

    # User-defined channels names to visualize
    INSTRUMENTATION_ADD_CHANNEL_NAMES(
        [f"Thread_{i}" for i in range(NRANKS)])

    # TraCR finished
    INSTRUMENTATION_END()


if __name__ == "__main__":
    main()
