#ifndef HW_CHAR_EUART_SC_H
#define HW_CHAR_EUART_SC_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <queue>
#include <cstdint>
#include <cstddef>

using namespace sc_core;

// Basic EUART register offsets
#define EUART_REG_RXDATA       0x00
#define EUART_REG_TXDATA       0x04
#define EUART_REG_CTRL         0x08
#define EUART_REG_STATUS       0x0C
#define EUART_RX_DATA_LEN      0x10
#define EUART_REG_TIMER_PERIOD 0x20
#define EUART_REG_TIMER_CTRL   0x24

// DMA registers
#define EUART_REG_DMA_SRC   0x40    // guest physical address for RAM->TX reads
#define EUART_REG_DMA_DST   0x44    // guest physical address for RX->RAM writes
#define EUART_REG_DMA_LEN   0x48
#define EUART_REG_DMA_CTRL  0x4C

// DMA_CTRL bits
#define DMA_CTRL_START       (1u << 0)
#define DMA_CTRL_DIR_RAM2TX  (1u << 1) // 0 = RX -> RAM, 1 = RAM -> TX

// Status bits
#define EUART_STATUS_RX_READY  (1u << 0)
#define EUART_STATUS_TX_EMPTY  (1u << 1)

// Control bits example
#define EUART_TX_START         (1u << 0)

// Timer flags
#define TIMER_EN               (1u << 0)

class EUART_SC : public sc_module {
public:
    tlm_utils::simple_target_socket<EUART_SC> socket;

    SC_HAS_PROCESS(EUART_SC);
    EUART_SC(sc_module_name name);

    // TLM b_transport handler
    void b_transport(tlm::tlm_generic_payload& trans, sc_time& delay);

    // register accessors used by b_transport
    uint32_t read_reg(uint64_t addr);
    void write_reg(uint64_t addr, uint32_t val);

    // FIFOs used internally
    std::queue<uint8_t> rx_fifo;
    std::queue<uint8_t> tx_fifo;

private:
    // internal registers / state
    uint32_t status_reg = 0;
    uint32_t ctrl_reg = 0;
    uint32_t timer_ctrl = 0;
    uint64_t timer_start_ns = 0;
    uint32_t timer_period_hz = 0;

    // DMA shadow registers
    uint32_t dma_src = 0;
    uint32_t dma_dst = 0;
    uint32_t dma_len = 0;
    uint32_t dma_ctrl = 0;

    // events & threads
    sc_event ev_tx;
    void tx_thread();
    void rx_thread();

    // DMA helper
    void do_dma_once();

    // fifo helpers
    size_t pop_rx_bytes(uint8_t *buf, size_t n);
    void push_tx_bytes(const uint8_t *buf, size_t n);
};

#endif // HW_CHAR_EUART_SC_H
