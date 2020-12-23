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
#ifndef WHOST_DMA_H
#define WHOST_DMA_H

#include "hw/sysbus.h"
#include "qom/object.h"

struct whost_dma_regs {
    uint32_t start;
    uint32_t control;
    uint32_t src;
    uint32_t len;
    uint64_t dst;
    uint32_t settings;
    uint32_t irq_en;
    uint32_t irq_sta;
};

#define WHOST_DMA_REG_SIZE        0x1000

typedef struct WHostDMAState {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint64_t base;  // Base address for src offset
    qemu_irq    irq;
    struct whost_dma_regs regs;
} WHostDMAState;

#define TYPE_WHOST_DMA  "wavious.host.dma"
#define WHOST_DMA(obj)  \
    OBJECT_CHECK(WHostDMAState, (obj), TYPE_WHOST_DMA)

#endif /* WHOST_DMA_H */
