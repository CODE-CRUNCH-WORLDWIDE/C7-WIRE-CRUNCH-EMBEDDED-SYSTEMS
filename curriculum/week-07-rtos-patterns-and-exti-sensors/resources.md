# Week 7 — Resources

Every reference here is **free and publicly accessible**. The FreeRTOS reference manual, the RP2040 datasheet, the MPU-6050 register map, the pico-sdk HTML docs, and the ARMv6-M ARM are all downloadable PDFs or browseable web pages without registration. Where a page number is cited, the document revision is noted; later revisions move tables but not concepts.

---

## Primary FreeRTOS references

- **FreeRTOS Kernel — official site.** Documentation hub. Start here on every question.
  <https://www.freertos.org/>
- **FreeRTOS Kernel sources.** MIT-licensed; clone alongside your project and read whenever the API doc is unclear.
  <https://github.com/FreeRTOS/FreeRTOS-Kernel>
  - `stream_buffer.c` — the source for Week 7's marquee primitive. ~1 200 lines. The SPSC reasoning is documented in the header comment.
  - `tasks.c` — `xTaskNotifyGive`, `ulTaskNotifyTake`, the direct-to-task notification implementation. Search for `pcTaskNotifyInternal`.
  - `queue.c` — queues, semaphores, and mutexes; the critical-section discipline that stream buffers skip.
  - `portable/GCC/ARM_CM0/port.c` — Cortex-M0+ port. `xPortPendSVHandler` (the context-switch trampoline) and `vPortYieldFromISR` (the pending-yield request from an ISR).
  - `portable/GCC/ARM_CM0/portmacro.h` — the macro layer where `configMAX_SYSCALL_INTERRUPT_PRIORITY` is consumed and where `portYIELD_FROM_ISR(x)` expands.

- **FreeRTOS API Reference** — every function, every parameter, the documented timing class.
  <https://www.freertos.org/a00106.html>
  - Stream buffers: <https://www.freertos.org/RTOS-stream-buffer-API.html>
    - `xStreamBufferCreate`, `xStreamBufferSend`, `xStreamBufferReceive`, `xStreamBufferSetTriggerLevel`, plus the `FromISR` variants.
  - Message buffers: <https://www.freertos.org/RTOS-message-buffer-API.html>
  - Task notifications: <https://www.freertos.org/RTOS-task-notifications.html>
    - `xTaskNotifyGive`, `ulTaskNotifyTake`, `xTaskNotify`, `xTaskNotifyWait`, plus `FromISR` variants and the four notification "actions" (`eNoAction`, `eSetBits`, `eIncrement`, `eSetValueWithOverwrite`, `eSetValueWithoutOverwrite`).
  - ISR API recap: <https://www.freertos.org/a00124.html>

- **"Mastering the FreeRTOS Real Time Kernel"** (Richard Barry, free PDF, ~400 pages). The canonical book. This week's chapters:
  - Chapter 5 — Stream and Message Buffers. End-to-end. Read before Lecture 2.
  - Chapter 6 — Interrupt Management. The deferred-interrupt pattern, `portYIELD_FROM_ISR`, the `*FromISR` discipline. Required for Lecture 1 and Exercise 1.
  - Chapter 7 — Resource Management. §7.6 (gatekeeper task) and §7.5 (priority-inheritance details). Required for Lecture 3 and the mini-project.
  - Chapter 9 — Task Notifications. Read end-to-end before Exercise 2.
  <https://www.freertos.org/Documentation/RTOS_book.html>

- **"FreeRTOS Reference Manual"** (Richard Barry, free PDF, ~350 pages). A–Z companion to the book.
  - §3 — Queue Management
  - §4 — Semaphore and Mutex Management
  - §5 — Stream and Message Buffer Management
  - §11 — Task Notifications
  <https://www.freertos.org/Documentation/RTOS_book.html>

- **`FreeRTOSConfig.h` reference** — every `config*` symbol. The Week 7 settings we change from the Week 6 defaults: `configUSE_STREAM_BUFFERS = 1`, `configUSE_MESSAGE_BUFFERS = 1`, `configUSE_TASK_NOTIFICATIONS = 1` (the latter is default-on, but we cite it). On Cortex-M0+ also `configMAX_SYSCALL_INTERRUPT_PRIORITY` (default 0xC0, kept).
  <https://www.freertos.org/a00110.html>

---

## Raspberry Pi Pico / RP2040 references

- **RP2040 Datasheet** (Raspberry Pi Ltd, ~640 pages, Sep-2024 revision). Free PDF; the silicon reference for this week.
  <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
  - §2.4 (Cortex-M0+), pp. 65–73 — the cores.
  - §2.4.5 (NVIC), pp. 99–104 — Nested Vectored Interrupt Controller. Two priority bits, four levels (0x00, 0x40, 0x80, 0xC0). FreeRTOS uses `PendSV` at 0xFF (the lowest possible — the architecture pins it there for the context-switch trampoline) and `SysTick` at 0xC0 by default on the Pico port. Every SDK-installed ISR is at 0xC0 unless you override it.
  - §2.4.7 (Power management — `WFI` and `WFE`), pp. 116–119 — the sleep states the idle hook engages. The RP2040 supports `WFI` (wake on next NVIC interrupt) and the deeper `dormant` mode; the idle hook uses `WFI`.
  - §2.16 (Single-cycle I/O — SIO), pp. 38–40 — the inter-core mailboxes. Not in scope for Week 7.
  - §2.19 (GPIO and pads), pp. 235–300.
  - §2.19.6 (GPIO IRQ), pp. 281–284 — the GPIO interrupt registers. `IO_BANK0.PROC0_INTE0..3` (interrupt enable), `IO_BANK0.PROC0_INTS0..3` (interrupt status — what the SDK's shared ISR reads), `IO_BANK0.INTR0..3` (raw interrupt status before mask). Each GPIO has 4 bits of state (rising edge, falling edge, level high, level low) packed into one nibble of these 32-bit registers.
  - §4.6 (TIMER), pp. 552–568 — the 64-bit 1 MHz timer. Useful as the high-resolution stopwatch for latency measurement; we read it in Exercise 1 to compute ISR-to-task wake time without a logic analyzer.

- **Raspberry Pi Pico C/C++ SDK documentation** (Raspberry Pi Ltd, browseable HTML).
  <https://www.raspberrypi.com/documentation/pico-sdk/>
  - `hardware_gpio` — `gpio_set_irq_enabled_with_callback`, `gpio_set_irq_enabled`, `gpio_acknowledge_irq`. The callback runs on core 0 in IRQ context at NVIC priority 0xC0 by default.
    <https://www.raspberrypi.com/documentation/pico-sdk/group__hardware__gpio.html>
  - `hardware_irq` — `irq_set_priority`, `irq_set_exclusive_handler`. The lower-level API if you want a GPIO ISR at a non-default priority.
    <https://www.raspberrypi.com/documentation/pico-sdk/group__hardware__irq.html>
  - `hardware_i2c` — `i2c_init`, `i2c_write_blocking`, `i2c_read_blocking`. Note: the `_blocking` variants spin-wait on the I²C peripheral; they are safe in a task context but *not* in an ISR. For ISR-context I²C you would use the DMA path covered in Week 8.
    <https://www.raspberrypi.com/documentation/pico-sdk/group__hardware__i2c.html>
  - `hardware_timer` — `time_us_32`, `time_us_64`. Reads the 1 MHz timer; safe from any context.
    <https://www.raspberrypi.com/documentation/pico-sdk/group__hardware__timer.html>

- **Raspberry Pi Pico SDK source** (BSD-3 licensed).
  <https://github.com/raspberrypi/pico-sdk>
  - `src/rp2_common/hardware_gpio/gpio.c` — the shared GPIO ISR dispatcher. ~150 lines. Read it before Exercise 1; the per-pin callback table and the priority handshake are documented inline.

- **Pico SDK + FreeRTOS-Kernel example.** Officially supported integration; the `CMakeLists.txt` patterns this week's mini-project follows.
  <https://github.com/raspberrypi/pico-examples/tree/master/freertos>

---

## ARM Cortex-M0+ references

- **ARMv6-M Architecture Reference Manual** (ARM DDI 0419D, ~480 pages). Free PDF; the architecture spec for the RP2040 cores.
  - §B3.4 (System Control Space) — the NVIC and the `SHPR2`/`SHPR3` registers FreeRTOS writes to set `SysTick` and `PendSV` priorities.
  - §B3.4.6 (Vector table) — vector offsets the NVIC uses to dispatch.
  - §B3.5 (System Control Block) — the `ICSR` register where you write `PENDSVSET` to request a `PendSV`.
  - §A6.7.74 (`WFI`) — the wait-for-interrupt instruction the idle hook uses.
  <https://developer.arm.com/documentation/ddi0419/latest/>

- **Cortex-M0+ Generic User Guide** (ARM DUI 0662B, ~110 pages). The programmer-friendly companion. The NVIC section is ~10 pages and clarifies the priority-bit layout (left-aligned within the byte) that confuses every embedded engineer once per career.
  <https://developer.arm.com/documentation/dui0662/latest/>

---

## MPU-6050 references

- **InvenSense MPU-6000 / MPU-6050 Register Map and Descriptions** (rev 4.2, 2013, ~46 pages). The full register reference.
  <https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf>
  - Page 8 — register list overview.
  - Page 11 — register 0x19 `SMPLRT_DIV`. Output data rate = 1 kHz / (1 + SMPLRT_DIV). We set SMPLRT_DIV = 4 for 200 Hz.
  - Page 13 — register 0x1A `CONFIG`. DLPF (digital low-pass filter) bits 0–2. We set DLPF_CFG = 3 (44 Hz accel, 42 Hz gyro bandwidth — appropriate for a 200 Hz sample rate).
  - Page 26 — register 0x37 `INT_PIN_CFG`. Bit 7 `INT_LEVEL` (0 = active-high), bit 6 `INT_OPEN` (0 = push-pull), bit 5 `LATCH_INT_EN` (0 = 50 µs pulse), bit 4 `INT_RD_CLEAR` (1 = clear on any read). We set this byte to 0x10 — active-high push-pull, 50 µs pulse, clear on any register read.
  - Page 27 — register 0x38 `INT_ENABLE`. Bit 0 `DATA_RDY_EN`. We set this to 0x01 — data-ready interrupt enabled.
  - Page 28 — register 0x3A `INT_STATUS`. Bit 0 `DATA_RDY_INT`. Read once per ISR to clear (with `INT_RD_CLEAR` in `INT_PIN_CFG`).
  - Page 28 onwards — registers 0x3B–0x48 the 14 sensor output bytes (accel X/Y/Z, temperature, gyro X/Y/Z). Each is 2 bytes big-endian. We read all 14 in one I²C burst.
  - Page 41 — register 0x6B `PWR_MGMT_1`. We set this to 0x01 — clock source = PLL with X-axis gyro reference (more stable than the internal 8 MHz oscillator).

- **InvenSense MPU-6000 / MPU-6050 Product Specification** (rev 3.4, 2013, ~52 pages). The datasheet (vs the register map). Used for the I²C timing, the absolute electrical limits, and the gyro noise spec.
  <https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf>

---

## Real-time scheduling theory

- **Sha, Rajkumar, Lehoczky — "Priority Inheritance Protocols: An Approach to Real-Time Synchronization"** (IEEE Transactions on Computers, Vol. 39, No. 9, September 1990, pp. 1175–1185). The paper FreeRTOS's mutex implements. Theorems 3 and 4 derive the priority-inversion bound we use in Lecture 3.
  Free via IEEE author preprint or institutional access; many CS-department mirrors carry the PDF. Search for the title; the most commonly available copy is the one hosted on the CMU SEI archive.

- **Liu and Layland — "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment"** (Journal of the ACM, Vol. 20, No. 1, January 1973). The rate-monotonic-priority paper. We cite the U-bound (Theorem 4: a set of N periodic tasks with periods equal to deadlines is schedulable under rate-monotonic if total utilization ≤ N · (2^(1/N) − 1)).
  Free via ACM Open. Search for the title.

- **Buttazzo — "Hard Real-Time Computing Systems: Predictable Scheduling Algorithms and Applications"** (Springer, 3rd ed. 2011, ~500 pages). The textbook treatment of rate-monotonic, EDF, priority-inheritance, and the priority-ceiling protocol. Chapter 7 is the priority-inheritance chapter and is the secondary source for Lecture 3.
  Not free, but most university libraries carry it. Self-paced cohorts may rely on the Sha 1990 paper alone.

---

## Tools and bench equipment

- **Saleae Logic 8** or compatible 24 MS/s 8-channel logic analyzer. The free Saleae Logic 2 software runs on macOS/Linux/Windows; the analog channels on the Logic 8 are useful for the power-profiling stretch goal but not required for the core mini-project.
  <https://www.saleae.com/downloads/>
- **OpenOCD** or **picoprobe + gdb** for SWD debugging. The pico-sdk's `pico_setup.sh` installs both. The Week 7 mini-project does not strictly require a debugger — `printf` via the gatekeeper task is enough — but you will be faster with one when chasing an ISR that does not fire.
- **Multimeter or INA219 breakout** for the power-profiling stretch goal. The INA219 has its own I²C bus (use `i2c0` since `i2c1` is the MPU-6050).
- **GY-521 MPU-6050 breakout board.** ~3 USD. Any vendor. The board has on-board 4.7 kΩ pull-ups on SDA/SCL and a 3.3 V LDO on VCC.

---

## Free supplementary lectures and videos

- **Miro Samek — "Practical UML Statecharts in C/C++" (free PDF, 2nd ed.).** Chapter 7 covers the active-object pattern, which is a generalization of the gatekeeper task. We do not adopt UML statecharts in C7, but Samek's argument for queues-everywhere-instead-of-mutexes-everywhere is the most thorough in the literature.
  <https://www.state-machine.com/psicc2>

- **EmbedXcode / Quantum Leaps videos on YouTube.** "Modern Embedded Systems Programming Course" — episodes 14–22 cover the RTOS abstractions we use this week. Episode 16 (interrupt handling) is the closest match to our Lecture 1.
  <https://www.youtube.com/playlist?list=PLPW8O6W-1chwyTzI3BHwBLbGQoPFxPAPM>

- **Memfault Interrupt blog — "FreeRTOS Notification Hooks"** (Tyler Hoffman, 2021). A practical write-up on `vApplicationStackOverflowHook`, `vApplicationMallocFailedHook`, `vApplicationTickHook`, and `vApplicationIdleHook`. The most concise treatment of the four hooks we enable this week.
  <https://interrupt.memfault.com/blog/freertos-hooks>

- **Memfault Interrupt blog — "A Practical Guide to FreeRTOS Tasks"** (François Baldassari, 2020). Companion reading for stack-budget reasoning.
  <https://interrupt.memfault.com/blog/freertos-tasks>

---

## Where to ask questions

- **FreeRTOS Community Forum.** Active, moderated, the API authors read it.
  <https://forums.freertos.org/>
- **Raspberry Pi Pico forum — `microcontrollers` board.** Pico-SDK-specific issues.
  <https://forums.raspberrypi.com/viewforum.php?f=145>
- **Stack Overflow tag `freertos`.** Good for narrow API questions; less good for design discussion.
  <https://stackoverflow.com/questions/tagged/freertos>

Cohort channel: post in `#c7-week-07` on Slack. Tag `@instructors-c7` for blocking issues. Include your `LATENCY.md` capture and the Saleae file when you ask about ISR latency — without those, the answers will be a guess.
