# Exercise 3 — First Blink in MicroPython

**Time estimate:** ~60 minutes.

## Problem statement

Reflash your Pi Pico W with MicroPython, write a five-line blink in Python, and record three numbers you will use against your C build: binary size, boot-to-first-blink time, and peak free RAM. The point is not to "do blink again." The point is to calibrate — in numbers, not adjectives — the cost of running a Python runtime on an MCU.

## Acceptance criteria

- [ ] `MicroPython v1.22.x` (or newer) is flashed onto the Pi Pico W, replacing the C blink from Exercise 2. The REPL banner over USB-CDC at 115200 8N1 prints the MicroPython version.
- [ ] A file `main.py` lives on the board that blinks the on-board LED at 1 Hz, in MicroPython.
- [ ] After a hard reset (re-plug USB), the LED begins blinking automatically. (MicroPython runs `main.py` after `boot.py` on every reset.)
- [ ] `notes/blink-micropython.md` contains all of:
  - The size of the MicroPython `.uf2` you flashed (bytes).
  - The size of your `main.py` (bytes).
  - The total flash usage as reported by `import os; os.statvfs('/')`.
  - The free RAM as reported by `import gc; gc.collect(); gc.mem_free()`.
  - The measured boot-to-first-blink time (stopwatch is fine; aim for ±0.5 s precision).
- [ ] A scope or Saleae trace of the GP15 toggle that runs alongside the LED, for comparison with the C trace from Exercise 2. (You will compare jitter side-by-side later.)

## Hints

<details>
<summary>Step 1 — Flash MicroPython onto the Pico W</summary>

1. Download the latest stable MicroPython `.uf2` for the Pico W from <https://micropython.org/download/RPI_PICO_W/>. The file is `RPI_PICO_W-<date>-v1.22.x.uf2` (≈ 1.5 MB).
2. Note the file size in bytes (right-click → properties; or `stat -f%z` on macOS, `stat -c%s` on Linux). Commit this number to `notes/blink-micropython.md`.
3. Boot the Pico W in BOOTSEL mode (hold BOOTSEL, plug in USB).
4. Drag the `.uf2` onto the `RPI-RP2` drive.
5. The board reboots. After ~1 second a new USB device enumerates — `/dev/tty.usbmodem*` on macOS, `/dev/ttyACM0` on Linux. That is the MicroPython REPL.

</details>

<details>
<summary>Step 2 — Connect to the REPL</summary>

```bash
# Easiest: mpremote
pip install --user mpremote
mpremote connect /dev/ttyACM0 repl       # Linux
mpremote connect /dev/tty.usbmodem01 repl  # macOS — tab-complete the device

# Alternative: minicom, picocom, screen
minicom -D /dev/ttyACM0 -b 115200
# or
screen /dev/ttyACM0 115200
```

You should see:

```
MicroPython v1.22.2 on 2024-02-22; Raspberry Pi Pico W with RP2040
Type "help()" for more information.
>>>
```

If you see garbled characters: wrong baud rate. MicroPython on RP2040 USB-CDC ignores the baud setting on its side, but some host terminals require 115200 anyway.

To exit `mpremote` REPL: Ctrl-X. To exit `screen`: Ctrl-A then K.

</details>

<details>
<summary>Step 3 — Test the blink interactively</summary>

At the REPL, type:

```python
>>> from machine import Pin
>>> led = Pin("LED", Pin.OUT)         # "LED" is the MicroPython alias for the Pico W on-board LED
>>> led.on()                          # LED turns on
>>> led.off()                         # LED turns off
>>> import time
>>> for _ in range(5):
...     led.toggle()
...     time.sleep_ms(500)
```

(After typing the `for`, press Enter twice to execute the block.)

The LED should blink 5 times. If `Pin("LED", Pin.OUT)` raises `ValueError`, you have an old MicroPython build; update to 1.20+.

</details>

<details>
<summary>Step 4 — The `main.py` for autonomous blink</summary>

Create a file `main.py` on your laptop:

```python
# C7 · Crunch Wire — Week 01 — MicroPython blink at 1 Hz on Pi Pico W.
from machine import Pin
import time

led = Pin("LED", Pin.OUT)
sentinel = Pin(15, Pin.OUT)   # for scope capture alongside the LED

while True:
    led.on()
    sentinel.on()
    time.sleep_ms(500)
    led.off()
    sentinel.off()
    time.sleep_ms(500)
```

Copy it to the board:

```bash
mpremote connect /dev/ttyACM0 cp main.py :
mpremote connect /dev/ttyACM0 reset
```

The board reboots and runs your `main.py` immediately on startup. The LED blinks at 1 Hz; GP15 toggles in sync.

</details>

<details>
<summary>Step 5 — Capture the three numbers</summary>

At the REPL after a fresh reset (Ctrl-C to interrupt your `main.py` first):

```python
>>> import gc, os
>>> gc.collect()
>>> gc.mem_free()
180304                       # bytes of free heap — write this down
>>> os.statvfs('/')
(4096, 4096, 212, 165, 165, 0, 0, 0, 0, 255)
>>> # decode statvfs: block_size, frag_size, total_blocks, free_blocks, ...
>>> # total flash = total_blocks * block_size = 212 * 4096 = 868352 bytes
>>> # free flash  = free_blocks  * block_size = 165 * 4096 = 675840 bytes
>>> 4096 * 212
868352
>>> import time
>>> # rough boot-to-first-blink: stopwatch is fine. From plug-in to first LED-on, MicroPython
>>> # typically takes 550–700 ms; a C build typically takes 30–80 ms.
```

Commit each of these numbers. Approximate is fine; the *order of magnitude* is what we are after.

</details>

<details>
<summary>Step 6 — Comparison table</summary>

Add this table to `notes/blink-micropython.md`. Fill in your own numbers.

| Metric                          | C (Exercise 2) | MicroPython (this exercise) | Ratio |
|--------------------------------:|---------------:|----------------------------:|------:|
| Firmware image size (bytes)     |    ~21,000     |              ~1,500,000     |  ~70× |
| Free RAM after boot (bytes)     |    ~250,000    |              ~180,000       |       |
| Boot-to-first-blink (ms)        |    ~50         |              ~600           |  ~12× |
| Time-to-write (minutes)         |    ~30         |              ~5             |       |
| LED jitter under load (µs)      |    < 1         |              > 1000         |       |

The "time-to-write" row is the human cost. It is real. It is why we keep MicroPython on the bench for prototyping even though we will not ship it.

</details>

## Why this matters

You now have the two numbers that justify the C-vs-MicroPython decision rule for the next 24 weeks:

- MicroPython costs ~70× more flash and ~12× more boot time than C, for the same observable behavior.
- MicroPython saves ~6× human time on this trivial example, and more as algorithms get hairier.

Neither tool is "better." Each has a regime. A Week-13 Wi-Fi telemetry node where the algorithm is "every 60 seconds, sample the BME280, push to MQTT, sleep" runs perfectly in MicroPython if you have flash to spare. A Week-11 audio DSP loop at 48 kHz with a 64-tap FIR cannot run in MicroPython at any speed; the GC pauses alone would ruin the audio. You will know which is which by Week 12.

For now, what you must internalize: the *same blink* costs 70× more flash in the high-level language. Embedded engineering is the discipline where that 70× has a price tag — measured in basis points of unit BOM cost, in seconds of charge-per-watt-hour, in 8 KB vs 256 KB MCU choices — and you start paying attention to it now.

## Submission

Commit `notes/blink-micropython.md` containing the five numbers, the table, and the scope screenshot.

After this exercise, the Pi Pico W is in MicroPython. For the rest of Week 1 — the UART challenge and the mini-project — you will reflash it back to C. Welcome to the iterate-and-reflash loop. You will run it thousands of times.
