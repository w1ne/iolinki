#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p validation/build
compiler="${CC:-cc}"
"$compiler" -std=c99 -O2 -Wall -Wextra -Werror -DIOLINK_RIG_HOST_MAIN -Iinclude \
  validation/protocol_rig.c src/{crc,frame,dll,isdu,events,params,data_storage,device_info,device,platform}.c \
  -o validation/build/protocol_rig
validation/build/protocol_rig
