/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal compatibility types for the public Taurus register description. */
#ifndef TYFZA1_CA_TYPES_H
#define TYFZA1_CA_TYPES_H
#ifndef __ASSEMBLER__
#include <linux/types.h>
typedef unsigned long ca_uint;
typedef u8 ca_uint8;
typedef u16 ca_uint16;
typedef u32 ca_uint32;
typedef u64 ca_uint64;
typedef u8 ca_uint8_t;
typedef u16 ca_uint16_t;
typedef u32 ca_uint32_t;
typedef u64 ca_uint64_t;
typedef s8 ca_int8_t;
typedef s16 ca_int16_t;
typedef s32 ca_int32_t;
typedef s64 ca_int64_t;
typedef int ca_status_t;
#define CA_IN
#define CA_OUT
#define CA_IN_OUT
#define CA_E_OK 0
#define CA_E_PARAM (-22)
#define CA_E_PORT_ID (-34)
#define CA_E_TIMEOUT (-110)
#endif
#endif
