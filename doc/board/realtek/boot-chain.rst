.. SPDX-License-Identifier: GPL-2.0+

Boot chain and signing
======================

The boot chain is fixed by the platform firmware and only its last stage is
ours:

.. code-block:: none

   BootROM -> BL2 (from the boot partition) -> BL31 + BL33 (from the FIP)

BL2 loads the FIP copies at 0x40000 and 0x100000 of the ``boot`` partition, and
BL33 is the U-Boot built here. Everything above BL33 is Realtek's; the image it
replaces on a shipping PT1500 identifies itself as ``U-Boot 2020.01``.

What the stock firmware does
----------------------------

A read-only dump of a running PT1500 shows that BL2 is a TF-A build with the
mbedTLS crypto module compiled in: X.509 parsing, ``sha256WithRSAEncryption``,
``RSASSA-PSS``, NV counters, and a ``ROTPK undeployed`` message. The FIP is not
only BL2/BL31/BL33 either: its tail carries a six certificate chain whose
extensions hold the SHA-256 of each of the three images (the Realtek OID branch
``1.3.6.1.4.1.4128.2100.x``, digest extension 201, 603 and 1201).

So an image is authenticated by binding the hash of every stage into a chain
that ends at a root key. Replacing BL33 with a different binary invalidates that
binding.

Whether that is *enforced* depends on whether the root public key hash is fused
into the OTP, which a flash dump cannot show. It has since been observed on a
PT1500-H10, and it is not enforced: the unit boots a stock firmware whose BL1,
BL2 and BL31 each report the key as absent.

.. code-block:: none

   NOTICE:  BL1: v1.5(release):TAURUS_TAPEOUT_2_0
   NOTICE:  ROTPK undeployed
   NOTICE:  BL1: Booting BL2
   NOTICE:  BL2: v1.5(release):gd0a5d7717
   NOTICE:  ROTPK undeployed
   NOTICE:  BL1: Booting BL31
   NOTICE:  BL31: v1.5(release):gd0a5d7717
   NOTICE:  ROTPK undeployed

Three stages, three "undeployed", and the boot continues past each of them: the
certificate chain is parsed but never checked. An unsigned U-Boot is therefore
accepted by this BootROM and by BL2.

That is a property of the units, not of the code. A unit whose ROTPK is fused
would reject the same image, and nobody outside Realtek can produce one it would
accept, because the signing key is theirs. The caution below therefore costs one
power cycle and is still worth following the first time a board is written.

Writing a board the first time
------------------------------

1. keep FIP0 as it is - the known-good image,
2. write the new U-Boot into FIP1 only,
3. power-cycle and watch the UART,
4. if it does not start, write the original FIP1 back from the dump. FIP0 was
   never touched, so the unit still boots.

BL31 keeps its secure region at 0x0eeff000, which is why the DRAM banks stop
there and resume at 0x0f000000.

What was disabled in this U-Boot
--------------------------------

The requirement was to stop verifying, and that is what changed. Everything
below was verification of *our own* images and is now gone:

* ``CONFIG_RSA``, ``CONFIG_RSA_VERIFY_WITH_PKEY``, ``CONFIG_RSASSA_PSS`` and
  the PKCS#1 v1.5 / PSS code they pull in - the image is 28 KiB smaller,
* the loader certificate chain parser (``loader-cert.c``,
  ``loader-cert-crypto.c``, ~400 lines): the boot area is now only checked
  structurally, that it is a 2 MiB image with a GPT and two identical FIPs that
  keep this device's BL2 and BL31,
* the FIT provenance checks (``verify_fit()`` and its compatible test): an
  uploaded image is booted as long as it is a valid FIT with the expected
  configuration name.

Note what this does *not* do: it cannot make BL2 accept an image it will not
accept, and it does not touch BL31 or the secure world. It only means U-Boot no
longer refuses anything on its own.

A consequence worth stating: the recovery service will now write a boot image
that BL2 may reject. The structural check in ``rtk_validate_loader()`` catches a
truncated or inconsistent image, not an unauthorised one.
