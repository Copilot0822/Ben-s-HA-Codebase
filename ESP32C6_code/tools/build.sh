#!/usr/bin/env bash
# Run with bash tools/build.sh from Linux/WSL. Stage on Linux: repo path has spaces/apostrophe.
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="${SDP810_SDK_ROOT:-$HOME/esp}"
stage_dir="${SDP810_BUILD_DIR:-$HOME/esp/projects/ESP32C6_code}"
set +u # Espressif export scripts inspect optional, unset environment variables.
source "$sdk_root/esp-idf/export.sh"
source "$sdk_root/esp-matter/export.sh"
set -u
export IDF_CCACHE_ENABLE=1
source "$source_dir/tools/sdk-versions.env"
[[ "$(git -C "$sdk_root/esp-idf" rev-parse HEAD)" == "$IDF_REV" ]]
[[ "$(git -C "$sdk_root/esp-matter" rev-parse HEAD)" == "$MATTER_REV" ]]
[[ "$(git -C "$sdk_root/esp-matter/connectedhomeip/connectedhomeip" rev-parse HEAD)" == "$CHIP_REV" ]]
python3 "$source_dir/tools/stage_project.py" "$source_dir" "$stage_dir"
cd "$stage_dir"
g++ -std=c++17 -Wall -Wextra -Werror -I main tests/pressure_math_test.cpp -o /tmp/sdp810_pressure_math_test
/tmp/sdp810_pressure_math_test
# SDKCONFIG lives in build/ so defaults are reapplied on every helper invocation.
# Use idf.py directly in stage_dir to retain personal menuconfig changes instead.
if [[ -f build/sdkconfig ]]; then
    mv build/sdkconfig build/sdkconfig.previous
fi
idf.py -D SDKCONFIG="$stage_dir/build/sdkconfig" -D IDF_TARGET=esp32c6 build
idf.py -D SDKCONFIG="$stage_dir/build/sdkconfig" size
python3 "$source_dir/tools/verify_build.py" "$stage_dir/build"
mkdir -p "$source_dir/artifacts"
cp build/sdp810_thread_pressure.bin build/sdp810_thread_pressure.elf build/flasher_args.json "$source_dir/artifacts/"
cp build/bootloader/bootloader.bin "$source_dir/artifacts/bootloader.bin"
cp build/partition_table/partition-table.bin "$source_dir/artifacts/partition-table.bin"
cp build/ota_data_initial.bin "$source_dir/artifacts/ota_data_initial.bin"
cp build/sdp810_thread_pressure-ota.bin "$source_dir/artifacts/sdp810_thread_pressure-ota.bin"
cp build/sdkconfig "$source_dir/artifacts/sdkconfig.built"
cp build/verification.json "$source_dir/artifacts/verification.json"
cp dependencies.lock "$source_dir/dependencies.lock"
python3 "$source_dir/tools/generate_onboarding.py" --output "$source_dir/commissioning/qr-development.svg"
printf 'Built project: %s\nFlash: cd "%s" && idf.py -D SDKCONFIG="%s/build/sdkconfig" -p /dev/ttyACM0 flash monitor\n' "$stage_dir" "$stage_dir" "$stage_dir"
