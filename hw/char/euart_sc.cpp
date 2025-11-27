#include "hw/char/euart_sc.h"
#include <iostream>
#include <cstdio>
#include <unistd.h>   // read()
#include <fcntl.h>    // fcntl()
#include <errno.h>
#include <cstring>

using namespace sc_core;

EUART_SC::EUART_SC(sc_module_name name)
: sc_module(name),
  socket("socket")
{
    // register target callback
    socket.register_b_transport(this, &EUART_SC::b_transport);

    // SystemC threads
    SC_THREAD(tx_thread);
    SC_THREAD(rx_thread);

    printf("[SystemC] EUART_SC constructed\n");
}

void EUART_SC::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    uint64_t addr = trans.get_address();
    uint32_t *data = reinterpret_cast<uint32_t*>(trans.get_data_ptr());

    if (trans.is_read()) {
        uint32_t v = read_reg(addr);
        memcpy(trans.get_data_ptr(), &v, sizeof(v));
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }

    if (trans.is_write()) {
        uint32_t v = *data;
        write_reg(addr, v);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }

    trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
}

uint32_t EUART_SC::read_reg(uint64_t offset)
{
    switch (offset)
    {
        case EUART_REG_RXDATA:
            if (!rx_fifo.empty()) {
                uint8_t v = rx_fifo.front();
                rx_fifo.pop();
                if (rx_fifo.empty())
                    status_reg &= ~EUART_STATUS_RX_READY;
                return v;
            }
            return 0;

        case EUART_REG_STATUS:
            return status_reg.load();

        default:
            return 0;
    }
}

void EUART_SC::write_reg(uint64_t offset, uint32_t value)
{
    switch (offset)
    {
        case EUART_REG_TXDATA:
            tx_fifo.push(value & 0xFF);
            status_reg &= ~EUART_STATUS_TX_EMPTY;
            break;

        case EUART_REG_CTRL:
            euart_ctrl = value;
            if (euart_ctrl & EUART_TX_START) {
                ev_tx.notify(SC_ZERO_TIME);
            }
            break;
    }
}

void EUART_SC::tx_thread()
{
    while (true)
    {
        wait(ev_tx);

        while (!tx_fifo.empty()) {
            uint8_t ch = tx_fifo.front();
            tx_fifo.pop();

            // Print to host stdout immediately
            printf("%c", ch);
            fflush(stdout);
        }

        // mark TX empty
        status_reg |= EUART_STATUS_TX_EMPTY;
    }
}

void EUART_SC::rx_thread()
{
    int fd = 0; // stdin
    int flags = fcntl(fd, F_GETFL);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    while (true) {
        // Try to read single byte from stdin (non-blocking)
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n > 0) {
            // push into FIFO and set status bit
            rx_fifo.push(static_cast<uint8_t>(c));
            status_reg |= EUART_STATUS_RX_READY;
        } 
        // Wait in simulation time so we don't hog CPU or block the kernel
        wait(sc_time(1, SC_NS));
    }
}

// Dummy sc_main so the module can be linked in a cosim environment
int sc_main(int argc, char* argv[])
{
    return 0;
}
