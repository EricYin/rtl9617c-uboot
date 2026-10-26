// SPDX-License-Identifier: GPL-2.0+
/*
 * TYFZA1 flash layout.
 *
 * The shared platform code (board/realtek/common) contains no volume name,
 * slot number or device tree string: everything product specific is here.
 */
#include <config.h>
#include <linux/kernel.h>
#include <rtk_recovery.h>

/*
 * UBI volumes in volume-id order. The factory, FPGA and board tree volumes
 * (ids 0, 1, 2 and 5) are never removed by a rebuild.
 */
static const struct rtk_ubi_volume tyfza1_volumes[] = {
	{ "ubi_Config", 0x60e000 }, { "ubi_fpga", 0x41e000 },
	{ "ubi_DTB0", 0x5d000 }, { "ubi_k0", 0x60e000 }, { "ubi_r0", 0x2017000 },
	{ "ubi_DTB1", 0x5d000 }, { "ubi_k1", 0x60e000 }, { "ubi_r1", 0x2017000 },
	{ "ubi_framework1", 0x101b000 }, { "ubi_framework2", 0x101b000 },
	{ "ubi_apps", 0x6007000 },
};

const struct rtk_recovery_layout *rtk_recovery_layout(void)
{
	static const struct rtk_recovery_layout layout = {
		.boot_mtd = "boot",
		.nand_dev = "nand0",
		.ubi_part = "ubi_device",
		.ubi_mtd = 4,
		.flash_size = 0x10000000,

		.kernel_fmt = "ubi_k%d",
		.root_index = { 4, 7 },
		.kernel_size = 0x600000,

		.bootargs = RTK_BOOTARGS,
		.dt_compatible = "tianyi,tyfza1-recovery-layout",
		/* The table the stock firmware passed; the kernel has none of its own. */
		.mtdparts = "mtdparts=ca_spinand_flash:"
			    "2048K(boot),256K@0x280000(env),256K@0x2C0000(env2),"
			    "256K@0x300000(static_conf),251648K@0x340000(ubi_device)",

		.volumes = tyfza1_volumes,
		.nr_volumes = ARRAY_SIZE(tyfza1_volumes),
		.keep_mask = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 5),
	};

	return &layout;
}
