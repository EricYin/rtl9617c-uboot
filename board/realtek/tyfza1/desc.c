// SPDX-License-Identifier: GPL-2.0+
/*
 * TYFZA1 board description.
 *
 * The lamps and the reset key are the only hardware the shared board UI needs
 * to know about; see include/rtk_board.h. The 2p5g variant has its first two
 * lamps wired active high and the other two active low, the 4ge variant drives
 * all four active low.
 */
#include <config.h>
#include <rtk_board.h>
#include <linux/kernel.h>

static const struct rtk_board_variant tyfza1_variants[] = {
	{
		.name = "2p5g",
		.lamp = {
			{ .pin = 11 }, { .pin = 12 },
			{ .pin = 3, .active_low = true },
			{ .pin = 4, .active_low = true },
		},
		.ports = "2.5GE + 3×GE",
		.fit_config = "conf-0",
	},
	{
		.name = "4ge",
		.lamp = {
			{ .pin = 4, .active_low = true },
			{ .pin = 3, .active_low = true },
			{ .pin = 1, .active_low = true },
			{ .pin = 0, .active_low = true },
		},
		.ports = "4×GE",
		.fit_config = "conf-1",
	},
};

static const struct rtk_board_desc tyfza1_desc = {
	.name = "TYFZA1",
	.version = "2026.07",
	.variant_env = "board_variant",
	.variants = tyfza1_variants,
	.nr_variants = ARRAY_SIZE(tyfza1_variants),
	.reset_pin = 8,
	.reset_active_low = true,
};

const struct rtk_board_desc *rtk_board_desc(void)
{
	return &tyfza1_desc;
}
