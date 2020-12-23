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
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/cpu/cluster.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/wavious_host.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/boot.h"
#include "hw/char/sifive_uart.h"
#include "hw/intc/sifive_clint.h"
#include "hw/intc/sifive_plic.h"
#include "chardev/char.h"
#include "net/eth.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} wavious_host_memmap[] = {
    [WAVIOUS_HOST_DEV_DEBUG] =  {        0x0,     0x1000 },
    [WAVIOUS_HOST_DEV_MROM] =   {    0x10000,    0x10000 },
    [WAVIOUS_HOST_DEV_CLINT] =  {  0x2000000,    0x10000 },
    [WAVIOUS_HOST_DEV_L2CC] =   {  0x2010000,     0x1000 },
    [WAVIOUS_HOST_DEV_PLIC] =   {  0xc000000,  0x4000000 },
    [WAVIOUS_HOST_DEV_UART0] =  { 0x54000000,     0x1000 },
    [WAVIOUS_HOST_DEV_SRAM] =   { 0x60000000,    0x20000 },
    [WAVIOUS_HOST_DEV_FLASH] =  { 0x70000000,  0x8000000 },
    [WAVIOUS_HOST_DEV_DMA] =    { 0x90000000,     0x1000 },
    [WAVIOUS_HOST_DEV_GPIO] =   { 0xA0000010,        0x4 },
    [WAVIOUS_HOST_DEV_DRAM] =   { 0xC0000000,        0x0 },
};

static void wavious_host_machine_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = wavious_host_memmap;
    WaviousHostState *s = WAVIOUS_HOST_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    target_ulong start_addr = memmap[WAVIOUS_HOST_DEV_MROM].base;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RISCV_WHOST_SOC);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                             &error_abort);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    /* register RAM */
    memory_region_init_ram(main_mem, NULL, "riscv.wavious.host.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[WAVIOUS_HOST_DEV_DRAM].base,
                                main_mem);
    /* register QSPI0 Flash */
    memory_region_init_ram(flash, NULL, "riscv.wavious.host.flash",
                           memmap[WAVIOUS_HOST_DEV_FLASH].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[WAVIOUS_HOST_DEV_FLASH].base,
                                flash);

    /* Reflect MSEL state in Memory; Read Only "GPIO" */
    rom_add_blob_fixed_as("gpio.msel", &s->msel, sizeof(uint32_t),
                          memmap[WAVIOUS_HOST_DEV_GPIO].base, &address_space_memory);

    /*
     * ZSBL should be doing this jump in real system but cannot load
     * ZSBL and FSBL at same time as they share SRAM region. This is a
     * QEMU limitation. It's okay since don't need to verify this jump in
     * ZSBL with QEMU. That can be doen elsewhere.
     */
    if(s->msel == WAV_MSEL_DEBUG)
    {
        start_addr =memmap[WAVIOUS_HOST_DEV_SRAM].base;
        riscv_find_and_load_firmware(machine, "app.bin", start_addr, NULL);

        // Jump code to SRAM
        uint32_t reset_vec[2] = {
            0x60000537,
            0x00008502,
        };

        /* copy in the reset vector in little_endian byte order */
        for (int i = 0; i < ARRAY_SIZE(reset_vec); i++) {
            reset_vec[i] = cpu_to_le32(reset_vec[i]);
        }

        /* Place Reset Vector in ROM */
        rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                    memmap[WAVIOUS_HOST_DEV_MROM].base + 0x40, &address_space_memory);
    }
    else
    {
        start_addr = memmap[WAVIOUS_HOST_DEV_MROM].base;
        riscv_find_and_load_firmware(machine, "zsbl.bin", start_addr, NULL);
    }
}

static void wavious_host_machine_get_uint32_prop(Object *obj, Visitor *v,
                                                 const char *name, void *opaque,
                                                 Error **errp)
{
    visit_type_uint32(v, name, (uint32_t *)opaque, errp);
}

static void wavious_host_machine_set_uint32_prop(Object *obj, Visitor *v,
                                                 const char *name, void *opaque,
                                                 Error **errp)
{
    visit_type_uint32(v, name, (uint32_t *)opaque, errp);
}

static void wavious_host_machine_instance_init(Object *obj)
{
    WaviousHostState *s = WAVIOUS_HOST_MACHINE(obj);

    s->msel = 0;
    object_property_add(obj, "msel", "uint32",
                        wavious_host_machine_get_uint32_prop,
                        wavious_host_machine_set_uint32_prop, NULL, &s->msel);
    object_property_set_description(obj, "msel",
                                    "Mode Select (MSEL[3:0]) pin state");
}

static void wavious_host_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Board compatible with Wavious Host";
    mc->init = wavious_host_machine_init;
    mc->max_cpus = WAVIOUS_HOST_MANAGEMENT_CPU_COUNT + WAVIOUS_HOST_COMPUTE_CPU_COUNT;
    mc->min_cpus = WAVIOUS_HOST_MANAGEMENT_CPU_COUNT + 1;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE64;
    mc->default_cpus = mc->min_cpus;
}

static const TypeInfo wavious_host_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("wavious_host"),
    .parent     = TYPE_MACHINE,
    .class_init = wavious_host_machine_class_init,
    .instance_init = wavious_host_machine_instance_init,
    .instance_size = sizeof(WaviousHostState),
};

static void wavious_host_machine_init_register_types(void)
{
    type_register_static(&wavious_host_machine_typeinfo);
}

type_init(wavious_host_machine_init_register_types)

static void wavious_host_soc_instance_init(Object *obj)
{
    WaviousHostSoCState *s = RISCV_WHOST_SOC(obj);
    const struct MemmapEntry *memmap = wavious_host_memmap;
    object_initialize_child(obj, "e-cluster", &s->e_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->e_cluster), "cluster-id", 0);

    object_initialize_child(OBJECT(&s->e_cluster), "e-cpus", &s->e_cpus,
                            TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "num-harts", 1);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->e_cpus), "cpu-type", SIFIVE_E_CPU);
    qdev_prop_set_uint64(DEVICE(&s->e_cpus), "resetvec", memmap[WAVIOUS_HOST_DEV_MROM].base + 0x40);

    object_initialize_child(obj, "u-cluster", &s->u_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->u_cluster), "cluster-id", 1);

    object_initialize_child(OBJECT(&s->u_cluster), "u-cpus", &s->u_cpus,
                            TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "mem_reader", &s->mem_reader, TYPE_WHOST_DMA);
}


static void wavious_host_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    WaviousHostSoCState *s = RISCV_WHOST_SOC(dev);
    const struct MemmapEntry *memmap = wavious_host_memmap;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *gpio = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    size_t plic_hart_config_len;
    int i;

    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "num-harts", ms->smp.cpus - 1);
    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "hartid-base", 1);
    qdev_prop_set_string(DEVICE(&s->u_cpus), "cpu-type", s->cpu_type);
    qdev_prop_set_uint64(DEVICE(&s->u_cpus), "resetvec", memmap[WAVIOUS_HOST_DEV_MROM].base + 0x40);

    sysbus_realize(SYS_BUS_DEVICE(&s->e_cpus), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->u_cpus), &error_abort);

    /*
     * The cluster must be realized after the RISC-V hart array container,
     * as the container's CPU object is only created on realize, and the
     * CPU must exist and have been parented into the cluster before the
     * cluster is realized.
     */
    qdev_realize(DEVICE(&s->e_cluster), NULL, &error_abort);
    qdev_realize(DEVICE(&s->u_cluster), NULL, &error_abort);

    /* boot rom */
    memory_region_init_rom(rom, OBJECT(dev), "riscv.wavious.host.mrom",
                           memmap[WAVIOUS_HOST_DEV_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[WAVIOUS_HOST_DEV_MROM].base,
                                rom);

    /* sram */
    memory_region_init_ram(sram, NULL, "riscv.wavious.host.sram",
                           memmap[WAVIOUS_HOST_DEV_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[WAVIOUS_HOST_DEV_SRAM].base,
                                sram);

    /* GPIO Pins */
    memory_region_init_rom(gpio, NULL, "riscv.wavious.host.gpio",
                           memmap[WAVIOUS_HOST_DEV_GPIO].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[WAVIOUS_HOST_DEV_GPIO].base,
                                gpio);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(WAVIOUS_HOST_PLIC_HART_CONFIG) + 1) *
                           ms->smp.cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < ms->smp.cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, "," WAVIOUS_HOST_PLIC_HART_CONFIG,
                    plic_hart_config_len);
        } else {
            strncat(plic_hart_config, "M", plic_hart_config_len);
        }
        plic_hart_config_len -= (strlen(WAVIOUS_HOST_PLIC_HART_CONFIG) + 1);
    }

    /* MMIO */
    s->plic = sifive_plic_create(memmap[WAVIOUS_HOST_DEV_PLIC].base,
        plic_hart_config, 0,
        WAVIOUS_HOST_PLIC_NUM_SOURCES,
        WAVIOUS_HOST_PLIC_NUM_PRIORITIES,
        WAVIOUS_HOST_PLIC_PRIORITY_BASE,
        WAVIOUS_HOST_PLIC_PENDING_BASE,
        WAVIOUS_HOST_PLIC_ENABLE_BASE,
        WAVIOUS_HOST_PLIC_ENABLE_STRIDE,
        WAVIOUS_HOST_PLIC_CONTEXT_BASE,
        WAVIOUS_HOST_PLIC_CONTEXT_STRIDE,
        memmap[WAVIOUS_HOST_DEV_PLIC].size);
    g_free(plic_hart_config);
    sifive_uart_create(system_memory, memmap[WAVIOUS_HOST_DEV_UART0].base,
        serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), WAVIOUS_HOST_UART_IRQ));
    sifive_clint_create(memmap[WAVIOUS_HOST_DEV_CLINT].base,
        memmap[WAVIOUS_HOST_DEV_CLINT].size, 0, ms->smp.cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE,
        SIFIVE_CLINT_TIMEBASE_FREQ, false);

    /* Memory Reader */
    qdev_prop_set_uint64(DEVICE(&s->mem_reader), "base", memmap[WAVIOUS_HOST_DEV_FLASH].base);
    sysbus_realize(SYS_BUS_DEVICE(&s->mem_reader), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mem_reader), 0, memmap[WAVIOUS_HOST_DEV_DMA].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mem_reader), 0,
                       qdev_get_gpio_in(DEVICE(s->plic),
                                        WAVIOUS_HOST_DMA_IRQ));

    create_unimplemented_device("riscv.wavious.host.l2cc",
        memmap[WAVIOUS_HOST_DEV_L2CC].base, memmap[WAVIOUS_HOST_DEV_L2CC].size);

}

static Property wavious_host_soc_props[] = {
    DEFINE_PROP_STRING("cpu-type", WaviousHostSoCState, cpu_type),
    DEFINE_PROP_END_OF_LIST()
};

static void wavious_host_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, wavious_host_soc_props);
    dc->realize = wavious_host_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo wavious_host_soc_type_info = {
    .name = TYPE_RISCV_WHOST_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(WaviousHostSoCState),
    .instance_init = wavious_host_soc_instance_init,
    .class_init = wavious_host_soc_class_init,
};

static void wavious_host_register_types(void)
{
    type_register_static(&wavious_host_soc_type_info);
}

type_init(wavious_host_register_types);
