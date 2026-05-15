# Resources — Week 10

Every link below is free unless explicitly marked otherwise. The RP2040 datasheet section, the UF2 README, the USB DFU 1.1 spec, the MCUboot project documentation, and RFC 8032 are the five load-bearing references; if you read only these five, you will pass the quiz and the mini-project will work.

---

## Primary references — read this week

### 1. RP2040 datasheet §2.8 ("Bootrom")

- **Source:** Raspberry Pi Ltd., "RP2040 Datasheet" rev. 2.1, March 2022.
- **URL:** <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- **Section we use:** §2.8 ("Bootrom"), pp. 130–141. Eleven pages, three figures, one table.
  - §2.8.1, pp. 130–132 — "Processor Controlled Boot Sequence". The cold-boot flowchart on Figure 11, p. 131, is the diagram you will draw on a whiteboard six times this week.
  - §2.8.1.2, p. 132 — "Stage 1 Bootloader". Defines the 256-byte budget, the CRC32 trailer, and the entry-point register conventions.
  - §2.8.3, pp. 132–135 — "Bootrom API". Table 167 on pp. 132–134 is the function table you call from your bootloader.
  - §2.8.5, pp. 138–141 — "USB Mass Storage / PICOBOOT Interface". Figure 12 on p. 138 is the USB MSC state diagram; the table on p. 140 lists the vendor-specific PICOBOOT commands `picotool` uses.
- **Cite as:** "RP2040 datasheet §2.8.x, p. Y".

### 2. UF2 (USB Flashing Format) specification

- **Source:** Microsoft Corporation, `microsoft/uf2` repository on GitHub.
- **URL:** <https://github.com/microsoft/uf2>
- **License:** BSD-2-Clause.
- **Sections we use:** the README's "File format" section (lines 1–96 of `README.md` at HEAD-ref); the `utils/uf2families.json` registry; the `uf2format.h` header (~80 lines, gives you the struct layout in C).
- **What you will write against it:** Exercise 1 is a 120-line UF2 parser; homework problem 4 is a UF2 packer.
- **Cite as:** "UF2 spec, microsoft/uf2 README".

### 3. USB Device Firmware Upgrade Specification, Revision 1.1

- **Source:** USB Implementers Forum, "USB Device Firmware Upgrade Specification, Revision 1.1, August 5, 2004".
- **URL:** <https://www.usb.org/sites/default/files/DFU_1.1.pdf>
- **License:** USB-IF's standard "may be reproduced for use in implementing the spec" license — free to read, free to implement against.
- **Sections we use:**
  - §3, pp. 8–10 — "DFU Class Specific Descriptors and Requests". The seven request codes are in Table 3.1, p. 10.
  - §4, pp. 11–15 — "DFU Class Specific Descriptors and Strings". The DFU Functional Descriptor is Table 4.2, p. 14.
  - §6, pp. 16–25 — "DFU Operation". The state machine is Figure 6.1 on p. 17 plus the text on pp. 18–25.
  - Appendix A, p. 38 — "DFU State Diagram", same state machine in a single full-page graphic.
- **What you will build against it:** a subset run-time DFU descriptor on the device (Challenge 2), and you will read a `dfu-util` capture of the seven requests.
- **Cite as:** "USB DFU 1.1 §X, p. Y".

### 4. MCUboot project documentation

- **Source:** Linaro / `mcu-tools/mcuboot` on GitHub, published docs site.
- **URL:** <https://docs.mcuboot.com/>
- **License:** Apache 2.0 (MCUboot itself); docs are CC-BY-4.0.
- **Sections we use:**
  - "Design" — <https://docs.mcuboot.com/design.html>. The vocabulary chapter. Slots, primary/secondary, swap, image trailers, magic numbers. Read this on Wednesday before Lecture 2.
  - "Encrypted images" — <https://docs.mcuboot.com/encrypted_images.html>. We do not use these this week but the doc is short and clarifies what a real product would add.
  - "Serial recovery" — <https://docs.mcuboot.com/serial_recovery.html>. Inspiration for our OTA protocol.
  - "Image swap with rollback" — section in the design doc, around `#image-swap-with-rollback`.
- **Cite as:** "MCUboot design doc, §title".

### 5. RFC 8032 — Edwards-Curve Digital Signature Algorithm (EdDSA)

- **Source:** Josefsson & Liusvaara, IETF, January 2017.
- **URL:** <https://www.rfc-editor.org/rfc/rfc8032.html>
- **License:** IETF Trust legal provisions — free to read, free to implement.
- **Sections we use:**
  - §5.1, pp. 13–17 — "Ed25519ph, Ed25519ctx, and Ed25519". Defines the curve parameters, the key encoding, and the sign/verify equations.
  - §5.1.7, p. 17 — "Verify". The cofactored verification equation we implement.
  - §7.1, pp. 24–25 — "Ed25519 Test Vectors". The vectors Exercise 3 verifies against.
- **Cite as:** "RFC 8032 §5.1.X" or "RFC 8032 §7.1 test vector N".

---

## Secondary references — consult as needed

### ed25519-donna (reference implementation)

- **Source:** Andrew Moon (`floodyberry`), `floodyberry/ed25519-donna` on GitHub.
- **URL:** <https://github.com/floodyberry/ed25519-donna>
- **License:** Public domain.
- **What it is:** A C reference implementation of Ed25519 sign+verify, ~1500 LOC. We extract the verify path (`ed25519_sign_open`, `curve25519_*`, `ge25519_*`, the SHA512 implementation) into a ~12 KB blob suitable for the bootloader's `.text` section. The `donna64` and `donna32` variants are mathematically equivalent but use 64-bit and 32-bit limbs respectively; the Cortex-M0+ has no native 64-bit multiply, so `donna32` is smaller and faster on this part.
- **Compile-time options we set:** `-DED25519_REFHASH` (use the included SHA512, not a system one), `-DED25519_NO_INLINE_ASM` (the M0+ has no inline-assemblable accelerators), `-DED25519_FORCE_32BIT` (use the 32-bit limb path).

### Monocypher (an alternative)

- **Source:** Loup Vaillant, `monocypher` website.
- **URL:** <https://monocypher.org/>
- **License:** CC0 (public-domain-equivalent) or BSD 2-Clause.
- **Why we mention it:** Smaller than ed25519-donna (~2000 LOC for the entire library, ~600 LOC for Ed25519 alone), more modern API, single-file `monocypher.c`. If you want to swap our verifier for Monocypher in the homework, the function call is `crypto_check(sig, pk, msg, msg_len)` returning 0 on success.

### picotool

- **Source:** Raspberry Pi Ltd., `raspberrypi/picotool` on GitHub.
- **URL:** <https://github.com/raspberrypi/picotool>
- **License:** BSD-3-Clause.
- **Installation:** macOS `brew install picotool`; Linux build-from-source per the README; Windows pre-built binaries in the Releases.
- **What we use it for:**
  - `picotool info -a` — print binary info from flash; you confirm your bootloader's `bi_decl(bi_program_description("CC bootloader"))` shows up.
  - `picotool save -r 0x10000000 0x10100000 flash.bin` — dump 1 MB of flash to a file. We use this every time we want to inspect what the bootloader wrote.
  - `picotool reboot -u -f` — force a reboot into BOOTSEL/USB MSC mode without holding the button. This is your "I just bricked it, get me back" command.
  - `picotool load app.uf2` — flash a UF2.
  - `picotool verify app.uf2` — read back and compare.

### Pico SDK source — boot stage 2

- **Source:** Raspberry Pi Ltd., `raspberrypi/pico-sdk` on GitHub.
- **URL:** <https://github.com/raspberrypi/pico-sdk/tree/master/src/rp2_common/boot_stage2>
- **License:** BSD-3-Clause.
- **Files we read:**
  - `boot2_w25q080.S` — 200 lines of ARM assembly, the Pico's canonical Stage 1.
  - `pad_checksum` (Python) — the script that computes and embeds the CRC32 trailer.
  - `boot_stage2.ld` — the tiny linker script that places the 256 bytes at the correct offset.
- **What we use it for:** Lecture 1 walks the assembly line-by-line; the homework asks you to write `boot2.S` for a hypothetical alternate flash chip (Adesto AT25SF128A, datasheet linked below).

### Pico SDK source — flash API

- **URL:** <https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_flash/flash.c>
- **Files we read:** `flash.c` (~150 lines), `include/hardware/flash.h` (~80 lines).
- **What we use it for:** Reference implementation of `flash_range_erase` and `flash_range_program` that we partially re-implement in our bootloader (the bootloader cannot link the SDK because of size; we replicate the function bodies inline). Walk this on Wednesday before Exercise 2.

### Winbond W25Q16JV-IQ datasheet (the Pico's QSPI flash chip)

- **Source:** Winbond Electronics Corporation, "W25Q16JV-DTR (1V8) 16M-bit Serial Flash Memory" rev. D, August 2016.
- **URL:** <https://www.winbond.com/resource-files/W25Q16JV_DTR_RevD%2008292016.pdf>
- **Sections we use:**
  - §7.2.10, p. 32 — "Page Program (02h)". 256-byte program page.
  - §7.2.16, p. 35 — "Quad I/O Fast Read (EBh)". The XIP read instruction.
  - §7.2.18, p. 37 — "Sector Erase (20h)". 4 KB erase sector.
- **Cite as:** "Winbond W25Q16JV §X.Y, p. Z".

### Adesto AT25SF128A datasheet (the homework's alternate chip)

- **Source:** Adesto Technologies, "AT25SF128A 128-Mbit SPI Serial Flash Memory" rev. M, June 2018.
- **URL:** <https://www.dialog-semiconductor.com/sites/default/files/2020-09/AT25SF128A-DS.pdf>
- **What we use it for:** Homework problem 6 asks you to write `boot2_at25sf128a.S` based on the SDK's `boot2_w25q080.S` and the AT25SF128A's command set. The relevant section is §6 ("AT25SF128A Software Commands"), pp. 8–30.

---

## Tertiary references — context and depth

### USB 2.0 specification (the parent spec for DFU)

- **URL:** <https://www.usb.org/document-library/usb-20-specification>
- **What you cite:** §9 ("USB Device Framework"). You read this in Week 9. This week the relevant cross-reference is §9.4 (standard requests) because DFU's class-specific requests follow the same control-transfer envelope.

### MCUboot — the reference bootloader source

- **URL:** <https://github.com/mcu-tools/mcuboot>
- **Files worth opening:**
  - `boot/bootutil/src/loader.c` — the bootloader's top-level driver, ~1500 LOC.
  - `boot/bootutil/src/swap_move.c` — the swap-with-revert implementation.
  - `boot/bootutil/src/image_validate.c` — the signature/TLV validator.
- **What we use it for:** when your dual-bank implementation differs from MCUboot's, the comparison is instructive. Most divergences are about whether you want size + correctness + power-loss-safety (MCUboot) or simplicity + comprehensibility (this week's code).

### "Practical UNIX and Internet Security" — anti-bricking patterns

- **Source:** Garfinkel, Spafford, Schwartz, O'Reilly, 3rd ed., 2003.
- **Chapter we use:** Ch. 10, "Auditing, Logging, and Forensics" — not specific to bootloaders but the watchdog-confirmation pattern in our `application.c` is a transposition of the "audit before commit" pattern Garfinkel describes for filesystem checkpoints. The chapter is referenced in passing in Lecture 3.

### "Embedded Software Security: From Boot to Bricked"

- **Source:** Patrick Schaumont, *Practical Embedded Security Workshop materials*, Virginia Tech, 2018.
- **URL:** <https://schaumont.dyn.wpi.edu/ece4530s23/index.html> (the W&M / WPI lecture notes repository).
- **What we use it for:** the "anti-rollback" discussion in Lecture 3 cites Schaumont's slides on monotonic version counters in OTP. Free, slightly dated, still good.

### Beningo, *Reusable Firmware Development*

- **Source:** Jacob Beningo, Apress, 2017. ISBN 978-1-4842-3296-5.
- **Chapter we use:** Ch. 7, "Bootloader Design". A good vendor-neutral treatment of dual-bank schemes; predates MCUboot's documentation explosion and reads more like an engineering essay than a reference. Library copy; not free online.

---

## Free Apple/Adafruit/Sparkfun teaching materials

### Adafruit Learn — UF2 bootloader explainer

- **URL:** <https://learn.adafruit.com/introducing-the-adafruit-nrf52840-feather/uf2-bootloader-details>
- **What it covers:** Adafruit's nRF52840 UF2 bootloader from the user's perspective. The author (Limor Fried's team) explains the BOOTSEL-equivalent ("double-tap reset") on Adafruit boards and walks the UF2 drag-and-drop flow. 15 minutes.
- **Why we link it:** the most reader-friendly UF2 intro on the internet; useful before you read the spec.

### Hackaday — "Bootloaders and System Integrity" series

- **URL:** <https://hackaday.com/tag/bootloader/>
- **Article we link:** "How A Bootloader Works", 2017.
- **What it covers:** A pop-engineering explainer of the basic bootloader recipe (validate / copy / jump) across various chips. 10 minutes. Useful as a context-setter on Monday.

---

## Tooling — what to install

You should have these on your path by Tuesday evening:

- **Pico SDK** at v1.5.1 or later, with `PICO_SDK_PATH` set in your shell.
- **`picotool`** built from source or installed via Homebrew (`brew install picotool` on macOS).
- **Python 3.10+** with a virtualenv containing `pyserial>=3.5`, `cryptography>=41.0`, `intelhex>=2.3`, `pyusb>=1.2`.
- **`openssl`** 3.0+ for generating Ed25519 keys (`openssl genpkey -algorithm Ed25519 -out priv.pem`).
- **`dfu-util`** for Challenge 2 (`brew install dfu-util` on macOS; `apt install dfu-util` on Debian/Ubuntu).
- **Wireshark** with USBPcap (Windows) or `usbmon` (Linux). On macOS, the easiest path is to do the captures from a Linux VM with USB passthrough.

A `requirements.txt` in `mini-project/` pins the Python versions.

---

## Reading time budget

| Reference                                            | Time     | When             |
|------------------------------------------------------|----------|------------------|
| RP2040 datasheet §2.8                                | 60 min   | Monday morning   |
| UF2 README                                           | 15 min   | Monday afternoon |
| USB DFU 1.1 §6 (state machine)                       | 45 min   | Tuesday morning  |
| MCUboot design doc                                   | 45 min   | Wednesday morning |
| RFC 8032 §5.1 + §7.1                                 | 30 min   | Thursday morning |
| ed25519-donna README                                 | 10 min   | Thursday afternoon |
| Pico SDK boot2 source                                | 30 min   | Wednesday        |
| Total                                                | ~4 hours | spread across the week |

If you do the readings on the day they support, the lectures and exercises will track cleanly. If you defer all the reading to Friday, you will be writing the bootloader in the dark — measurably slower and dramatically more frustrating.
