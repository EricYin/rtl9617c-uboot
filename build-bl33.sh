#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# One-shot build of the flashable U-Boot for the RTL9617C boards.
#
#   ./build-bl33.sh pt1500|tyfza1|all
#
# What it does, per board:
#   1. build U-Boot with the board defconfig (output/<board>/u-boot-dtb.bin)
#   2. compress it with LZMA1 (alone format, dict 1 MiB, lc3/lp0/pb2)
#   3. compile loader/ into the self-decompressing BL33 stub with the payload
#      embedded, and emit output/<board>/bl33.bin
#
# output/<board>/bl33.bin is what goes into the FIP slot; the stock layout
# gives BL33 459,961 bytes on PT1500 and 467,222 on the tighter TYFZA1
# variant, and the stub is what makes a full-featured U-Boot fit there.
# The plain u-boot-dtb.bin stays in the output directory for the RAM-boot
# path (uboot-test-script/build-itb.sh, which is independent of this script;
# point it here with --from output/<board> to reuse the build).
#
# Requires: aarch64-linux-gnu- toolchain, xz, gzip, od, awk.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$REPO"
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
export CROSS_COMPILE
CC=${CROSS_COMPILE}gcc
LD=${CROSS_COMPILE}ld
OBJCOPY=${CROSS_COMPILE}objcopy
NM=${CROSS_COMPILE}nm

EXEC_ADDR=0x08000000		# stub self-relocates here, decodes from here
RUN_ADDR=0x04000000		# CONFIG_TEXT_BASE of both boards
STACK_TOP=0x01000000
WINDOW=$((0x1000000))		# stub image + workspace must stay under 16 MiB
BUDGET_PT1500=459961		# measured: BL33 payload start to the first certificate
BUDGET_TYFZA1=466854		# measured on a real unit; the 2p5g variant governs

die() { echo "build-bl33: $1" >&2; exit 1; }

budget_for() {
	case "$1" in
		pt1500) echo "$BUDGET_PT1500" ;;
		tyfza1) echo "$BUDGET_TYFZA1" ;;
	esac
}

build_board() {
	board=$1
	case "$board" in
		pt1500) board_id="PT1500-H10" ;;
		tyfza1) board_id="TYFZA1" ;;
	esac
	out=output/$board
	ldir=$out/loader
	budget=$(budget_for "$board")
	log=$REPO/$out/build.log

	echo "==> $board: building U-Boot (log: $out/build.log)"
	mkdir -p "$ldir"
	make O="$out" "${board}_defconfig" >"$log" 2>&1
	make O="$out" -j"$(nproc)" >>"$log" 2>&1
	bin=$out/u-boot-dtb.bin
	[ -f "$bin" ] || die "$bin not found - unexpected build output"
	plain_size=$(stat -c%s "$bin")

	echo "==> $board: compressing (LZMA1, dict 1MiB, lc3 lp0 pb2)"
	payload=$REPO/$ldir/payload.lzma
	xz --format=lzma --lzma1=preset=9e,dict=1MiB,lc=3,lp=0,pb=2 \
		-c "$bin" >"$payload"
	props=$(od -An -tx1 -N1 "$payload" | tr -d ' \n')
	[ "$props" = "5d" ] || die "unexpected LZMA props byte $props"
	lzma_size=$(stat -c%s "$payload")
	xz -dc "$payload" | cmp -s - "$bin" || die "LZMA roundtrip mismatch"

	# CRC32 of the plain image, straight out of the gzip footer.
	gzip -c "$bin" >"$ldir/plain.gz"
	crc=$(gzip -lv "$ldir/plain.gz" | awk 'NR==2 {print $2}')
	rm -f "$ldir/plain.gz"
	case "$crc" in ????????) ;; *) die "could not read CRC32 from gzip -lv" ;; esac

	echo "==> $board: building the BL33 stub"
	cat >"$ldir/loader-config.h" <<EOF
#define EXEC_ADDR ${EXEC_ADDR}
#define RUN_ADDR ${RUN_ADDR}
#define STACK_TOP ${STACK_TOP}
#define PLAIN_SIZE ${plain_size}
#define PLAIN_CRC 0x${crc}
#define STUB_BOARD "${board_id}"
EOF
	cat >"$ldir/payload.S" <<EOF
.section .rodata.payload,"a"
.balign 16
.global payload_start,payload_end
payload_start:
.incbin "$payload"
payload_end:
EOF
	$CC -E -P -x c -I "$REPO/$ldir" loader/linker.lds.S >"$ldir/linker.lds"

	# lib/lzma is written against the U-Boot environment; strip that away.
	mkdir -p "$ldir/lzma"
	for f in LzmaDec.h 7zTypes.h Compiler.h Precomp.h; do
		cp "lib/lzma/$f" "$ldir/lzma/$f"
	done
	sed -e '/#include <config.h>/d' \
	    -e '/#include <watchdog.h>/d' \
	    -e 's|#include <linux/string.h>|#include <string.h>|' \
	    -e 's|schedule();|((void)0);|' \
	    lib/lzma/LzmaDec.c >"$ldir/lzma/LzmaDec.c"

	cflags="-Os -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
-mgeneral-regs-only -mstrict-align -ffunction-sections -fdata-sections \
-Wall -Wextra -Werror -DZ7_LZMA_SIZE_OPT -I$REPO/$ldir -I$REPO/$ldir/lzma"
	for src in loader/start.S loader/main.c "$ldir/payload.S" \
			"$ldir/lzma/LzmaDec.c"; do
		obj=$ldir/$(basename "${src%.*}").o
		$CC $cflags -c "$src" -o "$obj"
	done
	elf=$ldir/bl33.elf
	$LD --gc-sections --build-id=none -T "$ldir/linker.lds" \
		"$ldir/start.o" "$ldir/main.o" "$ldir/payload.o" \
		"$ldir/LzmaDec.o" -o "$elf"
	$OBJCOPY -O binary "$elf" "$out/bl33.bin"

	# Geometry: the relocation copy and the decode output must not fight.
	addresses=$($NM "$elf" | awk '$3 ~ /^(__bss_end|_image_end)$/ {print $3"=0x"$1}')
	eval "$addresses"
	[ -n "${__bss_end:-}" ] && [ -n "${_image_end:-}" ] \
		|| die "loader symbols missing"
	bl33_size=$(stat -c%s "$out/bl33.bin")
	[ "$((_image_end - EXEC_ADDR))" = "$bl33_size" ] \
		|| die "image length does not match _image_end"
	[ $(($__bss_end - EXEC_ADDR)) -lt "$WINDOW" ] \
		|| die "stub workspace overflows the 16 MiB window"
	[ $((RUN_ADDR + plain_size - EXEC_ADDR)) -lt 0 ] \
		|| die "decompressed U-Boot would overwrite the stub at EXEC_ADDR"
	[ "$bl33_size" -le "$budget" ] \
		|| die "bl33 is $bl33_size bytes, budget is $budget"

	echo "==> $board: plain $plain_size, lzma $lzma_size, bl33 $bl33_size" \
		"(budget $budget)"
}

case "${1:-}" in
pt1500 | tyfza1) build_board "$1" ;;
all)
	build_board pt1500
	build_board tyfza1
	;;
*) die "usage: $0 pt1500|tyfza1|all" ;;
esac
