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

#define RTK_FIT_PROBE_SIZE	0x4000U
#define RTK_FIT_MAX_SIZE	0x10000000U

size_t rtk_fit_image_size(const void *header)
{
	const unsigned char *p = header;
	size_t total;

	if (p[0] != 0xd0 || p[1] != 0x0d || p[2] != 0xfe || p[3] != 0xed)
		return 0;
	total = ((size_t)p[4] << 24) | ((size_t)p[5] << 16) |
		((size_t)p[6] << 8) | p[7];
	if (total < 64 || total > RTK_FIT_MAX_SIZE)
		return 0;

	return total;
}

int rtk_board_env_init(void)
{
	env_set("bootcmd", "rtk_boot");
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

/* Kernel command line; the image volume carries both kernel and root. */
static void rtk_bootargs(void)
{
	const struct rtk_recovery_layout *l = rtk_recovery_layout();
	char cmdline[192];

	snprintf(cmdline, sizeof(cmdline), l->bootargs, l->ubi_mtd);
	env_set("bootargs", cmdline);
}

/*
 * Read the system image - kernel and root filesystem in one volume - and
 * start it. Shared by the boot command and by the recovery service when it
 * boots an uploaded image.
 */
int rtk_boot_system(void)
{
	const struct rtk_recovery_layout *l = rtk_recovery_layout();
	const struct rtk_board_variant *variant = rtk_board_variant();
	ulong address = env_get_ulong("unzip_src", 16, CONFIG_SYS_LOAD_ADDR);
	size_t total;

	if (ubi_part((char *)l->ubi_part, NULL))
		return CMD_RET_FAILURE;
	if (run_commandf("ubi read 0x%lx %s 0x%zx", address, l->firmware_volume,
			 (size_t)RTK_FIT_PROBE_SIZE)) {
		printf("No system image in %s.\n", l->firmware_volume);
		return CMD_RET_FAILURE;
	}
	total = rtk_fit_image_size((void *)address);
	if (!total) {
		printf("Invalid system image in %s.\n", l->firmware_volume);
		return CMD_RET_FAILURE;
	}
	if (total > RTK_FIT_PROBE_SIZE &&
	    run_commandf("ubi read 0x%lx %s 0x%zx", address, l->firmware_volume,
			 total)) {
		printf("No system image in %s.\n", l->firmware_volume);
		return CMD_RET_FAILURE;
	}
	env_set_hex("unzip_src", address);
	rtk_bootargs();
	if (variant && variant->fit_config)
		return run_commandf("bootm 0x%lx#%s", address, variant->fit_config);

	return run_commandf("bootm 0x%lx", address);
}

static int rtk_start_recovery(void);

static int do_rtk_boot(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	int ret;

	ret = rtk_boot_system();
	if (ret) {
		printf("System boot failed (%d); starting the HTTP recovery\n",
		       ret);
		ret = rtk_start_recovery();
	}

	return ret;
}

U_BOOT_CMD(rtk_boot, 1, 0, do_rtk_boot,
	   "boot the system from the firmware volume", "");

/*
 * The recovery service is a build option: a board that leaves CONFIG_CMD_HTTPD
 * out still gets the command and the reset key, they just report that there is
 * no service to start.
 */
static int rtk_start_recovery(void)
{
	if (!IS_ENABLED(CONFIG_CMD_HTTPD)) {
		puts("This U-Boot was built without the HTTP recovery service.\n");
		return -ENOSYS;
	}

	return uboot_httpd_start(env_get_yesno("httpd_dhcp") != 0);
}

static int do_rtk_recovery(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	return rtk_start_recovery();
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
	puts("Reset held: starting the HTTP recovery\n");
	ret = rtk_start_recovery();
	rtk_board_leds(RTK_LEDS_OFF);
	env_set("bootcmd", "rtk_boot");
	if (ret)
		printf("Recovery network failed (%d); use rtk_recovery to retry.\n",
		       ret);

	return 0;
}
EVENT_SPY_SIMPLE(EVT_POST_PREBOOT, rtk_board_recovery_spy);
