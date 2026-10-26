// SPDX-License-Identifier: GPL-2.0-only
/*
 * Recovery engine: install a kernel, replace the boot area, rebuild the UBI
 * volumes. Every product specific name comes from the layout.
 */
#include <config.h>
#include <command.h>
#include <env.h>
#include <image.h>
#include <malloc.h>
#include <memalign.h>
#include <mapmem.h>
#include <mtd.h>
#include <stdio.h>
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

int rtk_install_kernel(ulong address, size_t size, void (*poll)(void))
{
	const struct rtk_recovery_layout *l = layout();
	const struct rtk_board_variant *variant = rtk_board_variant();
	int slot = env_get_ulong("slot", 10, 0);
	char volume[16];
	size_t used, capacity;
	int ret;

	if (slot >= RTK_SLOTS || !size)
		return -EINVAL;
	if (fit_check_format((void *)address, size))
		return -EINVAL;
	/* Only an image carrying this product's configuration is bootable. */
	if (variant && variant->fit_config &&
	    fit_conf_get_node((void *)address, variant->fit_config) < 0)
		return -EINVAL;
	ret = ubi_part((char *)l->ubi_part, NULL);
	if (ret)
		return ret;
	snprintf(volume, sizeof(volume), l->kernel_fmt, slot);
	ret = ubi_volume_get_size(volume, &used, &capacity);
	if (ret)
		return ret;
	if (size > capacity)
		return -EFBIG;
	ret = ubi_volume_write(volume, (void *)address, 0, size);
	if (!ret)
		ret = volume_verify(volume, (void *)address, size, poll);
	if (ret)
		return ret;
	printf("Kernel written to %s (%zu bytes), slot %d stays selected.\n",
	       volume, size, slot);

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

int rtk_rebuild_ubi(void (*poll)(void))
{
	const struct rtk_recovery_layout *l = layout();
	struct ubi_device *ubi;
	struct ubi_volume *vol;
	int available, needed = 0;
	unsigned int i;
	int ret = ubi_part((char *)l->ubi_part, NULL);

	if (ret)
		return ret;
	ubi = ubi_devices[0];
	if (!ubi || ubi->ro_mode)
		return -EROFS;
	available = ubi->avail_pebs;
	if (ubi->beb_rsvd_level > ubi->beb_rsvd_pebs)
		available -= ubi->beb_rsvd_level - ubi->beb_rsvd_pebs;
	/* Keep the volumes holding device identity in Flash throughout. */
	for (i = 0; i < ubi->vtbl_slots; i++) {
		vol = ubi->volumes[i];
		if (!vol)
			continue;
		if (i >= l->nr_volumes || strcmp(vol->name, l->volumes[i].name)) {
			puts("Unknown UBI volume layout; rebuild refused before removal.\n");
			return -EINVAL;
		}
		if (!(l->keep_mask & (1U << i)))
			available += vol->reserved_pebs;
	}
	for (i = 0; i < l->nr_volumes; i++) {
		if (l->keep_mask & (1U << i)) {
			vol = ubi->volumes[i];
			if (!vol || vol->upd_marker || vol->corrupted)
				return -EINVAL;
		} else if (l->volumes[i].size) {
			needed += DIV_ROUND_UP(l->volumes[i].size, ubi->leb_size);
		}
	}
	if (needed > available)
		return -ENOSPC;
	/* A rebuilt device has no bootable slot until one is installed. */
	ret = env_set("slot", NULL);
	if (!ret)
		ret = env_save();
	if (ret)
		return ret;
	for (i = 0; i < l->nr_volumes; i++) {
		if ((l->keep_mask & (1U << i)) || !ubi->volumes[i])
			continue;
		ret = ubi_volume_remove((char *)l->volumes[i].name);
		if (ret)
			return ret;
		poll();
	}
	for (i = 0; i < l->nr_volumes; i++) {
		if (l->keep_mask & (1U << i))
			continue;
		ret = run_commandf("ubi create %s %x d %x", l->volumes[i].name,
				   l->volumes[i].size, i);
		if (ret)
			return ret;
		poll();
	}
	puts("UBI system volumes rebuilt; the identity volumes stayed in Flash. Install a system before booting.\n");

	return 0;
}
