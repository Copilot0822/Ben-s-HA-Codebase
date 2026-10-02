# Verified ESP32-C6 build

**Result: compiled and linked successfully for ESP32-C6 on October 2, 2026.** The final build includes Matter OTA Requestor and bootloader rollback, fits the N4 board's 4 MB flash, and produces the onboarding QR image. No physical board was flashed or tested.

## Toolchain

Build host: WSL2 Ubuntu-22.04, Python 3.10.12. ESP-IDF v5.5.5 (`b774170ff46c393eeb5e495ea37936038d3f4f4f`), esp-matter release/v1.5 / Matter 1.5.1 (`ae9001236dddd3f5fd953bed1c4f483c1b9beba3`), Espressif ConnectedHomeIP (`392b307067a10514615ea4fbd3edae5da5b25133`), RISC-V GCC 14.2.0 (`esp-14.2.0_20260121`). SDK repositories are outside this project, with no tracked source modifications. IDF registry versions are recorded in `dependencies.lock`.

Command from PowerShell:

```powershell
wsl -d Ubuntu-22.04 -- bash "/mnt/c/Users/copil/Documents/GitHub/Ben's HA Codebase/ESP32C6_code/tools/build.sh"
```

Underlying cross-build, from the Linux staging project after sourcing both SDK exports:

```bash
idf.py -D SDKCONFIG=/home/copil/esp/projects/ESP32C6_code/build/sdkconfig -D IDF_TARGET=esp32c6 build
idf.py -D SDKCONFIG=/home/copil/esp/projects/ESP32C6_code/build/sdkconfig size
```

## Final images and capacity

| Artifact / capacity | Bytes |
| --- | ---: |
| Application `.bin` | **1,544,768** / 0x179240 |
| Each OTA slot | **1,966,080** / 0x1E0000 |
| Free space in **each** slot | **421,312** / 0x66DC0 (21.43%) |
| Matter OTA envelope plus payload | **1,544,849** |
| Bootloader | **22,320** / 0x5730 |
| Bootloader free space before partition table | **26,832** / 0x68D0 |
| End of partition allocation | **4,194,304** / 0x400000 |
| Static DIRAM use from `idf.py size` | **184,746** (40.86% of the linker's 452,112 bytes) |
| Remaining static DIRAM capacity | **267,366** |

Runtime heap use also includes dynamically allocated Thread/BLE/Matter state; static linker capacity is not a measured runtime heap guarantee. ESP-IDF's checks passed for bootloader and both application slots. OTA is practical for this exact build with roughly 411 KiB of application growth space per slot; repeat size checks after SDK/features change.

Application SHA-256:

```text
54a766f31f2240cce399cb8368245cca93863a35b9e3ac8b059304f64fae662a
```

Matter OTA file SHA-256:

```text
6acb7396de7b4c3c88ad4cb2c3b09bbc423ad42c65cf2089f9cfe7a22e035fa5
```

## Verification performed

- Full ESP32-C6 cross-compilation and final link: **passed**. Build printed `Project build complete` and generated the raw ESP image and official Matter OTA image.
- Final SDK configuration and GN arguments: **Thread enabled**, **native radio**, **BLE commissioning enabled**, **Wi-Fi disabled**, **Matter OTA Requestor enabled**, **4 MB flash**, **rollback enabled**. The application also has a compile-time guard against a Wi-Fi transport build.
- Official IDF partition parser: binary table validated for alignment/overlap and allocation within 4 MB; raw application fits both slots.
- Official ConnectedHomeIP OTA parser: magic, size, VID/PID, version 1 / `1.0.0`, payload and SHA-256 digest verified. Extracted OTA payload exactly matches the flash `.bin`.
- Host C++ regression test: all requested positive/negative pressure mappings, ±125 Pa limits, half-way rounding, independent CRC test vector, corrupted CRC in each word, signed sensor wire decoding, invalid scale factors and filter recovery **passed**.
- Official ConnectedHomeIP setup-payload generator/parser: QR and manual codes generated and round-tripped, with BLE discovery and the configured discriminator/passcode.
- Shell helpers checked with `bash -n`; SDK setup, project staging and build/QR helpers executed successfully. The setup helper uses the official tool package pins with unrelated host Python testing dependencies omitted.

`tools/verify_build.py` automates the transport, partition and OTA integrity checks for future builds. The binary verification result is also saved in `artifacts/verification.json`. SDK Kconfig emits upstream warnings about a duplicate `SEC_CERT_DAC_PROVIDER` definition/choice; effective DAC and CommissionableData providers are the explicit test/example providers. These warnings did not prevent a successful build. No unknown project Kconfig settings remain in the final configuration.

## Commissioning values

- QR payload: **`MT:Y.K9042C00KA0648G00`**
- Manual pairing code: **34970112332**
- Setup passcode: **20202021**
- Long discriminator: **3840 / 0xF00**
- Development VID/PID: **0xFFF1 / 0x8000**
- QR image: [`commissioning/qr-development.svg`](commissioning/qr-development.svg)

## Project files

```text
ESP32C6_code/
  .gitignore
  README.md
  BUILD_REPORT.md
  CMakeLists.txt
  partitions.csv
  sdkconfig.defaults
  sdkconfig.clusters
  dependencies.lock
  main/
    CMakeLists.txt
    app_main.cpp
    app_config.h
    CHIPProjectConfig.h
    thread_config.h
    sdp810.h
    sdp810.cpp
    pressure_math.h
  tests/
    pressure_math_test.cpp
  tools/
    sdk-versions.env
    setup.sh
    prepare_bootstrap.py
    build.sh
    stage_project.py
    verify_build.py
    generate_onboarding.py
  commissioning/
    qr-development.svg
    qr-development.txt
  artifacts/                    # generated, ignored by Git
    sdp810_thread_pressure.bin
    sdp810_thread_pressure-ota.bin
    sdp810_thread_pressure.elf
    bootloader.bin
    partition-table.bin
    ota_data_initial.bin
    flasher_args.json
    sdkconfig.built
    verification.json
    build-output.log
```

Generated images/debug symbols and managed SDK/build outputs are ignored by Git. Source, lock file and development QR are ready to review; no commit or push was performed.

## Hardware-dependent work

Wire SDA GPIO6 / SCL GPIO7 / 3V3 / GND, power the board through regulated 5V/GND, flash the generated image, commission via BLE using the existing DIRIGERA Thread network's credentials, and verify HA pressure numbers and signed port polarity. Test sensor disconnection/recovery, long-press/release BOOT GPIO9 reset, power-cycle persistence, border-router interruption/reconnection, and OTA/rollback if used. Controllers' test-attestation acceptance and access to DIRIGERA's Thread credentials remain installation-dependent.
