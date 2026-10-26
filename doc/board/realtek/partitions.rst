.. SPDX-License-Identifier: GPL-2.0+

Flash layout
============

Every board of this platform uses the same physical partitioning, and it is the
one the firmware being replaced already used. It appears once per board, in the
device tree, and nothing at runtime rebuilds it from a ``mtdparts`` string.

===================== ========= =========== ============================
MTD                   Offset    Size        Contents
===================== ========= =========== ============================
``boot``              0x000000  2 MiB       BL2, and the FIP with BL31/BL33
(gap, no partition)   0x200000  512 KiB     left unowned, as the vendor did
``env``               0x280000  256 KiB     U-Boot environment
``env2``              0x2C0000  256 KiB     unused; the vendor's backup slot
``static_conf``       0x300000  256 KiB     vendor configuration area
``ubi_device``        0x340000  251648 KiB  the UBI device
(gap, no partition)   0xF900000 7 MiB       tail, unowned
===================== ========= =========== ============================

Both products were measured on hardware - ``/proc/mtd``, ``/proc/cmdline`` and
the stored environment agree - and the table is byte-identical between them. The
512 KiB at 0x200000 is a gap in both: the vendor leaves it unowned rather than
naming it.

Why the vendor offsets, and not something tighter
-------------------------------------------------

An earlier revision moved ``ubi_device`` down to 0x240000 to reclaim the
512 KiB gap, the second environment and ``static_conf``: about 8 MiB more UBI.
Three things make that a bad trade, and all three were found by reading the
device, not by reasoning about the port.

**The kernel has no partitions of its own.** The vendor device trees describe
the SPI NAND as a bare ``spi-nand`` node with no ``partitions`` child, so the
running kernel gets its table from the command line alone:

.. code-block:: none

   console=ttyS0,115200 ubi.mtd=4 root=31:8 rootfstype=squashfs \
   mtdparts=ca_spinand_flash:2048K(boot),256K@0x280000(env),256K@0x2C0000(env2),\
   256K@0x300000(static_conf),251648K@0x340000(ubi_device)

That numbering is the whole reason the offsets cannot move on their own:
``ubi.mtd=4`` is the index of ``ubi_device`` among the partitions (boot is 0),
and ``root=31:8`` is ``ubi_mtd + 1 + volume id``, where 3 is ``ubi_r0``. Shift
one partition and the kernel attaches the wrong device, or none.

So the boards pass that string verbatim. Each board stores it in its layout
(``mtdparts``) and appends it to the command line it generates; the string is
part of the product description, not of the boot policy.

**UBI aligns on erase blocks, so a shifted start is worse than a wrong one.** A
start at 0x240000 is exactly eight 128 KiB blocks below 0x340000. The PEB grid
still lines up, so UBI finds the vendor's headers, cannot find the volume table
where it expects it (first two PEBs), and rebuilds it - which is a write. That
is how a "read-only" RAM test ended up rewriting a unit's volume table.

**``static_conf`` is the vendor's, not ours.** On the unit inspected it happened
to be erased. That is not a licence to allocate it: it is a named partition in
the vendor table, and some builds or production steps can be expected to use
it. 256 KiB is not worth the risk.

Consequence for a first flash: only ``boot`` has to be written. ``env`` is ours
now, and everything from 0x300000 upwards stays exactly as it was, so the
existing UBI is not rebuilt and the existing system still starts.

Why not literally "boot + one whole UBI"
----------------------------------------

The obvious scheme is two partitions, ``boot`` and one UBI covering the rest.
Two properties of this firmware make that impossible:

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

The environment is deliberately **not** redundant. The boot policy - ``bootcmd``,
the boot menu, the kernel command line - is computed by code, so an unreadable
environment falls back to the compiled-in default and the board still boots.
The 256 KiB of a second copy buys nothing.

Why the partition table is in the device tree
---------------------------------------------

The table appears in ``arch/arm/dts/rtl9617c-<board>.dts`` as a
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

                   env@280000 {
                           label = "env";
                           reg = <0x280000 0x40000>;
                   };

                   env2@2c0000 {
                           label = "env2";
                           reg = <0x2c0000 0x40000>;
                   };

                   static_conf@300000 {
                           label = "static_conf";
                           reg = <0x300000 0x40000>;
                   };

                   ubi_device@340000 {
                           label = "ubi_device";
                           reg = <0x340000 0xf5c0000>;
                   };
           };
   };

``mtd_probe_devices()`` cannot be relied on to add these: it returns early while
both ``mtdparts`` and ``mtdids`` are unset, which is the state a fresh board is
in. The driver therefore sets ``mtd->flash_node`` and calls
``add_mtd_partitions_of()`` itself, and the board clears any ``mtdparts`` an
older firmware left in the environment - a non-empty value would make the same
function delete the device tree partitions and create none.
