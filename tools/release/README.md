# Boot release tools

Pack a flashable boot partition (and its Xmodem rescue slice) around a
freshly signed BL33. Everything the build needs lives inside this repo:
board definitions, stock backups, the Taurus TF-A signing tools and
(once generated) the signing keys. No board knowledge or external path
is hardcoded.

## Build

    ./build.sh all              # every board, end to end
    ./build.sh pt1500           # one board: bl33 -> pack -> verify

Steps can be run individually:

    ./build.sh pt1500 bl33      # U-Boot + LZMA self-decompressing stub
    ./build.sh pt1500 pack      # sign, rebuild both FIP slots, slice
    ./build.sh pt1500 verify    # verify the existing images

Outputs in `output/<board>/`:
- `<board>.boot.bin` — full mtd0 image; the normal flash path
- `<board>.fip.bin` — main-slot slice; BL2 Xmodem `F` rescue input
- `<board>.manifest.json` — digests of every input and output

Signing keys are generated on first use into `tools/release/keys/`
(never committed). The packer refuses a stock backup whose digest does
not match the board record, and verifies the rebuilt certificate chain
before declaring success.

## Layout

    tools/release/pack_boot.py    pack + verify (single tool)
    tools/release/build_bl33.sh   U-Boot -> LZMA -> BL33 stub
    tools/release/boards/*.json   per-board geometry, UUIDs, budget
    tools/release/stocks/         stock mtd0 backups (in git)
    tools/release/tf-a/           Taurus cert_create/fiptool sources
    tools/release/keys/           signing keys (generated, not in git)
    output/<board>/               build products (not in git)

## Adding a board

1. Back up the stock boot partition (mtd0, 2 MiB) from a working unit;
   store it as `tools/release/stocks/<board>/stock-mtd0.bin`.
2. Record its digest and parse the FIP ToC: magic, header/entry sizes,
   and the UUID of every entry (`parse_fip` in pack_boot.py reads a
   slot; UUIDs are the little-endian display form).
3. Copy `boards/pt1500.json` to `boards/<board>.json`; update `board_id`,
   `defconfig`, `bl33_budget` (= stock BL33 size), `stock_sha256` and
   the UUID table. The `layout` block only changes if the slot geometry
   differs.
4. `./build.sh <board>`.
