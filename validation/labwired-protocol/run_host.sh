#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/labwired-protocol
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  validation/labwired-protocol/host_main.c validation/labwired-protocol/rig_tests.c \
  src/{crc,frame,dll,isdu,events,params,data_storage,device_info,device,platform}.c \
  -o build/labwired-protocol/host-rig
exec build/labwired-protocol/host-rig
