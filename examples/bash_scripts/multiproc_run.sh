#!/bin/bash
# multiproc_run.sh
#
# Simulates a multiproc (e.g. MPI) environment without MPI: launches N
# instances of an instrumented binary concurrently from the same working
# directory. Every instance writes its own proc.<pid>/ folder into the shared
# tracr/ directory, which is exactly what N MPI ranks on one node would do.
#
# Usage: multiproc_run.sh <num_procs> <executable> [args...]

if [ $# -lt 2 ]; then
    echo "Usage: $0 <num_procs> <executable> [args...]" >&2
    exit 1
fi

NUM_PROCS=$1
shift

PIDS=()
for ((i = 0; i < NUM_PROCS; i++)); do
    "$@" &
    PIDS+=($!)
done

STATUS=0
for pid in "${PIDS[@]}"; do
    wait "$pid" || STATUS=1
done

exit $STATUS
