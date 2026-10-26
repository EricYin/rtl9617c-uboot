// SPDX-License-Identifier: GPL-2.0-only
/*
 * Boot and recovery entry, shared by every board on this platform.
 *
 * The environment a board starts from is compiled in and identical everywhere:
 * one boot command, one recovery command. Nothing in the boot path depends on
 * a value left in NAND by a previous firmware, so a board with erased or
 * unreadable environment still boots.
 */
#include <config.h>
#include <command.h>
#include <env.h>
#include <event.h>
#include <net/httpd.h>
#include <rtk_board.h>
#include <rtk_recovery.h>
#include <ubi_uboot.h>
#include <asm/arch/soc.h>

/* Set by board_init() and reported by the lamps until the recovery starts. */
static bool reset_latched;

int rtk_board_env_init(void)
{
	env_set("bootcmd", "bootmenu 3");
	env_set("bootmenu_0", "Start OpenWrt=rtk_boot");
	env_set("bootmenu_1", "Web recovery=rtk_recovery");
	/*
	 * Partitions come from the device tree. A unit that still carries the
	 * mtdparts string of the firmware this replaces would make
	 * mtd_probe_devices() delete the device tree partitions and fail to
	 * recreate any, so both variables are dropped.
	 */
	env_set("mtdparts", NULL);
	env_set("mtdids", NULL);

	return 0;
}

void rtk_board_reset_latch(void)
{
	reset_latched = rtk_board_reset_held();
}

void rtk_board_late_env(void)
{
	rtk_board_env_init();
	if (reset_latched)
		rtk_board_leds(RTK_LEDS_HELD);
}

void rtk_board_preboot(void)
{
	rtk_board_leds(RTK_LEDS_OFF);
	taurus_ni_stop_all();
	taurus_xram_restore();
}

/* Kernel command line of a slot; the root volume id follows the slot. */
static void rtk_bootargs(unsigned int root)
{
	const struct rtk_recovery_layout *l = rtk_recovery_layout();
	char cmdline[192];

	snprintf(cmdline, sizeof(cmdline), l->bootargs, root);
	env_set("bootargs", cmdline);
}

/*
 * Read the kernel of the selected slot and start it. Shared by the boot
 * command and by the recovery service when it boots an uploaded image.
 */
int rtk_boot_slot(void)
{
	const struct rtk_recovery_layout *l = rtk_recovery_layout();
	const struct rtk_board_variant *variant = rtk_board_variant();
	ulong address = env_get_ulong("unzip_src", 16, CONFIG_SYS_LOAD_ADDR);
	int slot = env_get_ulong("slot", 10, 0);
	char volume[16];

	if (slot >= RTK_SLOTS)
		return CMD_RET_FAILURE;
	if (ubi_part((char *)l->ubi_part, NULL))
		return CMD_RET_FAILURE;
	snprintf(volume, sizeof(volume), l->kernel_fmt, slot);
	if (run_commandf("ubi read 0x%lx %s 0x%zx", address, volume,
			 l->kernel_size)) {
		printf("Slot %d has no kernel in %s.\n", slot, volume);
		return CMD_RET_FAILURE;
	}
	env_set_hex("unzip_src", address);
	rtk_bootargs(l->ubi_mtd + 1 + l->root_index[slot]);
	if (variant && variant->fit_config)
		return run_commandf("bootm 0x%lx#%s", address, variant->fit_config);

	return run_commandf("bootm 0x%lx", address);
}

static int do_rtk_boot(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	return rtk_boot_slot();
}

U_BOOT_CMD(rtk_boot, 1, 0, do_rtk_boot,
	   "boot the system in the selected UBI slot", "");

static int do_rtk_recovery(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	return uboot_httpd_start(env_get_yesno("httpd_dhcp") != 0);
}

U_BOOT_CMD(rtk_recovery, 1, 0, do_rtk_recovery,
	   "start the recovery service using the saved httpd settings", "");

/*
 * The reset key is sampled once more at the last moment before bootcmd runs,
 * so a key pressed after power-on still enters recovery, and a released one
 * boots normally.
 */
static int rtk_board_recovery_spy(void)
{
	int ret;

	reset_latched |= rtk_board_reset_held();
	if (reset_latched)
		rtk_board_leds(RTK_LEDS_HELD);
	rtk_board_boot_leds();
	if (!reset_latched) {
		puts("Reset released: normal boot\n");
		return 0;
	}
	/* On exit or a network error, stay at the prompt instead of booting. */
	env_set("bootcmd", "");
	puts("Reset held: starting the HTTP recovery\n");
	ret = uboot_httpd_start(env_get_yesno("httpd_dhcp") != 0);
	rtk_board_leds(RTK_LEDS_OFF);
	if (ret)
		printf("Recovery network failed (%d); use rtk_recovery to retry.\n",
		       ret);

	return 0;
}
EVENT_SPY_SIMPLE(EVT_POST_PREBOOT, rtk_board_recovery_spy);
