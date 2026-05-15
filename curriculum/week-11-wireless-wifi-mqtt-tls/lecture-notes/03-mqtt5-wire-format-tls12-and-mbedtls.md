# Lecture 3 — MQTT 5 Wire Format, TLS 1.2, and Trimming mbedTLS

> *MQTT was designed in 1999 by Andy Stanford-Clark at IBM for SCADA over satellite links: a protocol that survives 30-second latency, 1200-baud links, and link drops every few minutes. Twenty-five years later that design choice is why it has eaten the IoT world. The protocol is 167 pages in spec form, but the publish-only subset is genuinely small: four packet types, one variable-length integer, fifteen optional properties, no out-of-order delivery, no flow control beyond TCP's. We can hand-roll a publisher in 600 lines of C and have it interoperate with every broker on Earth. The TLS layer below it is the opposite — 100+ pages of RFC 5246 plus 160 of RFC 8446 — but we delegate that entire mountain to mbedTLS and only have to configure which subset to compile.*

## The MQTT 5 packet layout

Every MQTT packet on the wire has three sections, in order:

1. **Fixed header** (1 byte type+flags, 1–4 byte Variable Byte Integer for Remaining Length). 2–5 bytes total.
2. **Variable header** (per-packet-type fields; e.g. Packet Identifier, Properties).
3. **Payload** (per-packet-type bytes; e.g. PUBLISH's actual application bytes).

The fixed header's first byte:

```
+--+--+--+--+--+--+--+--+
|7 |6 |5 |4 |3 |2 |1 |0 |
+-----------+-----------+
|Packet Type|   Flags   |
+-----------+-----------+
```

The 4-bit Packet Type is one of 15 values:

| Value | Name        | Direction       | Notes                                   |
|------:|-------------|-----------------|-----------------------------------------|
| 1     | CONNECT     | Client → Server | First packet on the connection          |
| 2     | CONNACK     | Server → Client | Reply to CONNECT                        |
| 3     | PUBLISH     | Both            | Application message                     |
| 4     | PUBACK      | Both            | QoS 1 acknowledgement                   |
| 5     | PUBREC      | Both            | QoS 2 part 1                            |
| 6     | PUBREL      | Both            | QoS 2 part 2                            |
| 7     | PUBCOMP     | Both            | QoS 2 part 3                            |
| 8     | SUBSCRIBE   | Client → Server | Subscribe to topic filter(s)            |
| 9     | SUBACK      | Server → Client | Subscribe acknowledgement               |
| 10    | UNSUBSCRIBE | Client → Server | Unsubscribe                             |
| 11    | UNSUBACK    | Server → Client | Unsubscribe acknowledgement             |
| 12    | PINGREQ     | Client → Server | Keepalive ping                          |
| 13    | PINGRESP    | Server → Client | Keepalive pong                          |
| 14    | DISCONNECT  | Both            | Graceful close                          |
| 15    | AUTH        | Both            | MQTT 5 extended-auth handshake          |

The 4-bit Flags field is "Reserved" (must be zero on send, reject if non-zero on receive) for every packet type except:

- PUBLISH: bits 3=DUP, 2:1=QoS (00/01/10), 0=RETAIN
- PUBREL, SUBSCRIBE, UNSUBSCRIBE: bits = `0010` (literal 0x2)

So a CONNECT's first byte is `0x10`; a PUBLISH at QoS 0 is `0x30`; a SUBSCRIBE is `0x82`; a PINGREQ is `0xC0`. Memorize these four.

## The Variable Byte Integer (VBI)

The Remaining Length field is a Variable Byte Integer, defined in MQTT 5 §1.5.5 (p. 16):

> The Variable Byte Integer is encoded using an encoding scheme which uses a single byte for values up to 127. Larger values are handled as follows. The least significant seven bits of each byte encode the data, and the most significant bit is used to indicate whether there are bytes following in the representation.

In practice:

| Decimal value | Bytes | Encoded                  |
|--------------:|------:|--------------------------|
| 0             | 1     | `0x00`                   |
| 127           | 1     | `0x7F`                   |
| 128           | 2     | `0x80 0x01`              |
| 16383         | 2     | `0xFF 0x7F`              |
| 16384         | 3     | `0x80 0x80 0x01`         |
| 2097151       | 3     | `0xFF 0xFF 0x7F`         |
| 2097152       | 4     | `0x80 0x80 0x80 0x01`    |
| 268435455     | 4     | `0xFF 0xFF 0xFF 0x7F`    |

A VBI is **never** more than 4 bytes; the maximum encodable value is `268435455` (256 MB minus 1). The encoder:

```c
typedef struct {
    uint8_t bytes[4];
    uint8_t count;
} mqtt_vbi_t;

mqtt_vbi_t mqtt_vbi_encode(uint32_t value) {
    mqtt_vbi_t out = {0};
    do {
        uint8_t b = (uint8_t)(value & 0x7FU);
        value >>= 7U;
        if (value > 0U) {
            b |= (uint8_t) 0x80U;
        }
        out.bytes[out.count] = b;
        out.count++;
    } while (value > 0U && out.count < (uint8_t) 4U);
    return out;
}
```

The decoder:

```c
typedef struct {
    uint32_t value;
    uint8_t  count;
    int8_t   error;
} mqtt_vbi_decoded_t;

mqtt_vbi_decoded_t mqtt_vbi_decode(const uint8_t *buf, uint32_t buflen) {
    mqtt_vbi_decoded_t out = {0};
    uint32_t multiplier = 1U;
    uint8_t  index = 0U;
    while (index < (uint8_t) 4U && (uint32_t) index < buflen) {
        uint8_t b = buf[index];
        out.value += ((uint32_t) (b & 0x7FU)) * multiplier;
        index++;
        if ((b & 0x80U) == 0U) {
            out.count = index;
            return out;
        }
        multiplier *= 128U;
    }
    out.error = -1; /* malformed: 4 bytes seen without continuation-bit clear */
    return out;
}
```

Off-by-one in `multiplier` and off-by-one in `index` are the two perennial VBI bugs. Test vectors from the spec (§1.5.5, Table 1-1):

```
0       → 00
127     → 7F
128     → 80 01
16383   → FF 7F
16384   → 80 80 01
2097151 → FF FF 7F
```

Round-trip every value in this table before you trust your encoder. Exercise 3 walks through this.

## The CONNECT packet

The CONNECT variable header (MQTT 5 §3.1.2, pp. 39–48):

```
+-----------------------------+
| Protocol Name "MQTT"        | 6 bytes: 0x00 0x04 'M' 'Q' 'T' 'T'
+-----------------------------+
| Protocol Level 5            | 1 byte:  0x05
+-----------------------------+
| Connect Flags               | 1 byte:  bit-mapped
+-----------------------------+
| Keep Alive                  | 2 bytes (big-endian seconds)
+-----------------------------+
| Properties Length (VBI)     | 1–4 bytes
+-----------------------------+
| Properties                  | Properties Length bytes
+-----------------------------+
```

The Connect Flags byte:

| Bit | Name              | Effect when set                                     |
|----:|-------------------|-----------------------------------------------------|
| 7   | User Name Flag    | Username field present in payload                   |
| 6   | Password Flag     | Password field present in payload                   |
| 5   | Will Retain       | If Will flag set, broker retains the will message   |
| 4:3 | Will QoS          | QoS for the will message (00, 01, 10)               |
| 2   | Will Flag         | Will message fields present in payload              |
| 1   | Clean Start       | Server should discard any existing session for this client-id |
| 0   | Reserved          | Must be 0                                           |

The payload of a CONNECT (in this exact order, present iff the relevant flag is set):

1. Client Identifier (always present; UTF-8 string up to 23 chars per spec, longer allowed by 5.0)
2. Will Properties + Will Topic + Will Payload (if Will Flag)
3. User Name (if User Name Flag)
4. Password (if Password Flag)

UTF-8 strings on the wire are length-prefixed: 2 bytes big-endian length, then that many bytes.

A minimal CONNECT for a no-auth, no-will publisher with client-id "cc7-pico-0001" and a 60-second keep-alive:

```
Fixed header:
  10 16
  (0x10 = CONNECT, 0x16 = 22 bytes remaining length)

Variable header:
  00 04 4D 51 54 54         ; "MQTT" length-prefixed
  05                        ; Protocol Level
  02                        ; Connect Flags = clean-start
  00 3C                     ; Keep Alive 60 s
  00                        ; Properties Length 0

Payload:
  00 0D 63 63 37 2D 70 69 63 6F 2D 30 30 30 31
  ; Client ID "cc7-pico-0001", 13 chars
```

Total: 2 + 22 = 24 bytes on the wire. The CONNACK from the broker is typically `20 03 00 00 00` (CONNACK with 3-byte remaining length; acknowledge flags = 0; Reason Code 0 = Success; Properties Length 0).

## The PUBLISH packet

PUBLISH at QoS 0 (the simplest case; MQTT 5 §3.3, pp. 64–78):

```
Fixed header:
  30 <vbi remaining-length>
  (0x30 = PUBLISH, QoS 0, DUP 0, RETAIN 0)

Variable header:
  <utf8 topic name>
  <vbi properties-length>
  <properties bytes>

Payload:
  <application bytes — opaque to MQTT>
```

For QoS > 0 a Packet Identifier (2 bytes) follows the topic name. We do not use QoS 1 or QoS 2 in the mini-project's publish path; the application can tolerate a small dropped-publish rate (the 99% delivery target leaves a 1% budget), and QoS 1 doubles the broker traffic per message (PUBLISH + PUBACK).

A minimal PUBLISH for topic `cc7/devices/0001/telemetry` with payload `{"temp_c":24.7,"vbat_v":3.28,"uptime_s":140}`:

```
Topic length 26 bytes:
  00 1A 63 63 37 2F 64 65 76 69 63 65 73 2F 30 30 30 31 2F 74 65 6C 65 6D 65 74 72 79

Properties Length 0:
  00

Payload "...":  41 bytes
  7B 22 74 65 6D 70 5F 63 22 3A 32 34 2E 37 ...
```

Variable header + payload total: 26 + 2 + 1 + 41 = 70 bytes. Add the fixed header (1 byte type + 1-byte VBI of 69) = 72 bytes on the wire per publish. At 10 s cadence that is 7.2 bytes/s of MQTT payload plus TCP/TLS/802.11 overhead — call it ~120 bytes/s total on the air. Negligible.

## CONNACK Reason Codes

The CONNACK's third byte is the Reason Code (MQTT 5 §3.2.2.2, pp. 56–58). The codes worth knowing:

| Code | Name                              | What it means                                              |
|-----:|-----------------------------------|------------------------------------------------------------|
| 0    | Success                           | Connection accepted                                        |
| 128  | Unspecified error                 | Broker says no, no specific reason                         |
| 130  | Protocol Error                    | Malformed CONNECT bytes                                    |
| 132  | Unsupported Protocol Version      | We sent Protocol Level 5; broker only supports 3 or 4      |
| 133  | Client Identifier not valid       | Client-id length or characters rejected                    |
| 134  | Bad User Name or Password         | Self-explanatory                                           |
| 135  | Not authorized                    | Auth succeeded but the user has no permissions             |
| 136  | Server unavailable                | Broker is starting up or going down                        |
| 137  | Server busy                       | Try again later (broker overloaded)                        |
| 138  | Banned                            | This client-id or IP is banned                             |
| 142  | Use another server                | Broker redirect (rare; mostly for cluster failover)        |
| 144  | Topic Name invalid                | Will topic has wildcards or invalid chars                  |
| 159  | Connection rate exceeded          | Slow down                                                  |

On a non-zero Reason Code, log it, back off according to the kind (rate-exceeded → longer backoff; not-authorized → never retry), and either retry or give up. The mini-project's retry-backoff state machine treats codes 136/137/159 as "retry with exponential backoff" and codes 133/134/135/138 as "fatal; do not retry."

## The TLS 1.2 handshake walkthrough

When you `mbedtls_ssl_handshake()` for the first time after connecting the TCP socket, mbedTLS drives the following exchange:

```
client                                           server
  |                                                 |
  | ---- ClientHello -------------------------->    |
  |      version=1.2, random, suites,               |
  |      ext: server_name="test.mosquitto.org",     |
  |      ext: supported_groups=[secp256r1],         |
  |      ext: signature_algorithms=[rsa_pkcs1_sha256] |
  |                                                 |
  |    <----- ServerHello -----------------------   |
  |           version=1.2, random, suite chosen,    |
  |           ext: server_name=ack                  |
  |    <----- Certificate -----------------------   |
  |           [leaf cert, intermediate R3]          |
  |    <----- ServerKeyExchange -----------------   |
  |           ECDH public_key on secp256r1,         |
  |           signed by leaf's RSA private key      |
  |    <----- ServerHelloDone -------------------   |
  |                                                 |
  | ---- ClientKeyExchange --------------------->   |
  |      ECDH public_key on secp256r1               |
  | ---- ChangeCipherSpec ---------------------->   |
  | ---- Finished ------------------------------>   |
  |      [encrypted with derived keys]              |
  |                                                 |
  |    <----- ChangeCipherSpec ------------------   |
  |    <----- Finished --------------------------   |
  |           [encrypted]                           |
  |                                                 |
  | ---- Application Data ---------------------->   |
  |      [encrypted MQTT bytes]                     |
```

Two round-trips, ~1.2 seconds on the Pico W's hardware. Once Finished is exchanged, both sides have derived:

```
pre_master_secret = ECDH(client_priv * server_pub)
master_secret     = PRF(pre_master_secret, "master secret",
                        client_random || server_random)[0..48]
key_block         = PRF(master_secret, "key expansion",
                        server_random || client_random)
client_write_key, server_write_key, client_iv, server_iv = split(key_block)
```

`PRF` for TLS 1.2 with our cipher suite is `P_SHA256` (RFC 5246 §5, pp. 14–15). The derived keys are 16 bytes each (AES-128); the IVs are the GCM nonces. From this point onward, every TLS record is `AES-128-GCM(plaintext, nonce, additional_data)` with the nonce being `iv || sequence_number` (4 bytes implicit + 8 bytes counter).

## mbedTLS API minimum surface

To run a TLS 1.2 client you call exactly 12 mbedTLS functions:

```c
mbedtls_ssl_init(&ssl);
mbedtls_ssl_config_init(&conf);
mbedtls_x509_crt_init(&cacert);
mbedtls_ctr_drbg_init(&drbg);
mbedtls_entropy_init(&entropy);

mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0U);

mbedtls_x509_crt_parse(&cacert, (const unsigned char *) ISRG_X1_PEM,
                       sizeof ISRG_X1_PEM);

mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                            MBEDTLS_SSL_TRANSPORT_STREAM,
                            MBEDTLS_SSL_PRESET_DEFAULT);
mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);

mbedtls_ssl_setup(&ssl, &conf);
mbedtls_ssl_set_hostname(&ssl, "test.mosquitto.org");
mbedtls_ssl_set_bio(&ssl, &my_ctx, my_send, my_recv, NULL);

while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
        ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        /* fatal handshake error */
        break;
    }
    cyw43_arch_poll();
}
```

The two callbacks you supply via `mbedtls_ssl_set_bio`:

```c
int my_send(void *ctx, const unsigned char *buf, size_t len);
int my_recv(void *ctx, unsigned char *buf, size_t len);
```

`my_send` writes bytes to the TCP pcb via `tcp_write` + `tcp_output`. `my_recv` reads from a buffer that the lwIP `recv_cb` has been filling. If `my_recv` has no bytes to give, it returns `MBEDTLS_ERR_SSL_WANT_READ`; mbedTLS will retry on the next `mbedtls_ssl_handshake` call. This is the integration seam between mbedTLS's blocking-ish API and lwIP's callback API.

## Trimming mbedTLS

The default mbedTLS build (no trimming) on Cortex-M0+ at `-Os`:

- `.text`: ~250 KB
- `.rodata`: ~30 KB (cipher tables, curve parameters)
- `.bss`: ~10 KB

That is too big. Our `mbedtls_config.h` removes:

- TLS 1.0, 1.1, 1.3, SSLv3 (we want only 1.2): `#undef MBEDTLS_SSL_PROTO_TLS1`, `MBEDTLS_SSL_PROTO_TLS1_1`, `MBEDTLS_SSL_PROTO_TLS1_3`, `MBEDTLS_SSL_PROTO_SSL3`.
- All cipher suites except ECDHE-RSA-AES128-GCM-SHA256: in the `mbedtls/ssl_ciphersuites.c` selection list, undef every `MBEDTLS_TLS_*` except the one we want.
- All curves except secp256r1: `#undef MBEDTLS_ECP_DP_SECP384R1_ENABLED`, `MBEDTLS_ECP_DP_SECP521R1_ENABLED`, etc.
- All key exchanges except ECDHE-RSA: `#undef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED`, `MBEDTLS_KEY_EXCHANGE_PSK_ENABLED`, etc.
- Client-cert support: `#undef MBEDTLS_X509_CSR_PARSE_C`, `MBEDTLS_SSL_KEYING_MATERIAL_EXPORT`.
- ALPN, SNI extension parsing on server side, session tickets, renegotiation: `#undef MBEDTLS_SSL_ALPN`, etc.

The result is ~95 KB of `.text` and ~6 KB of `.bss`. Cite the mbedTLS configuration guide at <https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how-do-i-configure-mbedtls/>.

## Pinning the CA

We do not have a system CA bundle. We have one broker (test.mosquitto.org) whose certificate currently chains to one root (ISRG Root X1). We embed that root's PEM directly:

```c
static const char ISRG_X1_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
    /* ... ~30 lines of base64 ... */
    "-----END CERTIFICATE-----\n";
```

Then `mbedtls_x509_crt_parse(&cacert, (const unsigned char *) ISRG_X1_PEM, sizeof ISRG_X1_PEM)` and `mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL)`. The parser walks the chain delivered by the server in its Certificate message: leaf → R3 → expected to chain to a CA in our trust store. Because R3's issuer is ISRG Root X1, and we have ISRG Root X1 in our trust store, the verification succeeds.

**The rotation problem.** ISRG Root X1 expires in June 2030 (its `notAfter` is `2035-06-04` for the current cross-sign; the older self-sign is `2024-06-04` — already expired, which is why Let's Encrypt rolled the cross-sign). When a CA in your firmware expires, every TLS handshake fails immediately at `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`. The mitigation is **OTA-update before the CA expires**. Challenge 2 walks you through a forced rotation: change the PEM, reflash, observe success; revert the PEM, observe failure; document the runbook.

## The keep-alive math

MQTT's keep-alive (the 2-byte field in CONNECT, in seconds) tells the broker how long to wait between client packets before assuming the connection is dead. If keep-alive is `K` seconds, the client must send *some* packet (PUBLISH, SUBSCRIBE, PINGREQ — anything) at least every `K * 1.5` seconds (the broker's grace window per §3.1.2.10, p. 47). If `K` is 60 and we publish every 10 s, our PUBLISH packets keep the connection alive without ever needing PINGREQ. If our publish cadence were 120 s and `K` is 60, we would need a PINGREQ every ~50 s to stay alive.

The pattern in `main.c`:

```c
absolute_time_t next_publish    = make_timeout_time_ms(0U);
absolute_time_t next_keepalive  = make_timeout_time_ms(30000U);

for (;;) {
    cyw43_arch_poll();
    mqtt_pump(&client);

    if (time_reached(next_publish)) {
        publish_sensor_reading(&client);
        next_publish = make_timeout_time_ms(10000U);
        next_keepalive = make_timeout_time_ms(30000U);
    }
    if (time_reached(next_keepalive)) {
        mqtt_pingreq(&client);
        next_keepalive = make_timeout_time_ms(30000U);
    }

    absolute_time_t soonest = next_publish;
    if (absolute_time_diff_us(next_keepalive, soonest) > 0) {
        soonest = next_keepalive;
    }
    cyw43_arch_wait_for_work_until(soonest);
}
```

The keep-alive timer resets every time we send a publish, so the PINGREQ branch only fires when there is no publish traffic — which in our case is never. We keep the PINGREQ branch in the code for robustness (if you change the publish cadence to 120 s, the PINGREQ keeps the connection alive without code changes).

## Summary

MQTT 5 is a small protocol with a wire format that fits on one page: type+flags byte, VBI remaining length, variable header, payload. The four packet types we hand-encode this week are CONNECT, PUBLISH, PINGREQ, DISCONNECT. TLS 1.2 below it is a 100-page RFC that mbedTLS handles for us as long as we configure it to the right cipher list, pin the right CA, and supply a BIO callback pair that bridges to lwIP's raw TCP API. Tomorrow you start the mini-project; the encoder you write in Exercise 3 today is the encoder you ship.
