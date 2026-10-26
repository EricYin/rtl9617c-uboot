.. SPDX-License-Identifier: GPL-2.0+

Adding a board
==============

The support for this SoC family is split in three layers. A new board only
touches the last one.

===================== ====================================================
Layer                 Where
===================== ====================================================
SoC                   ``arch/arm/mach-taurus/``, ``drivers/``,
                      ``arch/arm/include/asm/arch-taurus/``
Platform recovery     ``board/realtek/common/``
Board                 ``board/realtek/<board>/``
===================== ====================================================

A board directory holds four source files and an environment file:

========================== ==========================================
File                       Contents
========================== ==========================================
``board.c``                init, boot commands, boot policy
``desc.c``                 lamps, reset key, model strings
``layout.c``               flash layout and UBI volume table
``<board>.env``            default environment
``Kconfig``, ``Makefile``  naming and objects
========================== ==========================================

Six steps
---------

1. Device tree
~~~~~~~~~~~~~~

Include the SoC description and add what the board wires:

.. code-block:: dts

   /dts-v1/;
   #include <dt-bindings/gpio/gpio.h>
   #include <dt-bindings/input/input.h>
   #include "rtl9617c.dtsi"

   / {
           model = "Vendor Board";
           compatible = "vendor,board", "realtek,rtl9617c";

           aliases { serial0 = &uart0; ethernet0 = &eth; };
           chosen { stdout-path = "serial0:115200n8"; };

           buttons {
                   compatible = "gpio-keys";

                   reset {
                           label = "reset";
                           linux,code = <KEY_RESTART>;
                           gpios = <&gpio0 136 GPIO_ACTIVE_LOW>;
                   };
           };
   };

``rtl9617c.dtsi`` already provides ``memory``, ``reserved-memory``,
``serial0``, ``gpio0`` (160 pins), ``eth`` and the ``spi_nand`` node. GPIO
numbers are global, so ``136`` is bit 8 of bank 4.

Add the flash partitions below the SPI NAND node; the driver registers them and
no ``mtdparts`` string is involved:

.. code-block:: dts

   &spi_nand {
           partitions {
                   compatible = "fixed-partitions";
                   #address-cells = <1>;
                   #size-cells = <1>;

                   boot@0 {
                           label = "boot";
                           reg = <0x0 0x200000>;
                   };

                   env@200000 {
                           label = "env";
                           reg = <0x200000 0x40000>;
                   };

                   ubi_device@240000 {
                           label = "ubi_device";
                           reg = <0x240000 0xfdc0000>;
                   };
           };
   };

Validate it against the erase block size before trusting it: an offset that is
not a multiple of 128 KiB cannot be erased on its own. See :doc:`partitions`
for why the layout looks like this and what it must not be changed to.

2. Kconfig entry
~~~~~~~~~~~~~~~~

In ``arch/arm/Kconfig``, next to ``TARGET_TYFZA1``:

.. code-block:: none

   config TARGET_NEWBOARD
           bool "Vendor Newboard (RTL9617C)"
           select ARM64
           select DM
           select OF_CONTROL
           select BOARD_LATE_INIT
           select EVENT
           select SOC_RTL9617C
           select RTK_RECOVERY          # only with the recovery environment

and source the board Kconfig next to the others:

.. code-block:: none

   source "board/realtek/pt1500/Kconfig"

3. Board directory
~~~~~~~~~~~~~~~~~~

``board/<vendor>/<board>/`` with a ``Kconfig`` that only declares naming.
Keep ``SYS_VENDOR`` as ``realtek`` if the board uses ``board/realtek/common/``:
that directory is only built for the matching vendor.

.. code-block:: none

   if TARGET_NEWBOARD
   config SYS_BOARD
           default "newboard"
   config SYS_VENDOR
           default "realtek"
   config SYS_SOC
           default "taurus"
   config SYS_CONFIG_NAME
           default "newboard"
   endif

``board.c`` is deliberately short, because the boot policy is not per board:

* ``board_init()`` - ``taurus_early_init()`` then ``rtk_board_reset_latch()``.
  Call ``taurus_ni_gphy_setup()`` only when the internal GPHYs are used; a
  board with external PHYs must not.
* ``dram_init()`` / ``dram_init_banksize()`` - validate
  ``taurus_dram_size_mb()`` and publish the usable banks; without the second
  one the DRAM bank list stays empty
* ``board_late_init()`` - set any variant the board has (TYFZA1 reads a strap),
  then ``rtk_board_late_env()``
* ``board_preboot_os()`` - ``rtk_board_preboot()``

``rtk_board_late_env()`` installs the environment, ``rtk_board_preboot`` hands
the packet memory back, and the reset-key spy that starts the recovery is
registered in ``board/realtek/common/boot.c``. The commands are ``rtk_boot``
and ``rtk_recovery``, the same two on every board, so ``bootcmd`` is identical
everywhere.

A board only writes its own boot code when it genuinely boots differently, and
then it changes the layout or the descriptor rather than the command.

``taurus_ni_board_ops()`` is the one optional hook: implement it when the
network needs board bring-up. Port mapping that the device tree can express
belongs in the tree instead, as ``p4-phy-config = <phy_id sds_mode>``.

4. Board descriptor
~~~~~~~~~~~~~~~~~~~

``desc.c`` returns ``rtk_board_desc()``, see ``include/rtk_board.h``. It names
the board, gives the port release shown by the recovery page, and describes
which pins drive the four status lamps, whether they are active low, and which
pin the reset key is on:

.. code-block:: c

   static const struct rtk_board_variant newboard_variants[] = {
           {
                   .lamp = {
                           { .pin = 102 }, { .pin = 103 },
                           { .pin = 97 },  { .pin = 100 },
                   },
                   .ports = "4×GE",
           },
   };

   static const struct rtk_board_desc newboard_desc = {
           .name = "NEWBOARD",
           .version = "2026.07",
           .variants = newboard_variants,
           .nr_variants = ARRAY_SIZE(newboard_variants),
           .reset_pin = 136,
           .reset_active_low = true,
   };

Each variant also names the FIT configuration to boot (``.fit_config``), so
the boot command stays generic: it asks the descriptor what this unit runs.

Boards with a variant strap fill ``variant_env`` and list one entry per
variant; ``rtk_board_variant()`` then resolves the environment value.

5. Flash layout
~~~~~~~~~~~~~~~

``layout.c`` returns ``rtk_recovery_layout()``, see ``include/rtk_recovery.h``.
It is the only place on the board that knows volume names, the volume id of each
slot's root filesystem, the kernel command line and the UBI rebuild table.
Everything in ``board/realtek/common/`` is driven by it and contains no product
data at all. The whole structure is:

* ``boot_mtd``, ``nand_dev``, ``ubi_part``, ``ubi_mtd``, ``flash_size`` - where
  the firmware, the UBI device and the whole flash are,
* ``kernel_fmt``, ``root_index[]``, ``kernel_size`` - the slot volumes,
* ``bootargs``, normally ``RTK_BOOTARGS``, with ``%u`` for the root block
  device; the value is derived from ``ubi_mtd`` and ``root_index``, not written
  by hand,
* ``dt_compatible`` - the device tree node listing the volumes a rebuild keeps,
* ``volumes[]``, ``nr_volumes``, ``keep_mask`` - the rebuild table and which
  entries survive it.

6. Defconfig
~~~~~~~~~~~~

.. code-block:: none

   CONFIG_TARGET_NEWBOARD=y
   CONFIG_DEFAULT_DEVICE_TREE="rtl9617c-newboard"
   CONFIG_ENV_SOURCE_FILE="newboard"
   CONFIG_DM_SERIAL=y
   CONFIG_CORTINA_UART=y
   CONFIG_DM_GPIO=y
   CONFIG_CORTINA_GPIO=y
   CONFIG_MTD=y
   CONFIG_DM_MTD=y
   CONFIG_MTD_RAW_NAND=y            # required by CORTINA_SPI_NAND
   CONFIG_CORTINA_SPI_NAND=y
   CONFIG_DM_ETH=y
   CONFIG_TAURUS_NI=y
   CONFIG_PHY_RTL8221B=y            # only with the external 2.5G PHY

``SOC_RTL9617C`` and ``RTK_RECOVERY`` are hidden symbols selected by the
target, so they do not belong in the defconfig. ``CONFIG_SYS_MAX_NAND_CHIPS``
and ``CORTINA_UART_CLOCK`` still come from ``include/configs/<board>.h``.

What RTK_RECOVERY means
-----------------------

``RTK_RECOVERY`` - declared in ``board/realtek/common/Kconfig`` - builds the
whole shared platform layer: the FIP and sysupgrade parsers, the loader
signature chain, the install and restore engine, the lamp state machine
(``board-ui.c``) and the web UI hooks (``recovery-http.c``).

Selecting it commits the board to providing both ``rtk_recovery_layout()`` and
``rtk_board_desc()``; without them the link fails with an undefined reference,
because neither has a default. A board that does not select it builds without
any of this: no install engine, no web recovery jobs, no status lamps.

The lamp pins are driven through ``arch/arm/mach-taurus/gpio.c``
(``taurus_gpio_*``), which understands all five banks and saves their state so
the lamps can be handed back to their previous owner.
