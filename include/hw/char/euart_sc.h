#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <queue>
#include <cstdint>
#include <atomic>

#define EUART_REG_RXDATA  0x00
#define EUART_REG_TXDATA  0x04
#define EUART_REG_CTRL    0x08
#define EUART_REG_STATUS  0x0C

#define EUART_TX_START        (1 << 0)
#define EUART_STATUS_RX_READY (1 << 0)
#define EUART_STATUS_TX_EMPTY (1 << 1)

struct EUART_SC : sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<EUART_SC> socket;

    SC_HAS_PROCESS(EUART_SC);
    EUART_SC(sc_core::sc_module_name name);

    // TLM entry point
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

    // register access
    uint32_t read_reg(uint64_t addr);
    void write_reg(uint64_t addr, uint32_t value);

    // threads
    void tx_thread();
    void rx_thread();    // SystemC RX thread (non-blocking stdin poll)

private:
    std::queue<uint8_t> rx_fifo;
    std::queue<uint8_t> tx_fifo;

    std::atomic<uint32_t> status_reg{EUART_STATUS_TX_EMPTY};
    uint32_t euart_ctrl = 0;

    sc_core::sc_event ev_tx;
};
