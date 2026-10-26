/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SoC level definitions for the Realtek RTL9617C (Cortina "Taurus").
 *
 * Boards and drivers of this SoC share the register windows and hand-off
 * conventions declared here; nothing in this file is board specific.
 */
#ifndef __ASM_ARCH_SOC_H
#define __ASM_ARCH_SOC_H

#include <linux/types.h>
#include <asm/io.h>

/* NI Ethernet packet memory (XRAM) window. */
#ifndef NI_XRAM_BASE
#define NI_XRAM_BASE		0xf4500000
#endif

/* Register accessors used by the NI MAC driver. */
#ifndef CA_REG_READ
#define CA_REG_READ(addr)		readl((unsigned long)(addr))
#define CA_REG_WRITE(value, addr)	writel((value), (unsigned long)(addr))
#endif

#ifndef KSEG1_ATU_XLAT
#define KSEG1_ATU_XLAT(addr)	(addr)
#endif

/*
 * BL31 keeps its secure region at the end of the low 256 MiB. The size here is
 * the window the shipping kernel is told to avoid (1 MiB starting at
 * 0x0eeff000); BL31 itself reports a 256 KiB secure DRAM area inside it. The
 * rest of the bank, from 0x0f000000, is ordinary memory.
 */
#define TAURUS_SECURE_DRAM_START	0x0eeff000
#define TAURUS_SECURE_DRAM_END		0x0f000000

/* BL2 publishes the trained DRAM capacity here, in MiB. */
#define TAURUS_BL2_DRAM_MB	0x0111100c

/* Scratch register that has to be cleared when entering from RAM. */
#define TAURUS_BOOT_SCRATCH	0xf4321000

/*
 * GPIO. The controller has five banks of 32 pins; a pin is addressed by its
 * global number, so pin 136 is bit 8 of bank 4.
 */
#define TAURUS_GPIO_BANKS	5
#define TAURUS_GPIO_BANK_PINS	32
#define TAURUS_GPIO_PINS	(TAURUS_GPIO_BANKS * TAURUS_GPIO_BANK_PINS)

/*
 * A set of pins that is driven as one unit, such as the status lamps. All of
 * them have to live in the same bank; taurus_gpio_group_select() rejects a
 * set that does not.
 */
struct taurus_gpio_group {
	unsigned int bank;
	u32 mask;
	u32 mux;	/* pin mux register, saved when claimed */
	u32 cfg;	/* direction register, saved when claimed */
	u32 out;	/* output register, saved when claimed */
};

/* Route the pins to the GPIO block, save their state and drive values. */
int taurus_gpio_group_select(struct taurus_gpio_group *group,
			     const unsigned int *pins, unsigned int count);
void taurus_gpio_group_claim(struct taurus_gpio_group *group, u32 values);
void taurus_gpio_group_write(struct taurus_gpio_group *group, u32 values);
/* Hand the pins back to whoever had them before the claim. */
void taurus_gpio_group_release(struct taurus_gpio_group *group);

/* Single pin helpers for keys and power enables. */
void taurus_gpio_input(unsigned int pin);
void taurus_gpio_output(unsigned int pin, bool value);
void taurus_gpio_set(unsigned int pin, bool value);
bool taurus_gpio_get(unsigned int pin);

/* DRAM capacity reported by BL2, in MiB. */
u32 taurus_dram_size_mb(void);

/*
 * True when the range overlaps memory the platform keeps for itself: the
 * exception vectors, the packet memory the NI MAC DMAs from, the secure region
 * BL31 owns, and the window the boot hand-off uses.
 */
bool taurus_memory_is_reserved(ulong start, size_t size);

/* Watchdog and packet memory set-up, called from board_init(). */
void taurus_early_init(void);

/* Restore the packet memory contents captured by taurus_early_init(). */
void taurus_xram_restore(void);

/* Stop all NI MACs, so the kernel finds them the way BL2 left them. */
void taurus_ni_stop_all(void);

#endif /* __ASM_ARCH_SOC_H */
