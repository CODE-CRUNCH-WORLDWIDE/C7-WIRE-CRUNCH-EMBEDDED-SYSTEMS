# Mini-Project — Signed Dual-Bank Bootloader with OTA over CDC

## Brief

Build a complete bootloader-plus-application-plus-host-tool system on the RP2040. The bootloader lives in the first 32 KB of flash; it validates Ed25519 signatures, manages a dual-bank update scheme, and rolls back on watchdog timeout. The application, running from the active bank, implements an OTA receiver over USB CDC. The host tool `cc-flash.py` packages a signed `.ccf` file and uploads it.

End-to-end demonstration: with the application running, you type `cc-flash.py app_v2.ccf` on your laptop. ~150 seconds later, the device is running `app_v2`. No BOOTSEL press required. If `app_v2` is broken, the bootloader rolls back to `app_v1` automatically after ~30 seconds of failed boot attempts.

Allocate 6 hours for this mini-project.

## Deliverables

In `mini-project/` directory of your fork:

- `bootloader.c` — the bootloader source (~1500 LOC).
- `application.c` — the sample application with the OTA receiver (~800 LOC).
- `usb_descriptors.c` and `usb_descriptors.h` — the application's USB descriptors (CDC-only).
- `bootloader_common.h` — shared header (the same file from `exercises/`).
- `ed25519-donna/` — the verify-only Ed25519 implementation.
- `sha256.c` and `sha256.h` — a small SHA-256 implementation.
- `public_key.h` — the bootloader's embedded public key.
- `memmap_bootloader.ld` and `memmap_app.ld` — the linker scripts.
- `CMakeLists.txt` — the build configuration, producing `bootloader.uf2` and `application.uf2` as separate artifacts.
- `cc-sign.py` — the host-side signer.
- `cc-flash.py` — the host-side uploader.
- `requirements.txt` — Python deps (`pyserial`, `cryptography`).
- `Makefile` — convenience wrapper for build + sign + flash.

## The architecture

```text
+----------------------+      +----------------------+
| Host laptop          |      | RP2040 Pico          |
|                      |      |                      |
| cc-flash.py          |      |  ----------------    |
|   reads .ccf file    | -USB-+->| Application   |   |
|   sends BEGIN        | CDC  |  |   - OTA recv  |   |
|   sends CHUNKs       |      |  |   - writes    |   |
|   sends END, REBOOT  |      |  |     staging   |   |
|                      |      |  |     bank      |   |
| cc-sign.py           |      |  |   - sets swap |   |
|   reads .bin         |      |  |     flag      |   |
|   computes SHA256    |      |  |   - reboots   |   |
|   signs Ed25519      |      |  ----------------    |
|   writes .ccf        |      |    | reset         |
+----------------------+      |    v                 |
                              |  -------------       |
                              | | Bootloader |       |
                              | |  - reads   |       |
                              | |    metadata|       |
                              | |  - verifies|       |
                              | |    staging |       |
                              | |  - copies  |       |
                              | |    staging |       |
                              | |    to      |       |
                              | |    active  |       |
                              | |  - jumps   |       |
                              | |    to app  |       |
                              |  -------------       |
                              +----------------------+
```

## Build prerequisites

You have:

- Pico SDK 1.5.1 or later, with `PICO_SDK_PATH` set.
- `arm-none-eabi-gcc` 10.3 or later.
- `picotool` 1.1.x.
- Python 3.10+ with `pyserial>=3.5`, `cryptography>=41.0`.
- `openssl` 3.0+.
- A Pico (W or non-W).

## Step-by-step build

### Step 1 — Generate the Ed25519 key pair

```bash
mkdir -p ~/.cc-flash-keys
chmod 700 ~/.cc-flash-keys

openssl genpkey -algorithm Ed25519 \
    -out ~/.cc-flash-keys/ed25519_private.pem

openssl pkey -in ~/.cc-flash-keys/ed25519_private.pem \
    -pubout -outform DER \
    | tail -c 32 > ~/.cc-flash-keys/ed25519_public.bin

chmod 400 ~/.cc-flash-keys/ed25519_private.pem
chmod 444 ~/.cc-flash-keys/ed25519_public.bin
```

Convert the public key to a C header:

```bash
xxd -i ~/.cc-flash-keys/ed25519_public.bin > mini-project/public_key.h
sed -i 's/unsigned char .*\[\] = {/const uint8_t bootloader_public_key[32] = {/' \
    mini-project/public_key.h
sed -i 's/unsigned int .*_len = .*;/const uint32_t bootloader_public_key_len = 32u;/' \
    mini-project/public_key.h
```

Inspect `public_key.h` to confirm it declares a 32-byte `bootloader_public_key` array.

### Step 2 — Build the bootloader

```bash
cd mini-project
mkdir -p build && cd build
cmake -G "Unix Makefiles" .. -DCC_TARGET=bootloader
make -j8 bootloader
```

Output: `bootloader.uf2` (~28 KB) and `bootloader.elf`. Verify size:

```bash
arm-none-eabi-size bootloader.elf
#   text    data     bss     dec     hex filename
#  28384     128    1024   29536    7360 bootloader.elf
```

The `text` size must be < 32 KB. If it exceeds, prune the Ed25519 implementation (e.g., disable SHA-512 if you have SHA-256 already, or drop unused curve points).

### Step 3 — Build the application

```bash
cmake -G "Unix Makefiles" .. -DCC_TARGET=application
make -j8 application
```

Output: `application.uf2` (~80 KB) and `application.elf`. The application contains TinyUSB CDC, the OTA receiver, and the metadata-write helpers.

### Step 4 — Sign the application

```bash
python3 ../cc-sign.py application.bin application.ccf
# Reads application.bin, computes SHA256, signs Ed25519,
# writes 48-byte header + firmware + 64-byte signature.

ls -la application.ccf
# -rw-r--r--  1 user  staff  82064 May 14 14:23 application.ccf
```

The `.ccf` file is `48 + fw_size + 64` bytes.

### Step 5 — Flash the bootloader and the application via BOOTSEL

Hold BOOTSEL, plug in USB, drop `bootloader.uf2` then drop the application's UF2 (built with `.uf2` extension targeting the active-bank address).

Two-stage flashing: the bootloader's UF2 writes to flash[0..32 KB]; the application's UF2 writes to flash[32 KB..application_size]. The Pico SDK's `elf2uf2` infers the target address from the linker script, so each UF2 lands at the correct offset automatically.

Confirm:

```bash
picotool info -a
# BINARY START                  10000100
# BINARY END                    100071E0  (bootloader)
# (next binary)
# BINARY START                  10008100
# BINARY END                    1001D440  (application)
```

### Step 6 — Run the application

Disconnect and reconnect (without BOOTSEL). The Pico boots:

1. Boot ROM jumps to flash[0] (Stage 1 bootloader).
2. Stage 1 enables XIP, jumps to flash[256] (our bootloader).
3. Our bootloader reads metadata, finds `state = BL_STATE_IDLE` (first boot), verifies the application's signature, jumps to flash[32 KB].
4. Application boots, initializes TinyUSB CDC, exposes `/dev/cu.usbmodem...`.

Connect a terminal:

```bash
screen /dev/cu.usbmodem* 115200
# (or: minicom -D /dev/cu.usbmodem*)
```

You should see periodic status messages from the application:

```text
[app] boot: state=IDLE seq=1 active=0x10008100 staging=0x100C4100
[app] confirmed boot
[app] OTA ready. Awaiting BEGIN.
```

### Step 7 — Upload a new version via cc-flash

Modify the application slightly (change the status message), rebuild, re-sign, then:

```bash
python3 cc-flash.py application_v2.ccf
# Connecting to /dev/cu.usbmodem...
# BEGIN sent (82064 bytes, sha=ab12cd...)
# Waiting for staging-bank erase (this takes ~8 seconds)...
# Erased. Uploading chunks:
#   uploaded 4096 / 82064 bytes (5.0%)
#   uploaded 8192 / 82064 bytes (10.0%)
#   ...
#   uploaded 82064 / 82064 bytes (100.0%)
# END sent
# REBOOT sent
# Device should be running new firmware in ~12 seconds.
```

The total upload time is ~150 seconds.

### Step 8 — Verify the swap

Reconnect with `screen` after the device reboots:

```text
[app] boot: state=BOOTING_NEW seq=42 active=0x10008100 staging=0x100C4100
[app] new version: 2.0.0 (was 1.0.0)
[app] confirmed boot
[app] OTA ready. Awaiting BEGIN.
```

The `state=BOOTING_NEW` on the first boot after upload, transitioning to `state=IDLE` after the watchdog-confirmation, is the diagnostic that confirms the swap worked.

## Pass criteria

- `bootloader.elf` text size is < 32 KB.
- `application.elf` text + rodata size is < 752 KB.
- The end-to-end OTA flow described in Step 7 succeeds without manual BOOTSEL intervention.
- A deliberately-broken `application_v3.bin` (one whose `main` HardFaults immediately) is uploaded; after 3 boot attempts (~90 seconds), the bootloader rolls back to v2 and the device resumes operation.
- Signature verification rejection: a `.ccf` file signed with a different private key (use `cc-sign --key /tmp/wrong.pem`) is rejected by the bootloader; the rejected image is not booted, and the device remains on the previous good firmware.
- The Wireshark capture of one upload run is committed to the repo with the writeup.

## Common bringup gotchas

1. **The `application.uf2` flashes over the bootloader.** If your application's linker script has `ORIGIN = 0x10000000`, the application's UF2 writes to flash[0], overwriting the second-stage bootloader (Stage 1) and your bootloader. Always use `memmap_app.ld` with `ORIGIN = 0x10008100`.
2. **The bootloader's vector table is at the wrong offset.** If you accidentally use `memmap_default.ld` for the bootloader, its vector table is at `0x10000100` (correct) but the linker also reserves the `boot2` slot at flash[0..255], so the linker output overlaps with the SDK's `boot_stage2` library. Cleanest fix: use `pico_set_binary_type(bootloader copy_to_ram)` *no* — that's wrong here; instead, use the explicit `memmap_bootloader.ld` script that has no `.boot2` section because the SDK provides it.
3. **The application calls `flash_safe_execute` and freezes.** TinyUSB's task may be running on core 1 (if you split that way) and trying to write flash while the USB stack reads it. Always stop the USB task before any flash write; or write only to the staging bank, never to active.
4. **The metadata write succeeds but the swap fails.** Always verify the metadata after writing — read back the page, recompute the CRC, confirm it matches.
5. **The Ed25519 verify is slow.** ~70 ms on the Pico at 125 MHz. If your bootloader's "verify and jump" sequence is too slow, the user perceives the device as taking forever to boot. 200 ms total bootloader time is acceptable; 2 seconds is not. Profile with a GPIO toggle at start and end of verify.

## Bench session structure

- **Saturday 9 AM – 11 AM** — build the bootloader, get it flashing via BOOTSEL.
- **Saturday 11 AM – 1 PM** — build the application, integrate the OTA receiver, get the CDC port talking.
- **Saturday 2 PM – 4 PM** — implement the swap logic in the bootloader, verify with manual metadata writes.
- **Saturday 4 PM – 6 PM** — end-to-end OTA upload, debug the protocol on Wireshark.
- **Sunday morning** — write the README addendum with your timing measurements, commit, push.

## References

- All of Week 10's lecture notes.
- RP2040 datasheet §2.8 (Bootrom).
- USB DFU 1.1 (for protocol comparison; not implemented here).
- ed25519-donna.
- MCUboot design doc.
