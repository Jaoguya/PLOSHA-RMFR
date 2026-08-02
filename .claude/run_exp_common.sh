#!/bin/bash
# Shared preflight, build and launch logic for the standalone experiment
# runners (run_exp1.sh, run_exp2.sh). This file is sourced, not executed.
#
# Callers must set before sourcing:
#   EXP_FLAG    binary's --experiment value (NOT the paper's experiment number)
#   EXP_NAME    human-readable name for logging
#   OUT_SUBDIR  directory under schemes/plosha_rmfr/ that the binary writes to
#
# Environment overrides:
#   EPOCHS=<n>   epoch count            (default 30)
#   REBUILD=1    force `make clean`     (default incremental)
#   SKIP_BUILD=1 reuse the existing plosha_rmfr binary, do NOT run make. Use
#                when launching two experiments in parallel against one
#                pre-built binary: a second `make` relinking the shared
#                binary while the first run is executing it fails with
#                ETXTBSY. Build once beforehand, then pass SKIP_BUILD=1 to
#                both runners. (Baseline schemes in run_exp2's extra_stages
#                build their own separate binaries and are unaffected.)
#   DEBUG=1      trace every command via `set -x`

set -euo pipefail

: "${EXP_FLAG:?must be set before sourcing run_exp_common.sh}"
: "${EXP_NAME:?must be set before sourcing run_exp_common.sh}"
: "${OUT_SUBDIR:?must be set before sourcing run_exp_common.sh}"

EPOCHS="${EPOCHS:-30}"
REBUILD="${REBUILD:-0}"
DEBUG="${DEBUG:-0}"
[ "$DEBUG" = "1" ] && set -x

# Resolve paths from this file's location, so the scripts work from any
# working directory instead of only from the repository root.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/schemes/plosha_rmfr/src"
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"
OUTPUT_ROOT="$ROOT_DIR/schemes/plosha_rmfr"
OUT_DIR="$OUTPUT_ROOT/$OUT_SUBDIR"
LOG_DIR="$ROOT_DIR/logs"

fail() { echo "" >&2; echo "ERROR: $*" >&2; exit 1; }

preflight() {
  echo "-- preflight --"

  command -v g++  >/dev/null 2>&1 || fail "g++ not found.  sudo apt-get install -y build-essential"
  command -v make >/dev/null 2>&1 || fail "make not found. sudo apt-get install -y build-essential"
  echo "   g++         $(g++ -dumpversion)"
  echo "   make        $(make --version | head -1 | awk '{print $3}')"

  # Compile-and-link probe: catches a missing libssl-dev before the full build.
  local probe
  probe="$(mktemp -d)"
  printf '#include <openssl/bn.h>\nint main(void){ BN_new(); return 0; }\n' > "$probe/probe.c"
  if ! gcc "$probe/probe.c" -lcrypto -o "$probe/probe" >/dev/null 2>&1; then
    rm -rf "$probe"
    fail "OpenSSL headers/libs not usable. sudo apt-get install -y libssl-dev"
  fi
  rm -rf "$probe"
  echo "   openssl     ok"

  [ -f "$DATASET_PATH" ] || fail "dataset not found: $DATASET_PATH"
  echo "   dataset     $(du -h "$DATASET_PATH" | cut -f1)"

  mkdir -p "$OUT_DIR" "$LOG_DIR"
  echo "   output dir  $OUT_DIR"
}

build() {
  echo ""
  echo "-- build --"
  cd "$SRC_DIR"
  if [ "${SKIP_BUILD:-0}" = "1" ]; then
    [ -x ./plosha_rmfr ] || fail "SKIP_BUILD=1 but ./plosha_rmfr is missing — build it first (make)"
    echo "   (SKIP_BUILD=1) reusing existing binary, no make"
  else
    [ "$REBUILD" = "1" ] && make clean
    make
  fi

  # A macOS arm64 build committed into the tree cannot run here; fail loudly
  # rather than with a confusing "cannot execute binary file". Runs in both
  # build and skip-build paths, so a stale/wrong binary is caught either way.
  if command -v file >/dev/null 2>&1; then
    if ! file ./plosha_rmfr | grep -q 'ELF 64-bit.*x86-64'; then
      fail "plosha_rmfr is not a Linux x86-64 ELF: $(file -b ./plosha_rmfr)"
    fi
  fi
  echo "   binary      $(du -h ./plosha_rmfr | cut -f1)"
}

execute() {
  echo ""
  echo "-- run: $EXP_NAME (--experiment $EXP_FLAG, $EPOCHS epochs) --"
  cd "$SRC_DIR"
  local started=$SECONDS
  # stdout is a pipe here (tee), so libc switches to 4 KB block buffering and
  # per-sweep progress lines would not appear until the run ends -- useless for
  # a multi-hour job. stdbuf forces line buffering so the log updates live.
  local runner=()
  command -v stdbuf >/dev/null 2>&1 && runner=(stdbuf -oL -eL)
  # Only forwarded when set, so the experiment's documented default applies
  # otherwise (and the log records which of the two was used).
  local rate=()
  [ -n "${FAILURE_RATE:-}" ] && rate=(--failure-rate "$FAILURE_RATE")
  "${runner[@]}" ./plosha_rmfr \
    --experiment "$EXP_FLAG" \
    --epochs "$EPOCHS" \
    "${rate[@]}" \
    --dataset "$DATASET_PATH" \
    --output "$OUTPUT_ROOT"
  echo "   elapsed     $((SECONDS - started))s"
}

verify() {
  echo ""
  echo "-- verify --"
  local csv="$OUT_DIR/results.csv"
  [ -f "$csv" ] || fail "no results.csv produced at $csv"
  local rows
  rows=$(($(wc -l < "$csv") - 1))
  [ "$rows" -ge 1 ] || fail "results.csv contains a header but no data rows: $csv"
  echo "   $csv ($rows data rows)"
}

main() {
  echo "=========================================="
  echo "    $EXP_NAME"
  echo "=========================================="
  preflight
  build
  execute
  verify
  # Extension point: if the sourcing script defines extra_stages() (e.g.
  # run_exp2.sh's baseline schemes), run it here so its output lands in the
  # same tee'd log as the PLOSHA stage.
  if declare -F extra_stages >/dev/null; then
    extra_stages
  fi
  echo ""
  echo "$EXP_NAME completed."
}

run_experiment() {
  mkdir -p "$LOG_DIR"
  local log="$LOG_DIR/$(basename "$0" .sh)_$(date +%Y%m%d_%H%M%S).log"
  # Announced up front so the path is known even when a stage fails and
  # `set -e` aborts before any trailing output.
  echo "log: $log"
  # pipefail (set above) ensures a failing stage is not masked by tee.
  main 2>&1 | tee "$log"
}
