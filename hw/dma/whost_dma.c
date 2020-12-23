/*
 * Wavious Host DMA emulation
 *
 * Copyright (c) 2020 Wavious, Inc.
 *
 * Author:
 *   William Patty (wpatty@wavious.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "hw/dma/whost_dma.h"

#define DMA_START       0x000
#define     DMA_START_START         BIT(0)
#define     DMA_START_MUX_START     BIT(1)
#define DMA_CONTROLS    0x004
#define     DMA_CONTROLS_MAX_BYTES_MSK  0xFF
#define DMA_SRC_ADDR    0x008
#define DMA_LEN         0x00C
#define DMA_DST_ADDR    0x010
#define DMA_DST_ADDR_HI 0x014
#define DMA_SETTINGS    0x018
#define     SETTING_MR_MODE         BIT(4)
#define     SETTING_MEM_MAP_MODE    BIT(5)
#define DMA_IRQ_EN      0x54
#define     DMA_IRQ_EN_DONE         BIT(1)
#define DMA_IRQ_STA     0x58
#define     DMA_IRQ_STA_DONE        BIT(1)
#define     DMA_IRQ_STA_IN          BIT(6)

static void whost_dma_run(WHostDMAState *s)
{

    uint64_t src = s->base + s->regs.src;
    uint64_t dst = s->regs.dst;
    int64_t len = s->regs.len + 1;
    int rsize = s->regs.control & DMA_CONTROLS_MAX_BYTES_MSK;
    int size;
    uint8_t buf[256];

    while (len)
    {
        size = MIN(rsize, len);
        cpu_physical_memory_read(src, buf, size);
        cpu_physical_memory_write(dst, buf, size);
        len -= size;
    }
    s->regs.irq_sta |= DMA_IRQ_STA_DONE;
    s->regs.irq_sta |= DMA_IRQ_STA_IN;
    return;
}

static inline void whost_dma_update_irq(WHostDMAState *s)
{
    bool done_ie;
    done_ie = !!(s->regs.irq_en & DMA_IRQ_EN_DONE);

    if (done_ie && (s->regs.irq_sta & DMA_IRQ_STA_IN))
    {
        qemu_irq_raise(s->irq);
    }
    else
    {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t whost_dma_read(void *opaque, hwaddr offset, unsigned size)
{
    WHostDMAState *s = opaque;
    offset &= 0xFFF;
    uint64_t val = 0;
    switch(offset)
    {
        case DMA_START:
            val = s->regs.start;
            break;
        case DMA_CONTROLS:
            val = s->regs.control;
            break;
        case DMA_SRC_ADDR:
            val = s->regs.src;
            break;
        case DMA_LEN:
            val = s->regs.len;
            break;
        case DMA_DST_ADDR:
            if (size == 4)
            {
                val = s->regs.dst & 0x00000000FFFFFFFF;
            }
            else
            {
               val = s->regs.dst;
            }
            break;
        case DMA_DST_ADDR_HI:
            val = s->regs.dst >> 32;
            break;
        case DMA_SETTINGS:
            val = s->regs.settings;
            break;
        case DMA_IRQ_EN:
            val = s->regs.irq_en;
            break;
        case DMA_IRQ_STA:
            val = s->regs.irq_sta;
            break;
        default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }

    return val;
}

static void whost_dma_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    WHostDMAState *s = opaque;
    offset &= 0xFFF;
    switch(offset)
    {
        case DMA_START:
            s->regs.start = value;

            if (value & DMA_START_START)
            {
                whost_dma_run(s);
            }
            whost_dma_update_irq(s);
            break;
        case DMA_CONTROLS:
            s->regs.control = value;
            break;
        case DMA_SRC_ADDR:
            s->regs.src = value;
            break;
        case DMA_LEN:
            s->regs.len = value;
            break;
        case DMA_DST_ADDR:
            if (size == 4)
            {
                s->regs.dst &= 0xFFFFFFFF00000000;
                s->regs.dst |= value & 0xFFFFFFFF;
            }
            else
            {
                s->regs.dst = value;
            }
            break;
        case DMA_DST_ADDR_HI:
            s->regs.dst &= 0x00000000FFFFFFFF;
            s->regs.dst |= value << 32;
            break;
        case DMA_IRQ_EN:
            s->regs.irq_en = value;
            whost_dma_update_irq(s);
            break;
        case DMA_SETTINGS:
            s->regs.settings = value;
            break;
        case DMA_IRQ_STA:
            s->regs.irq_sta ^= value & 0x3F;
            // Clear when all interrupt status have been cleared
            if (s->regs.irq_sta == DMA_IRQ_STA_IN)
            {
                s->regs.irq_sta = 0;
            }
            whost_dma_update_irq(s);
            break;
        default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps whost_dma_ops = {
    .read = whost_dma_read,
    .write = whost_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* there are 32-bit and 64-bit wide registers */
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    }
};

static void whost_dma_reset(DeviceState *dev)
{
    WHostDMAState *s = WHOST_DMA(dev);
    s->regs.start = 0x0;
    s->regs.control = 0x1FFF;
    s->regs.src = 0x0;
    s->regs.dst = 0x0;
    s->regs.len = 0x3FFF;
    s->regs.irq_en = 0;
    s->regs.irq_sta = 0;
}

static void whost_dma_realize(DeviceState *dev, Error **errp)
{
    WHostDMAState *s = WHOST_DMA(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &whost_dma_ops, s,
                       TYPE_WHOST_DMA, WHOST_DMA_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static Property whost_dma_properties[] = {
    DEFINE_PROP_UINT64("base", WHostDMAState, base, 0x70000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void whost_dma_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, whost_dma_properties);
    dc->desc = "Wavious Host DMA controller";
    dc->realize = whost_dma_realize;
    dc->reset = whost_dma_reset;
}

static const TypeInfo whost_dma_info = {
    .name          = TYPE_WHOST_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(WHostDMAState),
    .class_init    = whost_dma_class_init,
};

static void whost_dma_register_types(void)
{
    type_register_static(&whost_dma_info);
}

type_init(whost_dma_register_types);
