/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "stm32g0xx.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"

rx_queue_t board_rx;
volatile uint32_t board_tx_timeouts;
static volatile uint32_t timer_high;
static volatile uint8_t wake_pending;
static uint32_t critical_depth, saved_primask;

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
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        ++timer_high;
    }
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
    return (uint32_t) (iolink_time_get_us() / 1000U);
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
    /* Reset-start only: ST SystemInit keeps reset clock mux. HSI /1, AHB/APB /1. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CR &= ~RCC_CR_HSIDIV;
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE);
    while (RCC->CFGR & RCC_CFGR_SWS) {
    }
    SystemCoreClockUpdate();
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void) RCC->APBENR2;
    /* PA9/PA10 also have UCPD1 dead-battery pull-downs: release them for UART. */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE;
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void) RCC->APBENR1;
    TIM2->PSC = 15U;
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
    USART1->CR1 = 0;
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;                  /* APB clock = 16MHz */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~0xff0UL) | 0x110U; /* PA9/10 AF1 */
    mode(GPIOA, 9, 2);
    mode(GPIOA, 10, 2);
    USART1->BRR = (16000000UL + baud / 2U) / baud;
    USART1->CR2 = 0; /* one stop bit */
    USART1->CR3 = USART_CR3_EIE;
    USART1->ICR =
        USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF | USART_ICR_TCCF;
    USART1->RQR = USART_RQR_RXFRQ;
    iolink_critical_enter();
    board_rx.head = board_rx.tail = 0;
    board_rx.failed = 0;
    iolink_critical_exit();
    /* 9-bit word INCLUDING parity = eight payload bits and even parity. */
    USART1->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_UE;
    return 0;
}
void USART1_IRQHandler(void)
{
    uint32_t status = USART1->ISR;
    uint32_t errors = status & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
    if (errors) {
        USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        rx_queue_error(&board_rx);
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = (uint8_t) USART1->RDR;
        if (!errors) rx_queue_push(&board_rx, byte);
    }
}
static int recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    iolink_critical_enter();
    int result = rx_queue_pop(&board_rx, byte);
    iolink_critical_exit();
    return result;
}
static int wait_status(uint32_t flag, uint64_t deadline)
{
    while (!(USART1->ISR & flag))
        if (iolink_time_get_us() >= deadline) return -1;
    return 0;
}
static int send_complete(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    /* Disable receiver throughout transmission; no local CQ echo can enter ISR. */
    USART1->CR1 &= ~(USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    USART1->CR3 &= ~USART_CR3_EIE;
    uint32_t baud = 16000000UL / USART1->BRR;
    uint64_t deadline = iolink_time_get_us() + ((uint64_t) len * 11000000ULL / baud) + 2000U;
    int result = -1;
    while (USART1->ISR & USART_ISR_REACK) {
        if (iolink_time_get_us() >= deadline) goto finished;
    }
    USART1->ICR = USART_ICR_TCCF;
    for (size_t i = 0; i < len; ++i) {
        if (wait_status(USART_ISR_TXE_TXFNF, deadline) != 0) goto finished;
        USART1->TDR = data[i];
    }
    if (wait_status(USART_ISR_TC, deadline) == 0) result = (int) len;
finished:
    enable(user, false); /* release driver before accepting new receive bytes */
    if (result < 0) {
        ++board_tx_timeouts;
        USART1->CR1 &= ~USART_CR1_UE; /* abort any pending byte before reuse */
        USART1->RQR = USART_RQR_TXFRQ;
    }
    USART1->RQR = USART_RQR_RXFRQ;
    USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    USART1->CR3 |= USART_CR3_EIE;
    USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_UE;
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
