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
rig_timing="${IOLINK_RIG_TIMING_ENFORCE:-0}"
uart_backend="${IOLINK_G0_UART_BACKEND:-0}"
case "$uart_backend" in
    0|1) ;;
    2) : "${STM32_HAL_ROOT:?Set STM32_HAL_ROOT to ST stm32g0xx-hal-driver root for HAL DMA}" ;;
    *) echo 'IOLINK_G0_UART_BACKEND must be 0 (CMSIS IRQ), 1 (CMSIS DMA), or 2 (HAL DMA)' >&2; exit 1 ;;
esac
enable_guard_us="${IOLINK_G0_TIOL112_ENABLE_GUARD_US:-6}"
case "$enable_guard_us" in
    ''|*[!0-9]*) echo 'IOLINK_G0_TIOL112_ENABLE_GUARD_US must be a nonnegative integer' >&2; exit 1 ;;
esac
case "$rig_timing" in
    0|1) ;;
    *) echo 'IOLINK_RIG_TIMING_ENFORCE must be 0 or 1' >&2; exit 1 ;;
esac
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
flags=(-mcpu=cortex-m0plus -mthumb -std=c99 -Os -g3 -Wall -Wextra -Werror -MMD
       -ffunction-sections -fdata-sections -DSTM32G0B1xx "-DIOLINK_G0_SYSCLK_HZ=$clock_hz"
       "-DIOLINK_RIG_COM=$rig_com"
       "-DIOLINK_RIG_TIMING_ENFORCE=$rig_timing"
       "-DIOLINK_G0_UART_BACKEND=$uart_backend"
       "-DIOLINK_G0_TIOL112_ENABLE_GUARD_US=$enable_guard_us"
       -I"$repo_dir/include" -I"$repo_dir/examples/reference_device"
       -I"$STM32_CMSIS_ROOT/Include" -I"$CMSIS_CORE_ROOT/Include" -I"$example_dir")
sources=("$main_source" "$example_dir/board.c" "$example_dir/rx_queue.c" "$example_dir/gcc_runtime.c"
         "$example_dir/uart_dma.c" "$example_dir/dma_rx_ring.c"
         "$repo_dir/examples/reference_device/reference_device.c"
         "$STM32_CMSIS_ROOT/Source/Templates/system_stm32g0xx.c")
if [[ "$uart_backend" == 2 ]]; then
    flags+=(-DUSE_HAL_DRIVER -I"$STM32_HAL_ROOT/Inc")
    for module in uart uart_ex dma dma_ex rcc rcc_ex; do
        sources+=("$STM32_HAL_ROOT/Src/stm32g0xx_hal_$module.c")
    done
fi
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
manifest_args=(--repo "$repo_dir" --elf "$build_dir/$image_name.elf" --compiler "$compiler"
               --backend "$uart_backend" --clock "$clock_hz" --com "$rig_com"
               --timing "$rig_timing" --guard "$enable_guard_us"
               --device-root "$STM32_CMSIS_ROOT" --core-root "$CMSIS_CORE_ROOT"
               --input "$example_dir/build.sh" --input "$example_dir/stm32g0b1re.ld"
               --input "$STM32_CMSIS_ROOT/Source/Templates/gcc/startup_stm32g0b1xx.s")
if [[ "$uart_backend" == 2 ]]; then manifest_args+=(--hal-root "$STM32_HAL_ROOT"); fi
if [[ "${IOLINK_G0_PROTOCOL_PROFILE:-0}" == 1 ]]; then manifest_args+=(--protocol-profile); fi
for source in "${sources[@]}"; do manifest_args+=(--input "$source"); done
python3 "$repo_dir/tools/stm32g0_firmware_manifest.py" "${manifest_args[@]}"
