#include <hw/char/euart_sc.h>
#include <iostream>
#include <iomanip>

using namespace sc_core;
using namespace sc_dt;
using namespace std;

EUART_SC::EUART_SC(sc_module_name name)
    : sc_module(name), dma_src(0), dma_dst(0), dma_len(0), dma_ctrl(0)
{
    // device-local memory (for DMA demo)
    device_mem.resize(65536, 0);

    SC_THREAD(tx_thread);
    SC_THREAD(dma_thread);

    printf("[SystemC] EUART_SC constructor\n");
    fflush(stdout);
    // Seed rx_fifo with example data (so guest can read something)
    // const char *msg = "Hello-from-SystemC-EUART\n";
    // for (const char *p = msg; *p; ++p) rx_fifo.push((uint8_t)*p);
}

uint32_t EUART_SC::read_reg(uint64_t offset)
{
    switch (offset) {
    case 0x00: // RX data
        if (!rx_fifo.empty()) {
            uint8_t v = rx_fifo.front();
            rx_fifo.pop();
            return (uint32_t)v;
        }
        return 0;
    case 0x04: // TX read (not used)
        if (!tx_fifo.empty()) {
            uint8_t v = tx_fifo.front();
            tx_fifo.pop();
            return (uint32_t)v;
        }
        return 0;
    case 0x10: return dma_src;
    case 0x14: return dma_dst;
    case 0x18: return dma_len;
    case 0x1C: return dma_ctrl;
    default:
        return 0;
    }
}

void EUART_SC::write_reg(uint64_t offset, uint32_t value)
{
    printf("[SystemC] EUART_SC write_reg\n");
    fflush(stdout);
    switch (offset) {
    case 0x04: // TX data
        {
            uint8_t ch = value & 0xFF;

            tx_fifo.push(ch);
            ev_tx.notify(1, SC_NS);
        }
        break;
    case 0x10: dma_src = value; break;
    case 0x14: dma_dst = value; break;
    case 0x18: dma_len = value; break;
    case 0x1C:
        // write any non-zero to start DMA
        // if (value != 0) {
        //     dma_ctrl = value;
        //     ev_dma.notify(SC_ZERO_TIME);
        // }
        break;
    default:
        break;
    }
}

void EUART_SC::tx_thread()
{
    while (true) {
        printf("[SystemC] Tx thread called\n");
        fflush(stdout);
        wait(ev_tx);
        printf("[SystemC] Tx thread notified\n");
        fflush(stdout);
        while (!tx_fifo.empty()) {
            printf("[SystemC] Tx thread printing data\n");
            fflush(stdout);
            uint8_t ch = tx_fifo.front();
            tx_fifo.pop();
            // Print to stdout so you can see it in QEMU's terminal.
            printf("%c\n", (char)ch);
            fflush(stdout);
            // simulate a small transmit delay
            wait(sc_time(100, SC_US));
        }
    }
}

void EUART_SC::dma_thread()
{
    // while (true) {
    //     wait(ev_dma);
    //     // perform a simple device-local DMA for demonstration:
    //     // copy device_mem[dma_src .. dma_src+dma_len) to device_mem[dma_dst .. )
    //     uint32_t src = dma_src;
    //     uint32_t dst = dma_dst;
    //     uint32_t len = dma_len;

    //     if (len == 0) continue;

    //     // bounds check and clamp
    //     uint32_t maxlen = device_mem.size();
    //     if (src >= maxlen) src = 0;
    //     if (dst >= maxlen) dst = 0;
    //     if (len > maxlen) len = maxlen;

    //     for (uint32_t i = 0; i < len; ++i) {
    //         device_mem[dst + i] = device_mem[src + i];
    //         // for demo, also push into rx_fifo so guest can read data after DMA
    //         rx_fifo.push(device_mem[dst + i]);
    //     }

    //     // simulate DMA duration
    //     wait(sc_time(len * 1, SC_US));
    // }
}

// Dummy sc_main for SystemC so QEMU can link SystemC models

int sc_main(int argc, char* argv[])
{
    return 0;   // QEMU does not use SystemC's entry point
}
