#include <stdint.h>

#define EUART_BASE 0x0A100000

#define EUART_RX        (*(volatile unsigned int *)(EUART_BASE + 0x00))
#define EUART_TX        (*(volatile unsigned int *)(EUART_BASE + 0x04))
#define EUART_CTRL      (*(volatile unsigned int *)(EUART_BASE + 0x08))
#define EUART_STATUS    (*(volatile unsigned int *)(EUART_BASE + 0x0C))

// Timer registers (from SystemC/QEMU timer)
#define EUART_TIMER_PERIOD  (*(volatile unsigned int *)(EUART_BASE + 0x20))
#define EUART_TIMER_CTRL    (*(volatile unsigned int *)(EUART_BASE + 0x24))

// Control bits
#define EUART_TX_START      (1 << 0)
#define TIMER_EN            (1 << 0)

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
    uart_puts(" secs\n");
}


void main(void)
{
    // ------------------------------------------------
    // Print startup banner
    // ------------------------------------------------
    uart_puts("Hello World, Welcome to QEMU->SYSTEMC integration\n");

    // ------------------------------------------------
    // Configure TIMER
    // ------------------------------------------------
    EUART_TIMER_PERIOD = 1000000;      // 1 MHz timer
    EUART_TIMER_CTRL = TIMER_EN;       // enable timer

    // ------------------------------------------------
    // Echo loop
    // ------------------------------------------------
    char buf[100];

    while (1) {
        while (!(EUART_STATUS & EUART_STATUS_RX_READY));

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
        uart_puts("Ticks :- ");
        uart_put_dec(ticks);
    }
}
