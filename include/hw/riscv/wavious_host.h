/*
 * Wavious WHOST series machine interface
 *
 * Copyright (c) 2020 Wavious, Inc.
 *
 * Author:
 * William Patty (wpatty@wavious.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_WAVIOUS_HOST_H
#define HW_WAVIOUS_HOST_H

#include "hw/riscv/sifive_cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/dma/whost_dma.h"

#define WAV_MSEL_DEBUG  (0x3)

#define TYPE_RISCV_WHOST_SOC "riscv.wavious.host.soc"
#define RISCV_WHOST_SOC(obj) \
    OBJECT_CHECK(WaviousHostSoCState, (obj), TYPE_RISCV_WHOST_SOC)

typedef struct WaviousHostSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CPUClusterState e_cluster;
    CPUClusterState u_cluster;
    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
    DeviceState *plic;
    WHostDMAState mem_reader;
    char *cpu_type;
} WaviousHostSoCState;


#define TYPE_WAVIOUS_HOST_MACHINE MACHINE_TYPE_NAME("wavious_host")
#define WAVIOUS_HOST_MACHINE(obj) \
    OBJECT_CHECK(WaviousHostState, (obj), TYPE_WAVIOUS_HOST_MACHINE)

typedef struct WaviousHostState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    WaviousHostSoCState soc;
    uint32_t msel;
} WaviousHostState;

enum {
    WAVIOUS_HOST_DEV_DEBUG,
    WAVIOUS_HOST_DEV_MROM,
    WAVIOUS_HOST_DEV_CLINT,
    WAVIOUS_HOST_DEV_L2CC,
    WAVIOUS_HOST_DEV_PLIC,
    WAVIOUS_HOST_DEV_UART0,
    WAVIOUS_HOST_DEV_SRAM,
    WAVIOUS_HOST_DEV_FLASH,
    WAVIOUS_HOST_DEV_DMA, // MEM Reader
    WAVIOUS_HOST_DEV_GPIO,
    WAVIOUS_HOST_DEV_DRAM,
};

// TODO: Finalize
enum {
    WAVIOUS_HOST_DMA_IRQ = 1,
    WAVIOUS_HOST_UART_IRQ = 64,
};


#define WAVIOUS_HOST_MANAGEMENT_CPU_COUNT   1
#define WAVIOUS_HOST_COMPUTE_CPU_COUNT      4

#define WAVIOUS_HOST_PLIC_HART_CONFIG       "MS"
#define WAVIOUS_HOST_PLIC_NUM_SOURCES       65
#define WAVIOUS_HOST_PLIC_NUM_PRIORITIES    7
#define WAVIOUS_HOST_PLIC_PRIORITY_BASE     0x04
#define WAVIOUS_HOST_PLIC_PENDING_BASE      0x1000
#define WAVIOUS_HOST_PLIC_ENABLE_BASE       0x2000
#define WAVIOUS_HOST_PLIC_ENABLE_STRIDE     0x80
#define WAVIOUS_HOST_PLIC_CONTEXT_BASE      0x200000
#define WAVIOUS_HOST_PLIC_CONTEXT_STRIDE    0x100

#endif /* HW_WAVIOUS_HOST_H */
