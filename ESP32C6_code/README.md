# SDP810 differential pressure sensor: Matter over Thread on ESP32-C6

This is an ESP-IDF / Espressif esp-matter project for an **ESP32-C6-WROOM-1 N4 (4 MB)** and **Sensirion SDP810-125Pa**. BLE commissions the device; its native IEEE 802.15.4 radio carries Matter over Thread during operation. The existing **IKEA DIRIGERA is the Thread Border Router**. Home Assistant is the intended Matter controller and sensor consumer. No Wi-Fi SSID, password, connection or fallback is used by the firmware.

The pressure entity deliberately uses fake units: its kPa-labelled numeric value represents differential **Pa**. Read the scaling section before using it in automations.

## Hardware and wiring

Required: ESP32-C6-WROOM-1 development board with 4 MB flash, SDP810-125Pa, regulated 5 V supply for the board, USB data cable, jumper wires, tubing as appropriate, and two approximately 4.7 kΩ I²C pullups if not already fitted. DIRIGERA must have Thread enabled, and Home Assistant needs its Matter integration and a commissioning phone with BLE and the correct Thread credentials.

| ESP32-C6 connection | SDP810 connection | Notes |
| --- | --- | --- |
| 3V3 | VDD | Sensor pin 2 |
| GND | GND | Sensor pin 3; common supply ground |
| GPIO6 | SDA | Sensor pin 4; pull up to **3V3** |
| GPIO7 | SCL | Sensor pin 1; pull up to **3V3** |
| Board 5V | — | Regulated external **5 V positive**, board power input |
| Board GND | — | External supply ground |

The sensor pin numbers above are from the datasheet's **bottom view**. Check the connector orientation rather than assuming wire order. Supply the sensor from 3V3, with a 100 nF bypass capacitor near VDD/GND if the wiring/module lacks one. Keep I²C leads short. The seven-bit I²C address is **0x25**, not 0x4A/0x4B.

GPIO numbers are not physical header positions. GPIO6/7 are exposed on the usual C6 DevKit headers; they avoid native USB GPIO12/13, boot strapping GPIO8/9/15, UART0 GPIO16/17 and the module's flash connections. They are also the external JTAG signal pins: do not attach an external GPIO JTAG probe to them while using this wiring; native USB Serial/JTAG remains available. Thread uses the internal radio, with no external GPIO assignment. Check your particular development board schematic for added peripherals.

GPIO9 is the active-low BOOT button on common Espressif C6 development boards. If yours lacks it, connect a normally open momentary button from GPIO9 to GND. All pins, timings and the sign override are in **`main/app_config.h`**. BOOT remains a boot strap: hold the reset button only **after firmware has booted**, not during power-on.

Use your board's documented power arrangement when connecting external 5 V and USB together; avoid backfeeding a USB supply on boards without power-source isolation. The firmware's default serial console is the **native USB Serial/JTAG connector**, not a separate USB-to-UART connector. You can choose UART0 instead in `idf.py menuconfig` if required by your board.

## Pressure representation and filtering

The driver sends Sensirion command **0x3615** once to start continuous measurement with differential-pressure compensation and average-until-read. It reads nine bytes at 10 Hz, validates each of the three CRC bytes, decodes signed two's-complement pressure and temperature, and divides pressure counts by the sensor-provided scale factor. The 125 Pa model normally returns **240 counts/Pa**; a different factor is logged so an accidentally purchased 500 Pa model is visible. It does not take an absolute value or force the pressure positive.

A five-sample moving average covers approximately 0.5 seconds with about 0.2 seconds of group delay. The latest unfiltered physical reading remains in `raw_reading` for debugging. The default polarity follows the sensor's ports; swapping the tubing reverses the sign. If intentional reversal is needed, set `kPressureSign` to `-1.0f`.

The endpoint is the official **Pressure Sensor device type 0x0305**, with **Pressure Measurement cluster 0x0403**, **MeasuredValue attribute 0x0000**. MeasuredValue is nullable **signed int16**. MinMeasuredValue is -1250 and MaxMeasuredValue is +1250. The optional extended pressure scaling feature is not enabled.

**The only application-to-Matter conversion is:**

```text
MeasuredValue = lround(clamp(filtered_pressure_Pa, -125, +125) * 10)
Home Assistant native value = MeasuredValue / 10
```

`lround` rounds nearest, with exact half ties away from zero. Clamping affects only values outside the sensor's calibrated ±125 Pa range; raw readings remain available. There is **no division by 1000 and no extra Pa/kPa conversion** in the firmware.

| Actual/filtered pressure | Matter MeasuredValue | HA native kPa-labelled number, interpreted as Pa |
| ---: | ---: | ---: |
| -125 Pa | -1250 | -125.0 |
| -5.27 Pa | -53 | -5.3 |
| -1.04 Pa | -10 | -1.0 |
| 0 Pa | 0 | 0.0 |
| +1.46 Pa | 15 | 1.5 |
| +2.37 Pa | 24 | 2.4 |
| +8.92 Pa | 89 | 8.9 |
| +125 Pa | 1250 | 125.0 |

Matter normally specifies this integer in 0.1 kPa units. Home Assistant's pressure entity accordingly divides by 10 and labels it kPa. We intentionally repurpose that representation to retain useful **0.1 Pa numeric resolution**. These are intentionally false physical units on the Matter wire. Other Matter controllers will also see inflated physical pressure if they take the standard units literally.

Keep the original HA entity's display unit set to **kPa** when using this numeric convention. Selecting another pressure unit can trigger HA's normal unit conversion and change the number. For an entity labelled Pa, create a Home Assistant template sensor that copies the native kPa-labelled number and assigns `unit_of_measurement: "Pa"`; **do not multiply by 1000**. Carry over availability so null/stale readings are not converted to zero.

## Matter, Thread and DIRIGERA

Endpoint 0 is esp-matter's standard root node, including Thread Network Commissioning, operational credentials and the usual commissioning/security clusters. Endpoint 1 is the pressure sensor. The implementation follows Espressif's official light example's node/start/OpenThread launcher sequence, using the same `ESP_OPENTHREAD_DEFAULT_*_CONFIG` macros and the current `endpoint::pressure_sensor::create` API.

The device advertises Matter commissioning over **BLE**. The commissioner securely transfers a Thread operational dataset and fabric credentials. OpenThread joins that dataset, and encrypted Matter traffic travels over IPv6 through DIRIGERA to Home Assistant. The SDK persists commissioning data in NVS and handles normal Thread detach/reattach and Matter sessions after interruptions. No Thread dataset is hard-coded. The device is always powered and does not use sleepy-device polling.

DIRIGERA's Border Router role is independent of its Matter controller/bridge roles. Adding DIRIGERA's Zigbee bridge to HA does **not** automatically supply its Thread dataset to the phone or HA. Use the existing DIRIGERA network's credentials for commissioning. Home Assistant discovering a border router is not proof that it knows those credentials. Availability of dataset sharing/import depends on your IKEA app/hub, phone OS and Home Assistant versions. If that network's credentials cannot be provided to the commissioning phone, this is a setup prerequisite to resolve before BLE commissioning; the firmware cannot derive them from the border router's presence.

## Pinned and inspected SDK versions

The build uses the current `release/v1.5` Espressif source at these immutable revisions:

| Dependency | Version / commit |
| --- | --- |
| ESP-IDF | **v5.5.5**, `b774170ff46c393eeb5e495ea37936038d3f4f4f` |
| esp-matter | release/v1.5 (Matter 1.5.1), `ae9001236dddd3f5fd953bed1c4f483c1b9beba3` |
| Espressif ConnectedHomeIP submodule | `392b307067a10514615ea4fbd3edae5da5b25133` |
| Cross compiler | Espressif RISC-V GCC **14.2.0**, `esp-14.2.0_20260121` |

`tools/sdk-versions.env` holds the pins. `dependencies.lock` records the resolved IDF registry component versions. The build helper checks all three repository revisions. No Arduino, ESPHome, MQTT or custom transport is used.

## Install and build

Espressif supports Matter development on Linux/macOS and **Windows through WSL**. On this machine Ubuntu-22.04 is available. Build in its Linux filesystem because Matter tools and the repository's Windows path with spaces/apostrophe are a poor combination. `tools/build.sh` stages source into `~/esp/projects/ESP32C6_code`, retains incremental build products, and copies firmware back to this project's ignored `artifacts/` folder. The SDKs live in `~/esp/esp-idf` and `~/esp/esp-matter`, outside the Git repository.

In an Ubuntu WSL terminal, install prerequisites once:

```bash
sudo apt-get update
sudo apt-get install -y git wget curl flex bison gperf python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 unzip \
  build-essential pkg-config
cd "/mnt/c/Users/copil/Documents/GitHub/Ben's HA Codebase/ESP32C6_code"
bash tools/setup.sh
bash tools/build.sh
```

The setup helper uses official pinned submodules and Pigweed/CIPD package definitions. It omits unrelated host Python test packages, which require Python >=3.11 on this SDK and prevent the full default bootstrap from finishing under Ubuntu-22.04's Python 3.10. Firmware uses the IDF Python environment plus esp-matter's own requirements. This does not alter SDK source code. Setup refuses to replace an existing SDK at a different revision; set `SDP810_SDK_ROOT` to another directory if needed.

On this Windows machine, after setup, launch the complete build in PowerShell:

```powershell
wsl -d Ubuntu-22.04 -- bash "/mnt/c/Users/copil/Documents/GitHub/Ben's HA Codebase/ESP32C6_code/tools/build.sh"
```

The actual cross-build command executed by the helper is:

```bash
idf.py -D SDKCONFIG="$HOME/esp/projects/ESP32C6_code/build/sdkconfig" -D IDF_TARGET=esp32c6 build
```

The helper also runs the host pressure/CRC/filter regression test, `idf.py size`, and `tools/verify_build.py` to verify the effective transport, partitions and Matter OTA digest. It reapplies repository defaults on every invocation, backing up any prior staged config as `build/sdkconfig.previous`. To experiment with menuconfig and preserve it, work directly in the Linux staging directory and use the same `-D SDKCONFIG=...` argument with `idf.py menuconfig` and `idf.py build` instead of rerunning the helper. Change permanent defaults in repository source. Environment overrides `SDP810_SDK_ROOT` and `SDP810_BUILD_DIR` are supported; choose paths without spaces or apostrophes.

## Flash and serial monitor

For WSL, attach the board's **native USB Serial/JTAG** device to Ubuntu using [Microsoft's usbipd-win instructions](https://learn.microsoft.com/windows/wsl/connect-usb). Locate the resulting port (`/dev/ttyACM0` is typical) and ensure your user has serial access (`sudo usermod -aG dialout "$USER"`, then start a fresh login session). Substitute the actual device below.

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
cd ~/esp/projects/ESP32C6_code
idf.py -D SDKCONFIG="$PWD/build/sdkconfig" -p /dev/ttyACM0 flash
idf.py -D SDKCONFIG="$PWD/build/sdkconfig" -p /dev/ttyACM0 monitor
# Or both:
idf.py -D SDKCONFIG="$PWD/build/sdkconfig" -p /dev/ttyACM0 flash monitor
```

Exit the IDF monitor with **Ctrl+]**. If bootloader download mode is needed, hold BOOT, press/release EN/RESET, then release BOOT. USB may disconnect/reappear during flashing or reset. Reconnect the monitor and press EN to capture the startup/QR banner.

For Windows flashing without USB passthrough, install Python and `esptool==4.12.0` / `pyserial`, use the Windows COM port, and run from `ESP32C6_code/artifacts`:

```powershell
python -m pip install esptool==4.12.0 pyserial
python -m esptool --chip esp32c6 --port COM5 --baud 460800 write_flash --flash_size 4MB 0x0 bootloader.bin 0xC000 partition-table.bin 0x1D000 ota_data_initial.bin 0x20000 sdp810_thread_pressure.bin
python -m serial.tools.miniterm COM5 115200
```

The firmware prints over native USB Serial/JTAG. Select that COM port rather than the separate USB-to-UART COM port. The flash offsets above match this project's partition table. Flashing the bootloader/table/app normally preserves NVS; use factory reset for recommissioning. The project does not automatically flash any connected board.

## Onboarding codes and QR image

Development defaults are explicit in `main/CHIPProjectConfig.h` and `sdkconfig.defaults`:

| Setting | Value |
| --- | --- |
| Setup passcode | **20202021** |
| Long discriminator | **3840** / 0xF00 |
| VID / PID | **0xFFF1 / 0x8000** (test identity) |
| Commissioning flow | Standard (0) |
| Discovery capability | BLE (bitmask 2) |
| Manual pairing code | **34970112332** |
| QR payload | **`MT:Y.K9042C00KA0648G00`** |

These codes are generated with the SDK encoder, not a hand-written payload. The build helper creates **`commissioning/qr-development.svg`** and its `.txt` metadata file. Open/print the SVG and scan it with the commissioning app. The device also obtains QR/manual codes from CHIP's active CommissionableDataProvider and prints them on every boot; use the serial output as the authoritative code after changing credentials/providers.

Espressif's included ConnectedHomeIP tool generates the exact development strings:

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
python3 "$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/src/setup_payload/python/SetupPayload.py" \
  generate -d 3840 -p 20202021 --vendor-id 65521 --product-id 32768 -cf 0 -dm 2
```

To regenerate the SVG from this project's configured values:

```bash
cd "/mnt/c/Users/copil/Documents/GitHub/Ben's HA Codebase/ESP32C6_code"
python3 tools/generate_onboarding.py
```

Changing the passcode requires changing its **matching SPAKE2+ verifier** too. Generate one with the official tool, then update the passcode/verifier in `main/CHIPProjectConfig.h`, rebuild, flash, reset and regenerate the QR:

```bash
python3 "$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/scripts/tools/spake2p/spake2p.py" \
  gen-verifier -p 20202021 -s U1BBS0UyUCBLZXkgU2FsdA== -i 1000
```

Replace `20202021` in that command with the new valid Matter passcode. If changing salt/iteration count, update both the command and header. Changing only the long discriminator does not require a new verifier. These shared test DACs/setup values are for development. Controllers may require accepting a development/uncertified-device prompt; some ecosystems reject test attestation entirely. For a production device use unique factory provisioning and certified identity rather than changing VID/PID alone. The reserved `fctry` partition accommodates future provisioning; it is not used by the default test providers.

## Commission into Home Assistant

1. Power the wired board and confirm sensor initialization and the QR banner in the monitor. BLE commissioning works even when the sensor is absent.
2. Keep DIRIGERA powered, connected to your IPv6-capable LAN, updated and operating its existing Thread network. Ensure the commissioning phone can supply **that same network's Thread credentials**. Follow Home Assistant's Thread credential import/sync guidance for the phone platform.
3. Enable Home Assistant's Matter integration. In its companion app choose to add a **new Matter device**, scan this project's QR (or enter manual code **34970112332**), and accept a development-attestation prompt if offered. Keep the phone close to the C6 during BLE provisioning.
4. Check serial output for commissioning complete, a stored Thread dataset, `attached=yes`, and `fabrics=1` or higher. HA should discover the pressure entity. Confirm a small negative and positive pressure before using automations.
5. If already commissioned into another compatible controller, use that controller's **Share/add another ecosystem** operation and its fresh pairing code to add HA. The original boot QR does not reopen an already commissioned device. The IKEA app's support for this custom pressure device/test identity must be checked on actual hardware; DIRIGERA can still serve purely as its Thread Border Router.

## Factory reset

After the firmware boots, hold **BOOT (GPIO9) for at least five seconds**, wait for `Factory reset armed`, then **release**. The SDK's `esp_matter::factory_reset()` removes Matter fabrics, esp-matter persisted data and the Thread dataset, then reboots. The next boot is uncommissioned and advertises commissioning again. EN alone restarts while preserving fabrics/dataset.

If NVS is corrupt and the application cannot start, USB recovery is available (this erases the entire flash, including all commissioning/provisioning data):

```bash
idf.py -D SDKCONFIG="$PWD/build/sdkconfig" -p /dev/ttyACM0 erase-flash
idf.py -D SDKCONFIG="$PWD/build/sdkconfig" -p /dev/ttyACM0 flash monitor
```

## Expected serial output

Illustrative output, not a claim of physical hardware validation:

```text
Firmware sdp810_thread_pressure version 1.0.0; ESP-IDF v5.5.5; ESP32-C6, 4 MB, Matter over Thread + BLE
Wiring: sensor 3V3/GND, SDA=GPIO6 SCL=GPIO7 I2C=0x25; reset=GPIO9
Scaling: MeasuredValue=lround(filtered Pa*10); HA kPa-labelled number intentionally means Pa
SDP810 initialization: ESP_OK (I2C 0x25 SDA=6 SCL=7)
SDP810 scale factor: 240 counts/Pa (125 Pa model normally 240)
Pressure sensor endpoint 1; Pressure Measurement cluster 0x0403
Matter initialization: ESP_OK; native IEEE 802.15.4 Thread; Wi-Fi disabled
DEVELOPMENT setup passcode: 20202021; discriminator: 3840
Matter QR payload: MT:Y.K9042C00KA0648G00
Manual pairing code: 34970112332
Thread: dataset=absent attached=no; commissioning: fabrics=0 window=open
Factory reset: hold BOOT for 5000 ms after boot, then release
Matter commissioning complete
Thread: dataset=stored attached=yes; commissioning: fabrics=1 window=closed
Raw SDP810: 2.37 Pa | Filtered: 2.34 Pa | Matter MeasuredValue: 23
```

Pressure debug lines appear every **10 seconds**; status snapshots every **30 seconds**, plus connection/commissioning events. Matter attributes update about once per second. Routine CHIP logging is limited to errors to avoid per-sample chatter; use its progress log level in menuconfig when diagnosing commissioning. Measurements are RAM-only; ordinary pressure updates do not write flash.

One failed I²C transaction is logged at a bounded interval and does not reboot the device. Ten consecutive failed samples suspend reads and trigger reinitialization every five seconds. A reading older than three seconds becomes **Matter null**; sensor recovery clears the filter before reporting new values. Hardware timing, phone/controller subscription behaviour and network conditions can affect the final HA update cadence.

## Flash layout and OTA

`partitions.csv` fits entirely in **0x400000 bytes / 4 MiB**:

| Partition | Offset | Size |
| --- | ---: | ---: |
| Partition table | 0xC000 | 4 KiB reserved |
| nvs | 0xD000 | 64 KiB |
| otadata | 0x1D000 | 8 KiB |
| phy_init | 0x1F000 | 4 KiB |
| ota_0 | 0x20000 | **0x1E0000 / 1.875 MiB** |
| ota_1 | 0x200000 | **0x1E0000 / 1.875 MiB** |
| fctry | 0x3E0000 | 128 KiB, reserved for future factory provisioning |

The initial flash installs `ota_0` and empty OTA metadata. **Matter OTA Requestor is enabled**; esp-matter adds/initializes the standard OTA client/server clusters on endpoint 0. ESP-IDF's build rejects an application exceeding either slot. See `BUILD_REPORT.md` for the measured final size and free space; do not assume future SDK versions will fit without repeating this check.

Bootloader rollback is enabled. After an OTA reboot, the app confirms a pending image only after successful Matter initialization and ten seconds of sampling-task operation (`kOtaConfirmUptimeMs`). A boot crash or reset before confirmation allows the bootloader to revert on the next boot. An unplugged external sensor or unavailable border router does not by itself classify the new firmware as broken. This is a startup health policy, not a guarantee of application behaviour under every fault; verify an update/rollback cycle on your board before depending on OTA remotely.

Each build generates both `artifacts/sdp810_thread_pressure.bin` (raw ESP flash image) and **`artifacts/sdp810_thread_pressure-ota.bin`** (the official Matter header plus payload, VID/PID, version and SHA-256 digest). Supply the latter to a Matter OTA Provider on the same fabric. It is **not** an HTTP/Wi-Fi update service. DIRIGERA routes the Thread traffic; it does not automatically host your custom firmware images. HA's available OTA workflows/controller support may vary, and this project does not publish images to any registry.

For an update, increase **both `PROJECT_VER_NUMBER` and `PROJECT_VER`** in `CMakeLists.txt`, rebuild, check image fit, and use a compatible controller/OTA Provider to announce the new image. The official [Linux OTA Provider instructions](https://github.com/espressif/connectedhomeip/blob/392b307067a10514615ea4fbd3edae5da5b25133/examples/ota-provider-app/linux/README.md) cover building/commissioning the provider and its required ACLs. The host provider/chip-tool binaries are outside this firmware-only setup. Once you have built and commissioned that provider, its image argument is:

```bash
chip-ota-provider-app --filepath /absolute/path/sdp810_thread_pressure-ota.bin
```

Use the actual executable path from the provider build. Follow its controller instructions to announce the provider to this requestor; do not overwrite an existing provider ACL list without retaining its existing entries. Actual OTA transfer, interrupted-update recovery and rollback still require physical testing. USB flashing remains the recovery and development path.

## Troubleshooting

| Symptom | Checks and recovery |
| --- | --- |
| **SDP810 not detected** | Confirm 3.3 V at VDD, common ground, bottom-view connector pinout, GPIO6/7 (not header positions), SDA/SCL not swapped, both pullups to 3V3, short leads and sensor model/address 0x25. Check the logged ESP error and repeated initialization attempts. Network commissioning remains usable while the sensor is missing. |
| **CRC/read failures** | Check supply stability, grounding, cable length and pullups. No failed frame enters the filter. Disconnect/reconnect or power-cycle a sensor whose bus is physically stuck; the firmware keeps retrying. |
| **Thread network not joined** | `dataset=absent` means the commissioner has not supplied Thread credentials. `dataset=stored attached=no` means attachment is pending or unavailable: verify DIRIGERA/network reachability, distance, 2.4 GHz radio interference and matching network credentials. Give OpenThread time to reattach after an interruption. Do not reset merely because the border router temporarily loses power. |
| **Matter commissioning failure** | Use the native USB monitor to read the actual boot QR/manual code. Check BLE permissions, phone proximity, HA Matter integration, correct Thread credential sync, IPv6 and local mDNS reachability. Test credentials can trigger an uncertified-device warning or rejection. After a fail-safe timeout, retry with an open commissioning window; reboot an uncommissioned board or factory reset if needed. |
| **Device already commissioned** | `fabrics>0` with window closed is normal. Use the existing controller's sharing flow and new pairing code, or hold/release BOOT for a factory reset. Remove stale entries in the old controller when appropriate. |
| **Incorrect pressure sign** | Swap the sensor's pressure-port tubing or deliberately set `kPressureSign=-1.0f`. Compare raw and filtered logs; the driver decodes signed counts, and Matter is signed int16. Do not use unsigned attribute values or absolute values. |
| **Incorrect pressure scaling** | Confirm serial MeasuredValue equals `round(filtered Pa*10)`. The native HA kPa-labelled value should be MeasuredValue/10. Keep that HA display unit kPa for the hack; use a numeric-copy template for Pa. Check `240 counts/Pa` for the 125 Pa model. Avoid an additional ×1000 or ÷1000 anywhere. |
| **No serial output** | Use the C6 native USB connector/port and a data cable; reopen the port after reset. If your board exposes only USB-UART, select UART console in sdkconfig and rebuild. USB GPIO12/13 must remain free. |
| **Build cannot find GN/ZAP or host bootstrap fails** | Run the pinned `tools/setup.sh` helper; it uses official package pins without Python host tests. Source both SDK exports, build on Linux storage, and check SDK revision pins. Avoid cloning/building Matter under a Windows directory with spaces. |
| **NVS initialization fails** | NVS is intentionally not silently erased on boot. Diagnose the error, then use USB `erase-flash` and flash again if loss of commissioning data is acceptable. |

## Files and validation

`main/app_main.cpp` contains the Matter node, pressure publication, commissioning banner, status, reset and OTA health checks. `main/sdp810.*` implements IDF I²C transactions. `main/pressure_math.h` holds the CRC, signed decode, moving average and intentional scaling. `main/thread_config.h` contains the official example's native-radio platform defaults. `sdkconfig.defaults` selects C6/Thread/BLE/OTA/test identity; `sdkconfig.clusters` limits compiled clusters to the root node, OTA and pressure sensor plus the SDK's Binding implementation dependency. `tools/` pins/setup/builds the SDK and generates the QR. `tests/pressure_math_test.cpp` checks required conversions, signed wire readings, CRC corruption, invalid scale factors and filter recovery. `BUILD_REPORT.md` records the actual cross-build result.

Hardware work remaining after compilation: wire/power the device, flash it, validate sensor polarity/zero/full-range readings, commission with DIRIGERA's Thread credentials, check HA's displayed unit/number, test BOOT reset, interrupt/restore DIRIGERA power to verify reconnection, and test OTA/rollback with a provider if you intend to use it. There is no claim that physical BLE commissioning, OTA transfer or radio/sensor behaviour was exercised without that hardware.

## Primary references

- [Pinned Espressif esp-matter source and recommended IDF version](https://github.com/espressif/esp-matter/tree/ae9001236dddd3f5fd953bed1c4f483c1b9beba3)
- [Official C6 Thread configuration](https://github.com/espressif/esp-matter/blob/ae9001236dddd3f5fd953bed1c4f483c1b9beba3/examples/light/sdkconfig.defaults.c6_thread) and [application initialization example](https://github.com/espressif/esp-matter/blob/ae9001236dddd3f5fd953bed1c4f483c1b9beba3/examples/light/main/app_main.cpp)
- [Official pressure endpoint API](https://github.com/espressif/esp-matter/blob/ae9001236dddd3f5fd953bed1c4f483c1b9beba3/components/esp_matter/data_model/esp_matter_endpoint.h)
- [Sensirion SDP8xx digital datasheet](https://sensirion.com/file/datasheet_sdp800-d/)
- [Official ConnectedHomeIP setup payload encoder](https://github.com/espressif/connectedhomeip/blob/392b307067a10514615ea4fbd3edae5da5b25133/src/setup_payload/python/README.md)
- [Home Assistant Matter pressure conversion](https://github.com/home-assistant/core/blob/dev/homeassistant/components/matter/sensor.py) and [Thread credentials guidance](https://www.home-assistant.io/integrations/thread/)
- [IKEA's DIRIGERA/Matter/Thread explanation](https://www.ikea.com/gb/en/product-guides/ikea-home-smart-system/matter-smart-home-systems-pub7ab815e0/)
