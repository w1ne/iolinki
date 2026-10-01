#!/usr/bin/env python3
"""Describe concrete downloadable deliverables for this release."""
import sys
version = sys.argv[1]
base = f"https://github.com/w1ne/iolinki/releases/download/v{version}"
prefix = f"iolinki-{version}"
print(f"""
## Examples, IAR and LabWired downloads

- [Native STM32G0B1RE/TIOL112 IAR project]({base}/{prefix}-stm32g0-tiol112-IAR-project.zip): EWARM project/linker, stack/application source and pinned CMSIS headers/ST IAR startup. Open the ZIP's OPEN-IN-IAR.txt. Project paths are checked; EWARM compilation is separate.
- [Complete source and tests]({base}/{prefix}-source.zip): stack, test suites, source projects, IODD CLI and descriptions.
- [Example applications and wiring guides]({base}/{prefix}-examples.zip): counter/button/LED, threshold/hysteresis/inversion/teach sensor, G0, C3 and U5 projects.
- [Counter IODD package]({base}/{prefix}-counter-IODD.zip) and [switching-sensor IODD package]({base}/{prefix}-switching-sensor-IODD.zip): matching descriptions with CRC and SHA-256 manifests.
- [Built MCU firmware]({base}/{prefix}-firmware.tar.gz): ELF/BIN/HEX/MAP files built from this release commit and simulator logs.
- [LabWired recipes]({base}/{prefix}-labwired-recipes.zip): exact engine pins and commands for actual G0/C3 firmware startup and interrupt tests in both modes.
- [Linux host demos]({base}/{prefix}-host-demos.tar.gz): runnable reference/switching sensor executables built on the Ubuntu release runner; source rebuild recipes are also included.
- [Test results]({base}/test-results.xml), [test log]({base}/test_output.log), [checksums]({base}/SHA256SUMS), [source commit]({base}/SOURCE_COMMIT) and [engine commits]({base}/LABWIRED_COMMITS).

The sensor example uses a vendor parameter map, not a standardized Smart Sensor
Profile. IODD XML is schema/CLI validated; IO-Link product conformity and official
checker approval are separate. G0/C3 simulator tests apply the wake input directly;
complete analog transceiver fault behavior and physical-master validation are
separate work. U5 is a firmware build in this release package. Embedded NVM hooks
reject unsupported writes; no retained flash parameter claim is made.
""")
