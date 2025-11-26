/*
 * Enhanced UART with DMA and Timer (EUART)
 *
 * Behavior change:
 *  - Timer is not used to generate periodic IRQs automatically.
 *  - When the guest enables TIMER (TIMER_CTRL & TIMER_EN), the device
 *    records a start timestamp (virtual ns).
 *  - Guest writes TIMER_PERIOD with the desired frequency in Hz (uint32).
 *  - Reading TIMER_PERIOD returns the tick count (elapsed * freq / 1e9).
 *
 * Rest of the EUART behaviour (DMA, chardev, TX/RX) remains unchanged.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/char/euart.h"
#include "system/memory.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include <inttypes.h>

#define DPRINTF(fmt, ...) \
    do { \
        if (0) { \
            fprintf(stderr, "EUART: " fmt, ## __VA_ARGS__); \
        } \
    } while (0)

/* IRQ helpers */
static void euart_update_irq(EUARTState *s)
{
    uint32_t pending = s->int_status & s->int_enable;
    qemu_set_irq(s->irq, pending != 0);
}

static void euart_raise_irq(EUARTState *s, uint32_t irq_bit)
{
    s->int_status |= irq_bit;
    euart_update_irq(s);
}

/* TX completion: push FIFO to char backend */
static void euart_tx_complete(void *opaque)
{
    EUARTState *s = EUART(opaque);

    fprintf(stderr, "EUART: tx_complete called, tx_fifo_len=%u backend_connected=%d\n",
            s->tx_fifo_len, qemu_chr_fe_backend_connected(&s->chr));

    if (s->tx_fifo_len > 0 && qemu_chr_fe_backend_connected(&s->chr)) {
        ssize_t wr = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_fifo_len);
        fprintf(stderr, "EUART: tx_complete wrote %u bytes from fifo (wr=%zd)\n",
                s->tx_fifo_len, wr);
        s->tx_fifo_len = 0;
    }

    s->status |= EUART_STATUS_TX_READY;

    if (s->control & EUART_CTRL_TX_ENABLE) {
        euart_raise_irq(s, EUART_INT_TX);
    }
}

/* Immediate TX write when backend connected */
static void euart_transmit_byte(EUARTState *s, uint8_t byte)
{
    if (qemu_chr_fe_backend_connected(&s->chr)) {
        fprintf(stderr, "EUART: tx immediate backend_connected=1 byte=0x%02x (%c)\n",
                byte, (byte >= 32 && byte < 127) ? byte : '.');
        ssize_t wr = qemu_chr_fe_write(&s->chr, &byte, 1);
        fprintf(stderr, "EUART: qemu_chr_fe_write returned %zd\n", wr);

        /* emulate brief busy period */
        s->status &= ~EUART_STATUS_TX_READY;
        timer_mod(s->tx_timer,
                  (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000ULL);
        return;
    }

    /* FIFO fallback */
    if (s->tx_fifo_len < EUART_FIFO_SIZE) {
        s->tx_fifo[s->tx_fifo_len++] = byte;
        s->status &= ~EUART_STATUS_TX_READY;
        timer_mod(s->tx_timer,
                  (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000ULL);
    }
}

static void euart_receive_byte(EUARTState *s, uint8_t byte)
{
    if (s->rx_fifo_len < EUART_FIFO_SIZE) {
        s->rx_fifo[s->rx_fifo_len++] = byte;

        if (s->control & EUART_CTRL_RX_ENABLE) {
            euart_raise_irq(s, EUART_INT_RX);
        }
    }
}

/* chardev callbacks */
static int euart_can_receive(void *opaque)
{
    EUARTState *s = EUART(opaque);
    return EUART_FIFO_SIZE - s->rx_fifo_len;
}

static void euart_receive(void *opaque, const uint8_t *buf, int size)
{
    EUARTState *s = EUART(opaque);

    for (int i = 0; i < size; i++) {
        fprintf(stderr, "EUART RX BYTE = 0x%02X (%c)\n", buf[i],
                (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
        euart_receive_byte(s, buf[i]);
    }
    s->status |= EUART_STATUS_RX_READY;
}

static void euart_event(void *opaque, QEMUChrEvent event)
{
    (void)opaque;
    (void)event;
}

/* DMA step */
static void euart_dma_step(void *opaque)
{
    EUARTState *s = EUART(opaque);
    uint8_t buffer[EUART_DMA_CHUNK_SIZE];

    if (!(s->status & EUART_STATUS_DMA_BUSY)) {
        return;
    }

    uint32_t chunk = (s->dma_remaining > EUART_DMA_CHUNK_SIZE)
                         ? EUART_DMA_CHUNK_SIZE
                         : s->dma_remaining;

    if (s->dma_ctrl & EUART_DMA_DIR) {
        /* device -> guest (RX DMA) */
        uint32_t n = MIN(chunk, s->rx_fifo_len);

        if (n > 0) {
            cpu_physical_memory_write(s->dma_current_addr, s->rx_fifo, n);

            memmove(s->rx_fifo, s->rx_fifo + n, s->rx_fifo_len - n);
            s->rx_fifo_len -= n;

            if (s->rx_fifo_len == 0)
                s->status &= ~EUART_STATUS_RX_READY;

            s->dma_current_addr += n;
            s->dma_remaining -= n;
            s->dma_len -= n;
        }
    } else {
        /* guest -> device (TX DMA) */
        cpu_physical_memory_read(s->dma_current_addr, buffer, chunk);

        for (uint32_t i = 0; i < chunk; i++) {
            euart_transmit_byte(s, buffer[i]);
        }

        s->dma_current_addr += chunk;
        s->dma_remaining -= chunk;
        s->dma_len -= chunk;
    }

    if (s->dma_remaining == 0 ||
        ((s->dma_ctrl & EUART_DMA_DIR) && s->rx_fifo_len == 0)) {

        s->status &= ~EUART_STATUS_DMA_BUSY;
        s->dma_ctrl &= ~EUART_DMA_START;

        if (s->dma_ctrl & EUART_DMA_INT_EN)
            euart_raise_irq(s, EUART_INT_DMA);
    } else {
        timer_mod(s->dma_timer,
                  (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000ULL);
    }
}

static void euart_start_dma(EUARTState *s)
{
    if (s->dma_len == 0)
        return;

    s->status |= EUART_STATUS_DMA_BUSY;
    s->dma_remaining = s->dma_len;
    s->dma_current_addr =
        (s->dma_ctrl & EUART_DMA_DIR) ? s->dma_dst : s->dma_src;

    euart_dma_step(s);
}

/*
 * Timer: changed behavior (Option C)
 *
 * - timer_freq_hz: frequency in Hz written by the guest to TIMER_PERIOD.
 * - timer_start_ns: qemu virtual time when timer was enabled.
 * - timer_ctrl: controls TIMER_EN bit.
 *
 * When TIMER_EN transitions from 0->1 we record timer_start_ns.
 * When guest reads TIMER_PERIOD, if TIMER_EN is set, we return tick count:
 *    ticks = (now_ns - timer_start_ns) * timer_freq_hz / 1e9
 *
 * We keep timer_period member as the frequency value set by guest (Hz).
 * No periodic timer scheduling is used to generate IRQs automatically.
 */

static void euart_periodic_timer_tick(void *opaque)
{
    /* Not used for periodic free-running counter in Option C.
     * Keep function present in case other parts reuse it for IRQ generation.
     */
    EUARTState *s = EUART(opaque);

    /* Legacy behavior (not used) - simply raise IRQ if enabled.
     * But we won't schedule this function for Option C behavior.
     */
    if (s->timer_ctrl & EUART_TIMER_INT_EN) {
        euart_raise_irq(s, EUART_INT_TIMER);
    }
}

/* We no longer schedule a periodic timer for the free-running counter.
 * Record start time on enable, clear on disable.
 */
static void euart_start_timer_record(EUARTState *s)
{
    if (s->timer_period == 0) {
        /* If guest hasn't configured a frequency, don't record start. */
        s->timer_start_ns = 0;
        return;
    }
    s->timer_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->status |= EUART_STATUS_TIMER_ACTIVE;
    fprintf(stderr, "EUART: timer enabled - start_ns=%" PRIu64 " freq=%" PRIu32 "\n",
            s->timer_start_ns, s->timer_period);
}

static void euart_stop_timer_record(EUARTState *s)
{
    s->status &= ~EUART_STATUS_TIMER_ACTIVE;
    s->timer_start_ns = 0;
    fprintf(stderr, "EUART: timer disabled\n");
}

static void euart_reset_device(EUARTState *s)
{
    s->status = EUART_STATUS_TX_READY;
    s->control = 0;
    s->int_status = 0;
    s->int_enable = 0;
    s->dma_ctrl = 0;
    s->timer_ctrl = 0;

    s->rx_fifo_len = 0;
    s->tx_fifo_len = 0;

    /* timers may be NULL if called during init */
    if (s->dma_timer) timer_del(s->dma_timer);
    if (s->periodic_timer) timer_del(s->periodic_timer);
    if (s->tx_timer) timer_del(s->tx_timer);

    /* timer bookkeeping for Option C */
    s->timer_start_ns = 0;
    s->timer_period = 0; /* frequency Hz = 0 (not configured) */

    euart_update_irq(s);
}

static uint64_t euart_read(void *opaque, hwaddr offset, unsigned size)
{
    EUARTState *s = EUART(opaque);
    uint64_t ret = 0;

    switch (offset) {
    case EUART_REG_DATA:
        if (s->rx_fifo_len > 0) {
            ret = s->rx_fifo[0];
            memmove(s->rx_fifo,
                    s->rx_fifo + 1,
                    s->rx_fifo_len - 1);
            s->rx_fifo_len--;
            if (s->rx_fifo_len == 0)
                s->status &= ~EUART_STATUS_RX_READY;
        }
        break;

    case EUART_REG_STATUS:
        ret = s->status;
        break;

    case EUART_REG_CONTROL:
        ret = s->control;
        break;

    case EUART_REG_INT_STATUS:
        ret = s->int_status;
        break;

    case EUART_REG_INT_ENABLE:
        ret = s->int_enable;
        break;

    case EUART_REG_DMA_SRC:
        ret = s->dma_src;
        break;

    case EUART_REG_DMA_DST:
        ret = s->dma_dst;
        break;

    case EUART_REG_DMA_LEN:
        ret = s->dma_len;
        break;

    case EUART_REG_DMA_CTRL:
        ret = s->dma_ctrl;
        break;

    case EUART_REG_TIMER_PERIOD:
        /*
         * New semantics (Option C):
         *  - s->timer_period stores frequency in Hz (uint32)
         *  - if timer enabled and timer_start_ns set: return ticks
         *      ticks = (now_ns - timer_start_ns) * freq / 1e9
         *  - otherwise return configured frequency (as fallback)
         */
        if ((s->timer_ctrl & EUART_TIMER_EN) && s->timer_start_ns && s->timer_period) {
            uint64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            uint64_t elapsed_ns = (now_ns > s->timer_start_ns) ? (now_ns - s->timer_start_ns) : 0;
            /* compute ticks: (elapsed_ns * freq) / 1e9 */
            uint64_t ticks = (uint64_t)((__uint128_t)elapsed_ns * s->timer_period / 1000000000ULL);
            ret = ticks;
        } else {
            /* return the configured frequency (Hz) if present, else 0 */
            ret = s->timer_period;
        }
        break;

    case EUART_REG_TIMER_CTRL:
        ret = s->timer_ctrl;
        break;

    case EUART_REG_RX_DATA_LEN:
        /* Returns the len of data received*/
        ret = s->rx_fifo_len;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "EUART: Bad offset 0x%" HWADDR_PRIx "\n", offset);
        break;
    }

    DPRINTF("euart_read offset=0x%" PRIx64 " -> 0x%" PRIx64 "\n", (uint64_t)offset, ret);
    return ret;
}

static void euart_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    EUARTState *s = EUART(opaque);

    fprintf(stderr, "EUART: WRITE offset=0x%lx value=0x%lx\n",
            (unsigned long)offset, (unsigned long)value);

    switch (offset) {

    case EUART_REG_DATA:
        /* mask to 8-bit */
        if (s->control & EUART_CTRL_TX_ENABLE)
            euart_transmit_byte(s, (uint8_t)(value & 0xFF));
        break;

    case EUART_REG_CONTROL:
        s->control = value & 0x7;
        if (value & EUART_CTRL_RESET)
            euart_reset_device(s);
        break;

    case EUART_REG_INT_STATUS:
        s->int_status &= ~value;
        euart_update_irq(s);
        break;

    case EUART_REG_INT_ENABLE:
        s->int_enable = value & 0xF;
        euart_update_irq(s);
        break;

    case EUART_REG_DMA_SRC:
        s->dma_src = value;
        fprintf(stderr, "EUART: DMA_SRC write -> 0x%016" PRIx64 "\n", s->dma_src);
        break;

    case EUART_REG_DMA_DST:
        s->dma_dst = value;
        fprintf(stderr, "EUART: DMA_DST write -> 0x%016" PRIx64 "\n", s->dma_dst);
        break;

    case EUART_REG_DMA_LEN:
        s->dma_len = (uint32_t)value;
        fprintf(stderr, "EUART: DMA_LEN write -> %u\n", s->dma_len);
        break;

    case EUART_REG_DMA_CTRL:
        s->dma_ctrl = (uint32_t)(value & 0x7);
        fprintf(stderr, "EUART: DMA_CTRL write -> 0x%02x\n", s->dma_ctrl);
        if (value & EUART_DMA_START) {
            fprintf(stderr, "EUART: DMA START requested\n");
            euart_start_dma(s);
        }
        break;

    case EUART_REG_TIMER_PERIOD:
        /*
         * Guest writes desired frequency (Hz) into TIMER_PERIOD.
         * Store as 32-bit frequency and do not schedule periodic timer.
         */
        s->timer_period = (uint32_t)value;
        fprintf(stderr, "EUART: TIMER_PERIOD (freq Hz) write -> %u\n", s->timer_period);
        break;

    case EUART_REG_TIMER_CTRL: {
        uint32_t old = s->timer_ctrl;
        s->timer_ctrl = value & 0x7;
        fprintf(stderr, "EUART: TIMER_CTRL write -> 0x%02x (old 0x%02x)\n", s->timer_ctrl, old);

        if ((s->timer_ctrl & EUART_TIMER_EN) && !(old & EUART_TIMER_EN)) {
            /* enabled the timer: record start time (if frequency configured) */
            euart_start_timer_record(s);
        } else if (!(s->timer_ctrl & EUART_TIMER_EN) && (old & EUART_TIMER_EN)) {
            /* disabled */
            euart_stop_timer_record(s);
        }
        break;
    }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "EUART: Bad offset 0x%" HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps euart_ops = {
    .read = euart_read,
    .write = euart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* property check callback for QEMU 10 link property */
static void euart_prop_check_chardev(const Object *obj, const char *name,
                                     Object *val, Error **errp)
{
    if (val && !object_dynamic_cast(val, TYPE_CHARDEV)) {
        error_setg(errp, "Invalid chardev backend for EUART");
    }
}

static void euart_realize(DeviceState *dev, Error **errp)
{
    EUARTState *s = EUART(dev);

    /* create timers used by DMA/tx only; periodic timer for Option C isn't used
     * to generate free-running ticks, but keep objects for backwards compat.
     */
    s->dma_timer      = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_dma_step, s);
    s->periodic_timer = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_periodic_timer_tick, s);
    s->tx_timer       = timer_new(QEMU_CLOCK_VIRTUAL, 0, euart_tx_complete, s);

    /* link to chardev property (QEMU-10 style) */
    Object *obj = object_property_get_link(OBJECT(dev), "chardev", errp);
    if (*errp)
        return;

    if (obj) {
        Chardev *chr = CHARDEV(obj);

        fprintf(stderr, "EUART: realize found chardev object=%p\n", (void *)obj);

        qemu_chr_fe_init(&s->chr, chr, errp);
        if (*errp) {
            fprintf(stderr, "EUART: qemu_chr_fe_init FAILED\n");
            return;
        }

        fprintf(stderr, "EUART: qemu_chr_fe_init OK\n");

        qemu_chr_fe_set_handlers(
            &s->chr,
            euart_can_receive,
            euart_receive,
            euart_event,
            NULL,
            s,
            NULL,
            true);
    } else {
        fprintf(stderr, "EUART: realize no chardev linked\n");
    }

    euart_reset_device(s);
}

static void euart_unrealize(DeviceState *dev)
{
    EUARTState *s = EUART(dev);
    timer_free(s->dma_timer);
    timer_free(s->periodic_timer);
    timer_free(s->tx_timer);
}

static void euart_init(Object *obj)
{
    EUARTState *s = EUART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->dma_timer = NULL;
    s->periodic_timer = NULL;
    s->tx_timer = NULL;

    /* Add two fields in the state struct: timer_start_ns (uint64) and timer_period (uint32).
     * Make sure hw/char/euart.h has these members declared in EUARTState:
     *
     *   uint64_t timer_start_ns;
     *   uint32_t timer_period;   // frequency in Hz
     *
     * If not present, add them to the EUARTState definition.
     */

    memory_region_init_io(&s->iomem, obj, &euart_ops, s,
                          TYPE_EUART, EUART_REG_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    euart_reset_device(s);
}

/* VM state description - make sure timer_start_ns & timer_period are saved/restored */
static const VMStateDescription vmstate_euart = {
    .name = TYPE_EUART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data, EUARTState),
        VMSTATE_UINT32(status, EUARTState),
        VMSTATE_UINT32(control, EUARTState),
        VMSTATE_UINT32(int_status, EUARTState),
        VMSTATE_UINT32(int_enable, EUARTState),
        VMSTATE_UINT64(dma_src, EUARTState),
        VMSTATE_UINT64(dma_dst, EUARTState),
        VMSTATE_UINT32(dma_len, EUARTState),
        VMSTATE_UINT32(dma_ctrl, EUARTState),
        /* timer_period is used as frequency (Hz) */
        VMSTATE_UINT32(timer_period, EUARTState),
        VMSTATE_UINT32(timer_ctrl, EUARTState),
        VMSTATE_UINT64(timer_start_ns, EUARTState),
        VMSTATE_UINT8_ARRAY(rx_fifo, EUARTState, EUART_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(tx_fifo, EUARTState, EUART_FIFO_SIZE),
        VMSTATE_UINT32(rx_fifo_len, EUARTState),
        VMSTATE_UINT32(tx_fifo_len, EUARTState),
        VMSTATE_UINT32(dma_remaining, EUARTState),
        VMSTATE_UINT64(dma_current_addr, EUARTState),
        VMSTATE_END_OF_LIST()
    }
};

static void euart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize   = euart_realize;
    dc->unrealize = euart_unrealize;
    dc->vmsd      = &vmstate_euart;
    dc->user_creatable = true;
    // dc->categories[DEVICE_CATEGORY_MISC] = true;

    /* QEMU 10: link property */
    object_class_property_add_link(
        oc,
        "chardev",
        TYPE_CHARDEV,
        offsetof(EUARTState, chr),
        euart_prop_check_chardev,
        OBJ_PROP_LINK_STRONG
    );
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

type_init(euart_register_types)
