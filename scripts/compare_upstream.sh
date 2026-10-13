#!/usr/bin/env bash
# What do patches/ change? tests/upstream_compare.cpp -- every engine, a
# fixed pseudo-random performance -- is built against an UNTOUCHED checkout
# of the pinned upstream commits and against variants with patches/ applied,
# and the outputs (every OUT and AUX sample) are compared:
#
#   upstream + 0001 + 0002  must equal upstream byte for byte: those two only
#                           stop a read past a table that is weighted by 0.
#   upstream + all patches  may differ ONLY where the untouched code reads
#                           memory it does not own: WTERR's AUX (0003, a
#                           negative sine phase reads before lut_sine) and
#                           the first block after a switch into a 6-op engine
#                           (0004, a stale accumulator).
#
# The untouched code still performs those reads, so nothing here runs under
# a sanitizer, and its output depends on memory layout: every run is made
# with ASLR off (setarch -R) so the comparison is repeatable.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
EURORACK_COMMIT=$(sed -n 's/^EURORACK_COMMIT=//p' "$MODULE/scripts/fetch_plaits.sh")
STMLIB_COMMIT=$(sed -n 's/^STMLIB_COMMIT=\([0-9a-f]*\).*/\1/p' "$MODULE/scripts/fetch_plaits.sh")
mkdir -p "$MODULE/build"
work="$(mktemp -d "$MODULE/build/compare.XXXXXX")"
trap 'rm -rf "$work"' EXIT
git -C "$work" init -q eurorack
git -C "$work/eurorack" fetch -q --depth 1 https://github.com/pichenettes/eurorack "$EURORACK_COMMIT"
git -C "$work/eurorack" checkout -q FETCH_HEAD
git -C "$work" init -q stmlib
git -C "$work/stmlib" fetch -q --depth 1 https://github.com/pichenettes/stmlib "$STMLIB_COMMIT"
git -C "$work/stmlib" checkout -q FETCH_HEAD

tree() {  # tree <dir> <patch...>: upstream plus the given patches
  local dir=$1; shift
  mkdir -p "$dir"
  cp -r "$work/eurorack/plaits" "$dir/plaits"
  cp -r "$work/stmlib" "$dir/stmlib"
  for p in "$@"; do patch -s -d "$dir" -p1 < "$MODULE/patches/$p"; done
}
build() {  # build <include root> <output>
  local inc=$1 out=$2 objs=()
  mkdir -p "$out.o"
  for f in $(cd "$inc" && find plaits/dsp -name '*.cc' | sort) plaits/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc; do
    local o="$out.o/$(echo "$f" | tr '/' '_').o"
    g++ -std=gnu++11 -O2 -w -include stdio.h -DTEST -fno-exceptions -fno-rtti -ffp-contract=off \
        -isystem "$inc" -c "$inc/$f" -o "$o" &
    objs+=("$o")
  done
  wait
  g++ -std=gnu++17 -O2 -DTEST -fno-exceptions -fno-rtti -ffp-contract=off -isystem "$inc" \
      "$MODULE/tests/upstream_compare.cpp" "${objs[@]}" -o "$out" -lm
}
tree "$work/up"
tree "$work/p12" 0001-fm-velocity-table-end.patch 0002-speech-frame-table-end.patch
build "$work/up" "$work/up.bin"
build "$work/p12" "$work/p12.bin"
build "$MODULE/src/plaits/mi" "$work/all.bin"
for v in up p12 all; do setarch -R "$work/$v.bin" > "$work/$v.raw" 2> "$work/$v.log"; done

if ! cmp -s "$work/up.raw" "$work/p12.raw"; then
  cmp "$work/up.raw" "$work/p12.raw" | head -3
  echo "FAIL: patches 0001/0002 change the output" >&2
  exit 1
fi
python3 -I - "$work/up.raw" "$work/all.raw" "$work/up.log" <<'PY'
import array, sys
up, al = (array.array("h", open(f, "rb").read()) for f in sys.argv[1:3])
assert len(up) == len(al)
seg = [tuple(map(int, l.split())) for l in open(sys.argv[3])]   # (frame, engine)
frames = len(up) // 2
# engine per frame, and the frame where the current engine took over
eng, since = [0] * frames, [0] * frames
cur, start, j = None, 0, 0
for f in range(frames):
    while j < len(seg) and seg[j][0] <= f:
        if seg[j][1] != cur:
            cur, start = seg[j][1], seg[j][0]
        j += 1
    eng[f], since[f] = cur, start
bad = wterr_aux = sixop = 0
for f in range(frames):
    d_out, d_aux = up[2 * f] != al[2 * f], up[2 * f + 1] != al[2 * f + 1]
    if not (d_out or d_aux):
        continue
    if eng[f] == 5 and not d_out:
        wterr_aux += 1
    elif 2 <= eng[f] <= 4 and f - since[f] < 12:
        sixop += 1
    else:
        bad += 1
        if bad <= 3:
            print("  unexplained difference at frame %d (engine %d, %d frames in)" % (f, eng[f], f - since[f]))
print("  %d frames: identical except WTERR AUX %d frames (0003), first 6-op block %d frames (0004)"
      % (frames, wterr_aux, sixop))
if bad:
    print("FAIL: %d frames differ outside the patched reads" % bad)
    sys.exit(1)
PY
echo "PASS: 0001/0002 byte-identical to upstream; 0003/0004 differ only where upstream reads memory it does not own"
