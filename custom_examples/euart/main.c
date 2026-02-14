#include <stdint.h>

#define EUART_BASE 0x0A100000

#define EUART_RX        (*(volatile unsigned int *)(EUART_BASE + 0x00))
#define EUART_TX        (*(volatile unsigned int *)(EUART_BASE + 0x04))
#define EUART_CTRL      (*(volatile unsigned int *)(EUART_BASE + 0x08))
#define EUART_STATUS    (*(volatile unsigned int *)(EUART_BASE + 0x0C))
#define EUART_RX_DATA_LEN      (*(volatile uint32_t *)(EUART_BASE + 0x10))

// DMA Registers
#define EUART_DMA_SRC       (*(volatile unsigned int *)(EUART_BASE + 0x40))
#define EUART_DMA_DST       (*(volatile unsigned int *)(EUART_BASE + 0x44))
#define EUART_DMA_LEN       (*(volatile unsigned int *)(EUART_BASE + 0x48))
#define EUART_DMA_CTRL      (*(volatile unsigned int *)(EUART_BASE + 0x4C))

// Timer registers (from SystemC/QEMU timer)
#define EUART_TIMER_PERIOD  (*(volatile unsigned int *)(EUART_BASE + 0x20))
#define EUART_TIMER_CTRL    (*(volatile unsigned int *)(EUART_BASE + 0x24))

// Control bits
#define EUART_TX_START      (1 << 0)
#define TIMER_EN            (1 << 0)
#define DMA_START           (1 << 0)
#define DMA_DIR_RAM2TX      (1 << 1)   // 0 = RX->RAM, 1 = RAM->TX

// Status bits
#define EUART_STATUS_RX_READY (1 << 0)
#define EUART_STATUS_TX_EMPTY (1 << 1)

static void uart_puts(const char *s)
{
    while (*s) {
        EUART_TX = *s++;
    }
    EUART_CTRL |= EUART_TX_START;
}

static void uart_put_dec(uint32_t v)
{
    if (v == 0) {
        EUART_TX = '0';
        EUART_CTRL |= EUART_TX_START;
        return;
    }

    /* find highest power of 10 */
    uint32_t divisor = 1;
    while (v / divisor >= 10) {
        divisor *= 10;
    }

    /* print digits */
    while (divisor > 0) {
        uint32_t digit = v / divisor;
        char ch = '0' + digit;

        EUART_TX = ch;
        EUART_CTRL |= EUART_TX_START;

        v = v % divisor;
        divisor /= 10;
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
    EUART_DMA_CTRL = DMA_START;
}

static void dma_tx_test(uint32_t bytes)
{
    EUART_DMA_SRC = (uint32_t)dma_buf;
    EUART_DMA_LEN = bytes;
    EUART_DMA_CTRL = DMA_START | DMA_DIR_RAM2TX;
}

void main(void)
{
    // ------------------------------------------------
    // Print startup banner
    // ------------------------------------------------
    uart_puts("Hello World, Welcome to QEMU->SYSTEMC integration\n");

    uint32_t freq = 1000000;     // 1 MHz
    // ------------------------------------------------
    // Configure TIMER
    // ------------------------------------------------
    EUART_TIMER_PERIOD = freq;
    EUART_TIMER_CTRL = TIMER_EN;       // enable timer

    uart_puts("Test DMA\n");


    while (!(EUART_STATUS & EUART_STATUS_RX_READY));

    uint32_t count = EUART_RX_DATA_LEN;

    dma_rx_test(count);
    while (EUART_DMA_CTRL & DMA_START);

    dma_tx_test(count);

    for (uint32_t i = 0; i < count; i++)
        dma_buf[i] = 0;   
    
    uart_puts("Test I/O and Timer\n");
        
    while (1) {
        while (!(EUART_STATUS & EUART_STATUS_RX_READY));

        // ------------------------------------------------
        // Echo loop
        // ------------------------------------------------
        char buf[100];
        int i = 0;
        char c = 0;

        while (c != '\n') {
            c = EUART_RX;
            buf[i] = c;
            EUART_TX = buf[i++];
        }
        EUART_CTRL |= EUART_TX_START;

        while (!(EUART_STATUS & EUART_STATUS_TX_EMPTY));

        uint32_t ticks = EUART_TIMER_PERIOD;  // read counter
        uart_puts("Time :- ");
        uart_put_dec((ticks * 1000000)/freq);   // Gives time in milliseconds
        uart_puts(" us\n");
    }
}
