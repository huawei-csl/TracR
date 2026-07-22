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

import time

from tracr import *


def print_matrix(matrix):
    for row in matrix:
        print(" ".join(f"{value:f}" for value in row))
    print()


def main():
    """This is a basic example of the thread markers of TraCR (PyTraCR)"""
    t_start = time.time()

    N = 4

    # Initialize TraCR
    INSTRUMENTATION_START()

    # Each INSTRUMENTATION_MARK_ADD costs around (~3us)
    # Should be done at the beginning or at the ending of the code
    alloc_mem_label_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "Allocate Memory", mark_color.MARK_COLOR_TEAL)
    fill_mat_label_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "Fill matrices with values", mark_color.MARK_COLOR_LAVENDER)
    prt_mat_label_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "Print all matrices", mark_color.MARK_COLOR_RED)
    mmm_label_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "MMM", mark_color.MARK_COLOR_PEACH)
    prt_A_label_id = INSTRUMENTATION_MARK_W_COLOR_ADD(
        "Print solution of matrix A", mark_color.MARK_COLOR_LIGHT_GRAY)

    t_after_label_set = time.time()

    # allocate memory
    INSTRUMENTATION_MARK_SET(0, alloc_mem_label_id, N)
    A = [[0.0] * N for _ in range(N)]
    B = [[0.0] * N for _ in range(N)]
    C = [[0.0] * N for _ in range(N)]

    # fill matrices
    INSTRUMENTATION_MARK_SET(0, fill_mat_label_id)
    for i in range(N):
        for j in range(N):
            B[i][j] = float(i)
            C[i][j] = float(j)

    # print matrices
    INSTRUMENTATION_MARK_SET(0, prt_mat_label_id)
    print("A:")
    print_matrix(A)

    print("B:")
    print_matrix(B)

    print("C:")
    print_matrix(C)

    # mmm
    INSTRUMENTATION_MARK_SET(0, mmm_label_id)
    for i in range(N):
        for j in range(N):
            for k in range(N):
                A[i][j] += B[i][k] * C[k][j]

    # last print
    INSTRUMENTATION_MARK_SET(0, prt_A_label_id)
    print("A (after mmm):")
    print_matrix(A)

    INSTRUMENTATION_MARK_RESET(0)

    # User-defined number of channels to visualize
    INSTRUMENTATION_ADD_NUM_CHANNELS(1)

    if INSTRUMENTATION_ACTIVE():
        print(f"JSON: {INSTRUMENTATION_GET_JSON_STR()}")

    # TraCR finished
    INSTRUMENTATION_END()

    t_end = time.time()

    print(f"\nTotal time: {t_end - t_start}s")
    print(f"Label set time: {t_after_label_set - t_start}s")
    print(f"Marker set time: {t_end - t_after_label_set}s")


if __name__ == "__main__":
    main()
