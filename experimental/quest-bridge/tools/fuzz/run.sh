#!/bin/sh
# Generates tests, runs them on the headset and in the interpreter, compares.
#   tools/fuzz/run.sh <count> <seed>
set -e
COUNT=${1:-2000}
SEED=${2:-1}
ROOT=$(cd "$(dirname "$0")/../.." && pwd -W)
OUT=$ROOT/build/fuzz
NDK=/c/Users/joshu/android-sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin
ADB=/c/Users/joshu/android-sdk/platform-tools/adb.exe
export MSYS_NO_PATHCONV=1

python "$ROOT/tools/fuzz/gen.py" "$COUNT" "$SEED" "$OUT"
FLAGS="--target=aarch64-linux-android29 -O1 -I $OUT"
"$NDK/clang.exe" $FLAGS -DFUZZ_NATIVE "$ROOT/tools/fuzz/fuzz.c" "$OUT/fuzz_tests.S" -o "$OUT/fuzz_native"
"$NDK/clang.exe" $FLAGS -shared -fPIC -Wl,--unresolved-symbols=ignore-all "$ROOT/tools/fuzz/fuzz.c" "$OUT/fuzz_tests.S" -o "$OUT/libfuzz.so"

"$ADB" push "$OUT/fuzz_native" /data/local/tmp/qb_fuzz >/dev/null
"$ADB" shell "chmod 755 /data/local/tmp/qb_fuzz && /data/local/tmp/qb_fuzz" | tr -d '\r' > "$OUT/native.txt"
QB_SKIP_UNKNOWN=1 "$ROOT/build/Release/qb-test.exe" "$OUT/libfuzz.so" > "$OUT/guest.txt" 2> "$OUT/guest.err" || true
python "$ROOT/tools/fuzz/compare.py" "$OUT"
