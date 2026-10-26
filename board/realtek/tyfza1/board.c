// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tianyi TYFZA1 (RTL9617CVW).
 *
 * Everything that can be shared - the boot policy, the environment, the lamps,
 * the packet memory hand-off - lives in board/realtek/common. What is left
 * here is the board itself: the DRAM banks it wires, the PHY bring-up its 2.5G
 * variant needs, and the strap that says which variant this unit is.
 */
#include <config.h>
#include <env.h>
#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/registers.h>
#include <asm/arch/soc.h>
#include <rtk_board.h>
#include <taurus_ni.h>
#include <linux/errno.h>

DECLARE_GLOBAL_DATA_PTR;

/* Pins carrying the board variant strap. */
#define TYFZA1_STRAP_PIN_FIRST	111
#define TYFZA1_STRAP_PINS	3

int board_init(void)
{
	taurus_early_init();
	rtk_board_reset_latch();
	/* Both variants use the internal GPHYs for their GE ports. */
	taurus_ni_gphy_setup();

	return 0;
}

void board_preboot_os(void)
{
	rtk_board_preboot();
}

/*
 * The strap picks the variant, and the variant picks the lamps, the port
 * label and the FIT configuration to boot.
 */
int board_late_init(void)
{
	unsigned int i, strap = 0;

	for (i = 0; i < TYFZA1_STRAP_PINS; i++) {
		taurus_gpio_input(TYFZA1_STRAP_PIN_FIRST + i);
		strap |= taurus_gpio_get(TYFZA1_STRAP_PIN_FIRST + i) << i;
	}
	env_set("board_variant", strap > 1 ? "unknown" : strap ? "4ge" : "2p5g");
	rtk_board_late_env();

	return 0;
}
