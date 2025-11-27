#ifndef EUART_SC_H
#define EUART_SC_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <queue>

#define EUART_REG_RXDATA   0x00
#define EUART_REG_TXDATA   0x04
#define EUART_REG_CTRL     0x08
#define EUART_REG_STATUS   0x0C
#define EUART_RX_DATA_LEN  0x10

#define EUART_STATUS_TX_EMPTY  (1U << 0)
#define EUART_STATUS_RX_READY  (1U << 1)

#define EUART_TX_START         (1U << 0)

class EUART_SC : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<EUART_SC> socket;
    SC_HAS_PROCESS(EUART_SC);

    EUART_SC(sc_core::sc_module_name name);

    // TLM target callback
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

    uint32_t read_reg(uint64_t offset);
    void     write_reg(uint64_t offset, uint32_t value);

private:
    uint32_t status_reg = EUART_STATUS_TX_EMPTY;
    uint32_t euart_ctrl = 0;
    int32_t data_len = 0;

    std::queue<uint8_t> rx_fifo;
    std::queue<uint8_t> tx_fifo;

    sc_core::sc_event ev_tx;

    void tx_thread();                // TX drives RX
    void rx_method_blocking();       // plain C++ method (not SystemC!)
};

#endif
