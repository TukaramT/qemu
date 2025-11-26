#include "hw/char/euart_sc.h"

// Global instance
EUART_SC *uart_model = nullptr;

extern "C" {

void sc_init()
{
    std::cout << "sc_init in glue logic" << "\n";
    if (!uart_model) {
        uart_model = new EUART_SC("uart_model");
        sc_core::sc_start();
    }
}

void sc_write(uint32_t addr, uint32_t value)
{
    if (uart_model) {
        printf("[GLUE] sc_write addr=0x%x val=0x%x\n", addr, value);
        fflush(stdout);
        uart_model->write_reg(addr, value);
        printf("[GLUE] sc_write completed\n");
        fflush(stdout);
    }
}

uint32_t sc_read(uint32_t addr)
{
    if (!uart_model) {
        return 0;
    }
    return uart_model->read_reg(addr); 
}

} // extern "C"
