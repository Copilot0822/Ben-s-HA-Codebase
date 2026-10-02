"""Verify the built transport, N4 partition capacity and official Matter OTA header."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
args = parser.parse_args()
build = args.build.resolve()
sdk_root = Path(os.environ.get("SDP810_SDK_ROOT", str(Path.home() / "esp")))

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result

config = dict(line.split("=", 1) for line in (build / "sdkconfig").read_text().splitlines()
              if line.startswith("CONFIG_") and "=" in line)
for key in ("IDF_TARGET_ESP32C6", "ESPTOOLPY_FLASHSIZE_4MB", "ENABLE_MATTER_OVER_THREAD",
            "ENABLE_CHIPOBLE", "OPENTHREAD_RADIO_NATIVE", "ENABLE_OTA_REQUESTOR",
            "BOOTLOADER_APP_ROLLBACK_ENABLE"):
    if config.get("CONFIG_" + key) != "y":
        raise SystemExit(f"Required effective build configuration missing: {key}")
if config.get("CONFIG_ENABLE_WIFI_STATION") == "y":
    raise SystemExit("Wi-Fi station must remain disabled")
gn = (build / "esp-idf/chip/args.gn").read_text()
for setting in ("chip_enable_wifi = false", "chip_enable_openthread = true", "chip_enable_chipoble = true"):
    if setting not in gn:
        raise SystemExit(f"CHIP build setting mismatch: {setting}")

partitions = module("sdp810_partition_tool", sdk_root / "esp-idf/components/partition_table/gen_esp32part.py")
partitions.offset_part_table = 0xC000
table = partitions.PartitionTable.from_binary((build / "partition_table/partition-table.bin").read_bytes())
table.verify()
table.verify_size_fits(0x400000)
binary = (build / "sdp810_thread_pressure.bin").read_bytes()
slots = [table.find_by_name(name) for name in ("ota_0", "ota_1")]
if any(slot is None or slot.type != 0 or len(binary) > slot.size for slot in slots):
    raise SystemExit("The application must fit both OTA slots")

ota_tool = module("sdp810_ota_tool", sdk_root / "esp-matter/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py")
ota_path = build / "sdp810_thread_pressure-ota.bin"
header_args = SimpleNamespace(image_file=ota_path)
magic, total_size, header_size, header = ota_tool.parse_header(header_args)
ota = ota_path.read_bytes()
payload = ota[struct.calcsize(ota_tool.FIXED_HEADER_FORMAT) + header_size:]
tag = ota_tool.HeaderTag
if (magic != ota_tool.HEADER_MAGIC or total_size != len(ota) or payload != binary or
        header[tag.PAYLOAD_SIZE] != len(binary) or header[tag.DIGEST_TYPE] != 1 or
        header[tag.DIGEST] != hashlib.sha256(binary).digest()):
    raise SystemExit("Matter OTA header/payload/digest validation failed")
for key, field in (("CONFIG_DEVICE_VENDOR_ID", tag.VENDOR_ID), ("CONFIG_DEVICE_PRODUCT_ID", tag.PRODUCT_ID)):
    if header[field] != int(config[key], 0):
        raise SystemExit(f"OTA identity mismatch: {key}")

flash = json.loads((build / "flasher_args.json").read_text())
assert flash["extra_esptool_args"]["chip"] == "esp32c6"
assert flash["flash_settings"]["flash_size"] == "4MB"
assert flash["otadata"]["offset"] == "0x1d000"
report = {
    "target": "esp32c6", "transport": "Thread", "commissioning": "BLE", "wifi_enabled": False,
    "flash_bytes": 0x400000, "application_bytes": len(binary),
    "ota_slot_bytes": [slot.size for slot in slots], "ota_slot_free_bytes": [slot.size - len(binary) for slot in slots],
    "application_sha256": hashlib.sha256(binary).hexdigest(), "matter_ota_sha256": hashlib.sha256(ota).hexdigest(),
    "ota_version": header[tag.VERSION], "ota_version_string": header[tag.VERSION_STRING],
    "vendor_id": header[tag.VENDOR_ID], "product_id": header[tag.PRODUCT_ID],
    "partition_end": table.flash_size(), "ota_payload_digest_verified": True,
}
(build / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
