#ifndef EUART_SC_H
#define EUART_SC_H

// Pure SystemC header — no QEMU headers here.
#include <systemc>
#include <cstdint>
#include <vector>
#include <queue>

class EUART_SC : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(EUART_SC);

    EUART_SC(sc_core::sc_module_name name);

    // MMIO accessors called by the QEMU wrapper
    uint32_t read_reg(uint64_t offset);
    void     write_reg(uint64_t offset, uint32_t value);

private:
    // Simple register map (you can extend this)
    // 0x00 : RX data (read)
    // 0x04 : TX data (write)
    // 0x10 : DMA_SRC
    // 0x14 : DMA_DST
    // 0x18 : DMA_LEN
    // 0x1C : DMA_START (write to start)
    uint32_t dma_src;
    uint32_t dma_dst;
    uint32_t dma_len;
    uint32_t dma_ctrl;

    std::queue<uint8_t> rx_fifo;
    std::queue<uint8_t> tx_fifo;

    std::vector<uint8_t> device_mem; // device-local memory for DMA demo

    // internal threads
    void tx_thread();
    void dma_thread();

    sc_core::sc_event ev_tx;
    sc_core::sc_event ev_dma;
};
#endif
