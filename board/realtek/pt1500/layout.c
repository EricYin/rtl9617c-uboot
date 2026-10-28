// SPDX-License-Identifier: GPL-2.0+
/*
 * PT1500 flash layout.
 *
 * The physical partitioning is now the same on every board of this platform:
 * firmware, environment, one UBI device. What is left here is the volume table
 * of the product - no FPGA volume, and the configuration volumes at the end.
 */
#include <config.h>
#include <linux/kernel.h>
#include <rtk_recovery.h>

/*
 * The system image is one firmware volume carrying the kernel and the root
 * filesystem. A rebuild keeps the configuration volume byte for byte in
 * place, drops every other volume, and re-creates the firmware volume over
 * all the free space. Board data volumes are never created empty here: a
 * UBI volume reports its full size the moment it is created, so existence
 * created, so existence is the only honest signal that an image was
 * imported, and the install refuses to run without one.
 */
static const struct rtk_ubi_volume pt1500_volumes[] = {
	{ "ubi_Config", RTK_VOL_KEEP, 0 },
	{ "fip", RTK_VOL_FIRMWARE, 64 * 1024 * 1024 },
	{ "rootfs_data", RTK_VOL_DATA, 174 * 1000 * 1000 },
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
		.dt_compatible = "youhua,pt1500-recovery-layout",
		.volumes = pt1500_volumes,
		.nr_volumes = ARRAY_SIZE(pt1500_volumes),
	};

	return &layout;
}
