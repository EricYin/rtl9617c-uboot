.. SPDX-License-Identifier: GPL-2.0+

Flash layout
============

Every board of this platform uses the same physical partitioning. It is
described once per board, in its device tree, and nothing at runtime builds it
from a ``mtdparts`` string.

===================== ========= ========= ==================================
MTD                   Offset    Size      Contents
===================== ========= ========= ==================================
``boot``              0x000000  2 MiB     BL2, and the FIP with BL31/BL33
``env``               0x200000  256 KiB   U-Boot environment
``ubi_device``        0x240000  251.9 MiB The single UBI device
===================== ========= ========= ==================================

Everything that is not the boot area or the environment lives in one UBI
device. There is no ``reserved``, no second environment copy, no static
configuration partition: the products that had them kept them from a firmware
this port replaces.

Why not literally "boot + one whole UBI"
----------------------------------------

The obvious scheme is two partitions, ``boot`` and one UBI covering the rest of
the flash. Two things in this firmware make that impossible:

* **The boot area has fixed offsets.** BL2 loads the FIP copies from 0x40000 and
  0x100000, so the first 2 MiB cannot be handed to UBI. This is a property of
  BL2, not a choice.
* **The environment needs a raw block.** UBI owns every erase block from the
  start of its partition and moves data between them, so a raw environment
  inside the UBI range is destroyed by the first wear-levelling pass. The
  alternative - storing the environment as a UBI volume
  (``CONFIG_ENV_IS_IN_UBI``) - means attaching UBI before the environment is
  available, which costs a full NAND scan on every boot and makes a corrupted
  UBI cost the environment as well.

So the scheme is "one boot area, one environment block, one whole UBI". The
environment is the only concession, and it is 256 KiB.

The environment is deliberately **not** redundant any more. The boot policy -
``bootcmd``, the boot menu, the kernel command line - is computed by code, not
read from NAND, so an unreadable environment falls back to the compiled-in
default and the board still boots. Losing the second copy of a store that is
not needed booting is not worth 256 KiB and a second partition.

Why the partition table is in the device tree
---------------------------------------------

The table above appears in ``arch/arm/dts/rtl9617c-<board>.dts`` as a
``fixed-partitions`` node below ``&spi_nand``, and the SPI NAND driver registers
it:

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

This replaced ``CONFIG_MTDPARTS_DEFAULT``, ``CONFIG_MTDIDS_DEFAULT`` and the
``mtdparts`` environment variable. The advantages are that U-Boot and the
kernel read the same description, that the names in the code (``boot``,
``ubi_device``) cannot drift from the layout that is actually on the flash, and
that there is no string whose syntax can be wrong.

The driver has to register the partitions itself. ``mtd_probe_devices()`` has a
fallback to device tree partitions, but it returns early when both ``mtdparts``
and ``mtdids`` are unset, so relying on it would silently produce a flash with
no partitions at all. The driver therefore sets ``mtd->flash_node`` and calls
``add_mtd_partitions_of()`` right after registering the MTD device.

``board_late_init()`` clears any ``mtdparts``/``mtdids`` that an older firmware
left in the environment, because a non-empty value makes ``mtd_probe_devices()``
delete the device tree partitions and then fail to recreate any.

UBI volume tables
-----------------

The volumes are product data and stay in ``board/realtek/<board>/layout.c``.
They differ between the two boards that exist today: the TYFZA1 has an FPGA
volume and a data volume, the PT1500 has two extra configuration volumes and no
FPGA. See :doc:`tyfza1` and :doc:`pt1500` for the tables.

The volume id is what decides the kernel's block device: with the UBI partition
at MTD index 2, volume *n* is ``/dev/mtdblock(3 + n)``, which the boot code
computes from the layout instead of hardcoding ``root=31:...``.

Sizing
------

The UBI partition is 0xFDC0000 bytes, which is 2030 erase blocks of 128 KiB.
A volume table only has to fit in that; the check in ``rtk_rebuild_ubi()``
refuses a table that does not.
