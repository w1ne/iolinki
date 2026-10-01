/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "adapter.h"
#include "adapter_state.h"
#include "iolinki/time_utils.h"
#include "iolinki/platform.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <stm32u5xx_ll_usart.h>
#include <stm32u5xx_ll_tim.h>
#define IO DT_PATH(zephyr_user)
#define UART_NODE DT_NODELABEL(usart2)
PINCTRL_DT_DEV_CONFIG_DECLARE(UART_NODE);
static const struct device * const serial = DEVICE_DT_GET(UART_NODE);
static const struct gpio_dt_spec en = GPIO_DT_SPEC_GET(IO, en_gpios);
static const struct gpio_dt_spec wake = GPIO_DT_SPEC_GET(IO, wake_gpios);
static const struct gpio_dt_spec nfault = GPIO_DT_SPEC_GET(IO, nfault_gpios);
static const struct gpio_dt_spec tx = GPIO_DT_SPEC_GET(IO, tx_gpios);
static USART_TypeDef * const usart = (USART_TypeDef *) DT_REG_ADDR(UART_NODE);
static TIM_TypeDef * const timer = (TIM_TypeDef *) DT_REG_ADDR(DT_NODELABEL(timers2));
static struct micro_clock clock_state;
static struct rx_ring rx;
static struct gpio_callback wake_callback;
static bool waking, transmitting;
static int rx_error;
static uint32_t baudrate;
volatile uint32_t board_rx_dropped, board_uart_errors;
volatile int board_adapter_error, board_nfault_asserted;
static unsigned critical_depth, critical_key;

/* Single-core sample: nest safely and restore the original IRQ state. */
void iolink_critical_enter(void)
{
    unsigned key = irq_lock();
    if (critical_depth++ == 0) critical_key = key;
}
void iolink_critical_exit(void)
{
    __ASSERT(critical_depth != 0, "Unbalanced stack critical section");
    if (--critical_depth == 0) irq_unlock(critical_key);
}
/* No flash layout or endurance contract is supplied. Fail explicitly. */
int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
int board_clock_init(void)
{
    const struct device *rcc = DEVICE_DT_GET(DT_NODELABEL(rcc));
    struct stm32_pclken gate = {.bus = DT_CLOCKS_CELL(DT_NODELABEL(timers2), bus),
                                .enr = DT_CLOCKS_CELL(DT_NODELABEL(timers2), bits)};
    uint32_t rate;
    /* APB timer doubling changes with prescaler; reject unsupported clock trees. */
    BUILD_ASSERT(DT_PROP(DT_NODELABEL(rcc), apb1_prescaler) == 1);
    if (!device_is_ready(rcc) || clock_control_on(rcc, (clock_control_subsys_t) &gate) ||
        clock_control_get_rate(rcc, (clock_control_subsys_t) &gate, &rate))
        return -EIO;
    if (rate < 1000000U || rate % 1000000U) return -EINVAL;
    LL_TIM_DisableCounter(timer);
    LL_TIM_SetPrescaler(timer, rate / 1000000U - 1U);
    LL_TIM_SetAutoReload(timer, UINT32_MAX);
    LL_TIM_SetCounter(timer, 0);
    LL_TIM_GenerateEvent_UPDATE(timer);
    LL_TIM_ClearFlag_UPDATE(timer);
    clock_state = (struct micro_clock){0};
    LL_TIM_EnableCounter(timer);
    return 0;
}
uint64_t iolink_time_get_us(void)
{
    unsigned key = irq_lock();
    uint64_t value = micro_extend(&clock_state, LL_TIM_GetCounter(timer));
    irq_unlock(key);
    return value;
}
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (iolink_time_get_us() / 1000U);
}
static void on_wake(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    (void) port;
    (void) cb;
    (void) pins;
    waking = true;
}
static void on_uart(const struct device *dev, void *user)
{
    (void) user;
    if (!uart_irq_update(dev)) return;
    int errors = uart_err_check(dev);
    if (errors != 0 && !transmitting) {
        rx_error = errors < 0 ? errors : -EIO;
        board_uart_errors++;
    }
    while (uart_irq_rx_ready(dev)) {
        uint8_t bytes[16];
        int n = uart_fifo_read(dev, bytes, sizeof(bytes));
        if (n <= 0) break;
        for (int i = 0; i < n; i++)
            if (!transmitting) rx_push(&rx, bytes[i]);
    }
    board_rx_dropped = rx.dropped;
}
static void discard_echo(void)
{
    uint8_t b;
    while (uart_fifo_read(serial, &b, 1) > 0) {
    }
    int errors = uart_err_check(serial);
    if (errors < 0) board_adapter_error = errors;
}
static int init(void *user)
{
    (void) user;
    if (!gpio_is_ready_dt(&en)) return -ENODEV;
    int rc = gpio_pin_configure_dt(&en, GPIO_OUTPUT_INACTIVE);
    if (rc) return rc;
    if (!device_is_ready(serial) || !gpio_is_ready_dt(&tx) || !gpio_is_ready_dt(&wake) ||
        !gpio_is_ready_dt(&nfault))
        return -ENODEV;
    if ((rc = gpio_pin_configure_dt(&wake, GPIO_INPUT | GPIO_PULL_UP)) ||
        (rc = gpio_pin_configure_dt(&nfault, GPIO_INPUT | GPIO_PULL_UP)))
        return rc;
    /* Initialize before enabling edges; never overwrite an IRQ-latched pulse. */
    if ((rc = gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_DISABLE))) return rc;
    waking = false;
    gpio_init_callback(&wake_callback, on_wake, BIT(wake.pin));
    if ((rc = gpio_add_callback(wake.port, &wake_callback)) ||
        (rc = gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_EDGE_TO_ACTIVE)))
        return rc;
    if ((rc = uart_irq_callback_user_data_set(serial, on_uart, NULL))) return rc;
    uart_irq_rx_disable(serial);
    unsigned key = irq_lock();
    rc = wake_merge_sample(&waking, gpio_pin_get_dt(&wake));
    irq_unlock(key);
    if (rc < 0) {
        (void) gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_DISABLE);
        return rc;
    }
    return 0;
}
static void enable(void *user, bool enabled)
{
    (void) user;
    int rc = gpio_pin_set_dt(&en, enabled && board_adapter_error == 0);
    if (rc) board_adapter_error = rc;
}
static void sio(void *user, bool high)
{
    (void) user;
    uart_irq_rx_disable(serial);
    uart_irq_err_disable(serial);
    unsigned key = irq_lock();
    discard_echo();
    rx.head = rx.tail = 0;
    rx.overflow = false;
    rx_error = 0;
    LL_USART_Disable(usart);
    /* gpio_pin_configure switches PD5 from AF7 to GPIO; initial level avoids glitch. */
    int rc = gpio_pin_configure_dt(&tx, high ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
    if (rc) {
        board_adapter_error = rc;
        (void) gpio_pin_set_dt(&en, 0);
    }
    irq_unlock(key);
}
static int configure(void *user, uint32_t baud)
{
    (void) user;
    uart_irq_rx_disable(serial);
    uart_irq_err_disable(serial);
    unsigned key = irq_lock();
    discard_echo();
    rx.head = rx.tail = 0;
    rx.overflow = false;
    rx_error = 0;
    irq_unlock(key);
    int rc = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(UART_NODE), PINCTRL_STATE_DEFAULT);
    if (rc) return rc;
    LL_USART_Enable(usart);
    struct uart_config cfg = {.baudrate = baud,
                              .parity = UART_CFG_PARITY_EVEN,
                              .stop_bits = UART_CFG_STOP_BITS_1,
                              .data_bits = UART_CFG_DATA_BITS_8,
                              .flow_ctrl = UART_CFG_FLOW_CTRL_NONE};
    rc = uart_configure(serial, &cfg);
    if (rc) {
        LL_USART_Disable(usart);
        return rc;
    }
    baudrate = baud;
    key = irq_lock();
    discard_echo();
    irq_unlock(key);
    if (board_adapter_error) return board_adapter_error;
    uart_irq_err_enable(serial);
    uart_irq_rx_enable(serial);
    return 0;
}
static int send_complete(void *user, const uint8_t *data, size_t len)
{
    (void) user;
    if (board_adapter_error) return board_adapter_error;
    if (!baudrate || len > 255U) return -EINVAL;
    uint64_t deadline =
        iolink_time_get_us() + (len * 11ULL * 1000000ULL + baudrate - 1) / baudrate + 2000U;
    unsigned key = irq_lock();
    transmitting = true;
    uart_irq_rx_disable(serial);
    discard_echo();
    irq_unlock(key);
    int rc = (int) len;
    for (size_t i = 0; i < len; i++) {
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(usart)) {
            discard_echo();
            if (iolink_time_get_us() >= deadline) {
                rc = -ETIMEDOUT;
                goto done;
            }
        }
        LL_USART_TransmitData8(usart, data[i]);
        discard_echo();
    }
    while (!LL_USART_IsActiveFlag_TC(usart)) {
        discard_echo();
        if (iolink_time_get_us() >= deadline) {
            rc = -ETIMEDOUT;
            break;
        }
    }
done:
    key = irq_lock();
    /* Release C/Q before RX can observe anything beyond the local echo. */
    int disable_rc = gpio_pin_set_dt(&en, 0);
    if (disable_rc) board_adapter_error = disable_rc;
    discard_echo();
    if (rc >= 0 && board_adapter_error) rc = board_adapter_error;
    transmitting = false;
    if (rc < 0) { /* prevent a timed-out partial frame from continuing on CQ */
        (void) gpio_pin_set_dt(&en, 0);
        LL_USART_Disable(usart);
        board_adapter_error = rc;
    }
    else
        uart_irq_rx_enable(serial);
    irq_unlock(key);
    return rc;
}
static int receive(void *user, uint8_t *b)
{
    (void) user;
    unsigned key = irq_lock();
    int rc;
    if (rx_error) {
        rc = rx_error;
        rx_error = 0;
        rx.tail = rx.head;
    }
    else
        rc = rx_pop(&rx, b);
    irq_unlock(key);
    return rc;
}
static int consume(void *user)
{
    (void) user;
    unsigned key = irq_lock();
    bool found = waking;
    waking = false;
    irq_unlock(key);
    return found ? 1 : 0;
}
static bool fault_level(void *user)
{
    (void) user;
    int asserted = gpio_pin_get_dt(&nfault);
    if (asserted < 0) {
        board_adapter_error = asserted;
        return false;
    }
    return asserted == 0; /* callback requires raw high = no fault */
}
const iolink_tiol112_io_t board_tiol112_io = {.init = init,
                                              .set_enable = enable,
                                              .set_sio_tx = sio,
                                              .uart_configure = configure,
                                              .uart_send_complete = send_complete,
                                              .uart_recv = receive,
                                              .consume_wakeup = consume,
                                              .read_nfault = fault_level};
