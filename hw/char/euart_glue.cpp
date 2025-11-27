#include "hw/char/euart_sc.h"
#include <thread>
#include <atomic>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

//
// ---------------------------------------------------------------
//  Minimal SystemC module to hold initiator socket
// ---------------------------------------------------------------
//
struct SC_GLUE : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<SC_GLUE> socket;

    SC_GLUE(sc_core::sc_module_name nm)
        : sc_module(nm),
          socket("socket")
    {}
};

//
// ---------------------------------------------------------------
//  Global pointers
// ---------------------------------------------------------------
//
static SC_GLUE   *glue_module = nullptr;
EUART_SC         *uart_model  = nullptr;

extern "C" {

//
// ---------------------------------------------------------------
//  SystemC background runner (non-blocking for QEMU)
// ---------------------------------------------------------------
//
void start_systemc_background()
{
    static std::thread sc_thread;
    static std::atomic_bool started{false};

    if (started.exchange(true))
        return;

    sc_thread = std::thread([]() {
        while (true) {
            sc_core::sc_start(1, sc_core::SC_NS);
        }
    });

    sc_thread.detach();
}

//
// ---------------------------------------------------------------
//  SystemC initialization (called from QEMU device init)
// ---------------------------------------------------------------
//
void sc_init()
{
    if (!uart_model)
    {
        uart_model   = new EUART_SC("uart_model");
        glue_module  = new SC_GLUE("glue_module");

        // Bind:   initiator → target
        glue_module->socket.bind(uart_model->socket);

        start_systemc_background();
    }
}

//
// ---------------------------------------------------------------
//  WRITE: QEMU → TLM → EUART_SC::b_transport()
// ---------------------------------------------------------------
//
void sc_write(uint32_t addr, uint32_t value)
{
    if (!glue_module)
        return;

    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    trans.set_data_length(4);

    glue_module->socket->b_transport(trans, delay);
}

//
// ---------------------------------------------------------------
//  READ: QEMU → TLM → EUART_SC::b_transport()
// ---------------------------------------------------------------
//
uint32_t sc_read(uint32_t addr)
{
    if (!glue_module)
        return 0;

    uint32_t data = 0;

    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    trans.set_data_length(4);

    glue_module->socket->b_transport(trans, delay);

    return data;
}

} // extern "C"
