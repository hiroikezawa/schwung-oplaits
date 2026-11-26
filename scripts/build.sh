#!/usr/bin/env bash
# Build the oPlaits module.
#   scripts/build.sh native   desktop dsp.so for the tests (build/native)
#   scripts/build.sh arm64    Move dsp.so + install tarball (dist/)
set -euo pipefail
ID=oplaits
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/schwung (MIT, see its
# LICENSE), unmodified.
API="$MODULE/third_party/schwung"

case "${1:-arm64}" in
  native) CXX="${CXX:-g++}"; CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  arm64) CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"; CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac

GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-${1:-arm64}"
mkdir -p "$OUT" "$GEN" "$OBJ"
python3 "$MODULE/scripts/gen_tables.py" "$GEN/oplaits_tables.h"
python3 "$MODULE/scripts/metadata.py" "$GEN"
# The phrase sources: MIDI files compiled in (nothing is read on the device).
python3 "$MODULE/scripts/embed_patterns.py" "$MODULE/patterns" "$GEN/pg_patterns_data.h"

# -DTEST: stmlib's / Plaits' switch for a non-MCU build: no ".ramtext"
#   placement, and plaits/user_data.h's flash reads become "no user data"
#   (the hardware's state before anything is uploaded to it).
# -include stdio.h: user_data.h's TEST branch calls printf without including
#   it; included here rather than by editing the file.
# -fvisibility=hidden + -Bsymbolic: only move_plugin_init_v2 is exported and
#   every internal call binds locally, so another Plaits-based module loaded
#   in the same process can never interpose its plaits:: symbols on ours.
# -ffp-contract=off: no fused multiply-adds the x86 build would not also make,
#   so the desktop tests and the Move compute the same arithmetic as written.
# No exceptions/RTTI and linked with the C driver: no libstdc++ at runtime.
COMMON=(-O2 -fPIC -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST
        -fvisibility=hidden -fvisibility-inlines-hidden
        -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -I"$MODULE/src" -isystem "$MODULE/src/plaits/mi" -I"$GEN" -I"$API")
OURS=(-std=gnu++17 -Wall -Wextra -Werror)
VENDOR=(-std=gnu++11 -w -include stdio.h)   # third-party, compiled as shipped
# Phrase Gen's engine is C. -std=c11 matters: GCC 15 defaults to C23, whose
# headers bind string conversions to GLIBC_2.38 (Move has 2.34).
PHRASE=(-std=c11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -I"$MODULE/src/phrase" -I"$GEN")

objs=()
compile() {  # compile <flags-array-name> <source>
  local -n flags=$1
  local src=$2 obj
  obj="$OBJ/$(echo "${src#$MODULE/src/}" | tr '/' '_').o"
  "$CXX" "${COMMON[@]}" "${flags[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
}
while IFS= read -r f; do
  compile VENDOR "$f"
done < <(find "$MODULE/src/plaits/mi" -name '*.cc' | sort)
for f in "$MODULE"/src/phrase/*.c; do
  obj="$OBJ/phrase_$(basename "$f").o"
  "$CC" "${PHRASE[@]}" -c "$f" -o "$obj"
  objs+=("$obj")
done
for f in "$MODULE"/src/dsp_plugin.cpp "$MODULE"/src/wrapper/*.cpp "$MODULE"/src/plaits/*.cpp; do
  compile OURS "$f"
done
"$CC" -shared -o "$OUT/dsp.so" "${objs[@]}" -Wl,-z,defs -Wl,-Bsymbolic -Wl,--gc-sections -lm

cp "$MODULE/module.json" "$OUT/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so"
  for f in ui_chain.js ui_core.mjs ui_params.mjs help.json README.md LICENSE THIRD_PARTY_LICENSES.md; do
    cp "$MODULE/$f" "$OUT/"
  done
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/dsp.so"
