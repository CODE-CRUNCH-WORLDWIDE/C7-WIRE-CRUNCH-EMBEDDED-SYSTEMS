# Lecture 2 — lwIP's Raw API and the Life of a pbuf

> *Most TCP/IP stacks you have used were socket-based. You opened a socket with `socket()`, you blocked on `recv()`, the kernel handed you bytes, and memory was somebody else's problem. lwIP's raw API is the opposite contract: every operation is a registration, every byte that arrives fires a callback, every buffer you receive you must return, and "block on recv" does not exist as a concept. The shift in mental model is the largest single hurdle in the week. The reward, once it clicks, is that an entire TCP/IP stack fits in 8 KB of SRAM with no kernel threads, no preemption, no signal handlers, no mutex contention — which is exactly what running on a 264 KB SoC requires.*

The pico-sdk vendors lwIP 2.2.0 (the May 2024 release of Adam Dunkels' lightweight IP stack, originally written at SICS in 2001, now maintained at <https://savannah.nongnu.org/projects/lwip/>). It is BSD-3-Clause, ~50,000 lines of C, supports IPv4 + IPv6 + DHCP + DNS + ICMP + TCP + UDP, and has been the embedded TCP/IP default for two decades because it does the right thing at the right footprint. The total stack memory at runtime on our build is ~14 KB of SRAM (pbuf pool + TCP PCB pool + DHCP state) and ~25 KB of `.text`. That is the bargain.

## Three APIs, one stack

lwIP exposes three APIs into the same underlying stack:

- **Sockets API** (`<lwip/sockets.h>`): a BSD-sockets-compatible wrapper that gives you `socket`, `bind`, `listen`, `accept`, `recv`, `send`, etc. Familiar; requires a thread to block on `recv`; requires the multi-threading config (`LWIP_NETCONN_SEM_PER_THREAD`, `sys_arch_*` primitives). Adds ~10 KB of `.text` for the wrapper. Not exposed by the pico-sdk's default build.
- **Netconn API** (`<lwip/api.h>`): a thread-safe sequential API that is one level closer to the raw stack — operations like `netconn_connect`, `netconn_recv` block on a mailbox until the underlying raw stack signals "I have a buffer for you." Requires threading. Not exposed by the pico-sdk's default build.
- **Raw API** (`<lwip/tcp.h>`, `<lwip/udp.h>`, `<lwip/dns.h>`): the callback-driven native API. Single-threaded. No blocking. Every operation registers a function pointer, and lwIP's tick context invokes that function pointer when there is work to do. This is what `pico_cyw43_arch_lwip_poll` exposes and the only API we use in the mini-project.

The raw API is more verbose for trivial uses (a TCP echo client is ~80 lines instead of ~20) and *less* verbose for non-trivial uses (you do not need any of the threading scaffolding the netconn API requires). Once you have written one client against the raw API, the next ten clients are mechanical.

## The lwIP tick context

lwIP has no internal threads. Everything happens in one of two contexts:

1. **Tick context**, which is whatever calls `cyw43_arch_poll()` (in the `_poll` variant) or the SYS_TICK interrupt handler (in the `_threadsafe_background` variant). This context services incoming packets from the CYW43, advances the TCP retransmission timers, fires DNS-response callbacks, and so on.
2. **Application context**, which is your `main()` calling `tcp_connect`, `tcp_write`, etc. directly.

The single-threaded rule: in the `_poll` variant, tick context and application context are the *same* thread; you cannot have a race condition because there is no concurrency. In the `_threadsafe_background` variant they are different (foreground vs SYS_TICK background) and you must use `cyw43_arch_lwip_begin()` / `_end()` to bracket every raw-API call from foreground. This week we use `_poll` exclusively.

The rule of thumb when reading lwIP code: every callback you write (`connected_cb`, `recv_cb`, `sent_cb`, `err_cb`, `dns_found_cb`) runs in tick context. Every function you call directly (`tcp_new`, `tcp_connect`, `tcp_write`) is fine from either application or tick context as long as you are single-threaded.

## DNS resolution

To turn `test.mosquitto.org` into an IPv4 address you call `dns_gethostbyname`:

```c
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

static ip_addr_t broker_ip;
static volatile int dns_state = 0; /* 0 pending, 1 ok, 2 fail */

static void dns_found_cb(const char *name, const ip_addr_t *ip, void *arg) {
    (void) name;
    (void) arg;
    if (ip != NULL) {
        broker_ip = *ip;
        dns_state = 1;
    } else {
        dns_state = 2;
    }
}

int kick_dns(void) {
    err_t rc = dns_gethostbyname(
        "test.mosquitto.org", &broker_ip, dns_found_cb, NULL);
    if (rc == ERR_OK) {
        /* The name was cached; broker_ip is already filled in. */
        dns_state = 1;
        return 0;
    }
    if (rc == ERR_INPROGRESS) {
        /* A DNS query was issued; dns_found_cb will fire later. */
        return 0;
    }
    return -1;
}
```

Three return codes from `dns_gethostbyname`:

- `ERR_OK` (0): the answer was in the cache; `broker_ip` is filled and the callback will *not* fire.
- `ERR_INPROGRESS` (-5): a DNS query was issued; you wait for the callback.
- Any other negative: an immediate failure (typically `ERR_VAL` for a malformed name).

The DNS resolver in lwIP is in `core/dns.c` (~1800 LOC). It uses UDP to query the DNS server learned from DHCP (the AP's IP); it caches up to `DNS_TABLE_SIZE` entries (default 4) with a TTL drawn from the response; it retries up to `DNS_MAX_RETRIES` times (default 4) at increasing intervals (1 s, 2 s, 4 s, 8 s). If the configured DNS server does not respond, the resolution times out after ~15 s and your callback fires with `ip == NULL`.

A bug worth noticing: `dns_gethostbyname` accepts a *pointer* to the result buffer. If your `&broker_ip` goes out of scope before the callback fires (e.g. you put it on the stack of a function that returns immediately after the `ERR_INPROGRESS`), lwIP will write into freed memory and you will see crashes that bisect to "DNS resolution succeeded but the IP is garbage." Always make the result buffer a file-scope static or a heap allocation that outlives the request.

## TCP raw API

The TCP raw API has six core functions plus four callbacks:

```c
struct tcp_pcb *tcp_new(void);
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port);
err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port,
                  tcp_connected_fn connected);
err_t tcp_write(struct tcp_pcb *pcb, const void *arg, u16_t len, u8_t apiflags);
err_t tcp_output(struct tcp_pcb *pcb);
void  tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn recv);
void  tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn sent);
void  tcp_err(struct tcp_pcb *pcb, tcp_err_fn err);
err_t tcp_close(struct tcp_pcb *pcb);
```

Callback signatures (from `<lwip/tcp.h>`):

```c
typedef err_t (*tcp_connected_fn)(void *arg, struct tcp_pcb *pcb, err_t err);
typedef err_t (*tcp_recv_fn)     (void *arg, struct tcp_pcb *pcb,
                                  struct pbuf *p, err_t err);
typedef err_t (*tcp_sent_fn)     (void *arg, struct tcp_pcb *pcb, u16_t len);
typedef void  (*tcp_err_fn)      (void *arg, err_t err);
```

The lifecycle of a client connection:

1. Application calls `tcp_new()`. lwIP allocates a `tcp_pcb` (TCP Protocol Control Block, ~150 bytes) from a fixed pool (`MEMP_NUM_TCP_PCB`, default 5) and returns it. If the pool is exhausted, returns NULL — handle this.
2. Application calls `tcp_arg(pcb, my_state)`, attaching an application pointer that will be passed to every callback as the first argument.
3. Application calls `tcp_recv(pcb, recv_cb)`, `tcp_sent(pcb, sent_cb)`, `tcp_err(pcb, err_cb)`, registering callback functions.
4. Application calls `tcp_connect(pcb, &broker_ip, 8883, connected_cb)`. lwIP sends a SYN; returns `ERR_OK` if the SYN was queued, an error otherwise.
5. Application returns to the main loop, which keeps polling.
6. When the SYN-ACK arrives from the remote, lwIP's tick context invokes `connected_cb(arg, pcb, ERR_OK)`. The connection is established.
7. Application (typically in connected_cb or shortly after) calls `tcp_write(pcb, buf, len, TCP_WRITE_FLAG_COPY)` to queue outbound data, then `tcp_output(pcb)` to push it onto the wire. `tcp_write` returns `ERR_MEM` if the send buffer is full — back off and retry later.
8. When the remote sends data, lwIP delivers it to `recv_cb(arg, pcb, pbuf, ERR_OK)`. The application reads from the pbuf, then calls `tcp_recved(pcb, len)` to advance the receive window and `pbuf_free(pbuf)` to release the buffer.
9. Eventually one side wants to close. The application calls `tcp_close(pcb)`. lwIP sends a FIN, waits for the peer's FIN-ACK, transitions through `TIME_WAIT`, and eventually frees the pcb. After `tcp_close`, the pcb pointer is *invalid* — do not call any more functions on it.

The error callback `err_cb` is the special case: it fires when lwIP detected a fatal error (peer RST, route disappeared, connection reset, etc.) and *the pcb has already been freed* before err_cb runs. Do not call `tcp_close` from err_cb; do not touch the pcb in err_cb beyond logging the error.

## The pbuf chain

A `pbuf` is lwIP's network-buffer struct (`<lwip/pbuf.h>`):

```c
struct pbuf {
    struct pbuf *next;     /* chain to next pbuf if data spans multiple */
    void *payload;         /* the bytes */
    u16_t tot_len;         /* total bytes in this chain */
    u16_t len;             /* bytes in this pbuf only */
    u8_t  type_internal;   /* PBUF_POOL | PBUF_RAM | PBUF_ROM | PBUF_REF */
    u8_t  flags;
    LWIP_PBUF_REF_T ref;   /* reference count */
    u8_t  if_idx;
};
```

A single TCP segment is typically one pbuf but can be a chain of several pbufs whose `next` pointers link them together. To walk the data:

```c
for (struct pbuf *q = p; q != NULL; q = q->next) {
    process_bytes(q->payload, q->len);
}
```

The `tot_len` field of the head pbuf is the total bytes across the chain; subsequent pbufs' `tot_len` decreases monotonically (each pbuf's `tot_len` is "bytes from this pbuf onward").

### The reference counting rule

Every pbuf has a reference count. `pbuf_alloc` initializes it to 1. `pbuf_ref` increments it. `pbuf_free` decrements it; if it hits zero, the pbuf is returned to the pool. **Every pbuf you receive in a callback you must `pbuf_free` exactly once.** If you forget, the pbuf pool drains over time and lwIP starts dropping incoming packets ("pbuf pool exhausted" is what you will see in the lwIP log at `LWIP_DBG_ON`). If you free twice, you corrupt the pool and the next allocation returns a half-recycled pbuf with garbage payload.

The pattern in `recv_cb`:

```c
static err_t recv_cb(void *arg, struct tcp_pcb *pcb,
                     struct pbuf *p, err_t err) {
    if (p == NULL) {
        /* Peer closed the connection. */
        tcp_close(pcb);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    /* Consume the bytes. */
    consume_application_data(arg, p);

    /* Tell lwIP we have consumed `p->tot_len` bytes; advance window. */
    tcp_recved(pcb, p->tot_len);

    /* Release the pbuf back to the pool. */
    pbuf_free(p);

    return ERR_OK;
}
```

The `p == NULL` case is the FIN signal: the peer half-closed. You close your side with `tcp_close(pcb)` and *do not* free a NULL pbuf.

### When pbuf-leak bugs strike

The five canonical pbuf-leak bugs you will hit at some point in this week or next:

1. **Forgot the free in the error branch.** `if (err != ERR_OK) { return err; }` — leaks the pbuf. Fix: free before returning.
2. **Freed twice in the consume function.** `consume_application_data` calls `pbuf_free(p)` internally, then the outer recv_cb calls `pbuf_free(p)` again. Fix: pick one owner, document it.
3. **Stored the pbuf for later.** `static struct pbuf *queued; queued = p;` — and never freed it because nobody walked the queue. Fix: when you queue, `pbuf_ref(p)` first; when you dequeue, free.
4. **Walked the chain incorrectly.** Treated `p->len` as "total bytes" and missed the chained pbufs. Fix: always loop until `q == NULL`, use `q->len` per pbuf, sum to `p->tot_len`.
5. **Called `tcp_recved` after `pbuf_free`.** After `pbuf_free`, the pbuf's `tot_len` field is undefined. Fix: capture the length in a local variable before freeing.

When in doubt, enable `LWIP_STATS_DISPLAY` in `lwipopts.h` and call `stats_display()` periodically; it prints the pbuf-pool high-water mark and the number of allocations. If "used" climbs monotonically across hours, you have a leak.

## TCP write buffering

`tcp_write(pcb, buf, len, TCP_WRITE_FLAG_COPY)` queues `len` bytes for transmission. The `_COPY` flag tells lwIP to copy `buf` into its own internal queue, so the caller's buffer can be freed/reused immediately. Without `_COPY`, lwIP holds a pointer to the caller's buffer and the caller must keep it valid until the bytes are acknowledged (signaled via the `sent_cb`). For the mini-project we always use `_COPY` — it costs a memcpy of at most 1460 bytes per packet, but it eliminates a class of "the buffer went out of scope before sent_cb fired" bugs.

After `tcp_write`, the bytes are *queued* in lwIP's internal send buffer but not yet on the wire. To flush, call `tcp_output(pcb)`. If you `tcp_write` ten times and `tcp_output` once, lwIP coalesces the writes into a single TCP segment up to the MSS (typically 1460 bytes) — efficient. If you `tcp_write` once with a 10 KB buffer, lwIP fragments it into MSS-sized segments and sends them as the window allows; you may see `ERR_MEM` if the send buffer is full.

The send buffer's size is controlled by `TCP_SND_BUF` in `lwipopts.h`. Default is 2 * MSS = 2920 bytes. For an MQTT publisher sending 96-byte messages every 10 s this is wildly more than needed; for a streaming bulk transfer you would raise it to 16 KB to keep the wire saturated through window scaling.

## A reference TCP echo client

The complete echo client (~80 lines):

```c
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int   connected;
    int   done;
    char  send_buf[33];
    char  recv_buf[33];
    int   recv_len;
} echo_state_t;

static err_t echo_recv_cb(void *arg, struct tcp_pcb *pcb,
                          struct pbuf *p, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;
    if (p == NULL) {
        s->done = 1;
        return tcp_close(pcb);
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    u16_t copy = (p->tot_len < (u16_t) (sizeof s->recv_buf - 1U))
                 ? p->tot_len : (u16_t) (sizeof s->recv_buf - 1U);
    pbuf_copy_partial(p, s->recv_buf, copy, 0);
    s->recv_buf[copy] = '\0';
    s->recv_len = (int) copy;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    s->done = 1;
    return ERR_OK;
}

static err_t echo_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;
    if (err != ERR_OK) {
        s->done = 1;
        return err;
    }
    s->connected = 1;
    err_t wrc = tcp_write(pcb, s->send_buf, (u16_t) strlen(s->send_buf),
                          TCP_WRITE_FLAG_COPY);
    if (wrc != ERR_OK) {
        s->done = 1;
        return wrc;
    }
    return tcp_output(pcb);
}

static void echo_err_cb(void *arg, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;
    s->done = 1;
    /* pcb already freed; do not touch it */
    (void) err;
}

int echo_once(const ip_addr_t *server_ip, uint16_t port, const char *msg) {
    static echo_state_t state;
    memset(&state, 0, sizeof state);
    (void) snprintf(state.send_buf, sizeof state.send_buf, "%s", msg);

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        return -1;
    }
    tcp_arg(pcb, &state);
    tcp_recv(pcb, echo_recv_cb);
    tcp_err(pcb, echo_err_cb);

    err_t crc = tcp_connect(pcb, server_ip, port, echo_connected_cb);
    if (crc != ERR_OK) {
        return -1;
    }

    while (!state.done) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(
            make_timeout_time_ms(100U));
    }
    return state.recv_len;
}
```

That is the entire shape. Every subsequent client in the week is a variation: replace `tcp_write` with the MQTT encoder, replace `pbuf_copy_partial` with the MQTT decoder, layer mbedTLS's `mbedtls_ssl_set_bio` to wrap the TCP send/recv path. The contract — registration, callback, free the pbuf — does not change.

## Summary

The lwIP raw API has a small surface (six TCP functions, four callbacks, plus DNS) and a hard rule (every pbuf gets freed exactly once). The cost of the contract is verbosity; the reward is a TCP/IP stack in 14 KB of SRAM with no threads. Tomorrow we put MQTT on top of this and write the wire-format encoder/decoder for MQTT 5's variable-length integers and properties.
