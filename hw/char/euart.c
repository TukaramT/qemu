#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/char/euart.h"

/* ----------------------------------------------------------
 * External C++ glue functions
 * (implemented in euart_glue.cpp)
 * ---------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

void sc_init(void);
void sc_write(uint32_t addr, uint32_t value);
uint32_t sc_read(uint32_t addr);

#ifdef __cplusplus
}
#endif


/* ----------------------------------------------------------
 * QEMU MMIO callbacks
 * ---------------------------------------------------------- */

static uint64_t euart_read(void *opaque, hwaddr addr, unsigned size)
{
    // fprintf(stderr, "[EUART_WRAPPER]: READ offset=0x%lx\n",
    //         (unsigned long)addr);
    return sc_read((uint32_t)addr);
}

static void euart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    // fprintf(stderr, "[EUART_WRAPPER]: WRITE offset=0x%lx value=0x%lx\n",
    //         (unsigned long)addr, (unsigned long)value);
    sc_write((uint32_t)addr, (uint32_t)value);
}

static const MemoryRegionOps euart_ops = {
    .read = euart_read,
    .write = euart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


/* ----------------------------------------------------------
 * Device initialization
 * ---------------------------------------------------------- */

static void euart_init(Object *obj)
{
    EUARTState *s = EUART(obj);

    memory_region_init_io(&s->mmio, obj, &euart_ops, s,
                          "euart-mmio", 0x1000);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    /* Call SystemC world init */
    sc_init();
}


/* ----------------------------------------------------------
 * Type registration
 * ---------------------------------------------------------- */

static void euart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "External SystemC-backed EUART";
}

static const TypeInfo euart_info = {
    .name          = TYPE_EUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EUARTState),
    .instance_init = euart_init,
    .class_init    = euart_class_init,
};

static void euart_register_types(void)
{
    type_register_static(&euart_info);
}

type_init(euart_register_types);
