#!/usr/bin/env bash
# Linux/WSL, after installing README's apt prerequisites. Does not require host tools.
set -eo pipefail
tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$tools_dir/sdk-versions.env"
sdk_root="${SDP810_SDK_ROOT:-$HOME/esp}"
mkdir -p "$sdk_root"

clone_pinned() {
    local name="$1" url="$2" branch="$3" revision="$4"
    if [[ ! -d "$sdk_root/$name/.git" ]]; then
        git clone --no-checkout --depth 1 --branch "$branch" "$url" "$sdk_root/$name"
        git -C "$sdk_root/$name" fetch --depth 1 origin "$revision"
        git -C "$sdk_root/$name" checkout --detach "$revision"
    fi
    if [[ "$(git -C "$sdk_root/$name" rev-parse HEAD)" != "$revision" ]]; then
        printf 'Existing %s does not match pinned revision. Use another SDP810_SDK_ROOT.\n' "$name" >&2
        exit 1
    fi
}
clone_pinned esp-idf https://github.com/espressif/esp-idf.git v5.5.5 "$IDF_REV"
git -C "$sdk_root/esp-idf" submodule update --init --recursive --depth 1 --jobs 8
(cd "$sdk_root/esp-idf" && ./install.sh esp32c6)
source "$sdk_root/esp-idf/export.sh"
clone_pinned esp-matter https://github.com/espressif/esp-matter.git release/v1.5 "$MATTER_REV"
git -C "$sdk_root/esp-matter" submodule update --init --depth 1 connectedhomeip/connectedhomeip
chip_dir="$sdk_root/esp-matter/connectedhomeip/connectedhomeip"
[[ "$(git -C "$chip_dir" rev-parse HEAD)" == "$CHIP_REV" ]]
(cd "$chip_dir" && ./scripts/checkout_submodules.py --platform esp32 --shallow)

# Use the official Pigweed bootstrap/package definitions, but omit host Python
# test packages (e.g. bluezoo, which needs Python >=3.11). Firmware builds use
# the IDF Python environment and esp-matter/requirements.txt. No SDK source edits.
mkdir -p "$chip_dir/.environment"
python3 "$tools_dir/prepare_bootstrap.py" "$chip_dir"
cd "$chip_dir"
export PW_CONFIG_FILE=.environment/sdp810-tools.json
export PW_ACTIVATE_SKIP_CHECKS=1 # No host Python/pw CLI; the firmware build verifies required tools.
source ./scripts/bootstrap.sh -p none
unset PW_ACTIVATE_SKIP_CHECKS
source "$sdk_root/esp-idf/export.sh"
cd "$sdk_root/esp-matter"
./install.sh --no-bootstrap --no-host-tool
source ./export.sh
python3 -m pip install 'python-stdnum==1.18' 'qrcode==8.2'
printf 'SDK setup complete: ESP-IDF v5.5.5 and pinned esp-matter release/v1.5.\n'
