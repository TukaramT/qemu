#define EUART_BASE 0x0A100000
#define EUART_TX   (*(volatile unsigned int *)(EUART_BASE + 0x04))

static inline void delay() {
    for (volatile int i=0; i<10000000; i++);
}

void main(void)
{
    const char *msg = "TX Hello from Guest!\n";
    while (*msg) {
        EUART_TX = *msg++;
        delay();
    }

    while (1);
}
