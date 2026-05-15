# Challenge 1 — Brick and Recover

## Brief

Deliberately flash a deliberately-broken application onto the active bank, observe what the bootloader does, recover the device using the BOOTSEL physical button, and write a post-mortem document. The goal is not the bricking; the goal is the recovery story and the diagnostic trace.

You should spend ~2 hours on this challenge. The deliverable is a markdown document `BRICK-POSTMORTEM.md` in your repository fork.

## Setup

You have already completed:

- The Exercise 2 flash-write helper compiles and runs.
- You have read Lecture 1 and you can describe the three-stage boot sequence from memory.
- Your Pico is connected to USB, `picotool` is installed, and the BOOTSEL button is accessible.

You will need a known-good UF2 to recover with. Build any C1-week-1 or earlier program from the SDK examples and keep `blink.uf2` in your project directory.

## Procedure

### Phase 1 — Brick the chip

Build a "broken application" UF2 by following these steps:

1. Start from a working `blink` example.
2. Modify the reset handler so it immediately faults. The simplest reliable fault is dereferencing address 1 (which is unaligned and not in any valid memory region):

    ```c
    int main(void) {
        volatile uint32_t *crash = (volatile uint32_t *) 1u;
        *crash = 0xDEADBEEFu;
        return 0;
    }
    ```

3. Build with `cmake -B build && cmake --build build`.
4. Confirm the UF2 is built: `ls build/broken_app.uf2`.
5. Drop the UF2 onto the BOOTSEL drive on a Pico that is freshly plugged-in with BOOTSEL held. The chip will reboot, immediately fault inside `main`, and HardFault.

### Phase 2 — Observe the failure

The HardFault handler in a default Pico SDK build is an infinite loop. The chip is now "running" but doing nothing: USB will not enumerate (TinyUSB never got to start), GPIOs are not toggling, the LED is dark. From the host's perspective, the chip looks frozen.

Document the symptoms:

- What does `lsusb` (Linux) or `system_profiler SPUSBDataType` (macOS) or Device Manager (Windows) show?
- Does `picotool info` find the chip?
- Are any USB devices enumerated by the host?

Capture the output of each diagnostic in your post-mortem.

### Phase 3 — Recover the chip

Recover using the BOOTSEL physical button:

1. Disconnect the Pico from USB.
2. Hold the BOOTSEL button.
3. Reconnect the Pico to USB while still holding BOOTSEL.
4. Confirm the `RPI-RP2` MSC drive appears on your host.
5. Release BOOTSEL.
6. Drop the known-good `blink.uf2` onto the drive.
7. The chip reboots and runs blink.

Document this entire procedure in your post-mortem, with screenshots of the `RPI-RP2` drive on your file manager (Finder, Explorer, or `ls /Volumes/RPI-RP2/` output).

### Phase 4 — Identify the cause

In your post-mortem, answer:

1. **What exactly did the broken application do?** Walk the assembly of the broken `main`. Find the offending instruction (`str` to address 1). Cite the disassembly (`arm-none-eabi-objdump -d build/broken_app.elf | grep -A3 '<main>:'`).
2. **What did the CPU do in response?** Cite ARMv6-M Architecture Reference Manual §B1.5 (Exceptions); explain why an unaligned store to address 1 raises HardFault on Cortex-M0+ (the M0+ does not support unaligned access at all — datasheet §2.4.2, p. 17).
3. **Why did the Boot ROM not detect this and roll back?** The Boot ROM has no signature check on the application image (that is what your bootloader will add in the mini-project). The Boot ROM only CRCs the second-stage bootloader at flash[0..255]; the application at flash[256+] is trusted.
4. **What protected you from a permanent brick?** The Boot ROM's BOOTSEL recovery path is in mask ROM. The application's HardFault did not affect mask ROM. Holding BOOTSEL during power-on causes the Boot ROM to skip the application and enter USB MSC mode.

### Phase 5 — Predict the mini-project's behavior

In your post-mortem, predict what your week-10 mini-project's bootloader would do if you flashed the same broken application onto the active bank:

1. The bootloader runs first.
2. It checks signature on the active bank. If you signed the broken app with the right key, the signature verifies. The bootloader jumps to it.
3. The broken app crashes immediately.
4. Watchdog (which the broken app failed to initialize) does NOT reset the chip — no watchdog, no rollback.
5. Result: same brick as Phase 1.

Then explain how the watchdog-confirmation pattern in Lecture 3 would have prevented this:

1. New firmware boots; bootloader sets `state = BL_STATE_BOOTING_NEW`, `boot_attempts = 0`.
2. Application crashes before setting `boot_confirmed = 1`.
3. Watchdog (which the bootloader initialized with a 30-second timeout) fires.
4. Bootloader sees `state == BL_STATE_BOOTING_NEW && boot_confirmed == 0`, increments `boot_attempts`.
5. After 3 crashes, bootloader rolls back to staging (which holds the previous-good firmware).
6. Previous firmware boots; user is informed via LED blink pattern that a rollback occurred.

## Deliverable

`BRICK-POSTMORTEM.md` (1500–2500 words) in your repository. Required sections:

1. **Summary** — what happened, what the recovery was, how long it took.
2. **Symptoms** — exact diagnostic outputs from host tools.
3. **Root cause** — the assembly and the ARM exception model citation.
4. **Recovery** — step-by-step with timestamps.
5. **Anti-recurrence** — how your week-10 mini-project's watchdog-confirmation would have helped.
6. **References** — RP2040 datasheet sections, ARMv6-M sections, MCUboot docs cited inline.

Commit it to your fork with a message like `week-10/challenge-01: brick postmortem after deliberate HardFault`.

## Pass criteria

- The post-mortem document covers all six required sections.
- All host-side diagnostic outputs are captured verbatim (no paraphrasing of error messages).
- The assembly of the offending `main` is included in the document.
- The recovery procedure is reproducible (someone reading your post-mortem could follow the steps to recover their own bricked Pico).
- The anti-recurrence section correctly describes the watchdog-confirmation rollback logic from Lecture 3.

## Why this challenge matters

Every embedded developer bricks their first device. Many brick their tenth. The skill that separates senior firmware engineers from juniors is not "how do you avoid bricking" — you don't, completely — but "how fast can you diagnose and recover when it happens, and what did you learn that the next failure won't repeat."

Writing a brick post-mortem the moment after a recovery, while the diagnostic state is fresh, is a habit worth building now. The 2-hour cost of this challenge is the cheapest possible practice run for the real thing.

## References

- RP2040 datasheet §2.4 (Cortex-M0+ Processor), pp. 16–18.
- RP2040 datasheet §2.8.1 (Processor Controlled Boot Sequence), pp. 130–132.
- ARMv6-M Architecture Reference Manual §B1.5 (Exceptions), <https://developer.arm.com/documentation/ddi0419/latest/>.
- MCUboot design doc, "Image swap with rollback". <https://docs.mcuboot.com/design.html#image-swap-with-rollback>
