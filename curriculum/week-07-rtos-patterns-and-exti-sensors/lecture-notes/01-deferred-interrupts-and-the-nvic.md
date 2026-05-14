# Lecture 1 — Deferred Interrupts and the NVIC on Cortex-M0+

> *An ISR is rented time. The silicon hands you the CPU and starts a stopwatch; everything you do inside the handler is time another interrupt is waiting. The deferred-interrupt pattern is the only honest way to spend that time: read the source, post a token, return. The slow part runs as a task.*

## The contract of an ISR

When a peripheral asserts an interrupt line, the Cortex-M0+ NVIC does six things in hardware, in order, in approximately 16 cycles (RP2040 datasheet §2.4.5, pp. 99–104; ARMv6-M ARM §B3.4):

1. Pushes the current register file's caller-saved subset (`r0`–`r3`, `r12`, `lr`, `pc`, `xPSR`) onto the *interrupted task's* stack. Eight 32-bit words. This is the "exception stack frame".
2. Loads `lr` with a magic value called the `EXC_RETURN` code — 0xFFFFFFF9 if returning to thread-mode using MSP, 0xFFFFFFFD if returning to thread-mode using PSP. FreeRTOS tasks run on PSP; the kernel runs on MSP.
3. Sets the `IPSR` (Interrupt Program Status Register) field of the `xPSR` to the exception number of the interrupt being serviced.
4. Sets the privilege mode to handler mode.
5. Reads the vector table at address 0 + (4 × exception number) and loads it into `pc`.
6. Starts executing the handler.

When the handler executes `bx lr` (where `lr` still holds the `EXC_RETURN` code from step 2), the hardware does the reverse: pops the exception stack frame from the task's stack, restores `xPSR`, switches back to thread mode, and resumes the interrupted instruction.

This is 16 cycles in, 16 cycles out. At 125 MHz, ≈ 130 ns total NVIC overhead, before your handler body has executed a single instruction.

Your handler body is on the clock. Two consequences follow.

First: **no blocking calls in an ISR**. If your handler calls `vTaskDelay`, `xSemaphoreTake(handle, portMAX_DELAY)`, or `xQueueSend(q, &item, portMAX_DELAY)`, the kernel will trip `configASSERT` (if you have it enabled), or — worse — silently corrupt its task lists (if you do not). The FreeRTOS API divides into two halves: task-context functions (no suffix, may block) and ISR-context functions (`*FromISR` suffix, never block). Crossing the line is undefined behaviour.

Second: **keep the handler short**. On Cortex-M0+, there is no nested-priority preemption. The NVIC supports "tail-chaining" — if a second interrupt is pending when the first one returns, the hardware skips the stack-pop-and-repush dance and dispatches the second directly — but a higher-priority interrupt cannot preempt a currently-running ISR at the same priority level. (It *can* preempt a lower-priority ISR, but on Cortex-M0+ with only two priority bits and four levels, you rarely have the resolution to set that up.) The practical consequence: the time you spend inside one ISR is time every other ISR at the same priority level is delayed. If your GPIO ISR takes 200 µs (because it does an I²C burst read inline), and a UART RX ISR fires during that 200 µs, the UART RX is serviced 200 µs late — long enough to overflow the UART FIFO at 921 600 baud.

The deferred-interrupt pattern follows directly: do nothing in the ISR but acknowledge the source, signal a worker task, and return.

## The deferred-interrupt pattern

In schematic form:

```text
   sensor edge
       |
       v
   [NVIC] -- 130 ns --> [ISR entry]
                          |
                          |  read source register     (5 cycles, 40 ns)
                          |  give semaphore / send to (~50 cycles, 400 ns)
                          |   stream buffer (FromISR)
                          |  pxHigherPriorityTaskWoken = pdTRUE
                          |  portYIELD_FROM_ISR(...)  (~100 cycles, 800 ns)
                          v
                       [ISR return]
                          |
                          | PendSV pending -> tail-chain into context switch
                          v
                     [PendSV handler] -- 84 cycles --> [worker task wake]
                                                          |
                                                          v
                                                     do the slow part
```

Three lines of code in the ISR. Maybe four if you count the `BaseType_t xHigherPriorityTaskWoken = pdFALSE;` declaration. The slow part — the I²C burst read of the MPU-6050's 14 sensor bytes, ≈ 280 µs — happens in a task context, where it can be preempted by any higher-priority work, and where blocking on the I²C peripheral is permitted.

Notional handler:

```c
static void mpu6050_int_isr(uint gpio, uint32_t events)
{
    /* The shared GPIO ISR dispatcher has already identified that GP22
     * fired with GPIO_IRQ_EDGE_RISE. We just need to wake the consumer. */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* The token has no payload; the consumer will read the sensor itself. */
    xSemaphoreGiveFromISR(g_mpu_ready_sem, &xHigherPriorityTaskWoken);

    /* Request a context switch on the way out if we woke a higher-prio task. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

Five lines including the declaration and the comment. The `pxHigherPriorityTaskWoken` mechanism is the most subtle of the three idioms in this pattern: the `*FromISR` API takes a `BaseType_t *` out-parameter; the function sets it to `pdTRUE` if the operation made a task ready whose priority is higher than the currently-running task's; the ISR then feeds that value to `portYIELD_FROM_ISR`, which (on Cortex-M0+) writes `PENDSVSET` to `SCB->ICSR` to request a `PendSV`-driven context switch on the way out.

If you forget the `portYIELD_FROM_ISR` call, the task you just woke does not run until the next `SysTick` boundary — which can be up to 1 ms away at the default 1 kHz tick. Your "low-latency" handoff has just become a "low-latency until the next millisecond" handoff. The bug is invisible on average and catastrophic on tail latency.

## The NVIC priority structure on Cortex-M0+

Cortex-M0+ implements two priority bits, left-aligned in the priority byte. Four priority levels, with the byte layout `pppppppx_xxxxxxxx` where `pp` are the implemented bits and `x` are read-as-zero ignore-on-write. The four legal values are:

| Stored byte | Logical priority | Notes                                                  |
|-------------|-----------------:|--------------------------------------------------------|
| `0x00`      |                0 | Highest                                                |
| `0x40`      |                1 |                                                        |
| `0x80`      |                2 |                                                        |
| `0xC0`      |                3 | Lowest (configurable interrupts)                       |
| `0xFF`      | (effectively 3)  | Hardware-mandated for `PendSV` and `SysTick` defaults  |

`SysTick` and `PendSV` priorities are written by FreeRTOS to `SHPR3` (`0xE000ED20`); the FreeRTOS Cortex-M0 port sets both to `0xFF` (which the hardware truncates to `0xC0`), the lowest level. This is intentional: `PendSV` must be at or below every other interrupt so that context switches happen *after* every other ISR has finished, and `SysTick` is treated as a relatively-cheap interrupt that can be deferred to the same level. The architecture *requires* `PendSV` to be the lowest-priority handler if you use it as the context-switch trampoline; if any other ISR were at the same priority and tail-chained ahead of `PendSV`, you would race the context switch.

FreeRTOS's `configMAX_SYSCALL_INTERRUPT_PRIORITY` controls a different thing: it sets the threshold at which the kernel masks interrupts during critical sections. Interrupts at *or below* this priority (i.e. higher-or-equal stored byte value) are masked; interrupts at *higher* priority (lower stored byte) remain enabled. On the Pico SDK port the default is `0xC0` (= priority 3, the lowest), which means **every** SDK-installed interrupt is FreeRTOS-API-callable, and there is no room for a higher-priority "zero-latency" interrupt above the kernel.

Why is the default `0xC0` and not, say, `0x80`? Two reasons:

1. The Pico SDK ships with every interrupt at default priority (which on Cortex-M0 is 0xC0 unless `irq_set_priority` overrides it). Making the kernel ceiling anything other than 0xC0 would silently break the SDK assumption.
2. The class of applications the Pico is designed for — IoT, lightly-real-time, hobby/education — does not benefit from a zero-latency interrupt above the kernel. The benefit (microsecond-scale ISR latency on one specific source) is offset by the cost (the zero-latency ISR cannot call any FreeRTOS API, so the deferred-interrupt pattern is unavailable; you must do *all* the work inline).

For Week 7 we leave `configMAX_SYSCALL_INTERRUPT_PRIORITY` at `0xC0` and put every interrupt at the same priority. The MPU-6050 sample at 200 Hz has 5 ms of slack on a 4.5 µs handler; we do not need zero-latency.

For a hypothetical Week 7+ scenario — say, a 100 kHz brushless-motor commutation interrupt where the handler must complete in 5 µs — you would set the commutation ISR to priority 0x00 (highest), keep everything else at 0xC0, set `configMAX_SYSCALL_INTERRUPT_PRIORITY` to 0x40 (which masks every kernel-callable ISR but lets the commutation through), and the commutation handler would do its work inline without calling any FreeRTOS API. The kernel does not see the commutation interrupt; the commutation interrupt does not see the kernel. This is the "zero-latency interrupt" pattern and it is documented in the FreeRTOS Kernel docs at <https://www.freertos.org/RTOS-Cortex-M3-M4.html> (the document is M3/M4-focused but the principle is the same on M0+).

## Routing a GPIO interrupt on the RP2040

The RP2040 has 30 GPIOs and two NVIC slots for GPIO interrupts (`IO_IRQ_BANK0` exception number 13, `IO_IRQ_QSPI` exception number 14). All 30 GPIOs share these two slots; the IRQ source per pin is encoded in a 4-bit field in `IO_BANK0.PROC0_INTS0..3` (the masked status registers — there is one nibble per GPIO, packed eight to a register; datasheet §2.19.6 pp. 281–284):

| Bit within nibble | Meaning                              |
|------------------:|--------------------------------------|
| 0                 | LEVEL_LOW                            |
| 1                 | LEVEL_HIGH                           |
| 2                 | EDGE_FALL                            |
| 3                 | EDGE_RISE                            |

So if `GP22` fired on a rising edge, bit `(22 % 8) * 4 + 3 = 24 + 3 = 27` of `PROC0_INTS2` (since `22 / 8 = 2`) is set.

The SDK abstracts this with `gpio_set_irq_enabled_with_callback(gpio, events, enabled, callback)`. Under the hood it:

1. Configures the four bits in `IO_BANK0.PROC0_INTE0..3` (the *enable* register) for the pin.
2. Installs `gpio_default_irq_handler` as the handler for both `IO_IRQ_BANK0` and `IO_IRQ_QSPI` if it is not already installed.
3. Stores the user's callback function pointer in a static table indexed by core ID (the dispatcher is multi-core-aware even though our mini-project only runs on core 0).

When the interrupt fires, the dispatcher reads `IO_BANK0.PROC0_INTS0..3`, finds the set nibble, computes the GPIO number and event mask, calls the user callback with those values, then writes 1 to the corresponding nibble in `IO_BANK0.INTR0..3` to acknowledge — but only for `EDGE_*` events; `LEVEL_*` events are level-sensitive and clear when the level changes, not by software write. This is a subtle gotcha if you configure both edge and level sensitivity on the same pin.

Source for the dispatcher: `pico-sdk/src/rp2_common/hardware_gpio/gpio.c`, function `gpio_default_irq_handler`. ~50 lines of C with two nested loops (over the four 32-bit status registers and the eight GPIOs per register). Read it before Exercise 1.

A subtle point: the SDK's shared dispatcher reads *all four* status registers on every interrupt. If you have an active interrupt on GP22 and a fired-but-disabled interrupt on GP3, the dispatcher still wastes 4 register reads (one per `PROC0_INTSn`) before identifying GP22. The cost is ~30 cycles, 240 ns at 125 MHz. Acceptable for our 200 Hz sample rate; if you needed to optimize, you would replace the dispatcher with a single-source ISR via `irq_set_exclusive_handler`.

## Measuring ISR-to-task latency on the bench

You cannot trust a number you have not measured. The latency math above predicts ~4.2 µs from edge to task wake; you will confirm it with one of two methods.

**Method 1 — GPIO toggles on the Saleae.** Toggle a GPIO at three points: (a) just before the I²C burst-read that the task does, (b) inside the ISR, just after the `xSemaphoreGiveFromISR` returns, (c) in the task, just after `xSemaphoreTake` returns. The Saleae's edge-statistics view gives you the delta between (b) and (c) — that is your ISR-to-task latency. Capture 1000 events; report mean, p99, max.

**Method 2 — `time_us_32` deltas.** The RP2040's 1 MHz TIMER peripheral (datasheet §4.6 pp. 552–568) is safe to read from any context, including an ISR. Read it at ISR entry, store in a global, read again in the task immediately after `xSemaphoreTake` returns, subtract. The reading has 1 µs resolution — coarser than the logic analyzer but readable over the UART without any external equipment. The mini-project's `LATENCY.md` reports both methods.

You should see ~4–5 µs at the mean and ~6–8 µs at the p99. The p99 outliers correspond to ticks where a `SysTick` interrupt fired in the gap between ISR entry and the `PendSV` request; in that case the `SysTick` handler runs first (it has the same priority but is already at the front of the NVIC's queue, so it tail-chains ahead) and adds ~1.5 µs of its own work before the `PendSV` finally runs. If you see p99 values above 10 µs, something is wrong — the most common cause is a longer-than-expected ISR somewhere else in the system stealing time at the same priority level.

## What this lecture's exercise will show you

Exercise 1 builds the simplest possible EXTI handoff: a momentary button on `GP18`, a GPIO interrupt on `GPIO_IRQ_EDGE_FALL`, an ISR that gives a binary semaphore, a task that takes the semaphore and toggles an LED. The whole program is ~100 lines including the `FreeRTOSConfig.h`. You will measure the latency three ways: with two GPIO toggles on the Saleae (the canonical method), with `time_us_32` deltas printed over UART, and with the SDK's `picotool` flash-verification timing (just to confirm the build did what you expected).

The expected number on the bench is 3.8–4.5 µs from button-press edge to LED-toggle edge, dominated by the context switch. If you measure < 3 µs you probably forgot the `portYIELD_FROM_ISR` and the LED is toggling on the next `SysTick`. If you measure > 10 µs something is stealing time — usually a long-running idle hook or a tick hook you forgot you wrote.

Next lecture: stream buffers. The same pattern, but with a payload — the ISR sends bytes, not just a wake-up token.
