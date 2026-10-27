// SPDX-License-Identifier: GPL-2.0+
/*
 * TYFZA1 flash layout.
 *
 * The shared platform code (board/realtek/common) contains no volume name
 * or device tree string: everything product specific is here.
 */
#include <config.h>
#include <linux/kernel.h>
#include <rtk_recovery.h>

/*
 * The system image is one firmware volume carrying the kernel and the root
 * filesystem. A rebuild keeps both board data volumes - the factory
 * calibration and identity source (ubi_Config), and the FPGA firmware
 * (ubi_fpga) - byte for byte in place, drops every other volume, and
 * re-creates the firmware volume over all the free space. The board data
 * volumes keep the names the shipped firmware uses, and are never created
 * empty here: a UBI volume reports its full size the moment it is created,
 * so existence is the only honest signal that an image was imported, and
 * the install refuses to run without one.
 */
static const struct rtk_ubi_volume tyfza1_volumes[] = {
	{ "ubi_Config", RTK_VOL_KEEP },
	{ "ubi_fpga", RTK_VOL_KEEP },
	{ "fip", RTK_VOL_FIRMWARE },
};

const struct rtk_recovery_layout *rtk_recovery_layout(void)
{
	static const struct rtk_recovery_layout layout = {
		.boot_mtd = "boot",
		.nand_dev = "nand0",
		.ubi_part = "ubi_device",
		.ubi_mtd = 4,
		.flash_size = 0x10000000,

		.firmware_volume = "fip",
		.factory_volume = "ubi_Config",

		.bootargs = RTK_BOOTARGS,
		.dt_compatible = "tianyi,tyfza1-recovery-layout",
		.volumes = tyfza1_volumes,
		.nr_volumes = ARRAY_SIZE(tyfza1_volumes),
	};

	return &layout;
}
