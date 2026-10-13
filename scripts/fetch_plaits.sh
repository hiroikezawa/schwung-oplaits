#!/usr/bin/env bash
# Where src/plaits/mi came from, as a command.
#
#   scripts/fetch_plaits.sh --verify   download the pinned upstream commits,
#                                      apply patches/, and diff against the
#                                      vendored tree (exit 1 on any difference)
#   scripts/fetch_plaits.sh --update   the same, but overwrite the vendored tree
#
# Copied: all of plaits/dsp/ (every engine, oscillator, physical model, FM,
# speech and FX file), plaits/resources.{h,cc}, plaits/user_data.h, the
# resource generators (plaits/resources/*.py) and the stmlib headers the DSP
# includes. Nothing of the hardware (drivers/, ui.cc, settings.cc, plaits.cc)
# is copied.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec   # eurorack's stmlib submodule
STMLIB_FILES=(stmlib.h LICENSE
  dsp/cosine_oscillator.h dsp/dsp.h dsp/filter.h dsp/hysteresis_quantizer.h
  dsp/limiter.h dsp/parameter_interpolator.h dsp/polyblep.h dsp/rsqrt.h
  dsp/units.h dsp/units.cc
  utils/buffer_allocator.h utils/dsp.h utils/random.h utils/random.cc)

mode="${1:---verify}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git -C "$work" init -q eurorack
git -C "$work/eurorack" fetch -q --depth 1 https://github.com/pichenettes/eurorack "$EURORACK_COMMIT"
git -C "$work/eurorack" checkout -q FETCH_HEAD
git -C "$work" init -q stmlib
git -C "$work/stmlib" fetch -q --depth 1 https://github.com/pichenettes/stmlib "$STMLIB_COMMIT"
git -C "$work/stmlib" checkout -q FETCH_HEAD

out="$work/mi"
mkdir -p "$out/plaits/resources" "$out/stmlib"
cp -r "$work/eurorack/plaits/dsp" "$out/plaits/dsp"
cp "$work/eurorack/plaits/resources.h" "$work/eurorack/plaits/resources.cc" \
   "$work/eurorack/plaits/user_data.h" "$out/plaits/"
cp "$work/eurorack/plaits/resources/"*.py "$out/plaits/resources/"
for f in "${STMLIB_FILES[@]}"; do
  mkdir -p "$out/stmlib/$(dirname "$f")"
  cp "$work/stmlib/$f" "$out/stmlib/$f"
done
mv "$out/stmlib/LICENSE" "$out/LICENSE"
for p in "$MODULE"/patches/*.patch; do
  [[ -e "$p" ]] || continue
  patch -s -d "$out" -p1 < "$p"
done

case "$mode" in
  --verify)
    if diff -r "$out" "$MODULE/src/plaits/mi"; then
      echo "PASS: src/plaits/mi is upstream $EURORACK_COMMIT / $STMLIB_COMMIT + patches/"
    else
      echo "FAIL: src/plaits/mi differs from upstream + patches/" >&2
      exit 1
    fi ;;
  --update)
    rm -rf "$MODULE/src/plaits/mi"
    mkdir -p "$MODULE/src/plaits"
    cp -r "$out" "$MODULE/src/plaits/mi"
    echo "updated src/plaits/mi" ;;
  *) echo "usage: $0 [--verify|--update]" >&2; exit 2 ;;
esac
