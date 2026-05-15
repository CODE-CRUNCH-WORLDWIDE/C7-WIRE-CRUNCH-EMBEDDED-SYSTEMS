# Challenge 2 — USB DFU Class Bring-up

## Brief

Implement a minimal USB DFU-class device on the RP2040 using TinyUSB. Capture the enumeration in Wireshark and walk the seven DFU requests. Use the `dfu-util` host tool to upload firmware via the DFU class. Compare the DFU experience to your custom OTA protocol from the mini-project.

You should spend ~3 hours on this challenge. The deliverable is the firmware source plus a markdown writeup `DFU-WRITEUP.md`.

## Why DFU and not our custom protocol

USB DFU 1.1 is a USB-IF standard (free at <https://www.usb.org/sites/default/files/DFU_1.1.pdf>) that defines a complete firmware-update protocol over USB without any device-specific code on the host. The host runs `dfu-util` (open-source, BSD-2-Clause, <http://dfu-util.sourceforge.net/>) and it works with any compliant DFU device.

Trade-offs:

| Property                          | Custom OTA (mini-project) | USB DFU 1.1            |
|-----------------------------------|---------------------------|------------------------|
| Host tool required                | `cc-flash.py` (we wrote)  | `dfu-util` (universal) |
| Cross-platform                    | Wherever Python runs      | Linux, macOS, Windows  |
| Driver install needed (Windows)   | No (CDC inbox)            | Yes (libusb-win32)     |
| Speed                             | ~5 KB/s (hex)             | ~50 KB/s (binary)      |
| Signature verification            | We did it                 | Not in spec, vendor-extension |
| Spec depth                        | ~1 page of ASCII rules    | 47 pages with state machine |
| Debugging from a terminal         | Yes (it's ASCII)          | No (binary control transfers) |

For a hobbyist or single-vendor product, the custom protocol is simpler. For a multi-product ecosystem or third-party serviceability, DFU is the right answer.

## What you need to build

A TinyUSB-based device firmware that:

1. Enumerates with class triple `(0xFE, 0x01, 0x01)` — run-time DFU.
2. Exposes one interface with the DFU functional descriptor (9 bytes per DFU 1.1 Table 4.2, p. 14).
3. Implements the `DFU_DETACH` request (request code 0). When received, the device sets a flag, returns to TinyUSB, allows the SETUP STATUS phase to complete, then issues a USB disconnect-reconnect after the host's bus reset.
4. After detach, re-enumerates in DFU mode with class triple `(0xFE, 0x02, 0x02)`.
5. In DFU mode, implements `DFU_DNLOAD` (request code 1), `DFU_GETSTATUS` (request code 3), `DFU_CLRSTATUS` (request code 4), `DFU_GETSTATE` (request code 5), and `DFU_ABORT` (request code 6). `DFU_UPLOAD` and the manifest-tolerant variants are out of scope.

TinyUSB has a DFU implementation (`tinyusb/src/class/dfu/dfu_device.c`, ~700 lines) that you can enable via `CFG_TUD_DFU = 1` and `CFG_TUD_DFU_RUNTIME = 1` in `tusb_config.h`. Use it; do not write the class from scratch.

## The DFU state machine

USB DFU 1.1 §6 (pp. 16–25) defines 10 states. For a download-only device, the reachable states are:

```text
appIDLE -> DETACH -> appDETACH -> [bus reset] -> dfuIDLE
                                                   |
                                                   DNLOAD
                                                   v
                                              dfuDNLOAD_SYNC
                                                   |
                                                   GETSTATUS
                                                   v
                                              dfuDNBUSY (during flash write)
                                                   |
                                                   v
                                              dfuDNLOAD_IDLE
                                                   |
                                                   DNLOAD (more blocks)
                                                   v
                                              [back to dfuDNLOAD_SYNC]
                                                   |
                                                   DNLOAD zero-length
                                                   v
                                              dfuMANIFEST_SYNC
                                                   |
                                                   GETSTATUS
                                                   v
                                              dfuMANIFEST -> dfuMANIFEST_WAIT_RESET
                                                                |
                                                                v
                                                           [USB reset]
                                                                |
                                                                v
                                                           appIDLE (new app boots)
```

Read §6 with this diagram in front of you. The transitions are dense.

## Reference TinyUSB skeleton

```c
#include "tusb.h"
#include "bsp/board.h"

/* tusb_config.h must have CFG_TUD_DFU and CFG_TUD_DFU_RUNTIME enabled. */

/* The functional descriptor. */
static const uint8_t desc_dfu[] = {
    /* Standard interface descriptor for the DFU interface in run-time mode. */
    9,                  /* bLength */
    TUSB_DESC_INTERFACE,/* bDescriptorType */
    0x00,               /* bInterfaceNumber (0 for our single-interface device) */
    0x00,               /* bAlternateSetting */
    0x00,               /* bNumEndpoints (DFU runs on endpoint zero) */
    0xFE,               /* bInterfaceClass = APP_SPECIFIC */
    0x01,               /* bInterfaceSubClass = DFU */
    0x01,               /* bInterfaceProtocol = run-time */
    0x00,               /* iInterface */

    /* DFU functional descriptor. */
    9,                  /* bLength */
    0x21,               /* bDescriptorType = DFU functional */
    0x0F,               /* bmAttributes: bitWillDetach | bitManifestationTolerant | bitCanDnload */
    0x00, 0xFF,         /* wDetachTimeOut (255 ms) */
    0x00, 0x01,         /* wTransferSize (256 bytes per DNLOAD) */
    0x10, 0x01,         /* bcdDFUVersion = 0x0110 */
};

/* Callback invoked when the host sends DFU_DETACH. */
void tud_dfu_runtime_reboot_to_dfu_cb(uint16_t ms) {
    /* TinyUSB requested a reboot into DFU mode in `ms` milliseconds.
       We can either re-enumerate with new descriptors (the "willDetach"
       path) or trigger a watchdog reset that brings us back as a different
       USB device. */

    /* Simple approach: watchdog reset into a fixed "DFU mode" build. */
    /* Set a flag in watchdog scratch so the next boot enumerates as DFU. */
    watchdog_hw->scratch[5] = 0xDFUC0DE5u;
    watchdog_enable(1, false);  /* 1 ms timeout, reset after 1 ms */
    for (;;) { __WFI(); }
}

/* Callback invoked when the host sends a DNLOAD chunk. */
uint32_t tud_dfu_get_status_cb(uint8_t alt, uint8_t state) {
    (void) alt;
    (void) state;
    /* Return the poll timeout in ms — how long should the host wait
       before re-querying status. 100 ms is a safe default for our
       flash-program latency. */
    return 100u;
}

void tud_dfu_download_cb(uint8_t alt, uint16_t block_num,
                         uint8_t const *data, uint16_t length) {
    (void) alt;
    /* Write `length` bytes of `data` to flash at offset `block_num * 256`.
       This is the part you implement against your bootloader's
       flash_safe_write helper. */
    flash_safe_write(STAGING_BANK_FLASH_OFFSET + (block_num * 256u),
                     data,
                     length);
}

void tud_dfu_manifest_cb(uint8_t alt) {
    (void) alt;
    /* The host has finished sending; we've written everything to flash.
       Set the swap-requested flag in metadata; on next reset, the
       bootloader picks up the new image. */
    metadata_set_swap_requested();
}
```

This is ~80 lines including the TinyUSB descriptor scaffolding. Full source target: ~250 lines.

## Procedure

### Phase 1 — Build the run-time DFU descriptor

Modify your Week 9 composite-device firmware: remove the CDC, HID, and MSC interfaces; add the DFU run-time interface. Keep the device VID/PID the same so your host remembers it.

Verify by running `dfu-util --list`:

```bash
$ dfu-util --list
Found Runtime: [2e8a:000a] ver=0100, devnum=42, cfg=1, intf=0, path="3-1", alt=0, name="UNKNOWN", serial="UNKNOWN"
```

`Found Runtime` means the host recognized your DFU functional descriptor. Half the battle.

### Phase 2 — Implement the detach

When `dfu-util -e -d 2e8a:000a` is run, it sends `DFU_DETACH`. Your callback `tud_dfu_runtime_reboot_to_dfu_cb` is invoked. Set a watchdog scratch, trigger a 1 ms reset.

On the next boot, your code reads the scratch and selects a different USB descriptor set — the DFU-mode descriptors with class triple `(0xFE, 0x02, 0x02)` and no other interfaces. Re-enumerate.

Verify:

```bash
$ dfu-util --list
Found DFU: [2e8a:000a] ver=0100, devnum=43, cfg=1, intf=0, alt=0, name="UNKNOWN", serial="UNKNOWN"
```

`Found DFU` means you re-enumerated in DFU mode. The transition was successful.

### Phase 3 — Receive a DNLOAD

Run `dfu-util -d 2e8a:000a -D test_firmware.bin -a 0`. The host:

1. Sends `DFU_DNLOAD` with the first 256 bytes of `test_firmware.bin` in the data phase.
2. Sends `DFU_GETSTATUS` and waits for the response (state = `dfuDNLOAD_IDLE`, poll timeout = 100 ms).
3. After 100 ms, sends the next `DFU_DNLOAD`.
4. Continues until all bytes are sent.
5. Sends a `DFU_DNLOAD` with `wLength = 0` (zero-length packet) — the "manifestation phase" signal.
6. Sends `DFU_GETSTATUS` and waits for state = `dfuMANIFEST_WAIT_RESET`.
7. Sends a USB bus reset.

Your device, in response:

- For each non-zero-length `DFU_DNLOAD`, writes the bytes to staging flash.
- For the zero-length `DFU_DNLOAD`, transitions to `dfuMANIFEST_SYNC` then `dfuMANIFEST` then `dfuMANIFEST_WAIT_RESET`.
- On bus reset, re-enumerates as the application (run-time mode).

### Phase 4 — Capture in Wireshark

While the upload is happening, run a USB capture. On Linux:

```bash
sudo modprobe usbmon
sudo wireshark -i usbmon3 -k -f 'usb.idVendor == 0x2e8a'
```

Identify each of the seven DFU requests in the capture. Save the capture as `dfu-capture.pcapng`.

### Phase 5 — Compare with the mini-project's protocol

In `DFU-WRITEUP.md`, write side-by-side comparison tables. Cover:

- Bytes-on-the-wire for a 256-byte firmware chunk in each protocol.
- Number of round trips per chunk.
- How errors are reported.
- How a partial transfer is recovered.
- How a power loss during transfer is handled.

## Deliverable

1. `firmware-dfu.c` (or however you split it) — the modified Week 9 firmware with DFU class.
2. `dfu-capture.pcapng` — the Wireshark capture of one successful upload.
3. `DFU-WRITEUP.md` (1500–2500 words) — the comparison and the seven-request walk.

## Pass criteria

- `dfu-util --list` shows your device in both run-time and DFU modes.
- A 4 KB test firmware uploads successfully via `dfu-util -D`.
- The Wireshark capture shows all seven requests with annotations.
- The writeup correctly identifies which DFU state your device is in for each request.
- The comparison table is accurate (you can verify the byte counts by counting in the capture).

## References

- USB DFU 1.1 spec. <https://www.usb.org/sites/default/files/DFU_1.1.pdf> — §3 (requests), §4 (descriptors), §6 (state machine).
- `dfu-util` documentation. <http://dfu-util.sourceforge.net/dfu-util.1.html>
- TinyUSB DFU class source. `tinyusb/src/class/dfu/dfu_device.c`.
- TinyUSB DFU example. `tinyusb/examples/device/dfu/`.
