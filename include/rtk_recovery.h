/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Realtek Taurus platform: flash layout, boot and recovery.
 *
 * A board describes its flash once, in rtk_recovery_layout(); the shared code
 * here contains no volume name or device tree string.
 */
#ifndef __RTK_RECOVERY_H
#define __RTK_RECOVERY_H

#include <linux/types.h>
#include <stddef.h>
#include <stdint.h>

/* Boot area: the platform firmware, in two identical FIP copies. */
#define RTK_BOOT_SIZE		0x200000U
#define RTK_FIP_SIZE		0xc0000U
#define RTK_FIP0_OFFSET		0x40000U
#define RTK_FIP1_OFFSET		0x100000U

/*
 * Kernel command line every board uses unless it has a reason not to. Kernel
 * and root filesystem ship in one image volume, so the kernel finds the root
 * itself; only the UBI device is named here.
 */
#define RTK_BOOTARGS 	"console=ttyS0,115200 ubi.mtd=%u rootfstype=squashfs"

struct rtk_payload {
	const unsigned char *data;
	size_t size;
};

struct rtk_fip {
	struct rtk_payload bl2, bl31, bl33;
};

/* What a rebuild does to one UBI volume of the table. */
enum rtk_volume_policy {
	/*
	 * The system image: dropped on a rebuild and re-created as an empty
	 * dynamic volume covering every free eraseblock.
	 */
	RTK_VOL_FIRMWARE,
	/*
	 * Board data, named exactly as the shipped firmware names it: a
	 * rebuild leaves the volume byte for byte in place, and the volumes
	 * that do not exist yet are created when their image is imported.
	 */
	RTK_VOL_KEEP,
};

/* One UBI volume of the rebuild table. */
struct rtk_ubi_volume {
	const char *name;
	enum rtk_volume_policy policy;
};

/*
 * What the platform code needs to know about a product.
 */
struct rtk_recovery_layout {
	/* Flash. */
	const char *boot_mtd;		/* partition holding the firmware */
	const char *nand_dev;		/* whole device, used by a full restore */
	const char *ubi_part;		/* the single UBI partition */
	unsigned int ubi_mtd;		/* kernel mtd index of the UBI partition */
	size_t flash_size;

	/* The system image: kernel and root filesystem in a single volume. */
	const char *firmware_volume;
	/* The calibration and identity source; installed before a system. */
	const char *factory_volume;

	/* Kernel command line, %u replaced by the UBI device index. */
	const char *bootargs;

	/* Device tree node advertising the board-data volumes. */
	const char *dt_compatible;

	/* UBI rebuild table. */
	const struct rtk_ubi_volume *volumes;
	unsigned int nr_volumes;
};

/* Provided by the board. */
const struct rtk_recovery_layout *rtk_recovery_layout(void);

/* fip.c - the on-flash boot image format. */
int rtk_fip_parse(const void *data, size_t size, struct rtk_fip *out);
int rtk_loader_parse(const void *data, size_t size, struct rtk_fip *out);
int rtk_loader_preserves_device(const void *next, size_t next_size,
				const void *current, size_t current_size);

/* boot.c - the one boot policy every board shares. */
int rtk_board_env_init(void);
int rtk_boot_system(void);

/* recovery.c - install engine. */
int rtk_install_system(ulong address, size_t size, void (*poll)(void));
int rtk_validate_loader(ulong address, size_t size);
size_t rtk_loader_write_size(void);
int rtk_install_loader(ulong address, size_t size, void (*poll)(void));
int rtk_rebuild_ubi(void (*poll)(void));

/* restore.c - chunked whole-flash restore. */
struct rtk_restore_status {
	bool active;
	u32 id;
	size_t next, total;
};

const struct rtk_restore_status *rtk_restore_status(void);
int rtk_restore_job(const char *operation, ulong address, size_t size,
		    size_t total, size_t offset, u32 id, void (*poll)(void));

#endif /* __RTK_RECOVERY_H */
