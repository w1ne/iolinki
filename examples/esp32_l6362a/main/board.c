/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "tx_guard.h"
#include "rx_guard.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool wake_latched;
static QueueHandle_t uart_events;
static uint32_t current_baud = 4800;
static bool installed;
/* UART1 has exactly one writer: app_main's protocol task. No other task/ISR
 * may call UART1 TX APIs. Therefore IDF's internal TX mutex has no contender. */
static esp32_tx_guard_t tx_guard;
void iolink_critical_enter(void)
{
    portENTER_CRITICAL(&mux);
}
void iolink_critical_exit(void)
{
    portEXIT_CRITICAL(&mux);
}
uint64_t iolink_time_get_us(void)
{
    return (uint64_t) esp_timer_get_time();
}
uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (iolink_time_get_us() / 1000U);
}
/* Flash erase/commit stalls the only C3 core and breaks IO-Link deadlines.
 * Explicitly unsupported until deferred storage is integrated; never claim success. */
int iolink_nvm_read(uint32_t offset, uint8_t* b, size_t n)
{
    (void) offset;
    (void) b;
    (void) n;
    return -1;
}
int iolink_nvm_write(uint32_t offset, const uint8_t* b, size_t n)
{
    (void) offset;
    (void) b;
    (void) n;
    return -1;
}
static void IRAM_ATTR ol_irq(void* arg)
{
    (void) arg;
    portENTER_CRITICAL_ISR(&mux);
    wake_latched = true;
    portEXIT_CRITICAL_ISR(&mux);
}
static void enable(void* u, bool on)
{
    (void) u;
    gpio_set_level(PIN_EN, on && esp32_tx_enable_allowed(&tx_guard));
}
static int initialize(void* u)
{
    (void) u;
    if (!esp32_tx_enable_allowed(&tx_guard)) return -1;
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_EN) | (1ULL << PIN_TX) | (1ULL << PIN_LED),
                         .mode = GPIO_MODE_OUTPUT};
    if (gpio_config(&out) != ESP_OK) return -1;
    gpio_set_level(PIN_EN, 0);
    gpio_set_level(PIN_TX, 1);
    gpio_set_level(PIN_LED, 0);
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << PIN_RX) | (1ULL << PIN_DIAG) | (1ULL << PIN_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE};
    if (gpio_config(&inputs) != ESP_OK) return -1;
    inputs.pin_bit_mask = 1ULL << PIN_OL;
    inputs.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&inputs) != ESP_OK || gpio_install_isr_service(0) != ESP_OK ||
        gpio_isr_handler_add(PIN_OL, ol_irq, NULL) != ESP_OK)
        return -1;
    if (!installed) {
        if (uart_driver_install(UART_NUM_1, 512, 0, 32, &uart_events, 0) != ESP_OK) return -1;
        installed = true;
    }
    return 0;
}
static void in2(void* u, bool high)
{
    (void) u;
    /* GPIO output matrix replaces UART TX only for SIO; IN1 remains hardwired low. */
    esp_rom_gpio_connect_out_signal(PIN_TX, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(PIN_TX, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TX, high);
}
static int configure(void* u, uint32_t baud)
{
    (void) u;
    if (!esp32_tx_enable_allowed(&tx_guard)) return -1;
    uart_config_t c = {.baud_rate = (int) baud,
                       .data_bits = UART_DATA_8_BITS,
                       .parity = UART_PARITY_EVEN,
                       .stop_bits = UART_STOP_BITS_1,
                       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                       .source_clk = UART_SCLK_DEFAULT};
    if (uart_param_config(UART_NUM_1, &c) != ESP_OK ||
        uart_set_pin(UART_NUM_1, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) !=
            ESP_OK ||
        uart_set_rx_full_threshold(UART_NUM_1, 1) != ESP_OK ||
        uart_set_rx_timeout(UART_NUM_1, 1) != ESP_OK ||
        uart_enable_intr_mask(UART_NUM_1, UART_INTR_FRAM_ERR) != ESP_OK)
        return -1;
    current_baud = baud;
    uart_flush_input(UART_NUM_1);
    xQueueReset(uart_events);
    return 0;
}
static uint64_t tx_now(void* u)
{
    (void) u;
    return iolink_time_get_us();
}
static int tx_enqueue(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    return uart_tx_chars(UART_NUM_1, (const char*) b, n);
}
static int tx_done(void* u, uint32_t timeout_ms)
{
    (void) u;
    return uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(timeout_ms)) == ESP_OK ? 0 : -1;
}
static void rx_enabled(void* u, bool on)
{
    (void) u;
    if (on)
        uart_enable_rx_intr(UART_NUM_1);
    else
        uart_disable_rx_intr(UART_NUM_1);
}
static void flush_echo(void* u)
{
    (void) u;
    uart_flush_input(UART_NUM_1);
    xQueueReset(uart_events);
}
static int transmit(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    const esp32_tx_io_t tx = {NULL, tx_now, tx_enqueue, tx_done, rx_enabled, enable, flush_echo};
    return esp32_tx_run(&tx_guard, &tx, b, n, current_baud);
}
static bool rx_saturated(void* u)
{
    (void) u;
    return uxQueueMessagesWaiting(uart_events) >= 32U;
}
static int rx_event(void* u)
{
    (void) u;
    uart_event_t e;
    if (xQueueReceive(uart_events, &e, 0) == pdTRUE) {
        if (e.type == UART_FIFO_OVF || e.type == UART_BUFFER_FULL || e.type == UART_PARITY_ERR ||
            e.type == UART_FRAME_ERR || e.type == UART_BREAK)
            return -1;
        return 1;
    }
    return 0;
}
static int rx_read(void* u, uint8_t* b)
{
    (void) u;
    return uart_read_bytes(UART_NUM_1, b, 1, 0);
}
static int receive(void* u, uint8_t* b)
{
    (void) u;
    const esp32_rx_io_t rx = {NULL, rx_saturated, rx_event, flush_echo, rx_read};
    return esp32_rx_run(&rx, b);
}
static int consume(void* u)
{
    (void) u;
    iolink_critical_enter();
    bool pending = wake_latched;
    wake_latched = false;
    iolink_critical_exit();
    return pending ? 1 : 0;
}
static bool ol(void* u)
{
    (void) u;
    return gpio_get_level(PIN_OL) != 0;
}
static bool diag(void* u)
{
    (void) u;
    return gpio_get_level(PIN_DIAG) != 0;
}
static const iolink_l6362a_io_t io = {NULL,     initialize, enable,  in2, configure,
                                      transmit, receive,    consume, ol,  diag};
const iolink_l6362a_io_t* board_io(void)
{
    return &io;
}
bool board_button(void)
{
    return gpio_get_level(PIN_BUTTON) == 0;
}
void board_led(bool on)
{
    gpio_set_level(PIN_LED, on);
}
