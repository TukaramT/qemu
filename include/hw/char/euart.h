#ifndef HW_CHAR_EUART_H
#define HW_CHAR_EUART_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_EUART "euart"
OBJECT_DECLARE_SIMPLE_TYPE(EUARTState, EUART)

#define EUART_REG_SIZE 0x1000

typedef struct EUARTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    void *backend; /* opaque pointer to SystemC instance */
} EUARTState;

#endif /* HW_CHAR_EUART_H */
