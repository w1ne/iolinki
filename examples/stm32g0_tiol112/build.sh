#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "$0")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
: "${STM32_CMSIS_ROOT:?Set STM32_CMSIS_ROOT to ST cmsis-device-g0 root (Include and Source)}"
: "${CMSIS_CORE_ROOT:?Set CMSIS_CORE_ROOT to CMSIS/Core (contains Include)}"
build_dir="${BUILD_DIR:-$example_dir/build}"
compiler="${CC_ARM:-arm-none-eabi-gcc}"
clock_hz="${IOLINK_G0_SYSCLK_HZ:-16000000}"
case "$clock_hz" in
    16000000|64000000) ;;
    *) echo 'IOLINK_G0_SYSCLK_HZ must be 16000000 or 64000000' >&2; exit 1 ;;
esac
main_source="$example_dir/main.c"
image_name=reference-device
rig_com="${IOLINK_RIG_COM:-3}"
case "$rig_com" in
    2|3) ;;
    *) echo 'IOLINK_RIG_COM must be 2 or 3' >&2; exit 1 ;;
esac
if [[ "${IOLINK_G0_PROTOCOL_PROFILE:-0}" == 1 ]]; then
    if [[ "$clock_hz" != 64000000 ]]; then
        echo 'The protocol profile requires IOLINK_G0_SYSCLK_HZ=64000000' >&2
        exit 1
    fi
    main_source="$example_dir/protocol_rig_main.c"
    image_name=protocol-rig-64mhz
fi
mkdir -p "$build_dir"
flags=(-mcpu=cortex-m0plus -mthumb -std=c99 -Os -g3 -Wall -Wextra -Werror
       -ffunction-sections -fdata-sections -DSTM32G0B1xx "-DIOLINK_G0_SYSCLK_HZ=$clock_hz"
       "-DIOLINK_RIG_COM=$rig_com"
       -I"$repo_dir/include" -I"$repo_dir/examples/reference_device"
       -I"$STM32_CMSIS_ROOT/Include" -I"$CMSIS_CORE_ROOT/Include" -I"$example_dir")
sources=("$main_source" "$example_dir/board.c" "$example_dir/rx_queue.c" "$example_dir/gcc_runtime.c"
         "$repo_dir/examples/reference_device/reference_device.c"
         "$STM32_CMSIS_ROOT/Source/Templates/system_stm32g0xx.c")
for source in phy_tiol112 crc frame dll isdu events params data_storage device_info device; do
    sources+=("$repo_dir/src/$source.c")
done
objects=()
for source in "${sources[@]}"; do
    object="$build_dir/$(basename "${source%.c}").o"
    "$compiler" "${flags[@]}" -c "$source" -o "$object"
    objects+=("$object")
done
"$compiler" -mcpu=cortex-m0plus -mthumb -c \
    "$STM32_CMSIS_ROOT/Source/Templates/gcc/startup_stm32g0b1xx.s" -o "$build_dir/startup.o"
"$compiler" -mcpu=cortex-m0plus -mthumb -nostartfiles --specs=nosys.specs \
    "${objects[@]}" "$build_dir/startup.o" -T"$example_dir/stm32g0b1re.ld" \
    -Wl,--gc-sections,-Map,"$build_dir/$image_name.map" -o "$build_dir/$image_name.elf"
arm-none-eabi-objcopy -O ihex "$build_dir/$image_name.elf" "$build_dir/$image_name.hex"
arm-none-eabi-size "$build_dir/$image_name.elf"
if [[ -n "$(arm-none-eabi-nm -u "$build_dir/$image_name.elf")" ]]; then
    echo 'Unexpected undefined symbols in ELF' >&2
    exit 1
fi
