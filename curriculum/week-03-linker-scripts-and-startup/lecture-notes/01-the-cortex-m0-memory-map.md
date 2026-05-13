# Lecture 1 — The Cortex-M0+ Memory Map

> **Outcome:** You can draw, from memory, the fixed 4 GiB ARMv6-M memory partition and the RP2040-specific addresses that fill it. You can answer, in one sentence each, where `.text` lives, where `.data` lives at runtime vs in the binary image, and why a write to `0xd000_001c` takes one cycle while a write to `0x4001_407c` takes three.

---

## 1. Why a memory map is the first thing you draw

Every embedded bring-up starts with the same picture: a vertical bar with addresses on the left, and labels naming what is at each address on the right. The picture is sometimes called the "memory map," sometimes the "address map," sometimes the "system map." Whatever the name, it is the load-bearing artifact. You cannot write a linker script without it. You cannot debug a HardFault without it. You cannot understand why `*(volatile uint32_t *)0x4001_4000 = 0xdeadbeef;` either works, silently does nothing, or locks the peripheral until reset — unless you have the map.

The map on a Cortex-M-class MCU has two layers. The **architectural** layer is set by ARM and is identical across every M0/M0+/M3/M4/M7 part ever shipped: a fixed 4 GiB address space partitioned into six broad regions. The **silicon** layer is set by the chip vendor and fills those regions with specific peripherals and memories. The architectural layer tells you what *kind* of memory lives at an address (executable? device-typed? cacheable? ordered?); the silicon layer tells you what *specific* peripheral or memory bank you are talking to.

This lecture covers both layers. By the end you will be able to draw the RP2040's map from memory. By the end of Lecture 2 you will be using it to write a linker script.

---

## 2. The architectural map — ARMv6-M

Open the ARMv6-M Architecture Reference Manual (DDI 0419E), §B3 (System address map), pp. B3-211 to B3-216. The first table you see is this one:

```
   0x0000_0000  ┌─────────────────────────┐
                │       Code              │  512 MiB  Normal memory, executable
   0x2000_0000  ├─────────────────────────┤
                │       SRAM              │  512 MiB  Normal memory, executable
   0x4000_0000  ├─────────────────────────┤
                │       Peripheral        │  512 MiB  Device memory
   0x6000_0000  ├─────────────────────────┤
                │   External RAM (RAM)    │  1 GiB    Normal memory, executable
   0xa000_0000  ├─────────────────────────┤
                │   External device       │  1 GiB    Device memory
   0xe000_0000  ├─────────────────────────┤
                │       System            │  512 MiB  Strongly-ordered / Device
   0xffff_ffff  └─────────────────────────┘
```

Six regions. Each is a power-of-two size. Each has a default **memory type** assigned by the architecture — Normal, Device, or Strongly-Ordered. The memory type determines how the processor's load-store unit treats accesses to that region:

- **Normal** memory: caches are allowed, accesses can be merged (two adjacent `STR` instructions can become one wider store), accesses can be reordered by the bus matrix, speculative reads are permitted. This is the regime you want for code and data — fast, predictable.
- **Device** memory: caches are *not* allowed, accesses cannot be merged, accesses can be reordered relative to *Normal* accesses but not relative to other *Device* accesses to the same peripheral. This is the regime you want for memory-mapped I/O — every `STR` to a peripheral register hits the silicon exactly once, in program order.
- **Strongly-Ordered** memory: caches not allowed, no merging, no reordering at all. Used only for the System region (NVIC, SCB, SysTick). The cost is slow accesses; the benefit is predictability for interrupt configuration.

The Cortex-M0+ has no MPU on the RP2040 (the MPU is optional on M0+ and Raspberry Pi did not implement it). It does not have a data cache. So the memory-type distinctions matter less on the M0+ than they do on an M7 — but they still govern access ordering and the validity of the `volatile` qualifier on your peripheral pointers.

From DUI 0662B §2.2.2 (Memory regions, types, and attributes), p. 2-8:

> The Cortex-M0+ processor has a fixed default memory map that defines memory types for each of the regions. ... Software cannot change the memory map.

That is the whole story of architectural memory layout on M0+. The vendor cannot move the SRAM region. You cannot move the SRAM region. The compiler cannot move the SRAM region. The only thing anyone — vendor, you, compiler — can do is decide *what to put inside the region at what offset*. That decision is the linker script.

---

## 3. Why the architectural regions look the way they do

The six regions are not arbitrary. They reflect three architectural decisions that have been carried through every ARMv6/v7/v8-M part ever shipped:

**Decision 1: Code and data are in different regions so the bus matrix can run them in parallel.** The Cortex-M0+ has a single-cycle Harvard-ish bus split: instruction fetches go to the `ICode` bus, data accesses go to the `DCode`/`System` bus. If your code is in flash at `0x1000_0000` and your data is in SRAM at `0x2000_0000`, the processor can fetch the next instruction *and* load a global variable in the same cycle. If both lived in flash, every load would stall the fetcher.

**Decision 2: Peripherals are in a region that the architecture marks Device.** This is the entire reason `volatile` is mandatory on peripheral pointers in C. Without `volatile`, the compiler is allowed to assume that two reads of the same address return the same value (it is Normal memory, so caching is fine) — but a peripheral register may change underneath you (a UART RX register, for example, gives you a different byte each read). The `volatile` qualifier tells the compiler: "do not optimize across this access, do not cache its value, do not merge it with the next one." The Device memory type in the architecture is the hardware-level enforcement of the same rule.

**Decision 3: The NVIC and SCB live in the System region, accessed only by the processor itself.** The System region at `0xe000_0000`–`0xffff_ffff` is, on every ARMv6-M part, hardwired to the processor's internal Private Peripheral Bus (PPB). The bus matrix does not route external accesses there; only the core can read or write it. This is why you cannot DMA-write to the NVIC and why a stray pointer that escapes into `0xe000_0000` will produce a BusFault on M3/M4 (or a HardFault on M0+) instead of corrupting an NVIC register.

You will not find these three decisions stated this way in DUI 0662B. ARM expects you to deduce them. But every linker script and every startup file you ever write encodes them, whether you know it or not.

---

## 4. The RP2040 silicon map

Now the silicon layer. Open the RP2040 datasheet, §2.2 (Bus Fabric — Memory Map), p. 23 of the Sep-2024 rev. The table on p. 23 is the same vertical bar, but filled in:

```
   0x0000_0000  ┌─────────────────────────┐
                │   ROM (16 KiB)          │  Read-only, executable. Boot ROM.
   0x0000_4000  ├─────────────────────────┤
                │   (reserved)            │
   0x1000_0000  ├─────────────────────────┤
                │   XIP flash             │  Read-only, executable. 16 MiB window,
                │   (Pi Pico W: 2 MiB)    │  W25Q080 NOR flash on the W variant.
   0x1100_0000  ├─────────────────────────┤
                │   XIP_NOALLOC           │  Same flash, no cache allocation.
   0x1300_0000  ├─────────────────────────┤
                │   XIP_NOCACHE           │  Same flash, bypass cache (slower).
   0x1500_0000  ├─────────────────────────┤
                │   XIP_NOCACHE_NOALLOC   │  Same flash, both bypasses.
   0x1800_0000  ├─────────────────────────┤
                │   XIP control registers │  Cache control, QSPI configuration.
                │     0x1400_0000         │
                ├─────────────────────────┤
                │   (reserved)            │
   0x2000_0000  ├─────────────────────────┤
                │   SRAM0–SRAM3 striped   │  256 KiB. Striped across 4 banks
                │   (256 KiB total)       │  for parallel bus access.
   0x2004_0000  ├─────────────────────────┤
                │   SRAM4 (4 KiB)         │  Core 0 stack scratchpad.
   0x2004_1000  ├─────────────────────────┤
                │   SRAM5 (4 KiB)         │  Core 1 stack scratchpad.
   0x2004_2000  ├─────────────────────────┤
                │   (reserved)            │
   0x4000_0000  ├─────────────────────────┤
                │   APB peripherals       │  Slower peripherals: SYSINFO, SYSCFG,
                │                         │  CLOCKS, RESETS, PSM, IO_BANK0,
                │                         │  PADS_BANK0, XOSC, PLL_SYS, BUSCTRL,
                │                         │  UART0/1, SPI0/1, I2C0/1, ADC, PWM,
                │                         │  TIMER, WATCHDOG, RTC, ROSC, …
   0x5000_0000  ├─────────────────────────┤
                │   AHB-Lite peripherals  │  Higher-bandwidth: DMA, USB, PIO0/1.
   0x6000_0000  ├─────────────────────────┤
                │   IOPORT (legacy)       │  Reserved / debug.
                │                         │
   0xd000_0000  ├─────────────────────────┤
                │   SIO (single-cycle I/O)│  Core-local GPIO, mailbox, FIFO,
                │                         │  spinlocks, integer divider.
   0xd000_1000  ├─────────────────────────┤
                │   (reserved)            │
   0xe000_0000  ├─────────────────────────┤
                │   PPB (Cortex-M0+ SCS)  │  NVIC at 0xe000_e100,
                │                         │  SCB at  0xe000_ed00,
                │                         │  SysTick at 0xe000_e010.
   0xe010_0000  └─────────────────────────┘
```

Eight observations every C7 student should be able to recite:

- **ROM is at `0x0000_0000`, not at `0x1000_0000` where flash lives.** The boot ROM (16 KiB, mask-programmed at the foundry) is the first thing the CPU executes on power-up. It implements the BOOTSEL+UF2 protocol, the USB MSC interface that shows up as `RPI-RP2`, the verification of the second-stage bootloader's CRC, and the jump into `boot2` at `0x1000_0000`.
- **Flash is mapped at `0x1000_0000`, not where you might expect from looking at an STM32.** This is unusual. On most Cortex-M parts, flash is at `0x0800_0000` (STM32) or `0x0000_0000` (NXP / Atmel / TI / nRF52). The RP2040 puts the boot ROM at `0x0000_0000` because the chip has *no internal flash*; flash is a separate QSPI chip on the board, and the boot ROM has to configure it before XIP works. Datasheet §2.6.1, p. 99.
- **Flash appears four times** — at `0x1000_0000`, `0x1100_0000`, `0x1300_0000`, `0x1500_0000` — each window with different cache behavior. The same 2 MiB of physical flash is visible four times. Your linker script will use only `0x1000_0000` for normal code; the other three aliases exist for DMA-driven flash reads where cache pollution is unwanted.
- **SRAM is striped across four 64 KiB banks.** Bytes 0..3 live in SRAM0; bytes 4..7 in SRAM1; bytes 8..11 in SRAM2; bytes 12..15 in SRAM3; then bytes 16..19 back in SRAM0. This is for bandwidth — four bus masters can read four different addresses in the same cycle if they happen to land in four different banks. From the linker-script's perspective, the 256 KiB at `0x2000_0000` is one contiguous region; the striping is invisible to software.
- **SRAM4 and SRAM5 are *not* part of the striped 256 KiB.** They are 4 KiB scratchpads, one per core, intended for stack space that is guaranteed not to bank-conflict with the other core. The Pi Pico SDK does not use them by default. You will use them in Week 8 (multicore).
- **The APB peripheral region starts at `0x4000_0000`** with `SYSINFO` at the very bottom (read `0x4000_0000` to get the chip ID — useful as a sanity check that flash is not at offset 0, since you would read your own code instead). UART0 is at `0x4003_4000`; you wrote to it in Week 2.
- **SIO at `0xd000_0000` is in a region the architecture marks "External Device" — but it is not external.** The RP2040 places the SIO at a single-cycle CPU-local address because the bus matrix routes accesses to `0xd000_0000` directly to the processor's per-core SIO interface, not through the main AHB-Lite fabric. This is why `STR` to `0xd000_001c` is one cycle and `STR` to `0x4001_4000` is three cycles. The choice of `0xd000_0000` was driven by which architectural region had the right memory-type defaults (Device, no caching, no merging) and was otherwise unused on the RP2040.
- **PPB at `0xe000_0000` is the Cortex-M0+ System Control Space.** It contains the NVIC (`0xe000_e100`), the SCB including `VTOR` (`0xe000_ed00`), and SysTick (`0xe000_e010`). All three are ARM-defined, not RP2040-specific. Refer to DUI 0662B for the register details; the addresses are identical on every Cortex-M0+ part.

---

## 5. Flash vs SRAM — the practical split

For our purposes this week, the map collapses to four addresses you need to memorize:

| Region | Base | Length | What lives here |
|--------|------|------:|-----------------|
| **ROM** | `0x0000_0000` | 16 KiB | Boot ROM (mask-programmed; you do not write to this) |
| **XIP flash** | `0x1000_0000` | 2 MiB on Pi Pico W | Your `.text`, your `.rodata`, your `boot2`, the LMA of your `.data` |
| **SRAM** | `0x2000_0000` | 256 KiB | Your `.data` (at runtime), your `.bss`, your stack, optionally your heap |
| **Peripherals** | `0x4000_0000` to `0xe010_0000` | depends | Every register you have written to in Week 2, and many more |

The dividing line for the linker is **flash vs SRAM**. Anything that is read-only and "born" with the firmware (code, string literals, the vector table, `const` globals, the LMA of `.data`) goes in flash. Anything that the program may write to (initialized globals, uninitialized globals, the stack, the heap) goes in SRAM. The reset handler's job is to copy the `.data` section from its LMA in flash to its VMA in SRAM before `main()` runs.

This split is not negotiable. Writing to flash takes ~1 ms per page and requires programming the QSPI controller; it is not a single `STR`. So globals cannot live in flash if the program writes to them. Conversely, SRAM is volatile — it loses its contents on power-cycle — so the initial values of `.data` cannot live there; they must live in flash and be copied at boot. That copy is the whole reason a startup file exists.

---

## 6. The boot path on the RP2040

When you press the reset button on the Pi Pico W, the chain that runs before `main()` is:

1. **`0x0000_0000`**: The boot ROM starts. It is the only code running. It checks the `BOOTSEL` pin: if held low (the white button), the ROM stays here, enumerates as `RPI-RP2` USB mass storage, and waits for a `.uf2` drag. If not held low, the ROM continues to step 2. Datasheet §2.8.1, p. 130.
2. **Read the first 256 bytes of flash**: The ROM reads `[0x1000_0000, 0x1000_0100)` into SRAM at `0x2004_1f00`. This is `boot2`, the second-stage bootloader. The last 4 bytes are a CRC over the first 252 bytes. The ROM verifies the CRC.
3. **CRC fail**: Drop into BOOTSEL mode permanently, indicate via LED if possible. The board is recoverable; just drag a known-good `.uf2`.
4. **CRC pass**: Jump into the copied `boot2` at `0x2004_1f00`. `boot2` configures the QSPI flash chip for continuous-read XIP mode (so the rest of the firmware can run from `0x1000_0100` directly via memory-mapped reads), then returns by `bx lr` to the address `0x1000_0100 + 1` (thumb bit set).
5. **`0x1000_0100`**: Your vector table starts here. The Cortex-M0+ does **not** re-fetch the SP and PC from the vector table on a `bx lr` from boot2 — `boot2` is just a normal function call from the ROM's perspective, and `lr` points wherever the ROM set it. The convention is that `boot2` returns to your `Reset_Handler`, which is at the address stored in vector table word 1. So `boot2` ends with the instruction sequence "load PC with `[__isr_vector + 4]`" — that is, your reset handler.

   *Important nuance:* the boot ROM does, in fact, load the initial SP from word 0 of the vector table and the initial PC from word 1 — but it does this *before* calling `boot2`, not after. So when your `Reset_Handler` runs, `SP` is already set to `_estack`. We will revisit this in Lecture 3.
6. **`Reset_Handler`**: Now you are running C. The reset handler copies `.data` from LMA to VMA, zeroes `.bss`, calls `__libc_init_array` (if linked), and calls `main()`.
7. **`main()`**: Your code.

The entire chain from power-up to your `main()` takes ~50 ms on a stock Pi Pico W, dominated by the QSPI XIP setup in `boot2` and by the `clk_peri` PLL lock if you bring it up in `main`. Datasheet §2.8.1.5, p. 134, gives the breakdown.

---

## 7. Comparison: STM32F4 (RM0090)

The STM32F407 reference manual RM0090 (ST Microelectronics, 1751 pages) shows the same architectural map filled in differently. Some highlights, for comparison:

| Region | RP2040 | STM32F407 (RM0090 §2.3.1, p. 51) |
|--------|--------|----------------------------------|
| Boot ROM | `0x0000_0000`, 16 KiB | `0x1FFF_0000`, 30 KiB (system memory, holds DFU/USART boot loader) |
| Flash | `0x1000_0000`, 2 MiB (external QSPI) | `0x0800_0000`, 1 MiB (internal NOR) |
| SRAM | `0x2000_0000`, 256 KiB (striped) | `0x2000_0000`, 192 KiB (SRAM1 112K + SRAM2 16K + CCM 64K at `0x1000_0000`) |
| APB peripherals | `0x4000_0000` | `0x4000_0000` (APB1) + `0x4001_0000` (APB2) |
| AHB peripherals | `0x5000_0000` | `0x4002_0000` (AHB1) + `0x5000_0000` (AHB2) |
| GPIO | via SIO at `0xd000_0000` + IO_BANK0 | GPIOA–GPIOK at `0x4002_0000`–`0x4002_2c00` |
| NVIC, SCB | `0xe000_e000` | `0xe000_e000` (architectural, identical) |

The architectural skeleton is identical — same six regions, same NVIC/SCB addresses, same `VTOR` register. The silicon fill is different. This is why a linker script for the STM32F4 (e.g. `STM32F407VGTx_FLASH.ld`) is *recognizably the same shape* as a linker script for the RP2040: a `MEMORY` block with `FLASH` and `RAM` regions, output sections `.text`/`.data`/`.bss`, the vector table at the start of `.text`. The numbers differ. The structure does not.

This portability is the whole reason ARM dictates the architectural map. A senior firmware engineer who has written a linker script for one Cortex-M part can write one for any other Cortex-M part in 30 minutes, given the part's specific memory regions. By Sunday, that engineer is you.

---

## 8. The CCM problem — and why the RP2040 dodges it

A famous footgun on the STM32F407: it has 64 KiB of "Core-Coupled Memory" (CCM) SRAM at `0x1000_0000`. CCM is fast, single-cycle from the CPU — but it is **not** accessible by DMA, USB, or Ethernet. If you put a buffer in CCM and pass its address to a DMA descriptor, the DMA controller cannot reach it and you get either zero-filled data or a BusFault, depending on the silicon revision. The first time most STM32 developers hit this, they lose two days.

The RP2040 has no CCM. All 256 KiB of SRAM is accessible by every bus master (both cores, DMA, USB, the PIO blocks). The striping across SRAM0–SRAM3 is purely for bandwidth; there is no semantic difference between an address in SRAM0 and an address in SRAM2.

This is a real ergonomic win for the RP2040. It also means your linker script does not need a `CCM` region. Compare:

```
   /* STM32F407 — three SRAM regions */
   MEMORY {
       FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
       RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 112K    /* SRAM1 */
       SRAM2 (xrw) : ORIGIN = 0x2001C000, LENGTH = 16K
       CCM   (xrw) : ORIGIN = 0x10000000, LENGTH = 64K     /* DMA cannot reach */
   }

   /* RP2040 — one flash, one RAM */
   MEMORY {
       FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
       RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
   }
```

The RP2040 script is half the length and has no DMA-reachability traps. We will write it in Lecture 2.

---

## 9. What you should be able to do after this lecture

- Draw the ARMv6-M architectural map (six regions, address boundaries, memory types) from memory.
- Annotate each architectural region with what the RP2040 silicon puts there.
- Recite the four key addresses for this week: `0x0000_0000` (ROM), `0x1000_0000` (flash), `0x2000_0000` (SRAM), `0xe000_e000` (PPB).
- Explain in one sentence why `boot2` is 256 bytes and lives at `0x1000_0000`–`0x1000_0100`.
- Explain in one sentence why your `.data` LMA is in flash but its VMA is in SRAM.
- Identify the difference between Normal and Device memory types and when each one applies on the RP2040.

If you can do all six, you are ready for Lecture 2. If you cannot do at least the first four, re-read this lecture, do Problem 1 from the homework, and come back.

---

## 10. Reading list for tomorrow

Before Lecture 2, read:

- RP2040 datasheet §2.2 (Bus Fabric — Memory Map), pp. 23–24. The single table on p. 23 is the load-bearing artifact.
- RP2040 datasheet §2.8 (Boot Sequence), pp. 130–145. Skim; we will return to specific subsections in Lecture 3.
- ARM Cortex-M0+ Generic User Guide §2.2 (Memory model), pp. 2-7 to 2-12.
- GNU `ld` documentation §3.4 (MEMORY command). The whole subsection is ~3 pages.

Bring questions to Tuesday studio.

---

## 11. Common questions

**"Why does the boot ROM live at `0x0000_0000` and not at `0x1000_0000` with the flash?"** Because the Cortex-M0+ hardware reset vector is at `0x0000_0000`: when the chip comes out of reset, the processor loads SP from `[0x0000_0000]` and PC from `[0x0000_0004]`. The boot ROM has to be there. The RP2040 has no internal flash, so flash is in a separate window starting at `0x1000_0000`, and the ROM's job is to bridge between them: read `boot2` from flash, verify it, jump into it.

**"What happens if I write to `0x0000_0000`?"** Nothing visible — the boot ROM is mask-programmed and not writable. The write is silently discarded by the bus matrix. On most Cortex-M parts a write to ROM raises a BusFault; on M0+ with no BusFault distinction it would raise HardFault — but RP2040's bus matrix treats ROM as silently-write-ignored. You can confirm this with a quick `*(volatile uint32_t *)0 = 0xdeadbeef;` (just don't expect anything to happen).

**"Can I run code from SRAM?"** Yes. SRAM at `0x2000_0000` is executable. Many real-time hot paths copy themselves to SRAM at boot to avoid the XIP cache miss latency on the first call. Challenge 1 this week loads a blink firmware into SRAM and runs it from there, with no flash at all.

**"Can I write to flash from `main()`?"** Yes, but only through the QSPI controller via the `flash_range_program` ROM function, not by writing to the XIP window. The XIP window is read-only. The flash chip is a separate die on the board (W25Q080 on the Pi Pico W) connected over QSPI. Datasheet §2.6.3.1, p. 102. We will not write to flash this week; that is a Week 9 (Bootloaders) topic.

**"Is the on-board LED at `WL_GPIO0` reachable from a linker-script perspective?"** The LED is behind the CYW43439, not directly on a GPIO pin. From the linker's perspective there is nothing special about it — the firmware that drives the CYW43 lives in `.text` like any other code. But you cannot `SIO_GPIO_OUT_XOR = ...` your way to the LED; you have to talk to the CYW43 over its own SPI bus. We will use GP15 (a real GPIO) as our blink pin for the bare-metal work this week, exactly as we did in Week 2.

**"What is the difference between `0x1000_0000` and `0x1100_0000` if they are both flash?"** They are the same physical 2 MiB of flash chip, viewed through different XIP cache policies. `0x1000_0000` is "XIP with cache, allocate on read" — the default. `0x1100_0000` is "XIP with cache, do not allocate on read" — used when you are doing a one-shot DMA from flash and do not want to pollute the cache. The other two aliases bypass the cache entirely (slower, but predictable timing). For your `.text` you always want `0x1000_0000`.

---

Onward to Lecture 2, where we make the linker actually place sections at the right addresses.
