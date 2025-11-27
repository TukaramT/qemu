#include <stdint.h>

#define EUART_BASE 0x0A100000
#define EUART_RX   (*(volatile unsigned int *)(EUART_BASE + 0x00))
#define EUART_TX   (*(volatile unsigned int *)(EUART_BASE + 0x04))
#define EUART_CTRL   (*(volatile unsigned int *)(EUART_BASE + 0x08))
#define EUART_STATUS   (*(volatile unsigned int *)(EUART_BASE + 0x0C))


// Control bits
#define EUART_TX_START (1 << 0)

// Status bits
#define EUART_STATUS_RX_READY (1 << 0)

void main(void)
{
    const char *msg = "Hello World, Welcome to QEMU->SYSTEMC integration\n";
    while (*msg) {
        EUART_TX = *msg++;
    }
    EUART_CTRL |= EUART_TX_START;

    // while(1);

    // ---------------- Echo loop -------------------
    while(1) {
        while (!(EUART_STATUS & EUART_STATUS_RX_READY));
        char c = EUART_RX;   // wait + read from RX
        EUART_TX = c;
        EUART_CTRL |= EUART_TX_START;
    } 
}
