#include <hw/char/euart_sc.h>
#include <iostream>
#include <cstdio>

using namespace sc_core;

EUART_SC::EUART_SC(sc_module_name name)
: sc_module(name), socket("socket")
{
    socket.register_b_transport(this, &EUART_SC::b_transport);
    SC_THREAD(tx_thread);
    printf("[SystemC] EUART_SC constructor\n");
}

void EUART_SC::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    uint64_t addr = trans.get_address();
    uint32_t* data = reinterpret_cast<uint32_t*>(trans.get_data_ptr());
    uint32_t value;

    if (trans.is_read()) {
        value = read_reg(addr);
        memcpy(trans.get_data_ptr(), &value, 4);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }

    if (trans.is_write()) {
        value = *data;
        write_reg(addr, value);
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
        return status_reg;

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
        if (euart_ctrl & EUART_TX_START)
            ev_tx.notify(SC_ZERO_TIME);
        break;
    }
}

//
// ------------
void EUART_SC::rx_method_blocking()
{
    char c = '0';
    printf("[SC] Waiting on RX\n");
    while (c != '\n') {
        std::cin.get(c);        // <-- REAL BLOCK until user types a key
        rx_fifo.push((uint8_t)c);
        status_reg |= EUART_STATUS_RX_READY;
    }
}

//
// ------------- TX thread drives RX -------------
void EUART_SC::tx_thread()
{
    while (true)
    {
        wait(ev_tx);     // wait for firmware to start TX

        // Drain TX FIFO
        while (!tx_fifo.empty()) {
            uint8_t ch = tx_fifo.front();
            tx_fifo.pop();

            printf("%c", ch);
            fflush(stdout);
        }

        status_reg |= EUART_STATUS_TX_EMPTY;

        // NOW trigger RX and block for key
        rx_method_blocking();
    }
}

//
// Dummy sc_main for QEMU integration
//
int sc_main(int argc, char* argv[])
{
    return 0;
}
