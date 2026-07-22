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

import queue
import threading
import time

from tracr import *

NMSGS = 5

SENDER_CHANNEL = 0
RECEIVER_CHANNEL = 1


def receiver(msg_queue, recv_id):
    """The consumer thread: the arrow ends inside its "recv message" event"""
    INSTRUMENTATION_THREAD_INIT()

    for _ in range(NMSGS):
        flow_id = msg_queue.get()

        INSTRUMENTATION_MARK_SET(RECEIVER_CHANNEL, recv_id, flow_id)
        INSTRUMENTATION_FLOW_END(RECEIVER_CHANNEL, flow_id)
        time.sleep(0.0003)  # simulate some work on the received message
        INSTRUMENTATION_MARK_RESET(RECEIVER_CHANNEL)

    INSTRUMENTATION_THREAD_FINALIZE()


def main():
    """
    This example shows flow events: an arrow drawn between two traced
    events, here from a "send" on one thread to the matching "recv" on
    another — the producer -> consumer pattern, simulated with a queue.

    Both endpoints of a flow must use the same flow id, which has to be
    unique within the whole trace. Here it is transmitted inside the
    message itself.
    """
    # Initialize TraCR
    INSTRUMENTATION_START()

    send_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "send message", mark_color.MARK_COLOR_ORANGE)
    recv_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "recv message", mark_color.MARK_COLOR_GREEN)

    msg_queue = queue.Queue()

    recv_thread = threading.Thread(target=receiver, args=(msg_queue, recv_id))
    recv_thread.start()

    # The producer: the arrow starts inside its "send message" event
    for i in range(NMSGS):
        flow_id = i

        INSTRUMENTATION_MARK_SET(SENDER_CHANNEL, send_id, flow_id)
        INSTRUMENTATION_FLOW_START(SENDER_CHANNEL, flow_id)
        time.sleep(0.0001)  # simulated transmission delay (visible arrow)
        msg_queue.put(flow_id)
        INSTRUMENTATION_MARK_RESET(SENDER_CHANNEL)
        time.sleep(0.0005)  # pause between messages

    recv_thread.join()

    INSTRUMENTATION_ADD_CHANNEL_NAMES(["sender", "receiver"])

    # TraCR finished
    INSTRUMENTATION_END()


if __name__ == "__main__":
    main()
