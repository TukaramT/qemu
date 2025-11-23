#if 1
#include <stdint.h>

#define EUART_BASE       0x0A100000UL

/* Registers */
#define EUART_REG_DATA      (*(volatile uint32_t *)(EUART_BASE + 0x00))
#define EUART_REG_STATUS    (*(volatile uint32_t *)(EUART_BASE + 0x04))
#define EUART_REG_CONTROL   (*(volatile uint32_t *)(EUART_BASE + 0x08))
#define EUART_INT_STATUS    (*(volatile uint32_t *)(EUART_BASE + 0x0C))
#define EUART_INT_ENABLE    (*(volatile uint32_t *)(EUART_BASE + 0x10))
#define EUART_DMA_SRC       (*(volatile uint32_t *)(EUART_BASE + 0x14))
#define EUART_DMA_DST       (*(volatile uint32_t *)(EUART_BASE + 0x18))
#define EUART_DMA_LEN       (*(volatile uint32_t *)(EUART_BASE + 0x1C))
#define EUART_DMA_CTRL      (*(volatile uint32_t *)(EUART_BASE + 0x20))

/* Timer registers */
#define EUART_TIMER_PERIOD  (*(volatile uint32_t *)(EUART_BASE + 0x24)) /* Hz when writing, ticks when reading */
#define EUART_TIMER_CTRL    (*(volatile uint32_t *)(EUART_BASE + 0x28))

#define EUART_RX_DATA_LEN      (*(volatile uint32_t *)(EUART_BASE + 0x2C))

/* Bits */
#define EUART_STATUS_TX_READY   (1 << 0)
#define EUART_STATUS_RX_READY   (1 << 1)

#define EUART_CTRL_TX_ENABLE    (1 << 0)
#define EUART_CTRL_RX_ENABLE    (1 << 1)

/* DMA bits */
#define DMA_DIR_DEV2MEM   (1 << 0)
#define DMA_START         (1 << 1)

/* TIMER CTRL bits */
#define TIMER_EN          (1 << 0)

/****************************************************************/
/* UART helpers */
/****************************************************************/
static void euart_putc(char c)
{
    while (!(EUART_REG_STATUS & EUART_STATUS_TX_READY));
    EUART_REG_DATA = c;
}

static void euart_puts(const char *s)
{
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            euart_putc('\r');
            euart_putc('\n');
        } else {
            euart_putc(c);
        }
    }
}

/****************************************************************/
/* DMA helpers */
/****************************************************************/
static char dma_buf[64];

static void dma_rx_test(uint32_t bytes)
{
    EUART_DMA_DST = (uint32_t)dma_buf;
    EUART_DMA_LEN = bytes;
    EUART_DMA_CTRL = DMA_DIR_DEV2MEM | DMA_START;
}

static void dma_tx_test(uint32_t bytes)
{
    EUART_DMA_SRC = (uint32_t)dma_buf;
    EUART_DMA_LEN = bytes;
    EUART_DMA_CTRL = DMA_START;
}

/****************************************************************/
/* TIMER SETUP — MATCHES NEW MODEL (option-C) */
/****************************************************************/

/* Configure timer frequency and enable */
static void timer_start_hz(uint32_t frequency_hz)
{
    EUART_TIMER_PERIOD = frequency_hz;   // model stores frequency
    EUART_TIMER_CTRL = TIMER_EN;         // record start time inside model
}

static void euart_put_uint32(uint32_t v)
{
    if (v == 0) {
        euart_putc('0');
        return;
    }

    uint32_t divisor = 1;

    /* find the highest divisor (power of 10) */
    while (v / divisor >= 10) {
        divisor *= 10;
    }

    /* now print digits from MSB to LSB */
    while (divisor > 0) {
        uint32_t digit = v / divisor;
        euart_putc('0' + digit);
        v = v % divisor;
        divisor /= 10;
    }
}

/****************************************************************/
/* MAIN */
/****************************************************************/
void main(void)
{
    /* Enable TX/RX */
    EUART_REG_CONTROL = EUART_CTRL_TX_ENABLE | EUART_CTRL_RX_ENABLE;

    euart_puts("Welcome to Test EUART\n");

    /******************************************************
     * START TIMER — Example: 1 MHz tick counter
     ******************************************************/
    timer_start_hz(1000000);   // 1,000,000 ticks per second

    uint32_t last_ticks = 0;
    uint32_t show_time = 0;

    uint32_t rx = 1;
    while (rx) {
        if (EUART_REG_STATUS & EUART_STATUS_RX_READY)
        {
            char data = (char)EUART_REG_DATA;
            euart_putc(data);
            euart_putc('\n');

            rx = 0;
        }
    }

    while (show_time < 5) {
        /******************************************************
        * POLL TIMER TICK VALUE
        * Reading TIMER_PERIOD returns free-running tick count
        ******************************************************/
        uint32_t ticks = EUART_TIMER_PERIOD;   // device returns computed ticks

        if (ticks - last_ticks >= 10000000)      // 10 second @ 1 MHz
        {
            last_ticks = ticks;

            euart_put_uint32(ticks);
            euart_putc('\n');
            show_time++;
        }
    }

    while (1)
    {

        /******************************************************
         * DMA RX/TX (unchanged)
         ******************************************************/
        if (EUART_REG_STATUS & EUART_STATUS_RX_READY)
        {
            uint32_t count = EUART_RX_DATA_LEN;

            dma_rx_test(count);
            while (EUART_DMA_CTRL & DMA_START);

            dma_tx_test(count);

            for (uint32_t i = 0; i < count; i++)
                dma_buf[i] = 0;
        }
    }
}

#endif
