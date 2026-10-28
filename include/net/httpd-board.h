/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Optional board hooks for the HTTP management service.
 *
 * The service itself knows nothing about a product. A board that wants status
 * LEDs, extra fields in the status JSON, or its own install jobs returns this
 * structure from httpd_board_ops(); every callback is optional.
 */
#ifndef __NET_HTTPD_BOARD_H
#define __NET_HTTPD_BOARD_H

#include <net/httpd-json.h>

/* States a board can show while the service is running. */
enum httpd_led_state {
	HTTPD_LED_OFF,		/* service stopped, hand the pins back */
	HTTPD_LED_IDLE,		/* waiting for a client */
	HTTPD_LED_BUSY,		/* writing Flash */
	HTTPD_LED_DONE,		/* write succeeded */
	HTTPD_LED_FAILED,	/* write failed */
};

struct httpd_board_ops {
	/* Status indication. */
	void (*led)(enum httpd_led_state state);
	void (*led_busy)(size_t done, size_t total);
	/* Periodic housekeeping, called from the receive loop. */
	void (*poll)(void);

	/* Flash naming this product uses; the service defaults when unset. */
	const char *ubi_partition;

	/* Extra fields appended to /api/info and to the job status payload. */
	void (*info)(struct http_json *json);
	void (*status)(struct http_json *json);

	/* Upload policy: target check by URI and RAM window validation. */
	bool (*upload_allowed)(const char *uri, int content_len);
	bool (*upload_safe)(ulong addr, size_t len);

	/*
	 * Try to consume a job request. Return 1 when the operation does not
	 * belong to the board, 0 when it was accepted, negative errno on error.
	 * upload_ready tells whether the request refers to a completed upload.
	 */
	int (*job_parse)(struct http_json_request *request, const char *operation,
			 bool upload_ready);
	/* Run the accepted board job; service() keeps the network alive. */
	int (*job_run)(ulong addr, size_t size, void (*service)(void));
	/* Called when the service stops with a board job in progress. */
	void (*job_abort)(void);
	/* Expected Flash bytes for the progress display, 0 when unknown. */
	size_t (*job_write_size)(void);

	/* Boot the uploaded FIT; the board knows its firmware volume and config. */
	int (*boot_upload)(ulong addr, size_t size);

	/* Product install jobs behind the standard endpoints. */
	int (*install_loader)(ulong addr, size_t size, bool bl2,
			      void (*service)(void));
	int (*install_system)(ulong addr, size_t size, void (*service)(void));
	int (*rebuild_ubi)(void (*service)(void));
	int (*set_bootcmd)(void);

	/* UBI volume management behind the advanced page. */
	int (*ubi_create)(const char *name, u64 size, bool dynamic);
	int (*ubi_remove)(const char *name);
	int (*ubi_rename)(const char *old_name, const char *new_name);
	int (*ubi_attach)(const char *mtd_name);
	int (*ubi_detach)(void);
};

/* Implemented by the board; NULL when it needs none of the above. */
const struct httpd_board_ops *httpd_board_ops(void);

#endif /* __NET_HTTPD_BOARD_H */
