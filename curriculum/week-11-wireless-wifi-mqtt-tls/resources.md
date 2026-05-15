# Week 11 — Resources

Everything cited below is free to read and free to use. Specs are linked at their canonical homes (OASIS, IETF, Raspberry Pi); reference implementations have permissive licenses (BSD-3-Clause for lwIP, Apache-2.0 for mbedTLS, EPL-2.0 for Eclipse Paho, BSD-2-Clause for the pico-sdk). Where a vendor PDF has rotated URLs in the past (Raspberry Pi has done this once), the Wayback Machine URL is the fallback.

---

## Primary specifications

### MQTT 5.0 (OASIS Standard, 2019-03-07)

- **Spec:** <https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html> (HTML, 167 pages)
- **PDF:** <https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.pdf>
- **Errata:** <https://docs.oasis-open.org/mqtt/mqtt/v5.0/errata/> (track this; the 2020 errata fixes the VBI maximum-value definition)

The sections you will reference constantly this week:

- **§1.5.5 Variable Byte Integer** (p. 16). Two paragraphs and one table. The single most-bug-prone primitive in the protocol; reread this every time you find an off-by-one in your encoder.
- **§2.1 Fixed Header** (pp. 19–22). The 1-byte type+flags layout for all 15 packet types, plus the table of "Reserved" flag bits that MUST be zero.
- **§2.2.2 Properties** (pp. 22–28). Table 2-4 lists every property identifier (`Session Expiry Interval`, `Receive Maximum`, `Topic Alias Maximum`, `Reason String`, `User Property`, etc.) with its associated data type. Bookmark this table.
- **§3.1 CONNECT** (pp. 38–53). 15 pages on the connection-establishment packet. Read this *twice*. The first read is "what fields are in a CONNECT?" The second read is "which bits of the connect-flags byte mean what?" — the byte is densely overloaded and the spec's wording is precise.
- **§3.2 CONNACK** (pp. 54–63). The server's response. Pay attention to the 26 Reason Codes (Table 3-1) and which subset of them apply to CONNACK vs PUBACK vs DISCONNECT.
- **§3.3 PUBLISH** (pp. 64–78). The packet you will send 8640 times per day. The flags-byte DUP/QoS/RETAIN semantics are subtle; read §3.3.1 carefully.
- **§3.12 PINGREQ** (pp. 122–123) and **§3.13 PINGRESP** (pp. 123–124). One paragraph each; the simplest packets in the spec.
- **§3.14 DISCONNECT** (pp. 125–131). Both client and server send these; the Reason Code tells you why.

The OASIS process: the spec is "OASIS Standard" status (the highest), so it is stable and won't change. Errata are gathered into a future "Errata 01" document. If you find a wording ambiguity, the OASIS MQTT TC's mailing list at <https://www.oasis-open.org/committees/mqtt/> is the canonical place to ask.

### MQTT 3.1.1 (OASIS Standard, 2014-10-29)

- **Spec:** <https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html>
- **PDF:** <https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.pdf> (81 pages)

Read this only as comparison. MQTT 3.1.1 is the dominant version on shipping devices (most public brokers accept both), and the differences from 5.0 are documented in the MQTT 5.0 introduction §1.1. The main shift: no Properties, single-byte return codes in CONNACK, no Reason Codes on any other ACK packet, no Topic Aliases, no Shared Subscriptions, no Will Delay.

### MQTT-SN ("MQTT for Sensor Networks", 2013)

- **Spec:** <https://www.oasis-open.org/committees/document.php?document_id=66091&wg_abbrev=mqtt> (PDF, 28 pages)

A UDP-based fork of MQTT for radio networks where TCP cannot be afforded (LoRa, Zigbee, 802.15.4). Use of topic IDs (2 bytes) instead of topic strings, single-frame payloads, predefined topic table. We do *not* use this in the mini-project because the Pico W has TCP available and a TLS-friendly broker. Read this as a "what if we had to do this over LoRa next semester" reference.

### TLS 1.2 (RFC 5246, 2008-08)

- **Spec:** <https://www.rfc-editor.org/rfc/rfc5246> (HTML/text, 104 pages)

The transport-layer-security version mbedTLS uses when we pin it to TLS 1.2-only. Read §7.3 (Handshake Protocol Overview, pp. 33–38) for the message flow, §7.4 (Handshake Protocol, pp. 38–47) for the message contents, and §8 (Cryptographic Computations, pp. 47–50) for how the master_secret is derived.

### TLS 1.3 (RFC 8446, 2018-08)

- **Spec:** <https://www.rfc-editor.org/rfc/rfc8446> (HTML/text, 160 pages)

The replacement for TLS 1.2. Collapses the handshake from two round-trips to one (or zero, with 0-RTT). The pico-sdk's mbedTLS supports TLS 1.3, but for our broker (which accepts both) we pin TLS 1.2 because the cipher list is simpler and the certificate-verification path is more predictable on a 125 MHz Cortex-M0+. Future weeks will revisit.

---

## RP2040 + CYW43 documentation

### Pico-SDK doxygen

- **Networking root:** <https://www.raspberrypi.com/documentation/pico-sdk/networking.html>
- **`pico_cyw43_arch`:** <https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_cyw43_arch>
- **`pico_lwip`:** <https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_lwip>
- **`pico_mbedtls`:** <https://www.raspberrypi.com/documentation/pico-sdk/networking.html#pico_mbedtls>

The pico-sdk's networking section is the load-bearing reference for the week. Bookmark all four of the URLs above.

The four `pico_cyw43_arch_lwip_*` variants:

| Variant                                     | Threading model                                                  | When to use                                                                |
|---------------------------------------------|------------------------------------------------------------------|----------------------------------------------------------------------------|
| `pico_cyw43_arch_lwip_poll`                 | Single-threaded; call `cyw43_arch_poll()` in your main loop      | When your main loop is short and you can poll often; what we use this week |
| `pico_cyw43_arch_lwip_threadsafe_background`| Single-threaded foreground + SYS_TICK-driven background lwIP     | When you have non-trivial foreground work and cannot guarantee polling     |
| `pico_cyw43_arch_lwip_sys_freertos`         | FreeRTOS task; lwIP runs in its own task; thread-safe APIs       | When the rest of the project uses FreeRTOS                                 |
| `pico_cyw43_arch_threadsafe_background`     | Background lwIP without the lwIP build (raw CYW43, no TCP/IP)    | When you implement your own L3+ (rare)                                     |

### Pico W datasheet

- **PDF:** <https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf> (28 pages, 2022-08)
- **Wayback fallback:** <https://web.archive.org/web/2024/https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf>

§4 (Wireless, pp. 14–17) documents the CYW43439 part number, the antenna design, the PIO-SPI bus pinout (GPIO 23 power-enable, GPIO 24 SPI data, GPIO 25 SPI clock, GPIO 29 chip-select), and the onboard-LED routing (the LED is GP0 on the CYW43, not GP25 on the RP2040 — this is the gotcha that catches every Week 11 student).

### RP2040 datasheet (PIO section, for the curious)

- **PDF:** <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf> (637 pages)
- **§3 PIO** (pp. 308–355). 47 pages on the PIO state machines. The CYW43 driver uses PIO SM 1 with a 9-instruction program (`cyw43_bus_pio_spi.pio` in the SDK) that implements the half-duplex SPI variant the CYW43 chip uses. You do not need to write PIO code this week, but skimming this section makes the cyw43 driver source readable.

### CYW43439 datasheet (Infineon, not free in full but the public abridged version is)

- **Public summary:** <https://www.infineon.com/cms/en/product/wireless-connectivity/airoc-wi-fi-plus-bluetooth-combos/wi-fi-4-802.11n/cyw43439/>

The full register-level datasheet is NDA-only. For our purposes the public summary plus the pico-sdk's `cyw43-driver` source (BSD-3-Clause, ~5000 LOC at <https://github.com/georgerobotics/cyw43-driver>) is enough.

---

## lwIP documentation

### lwIP 2.2.0 raw API docs

- **Root:** <https://www.nongnu.org/lwip/2_2_x/index.html>
- **Raw API:** <https://www.nongnu.org/lwip/2_2_x/raw_api.html>
- **TCP module:** <https://www.nongnu.org/lwip/2_2_x/group__tcp__raw.html>
- **DNS module:** <https://www.nongnu.org/lwip/2_2_x/group__dns.html>

The raw API page is one screen of prose followed by tables of every function and callback. Print it; it lives next to your monitor for the week.

### The lwIP Wiki

- <https://lwip.fandom.com/wiki/LwIP_Wiki>

Community-maintained. The "Common pitfalls" page (<https://lwip.fandom.com/wiki/LwIP_Application_Developers_Manual>) is required reading; pay particular attention to the "pbuf reference counting" section and the "do not block in callbacks" section. Both are in the mini-project's grading rubric.

### The lwIP source (read these files)

In the pico-sdk tree at `lib/lwip/src/`:

- `core/tcp.c` (~2200 LOC). The TCP state machine. Read the comments at the top of the file; the comments in `tcp_input` (pp. ~800 LOC) document the state transitions per incoming segment.
- `core/dns.c` (~1800 LOC). The DNS resolver. The `dns_gethostbyname_addrtype` entry point is at the top; the response-parsing logic is at the bottom.
- `include/lwip/tcp.h`. Every TCP raw-API function declared with a one-line comment. Faster to grep than the docs site.

---

## mbedTLS documentation

### mbedTLS knowledge base

- **Root:** <https://mbed-tls.readthedocs.io/>
- **Configuration guide:** <https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how-do-i-configure-mbedtls/>
- **Compile-time options:** <https://mbed-tls.readthedocs.io/en/latest/kb/compiling-and-building/how-do-i-tune-the-default-configuration/>
- **Cipher-suite cheat sheet:** <https://mbed-tls.readthedocs.io/en/latest/kb/development/cipher-list/>

The configuration guide is *the* reference for trimming mbedTLS down to the embedded footprint. Our mini-project's `mbedtls_config.h` is derived from the example "minimum TLS client" config in that document, with the cipher list pinned to one suite.

### mbedTLS source headers

In the pico-sdk tree at `lib/mbedtls/include/mbedtls/`:

- `ssl.h`. The high-level TLS API. `mbedtls_ssl_init`, `mbedtls_ssl_setup`, `mbedtls_ssl_set_bio`, `mbedtls_ssl_handshake`, `mbedtls_ssl_read`, `mbedtls_ssl_write`, `mbedtls_ssl_close_notify`. The 12 functions you call from `tls.c`.
- `x509_crt.h`. Certificate parsing. `mbedtls_x509_crt_parse` (parses a PEM blob), `mbedtls_x509_crt_free`, `mbedtls_ssl_conf_ca_chain` (tells the SSL config to use this chain as the trust anchor).
- `ctr_drbg.h`. The deterministic random number generator. Seeded from `mbedtls_entropy_func` which on the RP2040 sources entropy from the ROSC ring oscillator's jitter.
- `entropy.h`. The entropy collector. The pico-sdk's `pico_mbedtls` target provides a hardware-entropy source via the ROSC random output register at `ROSC->RANDOMBIT` (RP2040 datasheet §2.18.3, pp. 246–247).

### The `mbedtls_ssl_conf_*` setter family

- `mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED)`. Reject any server whose certificate does not chain to our pinned CA.
- `mbedtls_ssl_conf_ca_chain(conf, &ca, NULL)`. Pin the ISRG Root X1.
- `mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, &drbg)`. Wire the RNG to the handshake's random-bytes generator.
- `mbedtls_ssl_conf_min_version(conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3)`. Pin TLS 1.2 minimum.
- `mbedtls_ssl_conf_max_version(conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3)`. Pin TLS 1.2 maximum.

---

## The test broker: test.mosquitto.org

- **Site:** <https://test.mosquitto.org/>
- **TLS certificates and CAs:** <https://test.mosquitto.org/ssl/>
- **The mosquitto project:** <https://mosquitto.org/>

Operated by Roger Light, who is the original author of Mosquitto and an active contributor to the OASIS MQTT TC. The broker has been running continuously since ~2013 and accepts:

| Port  | Transport          | Authentication      |
|------:|--------------------|---------------------|
| 1883  | TCP, unencrypted   | None                |
| 8883  | TLS 1.2 / 1.3      | None                |
| 8884  | TLS                | Client certificate  |
| 8885  | TLS                | Username + password |
| 8886  | TLS 1.2 / 1.3      | None (alt port)     |

We use **port 8883** (TLS, no auth). The broker's leaf cert at the time of writing is signed by Let's Encrypt's R3 intermediate, which is signed by ISRG Root X1. Roger Light publishes the current CA at the `/ssl/` URL above; that PEM is what we embed in `ca_bundle.h`. **If the chain rotates** (Let's Encrypt is known to rotate intermediates; the most recent shift was R3 → R10/R11 in 2024-Q1), you may need to update the PEM. Challenge 2 walks you through this exact scenario.

**Rate limits and abuse policy.** Roger Light has not published an SLA. Practical observation: a single client publishing at 10 Hz is fine, 100 Hz draws moderator attention, 1000 Hz gets your IP banned. Do not run load tests against this broker — install your own local Mosquitto for that.

---

## Local Mosquitto setup (the offline alternative)

```bash
# macOS
brew install mosquitto
brew services start mosquitto
# /opt/homebrew/etc/mosquitto/mosquitto.conf
# add: listener 1883
# add: allow_anonymous true

# Linux (Debian/Ubuntu)
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
# /etc/mosquitto/mosquitto.conf
# add: listener 1883
# add: allow_anonymous true
```

This gives you `mqtt://192.168.1.x:1883` on your LAN. For TLS-on-localhost, follow the "Generate a TLS certificate" tutorial at <https://mosquitto.org/man/mosquitto-tls-7.html> (uses openssl, ~30 minutes the first time). The mini-project's `WIFI_BROKER_HOST` is a `#define` so you can flip between `"test.mosquitto.org"` and `"192.168.1.50"` as you bench-test.

---

## Reference embedded MQTT clients (read these)

We write our own MQTT 5 client in the mini-project, but reading two existing implementations first makes the wire format click.

### Eclipse Paho MQTT 3.1.1 embedded-C client

- **Repo:** <https://github.com/eclipse/paho.mqtt.embedded-c> (EPL-2.0, ~3000 LOC)
- **The `MQTTClient` API:** `MQTTClient/src/MQTTClient.h`

This is the canonical embedded MQTT client. It is MQTT 3.1.1 only (the Paho project has not published an MQTT 5 embedded-C client as of mid-2026). The serialization layer (`MQTTPacket/src/MQTTPacket.c` and the `MQTTSerializePublish.c` family) is what we crib for the wire format; the 3.1.1 PUBLISH packet is byte-compatible with the MQTT 5 PUBLISH minus the Properties field, so the encoder pattern transfers cleanly.

### The `umqtt.simple` Python reference (the simplest one you can read)

- **Repo:** <https://github.com/micropython/micropython-lib/blob/master/micropython/umqtt.simple/umqtt/simple.py>
- **License:** MIT
- **Size:** ~200 LOC

The MicroPython project's MQTT 3.1.1 client. Two hundred lines of pure Python implements the entire protocol minus QoS 2. Read this first; the structure (one method per packet type) is the structure you will recreate in C.

### lwIP's built-in MQTT 3.1.1 client (vendored, but we ignore it)

- **Path in pico-sdk:** `lib/lwip/src/apps/mqtt/mqtt.c` (~1100 LOC)
- **Docs:** <https://www.nongnu.org/lwip/2_2_x/group__mqtt.html>

The lwIP stack vendors a small MQTT 3.1.1 client. It is 3.1.1 only, it does not integrate with mbedTLS by default (you have to write a custom `mqtt_set_inpub_callback`-adjacent shim), and its API is callback-driven in a way that does not compose well with mbedTLS's blocking handshake. We do not use it in the mini-project, but it is the official "if you want MQTT and you are on lwIP" answer and you should know it exists.

---

## TLS deep-dive references

### bear-ssl (a smaller TLS implementation)

- **Site:** <https://www.bearssl.org/>
- **License:** MIT
- **Size:** ~50 KB of `.text` on Cortex-M0 vs mbedTLS's ~95 KB

Thomas Pornin's TLS-only library. Smaller than mbedTLS, no PKI/X.509 parsing in the default build (you supply DER-parsed certs), explicit "make the user choose every cipher suite" philosophy. We do not use it because the pico-sdk vendors mbedTLS not bear-ssl, but if you ever need to trim TLS below 95 KB this is the alternative.

### The TLS 1.3 visual handshake

- <https://tls13.xargs.org/>

Michael Driscoll's byte-by-byte annotated TLS 1.3 handshake. Color-coded, with every record's bytes shown alongside what they decode to. The matching TLS 1.2 page at <https://tls12.xargs.org/> is the one you want for this week. Read either page once; the spec becomes much less abstract.

### Wireshark's TLS dissector docs

- <https://wiki.wireshark.org/TLS>

How to enable TLS dissection in Wireshark, including importing a pre-master secret log file from mbedTLS via the `MBEDTLS_SSL_KEY_LOG_FILE` callback. If you turn this on in the mini-project's debug build, Wireshark decodes the encrypted application data and you can see your MQTT PUBLISH packets in cleartext on the wire — a useful sanity check, *deeply unsafe in production*. Make sure the macro is `#undef`-d before you flash the deployed image.

---

## Backoff and reliability references

### "Exponential Backoff And Jitter" (Marc Brooker, AWS Architecture Blog)

- <https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/>

The canonical 2015 post on why a simple `delay *= 2` is insufficient for a fleet of clients and you need uniform-random jitter. Three pages, three graphs; reread it before writing the backoff code in Friday's mini-project session. The "Full Jitter" algorithm (delay = `random(0, base * 2^attempt)`) is what we implement in `retry_backoff.c`.

### The MQTT-related sections of the IoT Architecture working group's "Reliable Messaging" pattern

- <https://www.cncf.io/projects/cloudevents/> (related; CloudEvents is the broader pattern)

We do not implement CloudEvents in the week-11 mini-project (it is one layer up the stack and adds ~200 bytes of envelope per message), but reading the spec gives you the vocabulary for "what does a structured event payload look like" when you scale this past one device.

---

## Tools you will install this week

```bash
# Mosquitto clients (host-side debugging)
brew install mosquitto        # macOS
sudo apt install mosquitto-clients  # Debian/Ubuntu

# Python bindings for paho-mqtt (the cc-listen.py subscriber)
python3 -m venv .venv && source .venv/bin/activate
pip install paho-mqtt==2.1.0

# Wireshark (for sniffing the TLS handshake on a sniffer-AP)
brew install --cask wireshark # macOS
sudo apt install wireshark    # Linux

# openssl (for inspecting the broker's certificate chain)
openssl s_client -showcerts -connect test.mosquitto.org:8883 < /dev/null
```

---

## Reading order

The week's reading load is moderate. The compressed order:

1. **Monday:** Pico W datasheet §4 (Wireless, pp. 14–17), pico-sdk networking docs landing page, then the `pico_cyw43_arch` doxygen section.
2. **Tuesday:** lwIP raw API docs (one page), then the "Common pitfalls" wiki page. Skim `tcp.h` in the pico-sdk's lwIP tree.
3. **Wednesday:** MQTT 5 spec §1.5.5 (VBI), §2.1 (fixed header), §3.1 (CONNECT), §3.3 (PUBLISH). Read tls12.xargs.org alongside.
4. **Thursday:** RFC 5246 §7.3 (TLS 1.2 handshake), mbedTLS configuration guide, the AWS exponential-backoff post.
5. **Friday:** test.mosquitto.org's `/ssl/` page (current CA), the lwIP MQTT app code (for one comparison reading), then start the mini-project's bring-up.

Total reading: ~6 hours across the week. The MQTT 5 spec is the densest. The pico-sdk networking docs are short but you will return to them constantly. RFC 5246 you skim once and then trust mbedTLS for the rest of your career.

Ship it.
