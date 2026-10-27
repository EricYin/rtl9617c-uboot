// SPDX-License-Identifier: GPL-2.0-only
/*
 * Recovery engine: install a system image, replace the boot area, rebuild the
 * UBI volumes. Every product specific name comes from the layout.
 */
#include <config.h>
#include <command.h>
#include <image.h>
#include <malloc.h>
#include <memalign.h>
#include <mapmem.h>
#include <mtd.h>
#include <stdio.h>
#include <string.h>
#include <ubi_uboot.h>
#include <rtk_board.h>
#include <rtk_recovery.h>

/* Board supplied flash layout; see struct rtk_recovery_layout. */
static const struct rtk_recovery_layout *layout(void)
{
	return rtk_recovery_layout();
}

/* Read a volume back and compare it, so a write is never trusted blind. */
static int volume_verify(char *name, const void *data, size_t length,
			 void (*poll)(void))
{
	unsigned char *check = malloc_cache_aligned(0x10000);
	const unsigned char *input = data;
	size_t off, chunk;
	int ret = -ENOMEM;

	if (!check)
		return ret;
	for (off = 0; off < length; off += chunk) {
		chunk = min_t(size_t, 0x10000, length - off);
		ret = ubi_volume_read_quiet(name, (char *)check, off, chunk);
		if (ret || memcmp(check, input + off, chunk)) {
			ret = ret ? ret : -EIO;
			break;
		}
		poll();
	}
	free(check);

	return ret;
}

/* The pool ubi_volume_create() draws from; it checks the same counter. */
static unsigned int free_pebs(void)
{
	return ubi_devices[0]->avail_pebs;
}

/*
 * Install the system image into its own volume. The image is verified in RAM
 * first, then staged in a second volume and read back, so the installed
 * system only leaves Flash once the new image is proven good.
 */
int rtk_install_system(ulong address, size_t size, void (*poll)(void))
{
	const struct rtk_recovery_layout *l = layout();
	const struct rtk_board_variant *variant = rtk_board_variant();
	char staging[24];
	size_t used, reserved, needed;
	int ret;

	if (!size || fit_check_format((void *)address, size))
		return -EINVAL;
	/* Only an image carrying this product's configuration is bootable. */
	if (variant && variant->fit_config &&
	    fit_conf_get_node((void *)address, variant->fit_config) < 0)
		return -EINVAL;
	ret = ubi_part((char *)l->ubi_part, NULL);
	if (ret)
		return ret;
	if (!ubi_devices[0] || ubi_devices[0]->ro_mode)
		return -EROFS;

	/*
	 * The factory image is the calibration and identity source: without
	 * it a system has no MAC addresses or radio data to run with.
	 */
	if (ubi_volume_get_size(l->factory_volume, &used, &reserved) || !used) {
		printf("The factory volume is empty or missing; import the factory image first.\n");
		return -EINVAL;
	}

	snprintf(staging, sizeof(staging), "%s.new", l->firmware_volume);
	if (ubi_volume_exists(staging) && ubi_volume_remove(staging))
		return -EIO;
	/*
	 * The staged copy is as large as the image. Free the installed volume
	 * only when the free pool cannot hold both, so the old system stays
	 * bootable while the new one is written.
	 */
	needed = DIV_ROUND_UP(size, ubi_devices[0]->leb_size);
	if (free_pebs() < needed && ubi_volume_exists(l->firmware_volume) &&
	    ubi_volume_remove(l->firmware_volume))
		return -EIO;
	if (free_pebs() < needed)
		return -ENOSPC;
	ret = ubi_volume_create(staging, size, true);
	if (ret)
		return ret;
	ret = ubi_volume_write(staging, (void *)address, 0, size);
	if (!ret)
		ret = volume_verify(staging, (void *)address, size, poll);
	if (ret) {
		printf("Staged image failed (%d); retry, or rebuild the UBI volumes.\n",
		       ret);
		return ret;
	}
	if (ubi_volume_exists(l->firmware_volume) &&
	    ubi_volume_remove(l->firmware_volume))
		return -EIO;
	ret = ubi_volume_rename(staging, l->firmware_volume);
	if (ret) {
		printf("The verified image stays in %s.\n", staging);
		return ret;
	}
	printf("System written to %s (%zu bytes).\n", l->firmware_volume, size);

	return 0;
}

static int read_mtd(struct mtd_info *mtd, size_t offset, size_t length,
		    void *out)
{
	size_t done;
	int ret = mtd_read(mtd, offset, length, &done, out);

	if (ret == -EUCLEAN && done == length)
		ret = 0;

	return ret ? ret : done == length ? 0 : -EIO;
}

int rtk_validate_loader(ulong address, size_t size)
{
	struct rtk_fip next;
	const unsigned char *input = map_sysmem(address, size);
	unsigned char *current = NULL;
	struct mtd_info *mtd = NULL;
	int ret = -EINVAL;

	if (rtk_loader_parse(input, size, &next))
		goto out;
	mtd_probe_devices();
	mtd = get_mtd_device_nm(layout()->boot_mtd);
	if (IS_ERR(mtd)) {
		ret = PTR_ERR(mtd);
		mtd = NULL;
		goto out;
	}
	if (mtd->size != RTK_BOOT_SIZE || mtd->erasesize != 0x20000 ||
	    mtd->writesize != 0x800)
		goto out;
	current = malloc_cache_aligned(RTK_BOOT_SIZE);
	if (!current) {
		ret = -ENOMEM;
		goto out;
	}
	ret = read_mtd(mtd, 0, RTK_BOOT_SIZE, current);
	if (ret)
		goto out;
	ret = rtk_loader_preserves_device(input, size, current,
					  RTK_BOOT_SIZE) ? -EINVAL : 0;
	if (ret)
		puts("Loader must preserve this device's GPT, BL2, BL31 and reserved bytes.\n");
out:
	free(current);
	if (mtd)
		put_mtd_device(mtd);
	unmap_sysmem(input);

	return ret;
}

size_t rtk_loader_write_size(void)
{
	return 2 * RTK_FIP_SIZE;
}

int rtk_install_loader(ulong address, size_t size, void (*poll)(void))
{
	const unsigned char *input;
	unsigned char *check = NULL;
	struct mtd_info *mtd = NULL;
	size_t base, offset, done;
	int copy, ret;

	ret = rtk_validate_loader(address, size);
	if (ret)
		return ret;
	input = map_sysmem(address, size);
	mtd = get_mtd_device_nm(layout()->boot_mtd);
	if (IS_ERR(mtd)) {
		unmap_sysmem(input);
		return PTR_ERR(mtd);
	}
	check = malloc_cache_aligned(mtd->erasesize);
	if (!check) {
		ret = -ENOMEM;
		goto out;
	}
	for (offset = RTK_FIP0_OFFSET; offset < 0x1c0000;
	     offset += mtd->erasesize) {
		ret = mtd_block_isbad(mtd, offset);
		if (ret) {
			ret = ret < 0 ? ret : -EIO;
			goto out;
		}
	}
	/* Finish and verify FIP1 before touching FIP0: a failure stays bootable. */
	for (copy = 1; copy >= 0; copy--) {
		base = copy ? RTK_FIP1_OFFSET : RTK_FIP0_OFFSET;
		for (offset = base; offset < base + RTK_FIP_SIZE;
		     offset += mtd->erasesize) {
			struct erase_info erase = {
				.mtd = mtd, .addr = offset, .len = mtd->erasesize
			};

			ret = mtd_erase(mtd, &erase);
			if (ret)
				goto out;
			ret = mtd_write(mtd, offset, mtd->erasesize, &done,
					input + offset);
			if (ret || done != mtd->erasesize) {
				ret = ret ? ret : -EIO;
				goto out;
			}
			ret = read_mtd(mtd, offset, mtd->erasesize, check);
			if (ret || memcmp(check, input + offset, mtd->erasesize)) {
				ret = ret ? ret : -EIO;
				goto out;
			}
			poll();
		}
		printf("FIP%d written and read back.\n", copy);
	}
out:
	free(check);
	if (mtd)
		put_mtd_device(mtd);
	unmap_sysmem(input);

	return ret;
}

/* The rebuild table entry one volume name maps to, or NULL when unknown. */
static const struct rtk_ubi_volume *layout_volume(
	const struct rtk_recovery_layout *l, const char *name)
{
	unsigned int i;

	for (i = 0; i < l->nr_volumes; i++)
		if (!strcmp(l->volumes[i].name, name))
			return &l->volumes[i];

	return NULL;
}

/* Physically erase every good eraseblock of the UBI partition. */
static int erase_ubi_mtd(const struct rtk_recovery_layout *l,
			 void (*poll)(void))
{
	struct mtd_info *mtd;
	u64 offset;
	int ret;

	mtd = get_mtd_device_nm(l->ubi_part);
	if (IS_ERR(mtd))
		return PTR_ERR(mtd);
	if (!mtd_is_partition(mtd) || !mtd->erasesize ||
	    mtd->size % mtd->erasesize) {
		put_mtd_device(mtd);
		return -EINVAL;
	}
	for (offset = 0; offset < mtd->size; offset += mtd->erasesize) {
		struct erase_info erase = {
			.mtd = mtd, .addr = offset, .len = mtd->erasesize
		};

		ret = mtd_block_isbad(mtd, offset);
		if (ret) {
			if (ret < 0) {
				put_mtd_device(mtd);
				return ret;
			}
			printf("Bad block at 0x%08llx stays out of UBI.\n",
			       (unsigned long long)offset);
			continue;
		}
		ret = mtd_erase(mtd, &erase);
		if (ret) {
			put_mtd_device(mtd);
			return ret;
		}
		poll();
	}
	put_mtd_device(mtd);

	return 0;
}

/*
 * Rebuild the UBI layout. The volumes the table marks as board data survive
 * byte for byte where they stand, everything else is erased, and the system
 * volume is re-created over all the free space. A device UBI cannot attach
 * falls back to a full erase, where nothing can be kept; the web flow asks
 * for the factory image afterwards either way.
 */
int rtk_rebuild_ubi(void (*poll)(void))
{
	const struct rtk_recovery_layout *l = layout();
	struct ubi_device *ubi;
	unsigned int i;
	int ret;

	mtd_probe_devices();
	if (ubi_devices[0]) {
		/* Detach releases UBI's MTD reference before any erase. */
		ret = ubi_detach();
		if (ret)
			return ret;
	}

	/* Attach first: what can be read is what can be kept. */
	if (ubi_part((char *)l->ubi_part, NULL)) {
		puts("UBI attach failed; erasing the device instead.\n");
		ret = erase_ubi_mtd(l, poll);
		if (ret)
			return ret;
		if (ubi_part((char *)l->ubi_part, NULL))
			return -EIO;
	}
	ubi = ubi_devices[0];

	/* Drop the system volume and every volume the table does not know. */
	for (i = 0; i < ubi->vtbl_slots; i++) {
		const struct rtk_ubi_volume *spec;
		const char *name;

		if (!ubi->volumes[i])
			continue;
		name = ubi->volumes[i]->name;
		spec = layout_volume(l, name);
		if (spec && spec->policy == RTK_VOL_KEEP)
			continue;
		ret = ubi_volume_remove(name);
		if (ret) {
			printf("Removing %s failed (%d); kept volumes are untouched, retry the rebuild.\n",
			       name, ret);
			return ret;
		}
		poll();
	}

	/* The system volume takes every free eraseblock. */
	for (i = 0; i < l->nr_volumes; i++) {
		const struct rtk_ubi_volume *spec = &l->volumes[i];

		if (spec->policy != RTK_VOL_FIRMWARE)
			continue;
		ret = ubi_volume_create(spec->name, 0, true);
		if (ret) {
			printf("Creating %s failed (%d); kept volumes are intact, retry the rebuild.\n",
			       spec->name, ret);
			return ret;
		}
		poll();
	}
	puts("Board data kept in place; the system volume is empty, install a system.\n");

	return 0;
}
