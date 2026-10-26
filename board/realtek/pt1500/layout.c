// SPDX-License-Identifier: GPL-2.0+
/*
 * PT1500 flash layout.
 *
 * The physical partitioning is now the same on every board of this platform:
 * firmware, environment, one UBI device. What is left here is the volume table
 * of the product - no FPGA or data volume, and the configuration volumes at
 * the end.
 */
#include <config.h>
#include <linux/kernel.h>
#include <rtk_recovery.h>

/*
 * UBI volumes in volume-id order. The device identity and the two
 * configuration volumes are never removed by a rebuild.
 */
static const struct rtk_ubi_volume pt1500_volumes[] = {
	{ "ubi_Config", 0x706000 },
	{ "ubi_DTB0", 0x5d000 }, { "ubi_k0", 0x60e000 }, { "ubi_r0", 0x1e08000 },
	{ "ubi_DTB1", 0x5d000 }, { "ubi_k1", 0x60e000 }, { "ubi_r1", 0x1e08000 },
	{ "ubi_framework1", 0x101b000 }, { "ubi_framework2", 0x101b000 },
	{ "ubi_otherConfig", 0x1113000 },
	{ "ubi_apps", 0x611e000 },
	{ "ubi_BakConfig", 0x307000 },
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
		.root_index = { 3, 6 },
		.kernel_size = 0x600000,

		.bootargs = RTK_BOOTARGS,
		.dt_compatible = "youhua,pt1500-recovery-layout",
		.volumes = pt1500_volumes,
		.nr_volumes = ARRAY_SIZE(pt1500_volumes),
		.keep_mask = (1U << 0) | (1U << 9) | (1U << 11),
	};

	return &layout;
}
