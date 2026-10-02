"""Generate exact firmware onboarding codes using the pinned SDK's official code."""
import argparse
import os
from pathlib import Path
import re
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=Path("commissioning/qr-development.svg"))
args = parser.parse_args()
project = Path(__file__).resolve().parent.parent
matter = Path(os.environ["ESP_MATTER_PATH"])
sys.path.insert(0, str(matter / "connectedhomeip/connectedhomeip/src/setup_payload/python"))
from SetupPayload import SetupPayload  # Official ConnectedHomeIP encoder + Verhoeff checksum.
import qrcode
from qrcode.image.svg import SvgPathImage

header = (project / "main/CHIPProjectConfig.h").read_text()
defaults = (project / "sdkconfig.defaults").read_text()

def number(text, key):
    match = re.search(rf"^(?:#define\s+)?{key}(?:\s+|=)(0x[0-9A-Fa-f]+|[0-9]+)\s*$", text, re.M)
    if not match:
        raise ValueError(f"Cannot find {key}")
    return int(match.group(1), 0)

payload = SetupPayload(
    number(header, "CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR"),
    number(header, "CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE"),
    rendezvous=2, flow=0,
    vid=number(defaults, "CONFIG_DEVICE_VENDOR_ID"),
    pid=number(defaults, "CONFIG_DEVICE_PRODUCT_ID"),
)
qr = payload.generate_qrcode()
manual = payload.generate_manualcode()
decoded = SetupPayload.parse(qr)
assert decoded.pincode == payload.pincode and decoded.long_discriminator == payload.long_discriminator
assert SetupPayload.parse(manual).pincode == payload.pincode
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
qrcode.make(qr, image_factory=SvgPathImage, box_size=10, border=4).save(output)
output.with_suffix(".txt").write_text(
    f"DEVELOPMENT credentials\nQR payload: {qr}\nManual pairing code: {manual}\n"
    f"Setup passcode: {payload.pincode}\nDiscriminator: {payload.long_discriminator}\n"
    f"VID: 0x{payload.vid:04X}\nPID: 0x{payload.pid:04X}\nDiscovery: BLE (2)\n",
)
print(f"QR payload: {qr}\nManual pairing code: {manual}\nQR image: {output}")
