/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * What a board on this platform has to describe about itself.
 *
 * The status lamps, the reset key and the strings the recovery page shows are
 * the same logic on every board; only the pins, their polarity and the model
 * names differ. A board fills in struct rtk_board_desc once and returns it from
 * rtk_board_desc(); board/realtek/common/ does the rest.
 */
#ifndef __RTK_BOARD_H
#define __RTK_BOARD_H

#include <linux/types.h>

/* One status lamp, addressed by its global GPIO number. */
struct rtk_led {
	u8 pin;
	bool active_low;
};

/* Four LAN lamps plus one power/status lamp. */
#define RTK_BOARD_LAMPS	5

/*
 * A hardware variant of one board. Boards with a single build pass NULL as the
 * variant env and only the first entry; boards that read a strap expose the
 * value through the environment and list one entry per variant.
 */
struct rtk_board_variant {
	const char *name;			/* value of the variant env */
	struct rtk_led lamp[RTK_BOARD_LAMPS];
	const char *ports;			/* shown by the recovery page */
	const char *fit_config;		/* FIT configuration to boot */
};

struct rtk_board_desc {
	const char *name;			/* "TYFZA1", "PT1500" */
	const char *version;			/* port release, shown by the UI */
	const char *variant_env;		/* NULL: single variant board */
	const struct rtk_board_variant *variants;
	unsigned int nr_variants;
	int reset_pin;				/* global pin, -1 when absent */
	bool reset_active_low;
};

/* Provided by the board. */
const struct rtk_board_desc *rtk_board_desc(void);

/* States the lamps can show. */
enum rtk_led_state {
	RTK_LEDS_OFF,		/* pins returned to their previous owner */
	RTK_LEDS_HELD,		/* reset recognised: all on, safe to release */
	RTK_LEDS_IDLE,		/* waiting for a client: heartbeat */
	RTK_LEDS_UPLOAD,	/* receiving into RAM: first lamp flickers */
	RTK_LEDS_BUSY,		/* writing Flash: quarter progress bar */
	RTK_LEDS_DONE,		/* write succeeded: all on for two seconds */
	RTK_LEDS_FAILED,	/* write failed: lamps 1+3 and 2+4 alternate */
};

/* Variant of the board this unit is, or NULL when it cannot be determined. */
const struct rtk_board_variant *rtk_board_variant(void);

/* Board lifecycle shared by every board of this platform. */
void rtk_board_reset_latch(void);
void rtk_board_late_env(void);
void rtk_board_preboot(void);

bool rtk_board_reset_held(void);
void rtk_board_boot_leds(void);
void rtk_board_leds(enum rtk_led_state state);
void rtk_board_leds_busy(size_t done, size_t total);
void rtk_board_leds_progress(size_t bytes);
void rtk_board_leds_poll(void);

#endif /* __RTK_BOARD_H */
