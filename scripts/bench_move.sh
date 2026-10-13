#!/usr/bin/env bash
# Build tests/bench.cpp for the Move and run it there.
#   scripts/bench_move.sh [user@host] [ssh options...]
# The binary goes to /data/UserData/schwung/oplaits-bench (never /tmp: the
# Move's root filesystem is full), runs on cores 0-2 (core 3 is the SPI
# callback's), and is deleted afterwards.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-ableton@move.local}"
shift || true
bash "$MODULE/scripts/build.sh" arm64 >/dev/null
CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"
CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"
GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-arm64"
OUT="$MODULE/build/bench"
mkdir -p "$OUT"
"$CXX" -O2 -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST -fno-exceptions -fno-rtti \
  -fno-threadsafe-statics -std=gnu++17 -I"$MODULE/src" -isystem "$MODULE/src/plaits/mi" -I"$GEN" \
  -c "$MODULE/tests/bench.cpp" -o "$OUT/bench.o"
objs=$(ls "$OBJ"/*.o | grep -v dsp_plugin)
"$CC" -o "$OUT/oplaits-bench" "$OUT/bench.o" $objs -lm
REMOTE=/data/UserData/schwung/oplaits-bench
# Move has no sftp server: stream the binary over ssh instead of scp.
ssh "$@" "$TARGET" "cat > $REMOTE && chmod +x $REMOTE" < "$OUT/oplaits-bench"
ssh "$@" "$TARGET" "taskset 0x7 $REMOTE; rm -f $REMOTE"
