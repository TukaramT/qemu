// hw/char/euart_sc.cpp
#include <iostream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <inttypes.h>
#include "hw/char/euart_sc.h"

using namespace sc_core;

// Helper to get simulation time in ns
static inline uint64_t get_sim_time_ns() {
    return sc_time_stamp().to_default_time_units();
}

/* ------------------------------------------------------------------
 * Declare the QEMU memory access function we need.
 *
 * NOTE: cpu_physical_memory_read/write are macros in QEMU; the real
 * exported C symbol is cpu_physical_memory_rw().  We declare its
 * prototype here with simple types so we can call it from C++.
 *
 * Signature used:
 *   void cpu_physical_memory_rw(uint64_t addr, uint8_t *buf, int len, int is_write);
 *
 * This file must be built and linked into the QEMU executable so that
 * this symbol resolves at link time (it lives in softmmu).
 * ------------------------------------------------------------------ */
extern "C" {
    void cpu_physical_memory_rw(uint64_t addr, uint8_t *buf, int len, int is_write);
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
    
    case EUART_RX_DATA_LEN:
        return sizeof(rx_fifo);

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

    /* DMA reads: return shadowed DMA registers */
    case EUART_REG_DMA_SRC:
        return dma_src;
    case EUART_REG_DMA_DST:
        return dma_dst;
    case EUART_REG_DMA_LEN:
        return dma_len;
    case EUART_REG_DMA_CTRL:
        return dma_ctrl;

    case EUART_REG_CTRL:
        return ctrl_reg;

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

    /* DMA register writes (shadowed) */
    case EUART_REG_DMA_SRC:
        dma_src = val;
        break;
    case EUART_REG_DMA_DST:
        dma_dst = val;
        break;
    case EUART_REG_DMA_LEN:
        dma_len = val;
        break;
    case EUART_REG_DMA_CTRL:
        dma_ctrl = val;
        /* If START bit set, perform single-shot DMA */
        if (dma_ctrl & DMA_CTRL_START) {
            if (dma_len != 0) {
                do_dma_once();
            }
            /* clear START and set DONE flag (bit 8) */
            dma_ctrl &= ~DMA_CTRL_START;
            dma_ctrl |= (1u << 8);
        }
        break;

    default:
        break;
    }
}

size_t EUART_SC::pop_rx_bytes(uint8_t *buf, size_t n)
{
    size_t i = 0;
    while (i < n && !rx_fifo.empty()) {
        buf[i++] = rx_fifo.front();
        rx_fifo.pop();
    }
    if (rx_fifo.empty())
        status_reg &= ~EUART_STATUS_RX_READY;
    return i;
}

void EUART_SC::push_tx_bytes(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        tx_fifo.push(buf[i]);
    }
    if (!tx_fifo.empty()) {
        status_reg &= ~EUART_STATUS_TX_EMPTY;
        ev_tx.notify(SC_ZERO_TIME);
    }
}

void EUART_SC::do_dma_once()
{
    bool ram_to_tx = (dma_ctrl & DMA_CTRL_DIR_RAM2TX);

    if (!ram_to_tx) {
        // RX -> Guest RAM
        uint32_t len = dma_len;
        if (len == 0) {
            std::cout << "[SystemC][DMA] RX->RAM: len==0\n";
            return;
        }
        uint8_t *tmp = new uint8_t[len];
        size_t got = pop_rx_bytes(tmp, len);

        if (got != 0) {
            /* write into guest RAM using canonical QEMU API */
            cpu_physical_memory_rw((uint64_t)dma_dst, tmp, (int)got, 1); // is_write = 1
        }
        delete [] tmp;
    } else {
        // Guest RAM -> TX FIFO
        uint32_t len = dma_len;
        if (len == 0) {
            std::cout << "[SystemC][DMA] RAM->TX: len==0\n";
            return;
        }
        uint8_t *tmp = new uint8_t[len];
        /* read from guest RAM using canonical QEMU API */
        cpu_physical_memory_rw((uint64_t)dma_src, tmp, (int)len, 0); // is_write = 0
        push_tx_bytes(tmp, len);
        delete [] tmp;
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
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n > 0) {
            rx_fifo.push(static_cast<uint8_t>(c));
            status_reg |= EUART_STATUS_RX_READY;
        }
        wait(sc_time(1, SC_NS));
    }
}

int sc_main(int argc, char* argv[])
{
    // no top-level instantiation here; created by glue
    return 0;
}
