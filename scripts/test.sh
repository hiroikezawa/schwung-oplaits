#!/usr/bin/env bash
# Vendored-source check, native build + every test: engine (ASan/UBSan),
# real dlopen smoke, UI, then the Move build and its ABI check when a cross
# compiler is present.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${OPLAITS_SKIP_FETCH:-0}" != 1 ]]; then
  bash "$MODULE/scripts/fetch_plaits.sh" --verify
fi
bash "$MODULE/scripts/build.sh" native
OUT="$MODULE/build/native"
GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-test"
mkdir -p "$OBJ"

SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
COMMON=(-O1 -g -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST -fno-exceptions -fno-rtti
        -I"$MODULE/src" -isystem "$MODULE/src/plaits/mi" -I"$GEN")
objs=()
# The vendored Plaits left-shifts negative values (fm/dx_units.h; defined on
# its GCC/ARM and here); UBSan's shift check would only report that, so it is
# off for those files alone. Everything else, ours included, and every other
# check on the vendored files (bounds, overflow, alignment...) runs.
while IFS= read -r f; do
  o="$OBJ/$(echo "${f#$MODULE/src/}" | tr '/' '_').o"
  g++ -std=gnu++11 -w -include stdio.h "${COMMON[@]}" "${SAN[@]}" -fno-sanitize=shift -c "$f" -o "$o"
  objs+=("$o")
done < <(find "$MODULE/src/plaits/mi" -name '*.cc' | sort)
for f in "$MODULE"/src/wrapper/*.cpp "$MODULE"/src/plaits/*.cpp "$MODULE"/tests/test_engine.cpp; do
  o="$OBJ/$(basename "$f").o"
  g++ -std=gnu++17 -Wall -Wextra -Werror "${COMMON[@]}" "${SAN[@]}" -c "$f" -o "$o"
  objs+=("$o")
done
for f in "$MODULE"/src/phrase/*.c; do
  o="$OBJ/phrase_$(basename "$f").o"
  gcc -std=c11 -O1 -g -Wall -Wextra -Werror "${SAN[@]}" -I"$MODULE/src/phrase" -I"$GEN" -c "$f" -o "$o"
  objs+=("$o")
done
g++ "${SAN[@]}" "${objs[@]}" -o "$OUT/test-engine" -lm
"$OUT/test-engine"

python3 "$MODULE/tests/smoke.py" "$OUT/dsp.so"

SCHWUNG="${SCHWUNG_ROOT:-$(cd "$MODULE/.." && pwd)/schwung}"
if [[ ! -f "$SCHWUNG/src/shared/param_pages/page_controller.mjs" ]]; then
  echo "SKIP: UI test needs a Schwung checkout (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  node "$MODULE/tests/test_ui.mjs"
elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
  node.exe "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
else
  echo "Node.js is required for the UI test" >&2; exit 1
fi

if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}g++" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
else
  echo "SKIP: no aarch64 cross compiler; Move build and ABI not checked" >&2
fi
