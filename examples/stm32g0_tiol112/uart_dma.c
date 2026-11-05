/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "uart_dma.h"

volatile uint32_t board_uart_backend;
board_dma_stats_t board_dma_stats;

#if IOLINK_G0_UART_BACKEND != IOLINK_G0_UART_CMSIS_IRQ
#include "dma_rx_ring.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"
#include "stm32g0xx.h"

#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
#include "stm32g0xx_hal.h"
static UART_HandleTypeDef uart;
static DMA_HandleTypeDef rx_dma;
static DMA_HandleTypeDef tx_dma;
#endif

static volatile uint8_t rx_data[BOARD_DMA_RX_SIZE];
static board_dma_rx_ring_t rx_ring;
static volatile bool rx_active;
static volatile bool rx_error_pending;
static volatile int tx_state; /* 0 idle, 1 DMA/UART active, 2 UART TC, -1 failed */
static bool configured;

static void rx_fail(bool overrun)
{
    if (!rx_error_pending && overrun) ++board_dma_stats.rx_overruns;
    rx_error_pending = true;
    rx_active = false;
    rx_ring.failed = true;
    USART1->CR3 &= ~USART_CR3_DMAR;
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
}

static void rx_event(bool half, bool complete)
{
    if (board_dma_rx_ring_event(&rx_ring, half, complete) != 0) {
        rx_fail(true);
        return;
    }
    if (half) ++board_dma_stats.rx_half_events;
    if (complete) ++board_dma_stats.rx_wraps;
}

/* Called with interrupts masked, or in the channel IRQ at the UART priority.
 * HT+TC is deliberately rejected: sticky flags cannot count multiple laps. */
static void rx_service_flags(void)
{
    const uint32_t flags = DMA1->ISR;
    if (!rx_active) {
        DMA1->IFCR = DMA_IFCR_CGIF1;
        return;
    }
    if ((flags & (DMA_ISR_HTIF1 | DMA_ISR_TCIF1)) == (DMA_ISR_HTIF1 | DMA_ISR_TCIF1)) {
        rx_fail(true);
        DMA1->IFCR = DMA_IFCR_CGIF1;
        return;
    }
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    /* HAL handles one flag per entry. An error takes precedence over data. */
    if (flags & DMA_ISR_TEIF1) DMA1->IFCR = DMA_IFCR_CHTIF1 | DMA_IFCR_CTCIF1;
    if (flags & (DMA_ISR_TEIF1 | DMA_ISR_HTIF1 | DMA_ISR_TCIF1)) HAL_DMA_IRQHandler(&rx_dma);
#else
    if (flags & DMA_ISR_TEIF1) {
        ++board_dma_stats.dma_errors;
        rx_fail(false);
        DMA1->IFCR = DMA_IFCR_CGIF1;
        return;
    }
    if (flags & DMA_ISR_HTIF1) {
        DMA1->IFCR = DMA_IFCR_CHTIF1;
        rx_event(true, false);
    }
    if (flags & DMA_ISR_TCIF1) {
        DMA1->IFCR = DMA_IFCR_CTCIF1;
        rx_event(false, true);
    }
#endif
}

void DMA1_Channel1_IRQHandler(void)
{
    rx_service_flags();
}

void DMA1_Channel2_3_IRQHandler(void)
{
    const uint32_t flags = DMA1->ISR;
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    if (flags & DMA_ISR_TEIF2)
        DMA1->IFCR = DMA_IFCR_CHTIF2 | DMA_IFCR_CTCIF2;
    else if ((flags & DMA_ISR_TCIF2) && tx_state == 1)
        ++board_dma_stats.tx_dma_completed;
    HAL_DMA_IRQHandler(&tx_dma);
#else
    if (flags & DMA_ISR_TEIF2) {
        USART1->CR3 &= ~USART_CR3_DMAT;
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CGIF2;
        USART1->CR1 &= ~USART_CR1_TCIE;
        ++board_dma_stats.dma_errors;
        tx_state = -1;
    }
    else if (flags & DMA_ISR_TCIF2) {
        USART1->CR3 &= ~USART_CR3_DMAT;
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CGIF2;
        if (tx_state == 1) {
            ++board_dma_stats.tx_dma_completed;
            /* The last byte has reached TDR, but is not yet complete on wire. */
            USART1->CR1 |= USART_CR1_TCIE;
        }
    }
#endif
}

void board_dma_rx_pause(void)
{
    rx_active = false;
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    if (configured) {
        (void) HAL_UART_AbortReceive(&uart);
        /* An error callback can already have cleared DMAR. Ensure the channel
         * and HAL state are stopped even on that path. These APIs do not wait. */
        (void) HAL_DMA_Abort(&rx_dma);
    }
#else
    USART1->CR3 &= ~USART_CR3_DMAR;
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
#endif
    DMA1->IFCR = DMA_IFCR_CGIF1;
}

int board_dma_rx_resume(void)
{
    if (!configured) return -1;
    board_dma_rx_ring_reset(&rx_ring);
    DMA1->IFCR = DMA_IFCR_CGIF1;
    USART1->RQR = USART_RQR_RXFRQ;
    USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    rx_active = true;
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    if (HAL_UART_Receive_DMA(&uart, (uint8_t*) rx_data, BOARD_DMA_RX_SIZE) != HAL_OK) {
        rx_fail(false);
        return -1;
    }
#else
    DMA1_Channel1->CCR =
        DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_PL_1;
    DMA1_Channel1->CPAR = (uint32_t) &USART1->RDR;
    DMA1_Channel1->CMAR = (uint32_t) rx_data;
    DMA1_Channel1->CNDTR = BOARD_DMA_RX_SIZE;
    __DMB();
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    USART1->CR1 |= USART_CR1_PEIE;
    USART1->CR3 |= USART_CR3_EIE | USART_CR3_DMAR;
#endif
    ++board_dma_stats.rx_rearms;
    return 0;
}

int board_dma_recv(uint8_t* byte, uint32_t* observed_us)
{
    if (byte == NULL || observed_us == NULL) return -1;
    iolink_critical_enter();
    uint32_t remaining = 0U;
    if (rx_active) {
        /* PRIMASK stops the ISR, not DMA. Recheck sticky flags after CNDTR so
         * an intervening circular reload is accounted before using its index.
         * At most one new byte can arrive per UART character; the bounded
         * retry fails closed if that hardware contract cannot be observed. */
        unsigned attempts;
        for (attempts = 0U; attempts < 3U; ++attempts) {
            rx_service_flags();
            if (!rx_active) break;
            remaining = DMA1_Channel1->CNDTR;
            if (!(DMA1->ISR & (DMA_ISR_TCIF1 | DMA_ISR_TEIF1))) break;
        }
        if (attempts == 3U) rx_fail(true);
    }
    if (rx_error_pending) {
        rx_error_pending = false;
        board_dma_rx_pause();
        (void) board_dma_rx_resume();
        iolink_critical_exit();
        return -1; /* Forces the DLL to discard any partially assembled frame. */
    }
    int result = 0;
    if (rx_active) {
        __DMB();
        result = board_dma_rx_ring_pop(&rx_ring, rx_data, remaining, TIM2->CNT, byte);
        if (result > 0) {
            *observed_us = rx_ring.observed_us;
            ++board_dma_stats.rx_bytes;
        }
        else if (result < 0) {
            rx_fail(true);
            rx_error_pending = false;
            board_dma_rx_pause();
            (void) board_dma_rx_resume();
        }
    }
    iolink_critical_exit();
    return result;
}

int board_dma_tx_begin(const uint8_t* data, size_t length)
{
    if (!configured || data == NULL || length == 0U || length > 65535U || tx_state == 1) return -1;
    tx_state = 1;
    USART1->ICR = USART_ICR_TCCF;
    DMA1->IFCR = DMA_IFCR_CGIF2;
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    if (HAL_UART_Transmit_DMA(&uart, data, (uint16_t) length) != HAL_OK) {
        tx_state = -1;
        return -1;
    }
    /* No TX half-buffer work is needed for a single synchronous frame. */
    DMA1_Channel2->CCR &= ~DMA_CCR_HTIE;
#else
    DMA1_Channel2->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_PL_1;
    DMA1_Channel2->CPAR = (uint32_t) &USART1->TDR;
    DMA1_Channel2->CMAR = (uint32_t) data;
    DMA1_Channel2->CNDTR = (uint32_t) length;
    __DMB();
    DMA1_Channel2->CCR |= DMA_CCR_EN;
    USART1->CR3 |= USART_CR3_DMAT;
#endif
    ++board_dma_stats.tx_started;
    return 0;
}

int board_dma_tx_poll(void)
{
    if (tx_state == 2) return 1;
    return tx_state < 0 ? -1 : 0;
}

void board_dma_tx_abort(void)
{
    if (tx_state == 1 || tx_state < 0) ++board_dma_stats.tx_aborts;
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    if (configured) {
        (void) HAL_UART_AbortTransmit(&uart);
        (void) HAL_DMA_Abort(&tx_dma);
    }
#else
    USART1->CR3 &= ~USART_CR3_DMAT;
    USART1->CR1 &= ~USART_CR1_TCIE;
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
#endif
    DMA1->IFCR = DMA_IFCR_CGIF2;
    tx_state = 0;
}

void board_dma_stop(void)
{
    if (configured) {
        board_dma_rx_pause();
        board_dma_tx_abort();
    }
    configured = false;
    rx_error_pending = false;
}

void board_dma_uart_irq(void)
{
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    HAL_UART_IRQHandler(&uart);
#else
    const uint32_t status = USART1->ISR;
    if (status & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
        USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        ++board_dma_stats.uart_errors;
        rx_fail(false);
        if (tx_state == 1) tx_state = -1;
    }
    if ((status & USART_ISR_TC) && (USART1->CR1 & USART_CR1_TCIE)) {
        USART1->CR1 &= ~USART_CR1_TCIE;
        USART1->ICR = USART_ICR_TCCF;
        if (tx_state == 1) {
            ++board_dma_stats.tx_uart_completed;
            tx_state = 2;
        }
    }
#endif
}

int board_dma_configure(uint32_t baud)
{
    board_dma_stop();
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    (void) RCC->AHBENR;
    DMA1_Channel1->CCR = 0U;
    DMA1_Channel2->CCR = 0U;
    DMA1->IFCR = DMA_IFCR_CGIF1 | DMA_IFCR_CGIF2;
    NVIC_SetPriority(DMA1_Channel1_IRQn, 1U);
    NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1U);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
    uart.Instance = USART1;
    uart.Init.BaudRate = baud;
    uart.Init.WordLength = UART_WORDLENGTH_9B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_EVEN;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    rx_dma.Instance = DMA1_Channel1;
    rx_dma.Init.Request = DMA_REQUEST_USART1_RX;
    rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    rx_dma.Init.Mode = DMA_CIRCULAR;
    rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    tx_dma.Instance = DMA1_Channel2;
    tx_dma.Init.Request = DMA_REQUEST_USART1_TX;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    tx_dma.Init.Mode = DMA_NORMAL;
    tx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    __HAL_LINKDMA(&uart, hdmarx, rx_dma);
    __HAL_LINKDMA(&uart, hdmatx, tx_dma);
    if (HAL_DMA_Init(&rx_dma) != HAL_OK || HAL_DMA_Init(&tx_dma) != HAL_OK ||
        HAL_UART_Init(&uart) != HAL_OK)
        return -1;
#else
    /* Common BSP sets BRR and 8E1 before enabling DMA. */
    (void) baud;
    DMAMUX1_Channel0->CCR = 50U; /* USART1_RX: RM0444/ST LL_DMAMUX_REQ_USART1_RX */
    DMAMUX1_Channel1->CCR = 51U; /* USART1_TX */
    DMAMUX1_ChannelStatus->CFR = 3U;
    USART1->CR1 =
        USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE | USART_CR1_UE;
#endif
    configured = true;
    tx_state = 0;
    rx_error_pending = false;
    return board_dma_rx_resume();
}

#if IOLINK_G0_UART_BACKEND == IOLINK_G0_UART_HAL_DMA
uint32_t HAL_GetTick(void)
{
    /* TIM2 is already running before HAL UART initialization. No HAL_Init or
     * SysTick ownership is needed; retain monotonicity across 32-bit us wrap. */
    return iolink_time_get_ms();
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef* handle)
{
    if (handle == &uart && rx_active) rx_event(true, false);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* handle)
{
    if (handle == &uart && rx_active) rx_event(false, true);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* handle)
{
    if (handle == &uart && tx_state == 1) {
        ++board_dma_stats.tx_uart_completed;
        tx_state = 2;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* handle)
{
    if (handle != &uart) return;
    if (handle->ErrorCode & HAL_UART_ERROR_DMA)
        ++board_dma_stats.dma_errors;
    else
        ++board_dma_stats.uart_errors;
    /* A bad RX byte may already be in RAM before HAL observes PE/FE/ORE. */
    rx_fail(false);
    if (tx_state == 1) tx_state = -1;
}
#endif
#endif
