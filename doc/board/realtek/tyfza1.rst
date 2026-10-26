.. SPDX-License-Identifier: GPL-2.0+

TYFZA1
======

The TYFZA1 is a Tianyi home gateway built on the Realtek RTL9617CVW
("Cortina Taurus") SoC. U-Boot is the BL33 stage of the platform
firmware and provides the boot menu, the production boot path and a
browser based recovery environment.

Hardware
--------

* Realtek RTL9617CVW, 64-bit ARM
* 512 MiB DRAM
* SPI NAND behind the Cortina SPI NAND controller
* Cortina NI Ethernet MAC with four ports; on the ``2p5g`` board variant
  port 4 is driven by an RTL8221B 2.5G PHY. U-Boot does not drive that PHY -
  its driver is left out of the build to stay inside the BL33 budget, see
  :doc:`boot-chain` - so recovery runs on the four GE ports
* Cortina UART, 115200 8n1, 125 MHz input clock
* Reset key on GPIO 8 plus front panel status LEDs

Building
--------

.. code-block:: bash

   export CROSS_COMPILE=aarch64-linux-gnu-
   make tyfza1_defconfig
   make

This produces ``u-boot.bin`` (``u-boot-nodtb.bin`` with the device tree
appended) and ``u-boot.dtb``. U-Boot is only the BL33 payload: the
images have to be wrapped into the platform FIP together with BL2 and
BL31 by the platform flashing tooling.

Storage layout
--------------

.. note::

   The offsets were verified against a full backup of a unit (SWVer V1.0.4):
   ``/proc/mtd``, ``/proc/cmdline`` and the stored environment agree, and the
   table is byte-identical to PT1500's. The 512 KiB at 0x200000 is a gap - the
   vendor names no partition there, and neither does this port.

The physical partitioning is the platform one; see :doc:`partitions`. This
product's UBI volume table is:

===== ================== =========== ==============================
Id    Volume             Size        Contents
===== ================== =========== ==============================
0     ubi_Config         6.0 MiB     factory, calibration, identity
1     ubi_fpga           4.1 MiB     FPGA image
2     ubi_DTB0           380 KiB     slot A device tree
3     ubi_k0             6.0 MiB     slot A kernel (FIT)
4     ubi_r0             32.1 MiB    slot A rootfs (SquashFS)
5     ubi_DTB1           380 KiB     slot B device tree
6     ubi_k1             6.0 MiB     slot B kernel (FIT)
7     ubi_r1             32.1 MiB    slot B rootfs
8     ubi_framework1     16.1 MiB    framework SquashFS
9     ubi_framework2     16.1 MiB    framework SquashFS
10    ubi_apps           96.0 MiB    application stack
11    rootfs_data        16.0 MiB    writable overlay
===== ================== =========== ==============================

Volumes 0, 1, 2 and 5 hold device identity and are never removed by a rebuild.

Environment
-----------

``board/realtek/<board>/<board>.env`` holds only what is specific to the
hardware: the recovery network defaults and the load address. The boot policy -
``bootcmd``, the boot menu entries, the kernel command line - is set by
``rtk_board_env_init()`` in code, identically on every board, so an erased or
unreadable environment cannot change how the board boots.

The environment is stored once, at 0x200000, without a redundant copy.

Boot flow
---------

``bootcmd`` is ``bootmenu 3`` on every board, with the same two entries:

* ``Start OpenWrt`` runs ``rtk_boot``: attach UBI, read ``ubi_k<slot>`` for the
  slot named by the ``slot`` variable (default 0), set ``bootargs`` from the
  layout and boot the FIT configuration the board descriptor names for this
  unit's variant.
* ``Web recovery`` runs ``rtk_recovery``.

Holding the reset key at power on starts the recovery instead, and a key pressed
after power-on is still caught just before ``bootcmd`` runs.

Recovery
--------

The recovery environment is the HTTP service (``CONFIG_CMD_HTTPD``): a single
page on 192.168.0.1 with a built-in DHCP server, supporting RAM uploads and
downloads, command execution, whole-flash restore, kernel and boot-area
installation, boot of an uploaded FIT and reset.

Writes are range, alignment and bad-block checked before any erase, and the
details live in ``board/realtek/common/``; the board only says which pins are
the lamps and which volume names the product uses.

There is no longer an install job for the vendor's own upgrade archive: the
service writes a kernel FIT into the slot volume and leaves the rest of the
flash to the storage endpoints, which is all the layout needs.

Source layout
-------------

The board is one of three layers; see :doc:`porting` for the split.

Everything this port adds lives in:

* ``arch/arm/dts/rtl9617c.dtsi`` - SoC nodes shared by all boards
* ``arch/arm/dts/rtl9617c-tyfza1.dts`` - board nodes: reset key, flash
  partitions, recovery layout
* ``arch/arm/mach-taurus/`` - SoC init: MMU map, DRAM size, watchdog,
  PSCI reset, NI packet memory hand-off
* ``arch/arm/include/asm/arch-taurus/`` - register map and ``soc.h``
* ``drivers/net/taurus_ni*.c`` - NI Ethernet MAC
* ``drivers/net/phy/rtl8221b.c`` - 2.5G PHY with its revision 8 tables
* ``drivers/mtd/nand/raw/cortina_spi_nand/`` - SPI NAND controller
* ``board/realtek/common/`` - boot command and environment, FIP parsing,
  install and restore engine, lamp state machine, web UI hooks; driven by the
  board layout and descriptor
* ``board/realtek/tyfza1/`` - ``board.c`` (init and variant strap), ``desc.c``
  (lamps, key and FIT configuration), ``layout.c`` (flash layout),
  ``network.c`` (2.5G bring-up), ``tyfza1.env``
* ``configs/tyfza1_defconfig``, ``include/configs/tyfza1.h``
* shared headers: ``include/rtk_recovery.h``, ``include/rtk_board.h``,
  ``include/net/httpd-board.h``, ``include/taurus_ni.h``,
  ``include/cortina_nand.h``

Upstream files touched, and why:

* ``arch/arm/Kconfig``, ``arch/arm/Makefile`` - the target and the SoC layer
* ``drivers/serial/Kconfig``, ``drivers/gpio/Kconfig`` - let the existing
  Cortina UART and GPIO drivers depend on ``SOC_RTL9617C``
* ``drivers/gpio/cortina_gpio.c`` - optional pin mux block
* ``drivers/net/{Kconfig,Makefile}``, ``drivers/net/phy/{Kconfig,Makefile}``,
  ``drivers/mtd/nand/raw/{Kconfig,Makefile}`` - new drivers
* ``cmd/Kconfig``, ``cmd/lwip/Makefile``, ``net/lwip/``, ``lib/lwip/`` -
  the HTTP management service
* ``cmd/ubi.c``, ``include/ubi_uboot.h`` - UBI volume helpers
