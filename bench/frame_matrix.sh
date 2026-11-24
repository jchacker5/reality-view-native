#!/usr/bin/env bash
# A/B matrix for frame pacing. Runs the app in benchmark mode under several configurations and
# prints one line per run. Usage: bench/frame_matrix.sh <path-to-binary> [seconds] [repeats]
set -euo pipefail
BIN=${1:?path to reality-view-native binary}
SECS=${2:-8}
REPS=${3:-3}

configs=(
  "baseline (3d, msaa4, panel)|"
  "msaa off|--msaa 0"
  "2d view|--view 2d"
  "panel off|--panel off"
  "2d + msaa off + panel off|--view 2d --msaa 0 --panel off"
)

printf '%-28s %6s %7s %7s %7s %8s\n' config fps p50 p95 p99 jank%
for entry in "${configs[@]}"; do
  name=${entry%%|*}
  args=${entry#*|}
  for ((r = 1; r <= REPS; r++)); do
    # shellcheck disable=SC2086
    json=$("$BIN" --benchmark-seconds "$SECS" $args 2>/dev/null)
    python3 -c '
import json, sys
d = json.loads(sys.argv[2])
jank = 100.0 * d["jank_frames_total"] / max(d["frames"], 1)
print("%-28s %6.1f %7.2f %7.2f %7.2f %7.1f%%" % (sys.argv[1], d["fps"], d["frame_ms_p50"], d["frame_ms_p95"], d["frame_ms_p99"], jank))
' "$name" "$json"
  done
done
