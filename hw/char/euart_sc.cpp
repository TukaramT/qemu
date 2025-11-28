#include <iostream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include "hw/char/euart_sc.h"

using namespace sc_core;

// Helper to get simulation time in ns
static inline uint64_t get_sim_time_ns() {
    return sc_time_stamp().to_default_time_units();
}

EUART_SC::EUART_SC(sc_module_name name)
    : sc_module(name),
      socket("socket")
{
    socket.register_b_transport(this, &EUART_SC::b_transport);

    SC_THREAD(tx_thread);
    SC_THREAD(rx_thread);

    std::cout << "[SystemC] EUART_SC created\n";
}

void EUART_SC::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    uint64_t addr = trans.get_address();
    uint32_t* data = reinterpret_cast<uint32_t*>(trans.get_data_ptr());

    if (trans.is_read()) {
        uint32_t v = read_reg(addr);
        memcpy(data, &v, 4);
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

uint32_t EUART_SC::read_reg(uint64_t addr)
{
    switch (addr)
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

    case EUART_REG_TIMER_PERIOD:
        if ((timer_ctrl & TIMER_EN) &&
            timer_start_ns != 0 &&
            timer_period_hz != 0)
        {
            uint64_t now = get_sim_time_ns();
            uint64_t elapsed = now - timer_start_ns;

            uint64_t ticks =
                (uint64_t)(((__uint128_t)elapsed * timer_period_hz) /
                           1000000000ULL);

            return (uint32_t)ticks;
        }
        return timer_period_hz;

    case EUART_REG_TIMER_CTRL:
        return timer_ctrl;

    default:
        return 0;
    }
}

void EUART_SC::write_reg(uint64_t addr, uint32_t val)
{
    switch (addr)
    {
    case EUART_REG_TXDATA:
        tx_fifo.push(val & 0xFF);
        status_reg &= ~EUART_STATUS_TX_EMPTY;
        break;

    case EUART_REG_CTRL:
        ctrl_reg = val;
        if (val & EUART_TX_START)
            ev_tx.notify(SC_ZERO_TIME);
        break;

    case EUART_REG_TIMER_PERIOD:
        timer_period_hz = val;
        break;

    case EUART_REG_TIMER_CTRL: {
        timer_ctrl = val;

        if (timer_period_hz == 0)
            timer_start_ns = 0;
        else
            timer_start_ns = get_sim_time_ns();

        break;
    }

    default:
        break;
    }
}

void EUART_SC::tx_thread()
{
    while (true)
    {
        wait(ev_tx);

        while (!tx_fifo.empty()) {
            char ch = tx_fifo.front();
            tx_fifo.pop();
            std::cout << ch << std::flush;
        }

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

int sc_main(int argc, char* argv[])
{
    return 0;
}
