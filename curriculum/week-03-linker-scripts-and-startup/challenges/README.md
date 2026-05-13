# Week 3 — Challenges

One challenge this week. It is harder than the exercises and lighter than the mini-project. Spend ~2–3 hours on it. Self-paced cohorts may skip it; you will hit it again in Week 9 (Bootloaders), and the discipline you build here pays off then.

1. **[Challenge 1 — Bare-bare-metal blink](challenge-01-bare-bare-metal-blink.md)** — strip the build further: no `boot2`, no flash. Build a firmware that runs entirely from SRAM, loaded over SWD by OpenOCD or `probe-rs`. The boot ROM is involved only briefly (to halt the CPU); the rest is your code. This is the smallest possible Cortex-M0+ firmware that does anything useful.

Challenges differ from exercises in three ways: less hand-holding, an open-ended path, and a deliverable that lives at the *toolchain* level (not the firmware level). Challenge 1 makes you understand the boot ROM's responsibilities by *bypassing* the ROM with a SWD-loaded SRAM-resident firmware.
