// SPDX-License-Identifier: GPL-2.0+
/*
 * PT1500 board description.
 *
 * One hardware build, so there is no variant strap: the four status lamps are
 * LED_EPHY0..3 of the OEM device tree (bank 3, bits 6, 7, 1 and 4, all active
 * high) and the reset key sits on bank 4 bit 8.
 */
#include <config.h>
#include <rtk_board.h>
#include <linux/kernel.h>

static const struct rtk_board_variant pt1500_variants[] = {
	{
		.lamp = {
			{ .pin = 102 }, { .pin = 103 },
			{ .pin = 97 }, { .pin = 100 },
		},
		.ports = "4×GE",
		.fit_config = "9617c",
	},
};

static const struct rtk_board_desc pt1500_desc = {
	.name = "PT1500",
	.version = "2026.07",
	.variants = pt1500_variants,
	.nr_variants = ARRAY_SIZE(pt1500_variants),
	.reset_pin = 136,
	.reset_active_low = true,
};

const struct rtk_board_desc *rtk_board_desc(void)
{
	return &pt1500_desc;
}
