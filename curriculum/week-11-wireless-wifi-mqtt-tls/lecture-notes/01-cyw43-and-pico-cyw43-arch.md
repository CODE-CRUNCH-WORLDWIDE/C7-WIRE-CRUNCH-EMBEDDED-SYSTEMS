# Lecture 1 — The CYW43439 and the pico_cyw43_arch API

> *Adding a WiFi chip to a 5-USD microcontroller and selling the result for 6 USD is the kind of engineering that looks effortless until you go read what the firmware has to do. The CYW43 is not "a WiFi module" in the way a USB stick is; it is a second computer — a 160 MHz Cortex-M3 with its own RAM, its own firmware (closed-source, distributed as a 360 KB blob), its own SDIO/SPI link to the host, and its own slightly non-standard wire protocol — and the only reason it presents as "a WiFi network interface" to your application code is that the pico-sdk's `pico_cyw43_arch` layer ties roughly 8000 lines of glue between the CYW43's link-layer driver, lwIP's `netif` abstraction, and the SDK's `cyw43_arch_*` user-facing API. Today's job is to make that stack legible so that when your join times out, your DHCP fails, or your IP address comes back as 0.0.0.0, you have a mental map of where to look.*

The Raspberry Pi Pico W (RP2040 + CYW43439, released June 2022) is the wireless variant of the Pico. Pin-compatible with the original Pico, same RP2040 SoC, same 2 MB QSPI flash, same 264 KB SRAM — plus an Infineon (formerly Cypress, formerly Broadcom) CYW43439 module soldered to the underside of the PCB and an antenna trace around the top edge. The module gives you 802.11 b/g/n WiFi 4 (2.4 GHz only, no 5 GHz; that is the cost differential between CYW43439 and the higher-end CYW4343W in the Pico 2 W) plus Bluetooth 5.2 (which we do not cover this week — Week 13 picks it up).

## The CYW43439 as a computer in its own right

Open the Pico W datasheet (<https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf>) and turn to §4 Wireless, pp. 14–17. Four pages of block diagram, pinout, and antenna geometry. The block diagram on p. 14 tells the story: the CYW43439 has a 160 MHz ARM Cortex-M3, ~512 KB of internal RAM, a hardware MAC, a 2.4 GHz baseband, and a 2.4 GHz RF front-end with a transmit power amp and a low-noise amp. The Cortex-M3 runs Broadcom's closed-source WiFi firmware blob; on every cold boot of the Pico W, the pico-sdk's cyw43-driver downloads this blob (~360 KB) into the CYW43's RAM over the SPI link, then issues a "go" command that releases the CYW43's Cortex-M3 from reset and starts executing the firmware at offset zero in its RAM. The firmware blob itself is at `lib/cyw43-driver/firmware/wb43439A0_7_95_49_00_combined.h` in the pico-sdk tree — a giant C array — and is distributed under a binary-redistributable license from Infineon (you can ship it in your product; you cannot reverse-engineer it; you cannot modify it).

Two consequences worth internalizing. First, **WiFi power-on takes ~1.5 seconds** because that 360 KB blob has to traverse a ~30 MHz half-duplex SPI bus. Cite this number: it is the largest single contributor to your cold-boot-to-first-publish budget; if your application boot loop measures `tic = time_us_64()` before `cyw43_arch_init()` and `toc = time_us_64()` after it, `toc - tic` should be in the 1.4–1.6 million range. If it is much higher, the SPI bus is clocking slow (the CYW43 driver runs at 30 MHz by default; some old SDK versions ran at 8 MHz); if it is much lower, the firmware blob did not actually load and the subsequent association call will fail.

Second, **the WiFi firmware version determines what auth modes are supported**. The blob shipped in pico-sdk 1.5.0 (June 2023) was the first to include WPA3-SAE. Earlier pico-sdk releases (1.4.0 from December 2022 onward) had WPA3 stubs but the firmware would silently fall back to WPA2 on a WPA3-only network. If you are on an old SDK and your home network is WPA3-only (newer mesh systems default to this), your join will fail with `CYW43_LINK_BADAUTH` and the error will not tell you why. Update your SDK to ≥ 1.5.1 before debugging.

## The PIO-SPI bus to the CYW43

The link between the RP2040 and the CYW43 is a 4-wire bus driven not by the RP2040's hardware SPI peripheral but by **PIO state machine 1**. This is the most surprising design decision in the Pico W and worth understanding. The CYW43 supports two host-interface modes: SDIO (the same protocol SD cards use) and a half-duplex SPI variant where the data line is *bidirectional* (data-out from the host on the falling edge of the clock, data-in to the host on the rising edge of the clock, with a "gap" cycle inserted between bytes to give the chip time to flip the direction). The standard SPI peripheral on the RP2040 cannot do this — it has separate MOSI and MISO lines and no gap-insertion. The PIO peripheral can: a 9-instruction PIO program (`cyw43_bus_pio_spi.pio` in the SDK, ~30 lines including comments) implements the gap-inserted half-duplex SPI in real time.

The pinout on the Pico W board:

| Signal             | RP2040 GPIO | Notes                                                              |
|--------------------|------------:|--------------------------------------------------------------------|
| WL_REG_ON (power)  | 23          | Drive high to power the CYW43; ~50 ms ramp-up time after assertion |
| DAT (data line)    | 24          | Bidirectional; PIO drives output on falling clock, reads on rising |
| CLK (clock)        | 29          | PIO-driven; ~30 MHz                                                |
| CS (chip-select)   | 25          | PIO-asserted before transfer, deasserted after                     |
| IRQ                | 24 (shared) | The data line doubles as an interrupt-pending line when idle       |

GPIO 23, 24, 25, and 29 are **not available** for your application on a Pico W. The Pico W datasheet's pinout (p. 8) marks them as "Internal" or "WL" reserved. If you `gpio_init(24); gpio_put(24, 1);` from your application code, the CYW43 driver will get confused, the bus will desynchronize, and the next packet exchange will probably hang. Treat 23–29 as "do not touch" pins for the entire week.

## The onboard LED gotcha

On the original Pico (no W), GP25 is wired to the green onboard LED. `gpio_init(25); gpio_put(25, 1);` turns it on.

On the Pico W, **GP25 is the SPI chip-select** for the CYW43. The onboard LED is wired to **the CYW43's own GP0 pin**, not the RP2040's. To toggle the LED you must:

```c
#include "pico/cyw43_arch.h"

cyw43_arch_init();
cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
```

`CYW43_WL_GPIO_LED_PIN` is defined as `0` in the SDK; it is the CYW43's own GPIO 0. The `cyw43_arch_gpio_put` call sends a small message over the PIO-SPI bus to the CYW43, which then drives its own GPIO 0 high. Latency: ~200 microseconds. You cannot toggle this from interrupt context (the SPI message exchange would deadlock); use it for status indication from your main loop only.

Every Week 11 student gets bitten by this. Symptom: "my Pico W program is running but the LED never lights up." Fix: import `pico/cyw43_arch.h`, call `cyw43_arch_init` before any LED-toggling, and use `cyw43_arch_gpio_put`. Document this in the project's README so the next student does not waste an hour.

## The `pico_cyw43_arch` family of build targets

The pico-sdk exposes the CYW43 to your application through four "arch" build targets. You pick exactly one in your `CMakeLists.txt`:

```cmake
target_link_libraries(my_app
  pico_stdlib
  pico_cyw43_arch_lwip_poll       # or one of the three below
)
```

The four variants:

- **`pico_cyw43_arch_lwip_poll`**: Single-threaded, cooperative. Your main loop calls `cyw43_arch_poll()` (which services the CYW43 SPI bus and pumps the lwIP tick) and `cyw43_arch_wait_for_work_until(absolute_time)` (which sleeps the CPU until either the next timer fires or an SPI interrupt wakes it). This is what we use in the mini-project. Pros: deterministic, no race conditions, easy to integrate with mbedTLS's blocking handshake. Cons: if your main loop ever blocks for more than ~50 ms without calling `cyw43_arch_poll`, you will drop received packets and lwIP will retransmit.
- **`pico_cyw43_arch_lwip_threadsafe_background`**: Single-threaded foreground, but a SYS_TICK-driven background context services lwIP every tick. Your main loop can do other work; lwIP runs in interrupt-flavored background. Pros: easier to integrate when foreground does non-network work. Cons: every lwIP API call from foreground must be wrapped in `cyw43_arch_lwip_begin()` / `cyw43_arch_lwip_end()` to take the lock; forgetting one of those pairs causes corruption that you will never debug.
- **`pico_cyw43_arch_lwip_sys_freertos`**: lwIP runs in its own FreeRTOS task; all lwIP APIs are thread-safe via the SDK's `sys_*` mutex wrappers. Pros: composes with FreeRTOS the way you would expect. Cons: pulls in FreeRTOS (~30 KB of `.text`); overkill unless the rest of your project uses FreeRTOS.
- **`pico_cyw43_arch_threadsafe_background`**: Background CYW43 driver without the lwIP build (you would implement L3+ yourself or use a different stack). Rare; we never use this.

The `_poll` variant integrates cleanly with mbedTLS because the TLS handshake is itself a busy-wait loop: `mbedtls_ssl_handshake()` returns `MBEDTLS_ERR_SSL_WANT_READ` or `_WANT_WRITE` when it has nothing to do until the network catches up; in those cases we call `cyw43_arch_poll()` and retry. With the `_threadsafe_background` variant the handshake would just spin; with the FreeRTOS variant the handshake's blocking call would yield. Both work, but `_poll` makes the lifecycle most legible to a student reading the code for the first time.

## Joining a network

The four-step minimum to get from `main()` to a routable IPv4 address:

```c
#include "pico/cyw43_arch.h"

int main(void) {
    stdio_init_all();
    if (cyw43_arch_init() != 0) {
        printf("cyw43_arch_init failed\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    const char ssid[]     = "MyHomeAP";
    const char password[] = "correct-horse-battery-staple";
    const uint32_t timeout_ms = 30000U;

    int rc = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, timeout_ms);
    if (rc != 0) {
        printf("wifi connect failed: %d\n", rc);
        return 1;
    }
    printf("connected; ip = %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_default)));

    /* … application loop … */
}
```

Four things to know about that block.

**`cyw43_arch_init` blocks for ~1.5 s.** This is the firmware download. Do not call it inside an interrupt handler, do not call it before `stdio_init_all` (you will not see early-boot prints because USB CDC is not up yet), and do not call it twice (the SDK handles the idempotency but the second call is a 1.5-s wait for nothing).

**`cyw43_arch_enable_sta_mode` switches the CYW43 to STA (station/client) mode** as opposed to AP (access-point) mode. The alternative is `cyw43_arch_enable_ap_mode(ssid, password, auth)` which makes the Pico W *broadcast* its own network — useful for "first-time setup" flows where the device exposes a captive portal to receive credentials. We do not cover AP mode this week.

**`cyw43_arch_wifi_connect_timeout_ms` returns a non-negative int on success, negative on failure.** The negative codes are `PICO_ERROR_TIMEOUT` (-1, took longer than `timeout_ms`), `PICO_ERROR_CONNECT_FAILED` (-3, association failed for a reason other than wrong password), `PICO_ERROR_BADAUTH` (-4, wrong password). Inside the call, the four-way handshake (RFC 802.11i-2004, refined into 802.11-2020 §12.7) runs: ANonce from AP → SNonce from STA → MIC from STA → GTK install confirmation from AP. On a typical home AP this takes ~800 ms; DHCP DISCOVER → OFFER → REQUEST → ACK adds another ~500 ms; total time from `connect_timeout_ms` entry to "IP assigned" is ~1.3 s for an empty AP, longer on a busy one.

**The `CYW43_AUTH_*` enum** has six members worth knowing:

| Constant                          | What it means                                                        | When to use                                  |
|-----------------------------------|----------------------------------------------------------------------|----------------------------------------------|
| `CYW43_AUTH_OPEN`                 | No encryption; password ignored                                      | Open APs (rare in homes, common at cafes)   |
| `CYW43_AUTH_WPA_TKIP_PSK`         | WPA1 with TKIP cipher                                                | Legacy APs; avoid                            |
| `CYW43_AUTH_WPA2_AES_PSK`         | WPA2 with AES-CCMP cipher                                            | The default for most home APs                |
| `CYW43_AUTH_WPA2_MIXED_PSK`       | WPA2 PSK with mixed AES/TKIP allowed                                 | Some older APs in mixed mode                 |
| `CYW43_AUTH_WPA3_SAE_AES_PSK`     | WPA3 with SAE handshake                                              | Newer mesh APs that default to WPA3-only     |
| `CYW43_AUTH_WPA3_WPA2_AES_PSK`    | WPA3 SAE if the AP supports it, else fall back to WPA2-PSK           | The safest "I don't know what my AP runs"    |

For the mini-project we use `CYW43_AUTH_WPA3_WPA2_AES_PSK` because it works on the broadest set of APs. The fall-back logic is in the CYW43 firmware blob; we do not have visibility into the decision but we trust it.

## What `netif_default` looks like

After a successful `cyw43_arch_wifi_connect_timeout_ms`, the lwIP global `netif_default` points at the CYW43's STA-mode netif. Its fields:

```c
struct netif {
    ip4_addr_t ip_addr;     /* assigned by DHCP, or 0.0.0.0 if DHCP still in progress */
    ip4_addr_t netmask;     /* typically 255.255.255.0 */
    ip4_addr_t gw;          /* the AP / home router */
    u8_t hwaddr[6];         /* the MAC, e.g. 28:cd:c1:0d:fe:8c */
    u8_t flags;             /* NETIF_FLAG_UP | NETIF_FLAG_LINK_UP | NETIF_FLAG_ETHARP | ... */
    /* … many more … */
};
```

To check whether DHCP has completed:

```c
const ip4_addr_t *ip = netif_ip4_addr(netif_default);
if (ip4_addr_isany(ip)) {
    /* DHCP still pending */
}
```

The `ip4_addr_isany` helper returns true if the address is 0.0.0.0. There is a race window between the four-way-handshake completing (the netif is `NETIF_FLAG_UP`) and the DHCP exchange completing (the ip_addr is non-zero); the SDK's `cyw43_arch_wifi_connect_timeout_ms` waits for both, so by the time it returns 0, you have a usable IP. If you used the `cyw43_arch_wifi_connect_async` variant you must poll the netif yourself.

## RSSI, BSSID, and the scan API

Once associated, you can read the signal strength:

```c
int32_t rssi;
cyw43_wifi_get_rssi(&cyw43_state, &rssi);
printf("rssi: %ld dBm\n", rssi);
```

Typical values: -40 dBm right next to the AP, -65 dBm a couple of rooms away, -80 dBm at the edge of usable range, -90 dBm and association will fail. The CYW43 driver also exposes a scan API (`cyw43_wifi_scan`) that callback-streams every visible AP's `(SSID, BSSID, channel, RSSI, security)` — useful for "show me what's around" diagnostics. We use it once in Exercise 1.

## What can go wrong: a triage table

When `cyw43_arch_wifi_connect_timeout_ms` returns non-zero, the failure mode is one of:

| Return code                    | Most likely cause                              | First thing to check                                    |
|--------------------------------|------------------------------------------------|---------------------------------------------------------|
| `PICO_ERROR_BADAUTH` (-4)      | Wrong password                                 | Print the password literal you compiled in              |
| `PICO_ERROR_CONNECT_FAILED` (-3)| Wrong auth mode (WPA3-only AP, WPA2 client)    | Try `CYW43_AUTH_WPA3_WPA2_AES_PSK`                      |
| `PICO_ERROR_CONNECT_FAILED` (-3)| MAC filtering on the AP                        | Read the Pico's MAC; add it to the AP allow-list        |
| `PICO_ERROR_TIMEOUT` (-1)      | AP not in range or wrong SSID                   | Scan with `cyw43_wifi_scan`; check SSID matches exactly  |
| `PICO_ERROR_TIMEOUT` (-1)      | DHCP not responding                             | Check the AP's DHCP server is up; rule out IP exhaustion |
| `0` followed by `ip_addr == 0` | DHCP DISCOVER got no OFFER                      | Same as above                                            |

The triage move is always: print the SSID, the password length, the auth mode, the rc value, and the RSSI of the strongest visible AP (from a scan). If those four numbers do not point at the bug, then the bug is in the AP itself and you debug from the AP side with `tcpdump -i wlan0 -nn 'ether host 28:cd:c1:0d:fe:8c'`.

## The `cyw43_arch_wait_for_work_until` idiom

The main-loop idiom for `_poll`-variant projects:

```c
absolute_time_t next_publish = make_timeout_time_ms(10000U);
for (;;) {
    cyw43_arch_poll();
    if (time_reached(next_publish)) {
        publish_one_reading();
        next_publish = make_timeout_time_ms(10000U);
    }
    cyw43_arch_wait_for_work_until(next_publish);
}
```

`cyw43_arch_wait_for_work_until` sleeps the CPU (via `__wfi`, a WFI instruction that drops the core to the wait-for-interrupt low-power state) until *either* the absolute time is reached *or* an SPI interrupt fires (a packet has arrived from the CYW43, or the link state changed). Either way it returns and the next `cyw43_arch_poll()` services whatever woke us up. Power consumption during this idle window is ~5 mA at the RP2040 plus ~80 mA at the CYW43 (which is staying associated and decoding the broadcast traffic from the AP); total ~85 mA, or ~0.4 W at 5 V — about half a watt steady-state, which is the floor on a Pico W and matches the order of magnitude reported in the Pico W datasheet's §6 (Electrical Specifications).

If you want lower idle power, you switch the CYW43 to its sleep mode (`cyw43_wifi_pm`-managed; the CYW43 firmware can DTIM-sleep between AP beacons), but that adds 100–500 ms of wake-up latency to the next outbound send and complicates the publish-cadence math. We do not enable sleep in this week's mini-project; the assumption is "wall-powered USB" not "battery."

## Summary

The CYW43439 is a computer running closed-source WiFi firmware behind a PIO-SPI link, and the pico-sdk's `pico_cyw43_arch` API hides almost all of that behind eight functions you actually call (`init`, `enable_sta_mode`, `wifi_connect_timeout_ms`, `gpio_put`, `poll`, `wait_for_work_until`, `lwip_begin`/`_end`, `deinit`). The two things to internalize: the cold-boot firmware load is ~1.5 s and you cannot shorten it; the onboard LED is on the CYW43 not the RP2040 and you must use `cyw43_arch_gpio_put`. Everything else is bookkeeping that the SDK does for you. Tomorrow we open the lid on lwIP and look at the TCP and DNS raw-API contracts your code drives directly.
