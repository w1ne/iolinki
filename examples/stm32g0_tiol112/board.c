/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "timebase.h"
#include "stm32g0xx.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"

#ifndef IOLINK_G0_SYSCLK_HZ
#define IOLINK_G0_SYSCLK_HZ 16000000UL
#endif
#if (IOLINK_G0_SYSCLK_HZ != 16000000UL) && (IOLINK_G0_SYSCLK_HZ != 64000000UL)
#error "IOLINK_G0_SYSCLK_HZ must be 16000000 or 64000000"
#endif
#ifndef IOLINK_G0_TIOL112_ENABLE_GUARD_US
#define IOLINK_G0_TIOL112_ENABLE_GUARD_US 6U
#endif

rx_queue_t board_rx;
volatile uint32_t board_tx_timeouts;
static volatile uint32_t timer_high;
static volatile uint32_t timer_epoch_ms;
static volatile uint32_t timer_epoch_remainder_us;
static volatile uint8_t wake_pending;
static uint32_t critical_depth, saved_primask;
static uint32_t tx_octet_timeout_us;
static uint32_t response_start_floor_us;
static volatile uint32_t last_rx_irq_us;
static volatile bool have_rx_irq;

void iolink_critical_enter(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (critical_depth++ == 0U) saved_primask = mask;
}
void iolink_critical_exit(void)
{
    if (critical_depth != 0U && --critical_depth == 0U) __set_PRIMASK(saved_primask);
}
void TIM2_IRQHandler(void)
{
    /* Keep all epoch fields coherent even for a higher-priority reader. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        ++timer_high;
        uint32_t milliseconds = timer_epoch_ms;
        uint32_t remainder = timer_epoch_remainder_us;
        board_time_advance_epoch(&milliseconds, &remainder, timer_high);
        timer_epoch_ms = milliseconds;
        timer_epoch_remainder_us = remainder;
    }
    __set_PRIMASK(mask);
}
uint64_t iolink_time_get_us(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t high = timer_high, low = TIM2->CNT;
    /* Account for a wrap whose ISR has not run, rereading the post-wrap CNT. */
    if (TIM2->SR & TIM_SR_UIF) {
        ++high;
        low = TIM2->CNT;
    }
    __set_PRIMASK(mask);
    return ((uint64_t) high << 32) | low;
}
uint32_t iolink_time_get_ms(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t milliseconds = timer_epoch_ms;
    uint32_t remainder = timer_epoch_remainder_us;
    uint32_t high = timer_high;
    uint32_t low = TIM2->CNT;
    /* Account locally for an unserviced wrap and reread the post-wrap count,
     * just like get_us. The ISR later commits the same epoch, not an extra one. */
    if (TIM2->SR & TIM_SR_UIF) {
        board_time_advance_epoch(&milliseconds, &remainder, high + 1U);
        low = TIM2->CNT;
    }
    __set_PRIMASK(mask);
    return board_time_milliseconds(milliseconds, remainder, low);
}
int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
static void mode(GPIO_TypeDef* port, unsigned pin, unsigned value)
{
    port->MODER = (port->MODER & ~(3UL << (2U * pin))) | (value << (2U * pin));
}
void board_init(void)
{
    board_uart_backend = IOLINK_G0_UART_BACKEND;
    board_dma_stats.rx_bytes = 0U; /* Keep the debugger ABI in every backend ELF. */
    /* Reset-start only: ST SystemInit keeps reset clock mux. HSI /1, AHB/APB /1. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CR &= ~RCC_CR_HSIDIV;
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE);
    while (RCC->CFGR & RCC_CFGR_SWS) {
    }
#if IOLINK_G0_SYSCLK_HZ == 64000000UL
    /* Range 1 and two Flash wait states before raising the CPU/bus clock. */
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    (void) RCC->APBENR1;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0;
    while (PWR->SR2 & PWR_SR2_VOSF) {
    }
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | (2UL << FLASH_ACR_LATENCY_Pos);
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != (2UL << FLASH_ACR_LATENCY_Pos)) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {
    }
    /* HSI16 / M1 * N8 / R2 = 64MHz; M and R fields encode divisor minus one. */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (8UL << RCC_PLLCFGR_PLLN_Pos) | RCC_PLLCFGR_PLLREN |
                   (1UL << RCC_PLLCFGR_PLLR_Pos);
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLLRCLK;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLLRCLK) {
    }
#endif
    SystemCoreClockUpdate();
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void) RCC->APBENR2;
    /* PA9/PA10 also have UCPD1 dead-battery pull-downs: release them for UART. */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE;
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void) RCC->APBENR1;
    TIM2->PSC = SystemCoreClock / 1000000UL - 1U;
    TIM2->ARR = 0xffffffffU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM2_IRQn, 2);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
    GPIOA->BSRR = (1UL << (8U + 16U)) | (1UL << (5U + 16U));
    mode(GPIOA, 8, 1);
    mode(GPIOA, 5, 1);
    mode(GPIOC, 13, 0);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3UL << 26)) | (1UL << 26);
}
bool board_button(void)
{
    return !(GPIOC->IDR & (1UL << 13));
}
void board_led(bool on)
{
    GPIOA->BSRR = 1UL << (on ? 5U : 21U);
}
static void enable(void* user, bool enabled)
{
    (void) user;
    GPIOA->BSRR = 1UL << (enabled ? 8U : 24U);
}
static void sio_tx(void* user, bool high)
{
    (void) user;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    board_dma_stop();
#endif
    USART1->CR1 = 0;
    GPIOA->BSRR = 1UL << (high ? 9U : 25U);
    mode(GPIOA, 9, 1);
}
static int phy_init(void* user)
{
    enable(user, false);
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    (void) RCC->APBENR2;
    mode(GPIOB, 0, 0);
    mode(GPIOB, 1, 0);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~15UL) | 5U;
    EXTI->EXTICR[0] = (EXTI->EXTICR[0] & ~0xffUL) | 1U; /* EXTI0 = PB0 */
    EXTI->RTSR1 &= ~1UL;
    EXTI->FTSR1 |= 1U;
    EXTI->FPR1 = 1U;
    EXTI->IMR1 |= 1U;
    wake_pending = !(GPIOB->IDR & 1U);
    NVIC_SetPriority(EXTI0_1_IRQn, 0);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
    return 0;
}
void EXTI0_1_IRQHandler(void)
{
    if (EXTI->FPR1 & 1U) {
        EXTI->FPR1 = 1U;
        wake_pending = 1;
    }
}
static int wake(void* user)
{
    (void) user;
    iolink_critical_enter();
    int result = wake_pending;
    wake_pending = 0;
    iolink_critical_exit();
    return result;
}
static bool nfault(void* user)
{
    (void) user;
    return (GPIOB->IDR & 2U) != 0U;
}
static int configure(void* user, uint32_t baud)
{
    (void) user;
    if (baud != 4800U && baud != 38400U && baud != 230400U) return -1;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    board_dma_stop();
#endif
    USART1->CR1 = 0;
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;                  /* APB clock = core clock. */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~0xff0UL) | 0x110U; /* PA9/10 AF1 */
    mode(GPIOA, 9, 2);
    mode(GPIOA, 10, 2);
    USART1->BRR = (SystemCoreClock + baud / 2U) / baud;
    /* Cache a rounded-up character budget outside the response-start path. */
    const uint32_t actual_baud_floor = SystemCoreClock / USART1->BRR;
    tx_octet_timeout_us = (11000000UL + actual_baud_floor - 1U) / actual_baud_floor;
    /* RXNE follows stop-bit sampling. 1.5Tbit from the IRQ observation covers
     * the remaining half stop bit and the device's minimum 1Tbit response gap.
     * Add 1us for the timer's downward quantization; keep PHY EN guard separate. */
    response_start_floor_us = (1500000UL + actual_baud_floor - 1U) / actual_baud_floor + 1U;
    USART1->CR2 = 0; /* one stop bit */
    USART1->CR3 = USART_CR3_EIE;
    USART1->ICR =
        USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF | USART_ICR_TCCF;
    USART1->RQR = USART_RQR_RXFRQ;
    iolink_critical_enter();
    board_rx.head = board_rx.tail = 0;
    board_rx.failed = 0;
    have_rx_irq = false;
    iolink_critical_exit();
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    return board_dma_configure(baud);
#else
    /* 9-bit word INCLUDING parity = eight payload bits and even parity. */
    USART1->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_UE;
    return 0;
#endif
}
void USART1_IRQHandler(void)
{
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    board_dma_uart_irq();
#else
    uint32_t status = USART1->ISR;
    uint32_t errors = status & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
    if (errors) {
        USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        rx_queue_error(&board_rx);
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = (uint8_t) USART1->RDR;
        if (!errors) {
            last_rx_irq_us = TIM2->CNT;
            have_rx_irq = true;
            rx_queue_push(&board_rx, byte);
        }
    }
#endif
}
static int recv_byte(void* user, uint8_t* byte)
{
    (void) user;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    uint32_t observed_us = 0U;
    int result = board_dma_recv(byte, &observed_us);
    if (result > 0) {
        /* DMA progress is observed by this poll, not a physical-edge capture. */
        last_rx_irq_us = observed_us;
        have_rx_irq = true;
    }
    return result;
#else
    iolink_critical_enter();
    int result = rx_queue_pop(&board_rx, byte);
    iolink_critical_exit();
    return result;
#endif
}
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_CMSIS_IRQ
static int wait_status(uint32_t flag, uint64_t deadline)
{
    while (!(USART1->ISR & flag))
        if (iolink_time_get_us() >= deadline) return -1;
    return 0;
}
#endif
static int send_complete(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    /* The PHY asserted EN before this callback. TI specifies a typical 4us
     * enable delay, not a guaranteed maximum. This configurable 6us default
     * includes timer quantization margin; validate it on the actual hardware. */
    const uint64_t driver_ready_at = iolink_time_get_us() + IOLINK_G0_TIOL112_ENABLE_GUARD_US;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    board_dma_rx_pause();
#endif
    /* Disable receiver throughout transmission; no local CQ echo can enter ISR. */
    USART1->CR1 &= ~(USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    USART1->CR3 &= ~USART_CR3_EIE;
    /* len*ceil(character time) is no shorter than the previous frame budget. */
    uint64_t deadline = iolink_time_get_us() + (uint64_t) len * tx_octet_timeout_us + 2000U;
    int result = -1;
    while (USART1->ISR & USART_ISR_REACK) {
        if (iolink_time_get_us() >= deadline) goto finished;
    }
    while (iolink_time_get_us() < driver_ready_at) {
    }
    if (have_rx_irq) {
        const uint32_t received_at = last_rx_irq_us;
        while ((uint32_t) (TIM2->CNT - received_at) < response_start_floor_us) {
        }
    }
    USART1->ICR = USART_ICR_TCCF;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    /* DMA owns the caller's buffer until UART TC or an explicit abort. The
     * existing PHY contract remains synchronous, with a bounded foreground wait. */
    if (board_dma_tx_begin(data, len) != 0) goto finished;
    for (;;) {
        int state = board_dma_tx_poll();
        if (state > 0) {
            result = (int) len;
            break;
        }
        if (state < 0 || iolink_time_get_us() >= deadline) break;
    }
#else
    for (size_t i = 0; i < len; ++i) {
        if (wait_status(USART_ISR_TXE_TXFNF, deadline) != 0) goto finished;
        USART1->TDR = data[i];
    }
    if (wait_status(USART_ISR_TC, deadline) == 0) result = (int) len;
#endif
finished:
    if (result < 0) {
        ++board_tx_timeouts;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
        board_dma_tx_abort();
#endif
        USART1->CR1 &= ~USART_CR1_UE; /* abort any pending byte before reuse */
        USART1->RQR = USART_RQR_TXFRQ;
    }
    enable(user, false); /* UART completed/aborted before driver and buffer release. */
    USART1->RQR = USART_RQR_RXFRQ;
    USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    USART1->CR3 |= USART_CR3_EIE;
#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
    USART1->CR1 |= USART_CR1_RE | USART_CR1_PEIE | USART_CR1_UE;
    if (board_dma_rx_resume() != 0) return -1;
#else
    USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_UE;
#endif
    return result;
}
const iolink_tiol112_io_t* board_tiol112_io(void)
{
    static const iolink_tiol112_io_t io = {.init = phy_init,
                                           .set_enable = enable,
                                           .set_sio_tx = sio_tx,
                                           .uart_configure = configure,
                                           .uart_send_complete = send_complete,
                                           .uart_recv = recv_byte,
                                           .consume_wakeup = wake,
                                           .read_nfault = nfault};
    return &io;
}
