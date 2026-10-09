#!/usr/bin/env bash
# Build Refract's performance changes against a recorded Digitalis revision.
set -eo pipefail
aosp=${1:?usage: build_digitalis.sh /path/to/aosp [jobs]}
jobs=${2:-4}
case "$jobs" in ''|*[!0-9]*|0) echo 'jobs must be positive' >&2; exit 2;; esac
patch_dir=$(cd "$(dirname "$0")" && pwd)
cd "$aosp"
test -f build/envsetup.sh
translator=frameworks/libs/binary_translation
revision=9e7f4407e0e32a582fe2a56f87492443ca55d6fa
if [ "$(git -C "$translator" rev-parse HEAD)" != "$revision" ]; then
  test -z "$(git -C "$translator" status --porcelain)" || { echo 'Translator has local changes; refusing checkout' >&2; exit 1; }
  git -C "$translator" fetch --depth=1 https://github.com/DigitalisX64/platform_frameworks_libs_binary_translation.git "$revision"
  git -C "$translator" checkout --detach "$revision"
fi
if ! git -C "$translator" apply --reverse --check "$patch_dir/digitalis-axrb-performance.patch" 2>/dev/null; then
  git -C "$translator" apply --check "$patch_dir/digitalis-axrb-performance.patch"
  git -C "$translator" apply "$patch_dir/digitalis-axrb-performance.patch"
fi
git -C "$translator" diff --check
support=frameworks/libs/native_bridge_support
test "$(git -C "$support" rev-parse HEAD)" = ffd3f17ebbb244c01c274b6f527d016efc393813 || {
  echo 'Unexpected native_bridge_support revision' >&2; exit 1;
}
if ! git -C "$support" apply --reverse --check "$patch_dir/native-bridge-tls-compat.patch" 2>/dev/null; then
  git -C "$support" apply --check "$patch_dir/native-bridge-tls-compat.patch"
  git -C "$support" apply "$patch_dir/native-bridge-tls-compat.patch"
fi
git -C "$support" diff --check
bionic=bionic
test "$(git -C "$bionic" rev-parse HEAD)" = f22516cbc67e81c13cf943ce02f01b8929a98726 || {
  echo 'Unexpected Bionic revision' >&2; exit 1;
}
if ! git -C "$bionic" apply --reverse --check "$patch_dir/bionic-api36-compat.patch" 2>/dev/null; then
  git -C "$bionic" apply --check "$patch_dir/bionic-api36-compat.patch"
  git -C "$bionic" apply "$patch_dir/bionic-api36-compat.patch"
fi
git -C "$bionic" diff --check
export BUILD_USERNAME=axrb-digitalis BUILD_HOSTNAME=axrb-digitalis
export ALLOW_MISSING_DEPENDENCIES=true
export TARGET_PRODUCT=sdk_phone64_x86_64_digitalis TARGET_RELEASE=trunk_staging TARGET_BUILD_VARIANT=userdebug
source build/envsetup.sh >/dev/null
m -j"$jobs" berberis_arm64_host_tests
out/host/linux-x86/nativetest64/berberis_arm64_host_tests/berberis_arm64_host_tests
# The upstream module-name filter omits native_bridge guest libraries. Build
# the canonical installed artifacts directly, including the guest libgui stub.
product_out=$(get_build_var PRODUCT_OUT)
mapfile -t artifacts < <(sed -n '/^BERBERIS_DISTRIBUTION_ARTIFACTS_ARM64 :=/,/^# endregion/p' \
  "$translator/berberis_config.mk" | grep -oE 'system/[^ \\]+')
test "${#artifacts[@]}" -gt 0
for i in "${!artifacts[@]}"; do artifacts[$i]="$product_out/${artifacts[$i]}"; done
m -j"$jobs" "${artifacts[@]}"
bash digitalis/scripts/build-and-package-prebuilts.sh --collect-only --username axrb-digitalis
bash digitalis/scripts/verify-digitalis-prebuilts.sh
repo manifest -r -o digitalis/dist/axrb-source-manifest.xml
sha256sum "$patch_dir/digitalis-axrb-performance.patch" > digitalis/dist/axrb-patch.sha256
bundle=digitalis/dist/digitalis-prebuilts
mkdir -p "$bundle/axrb-source"
cp digitalis/dist/axrb-source-manifest.xml "$bundle/axrb-source/manifest.xml"
cp "$patch_dir/digitalis-axrb-performance.patch" "$bundle/axrb-source/"
cp "$patch_dir/native-bridge-tls-compat.patch" "$bundle/axrb-source/"
cp "$patch_dir/bionic-api36-compat.patch" "$bundle/axrb-source/"
cp "$patch_dir/../../licenses/Refract-MIT.txt" "$bundle/axrb-source/"
python3 "$patch_dir/collect_notices.py" "$aosp" "$bundle"
( cd "$bundle"; find . -type f ! -name SHA256SUMS -printf '%P\0' | sort -z | xargs -0 sha256sum > SHA256SUMS )
tar -C digitalis/dist -czf digitalis/dist/digitalis-prebuilts-axrb.tar.gz digitalis-prebuilts
( cd digitalis/dist; sha256sum digitalis-prebuilts-axrb.tar.gz > digitalis-prebuilts-axrb.tar.gz.sha256 )
