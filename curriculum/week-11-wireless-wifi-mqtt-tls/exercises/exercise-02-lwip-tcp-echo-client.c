/*
 * exercise-02-lwip-tcp-echo-client.c — Open a plain-TCP connection to
 * a local echo server (RFC 862; port 7 by default; on macOS/Linux
 * you can run one via `socat -v TCP-LISTEN:7,fork,reuseaddr EXEC:cat`).
 * Send "hello from pico-w\n" (18 bytes); receive the echo; print it;
 * close the connection.
 *
 * Demonstrates: lwIP raw-API TCP usage, pbuf-chain walking, the
 * `connected/recv/sent/err` callback contract, and correct pbuf
 * lifetime management.
 *
 * Build:
 *   target_link_libraries(exercise-02
 *     pico_stdlib
 *     pico_cyw43_arch_lwip_poll)
 *   target_compile_definitions(exercise-02 PRIVATE
 *     ECHO_SERVER_IP="\"192.168.1.50\"")
 *
 * Expected output (UART):
 *
 *   [boot] tcp-echo exercise
 *   [boot] wifi connected; ip=192.168.1.42
 *   [echo] resolving 192.168.1.50:7...
 *   [echo] connecting...
 *   [echo] connected; sending 18 bytes
 *   [echo] sent_cb: 18 bytes acked
 *   [echo] recv_cb: got 18 bytes => "hello from pico-w\n"
 *   [echo] peer FIN; closing
 *   [echo] done
 *
 * Cite: lwIP raw API docs at
 * https://www.nongnu.org/lwip/2_2_x/raw_api.html
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"

#include "wifi_common.h"

#ifndef ECHO_SERVER_IP
#define ECHO_SERVER_IP "192.168.1.50"
#endif

#define ECHO_MSG       "hello from pico-w\n"
#define ECHO_MSG_LEN   ((uint16_t) 18U)
#define ECHO_BUF_SIZE  ((uint16_t) 64U)

/*
 * Connection state. Allocated once, file-scope-static, so that the
 * pointer passed to lwIP via tcp_arg() outlives every callback
 * invocation up to and including tcp_close.
 */
typedef struct {
    int8_t   connected;     /* 1 once connected_cb fires with err==ERR_OK */
    int8_t   done;          /* 1 when the connection has fully closed */
    int8_t   error;         /* non-zero if any callback received err != ERR_OK */
    uint16_t bytes_sent;
    uint16_t bytes_recvd;
    uint8_t  recv_buf[ECHO_BUF_SIZE];
} echo_state_t;

static echo_state_t g_state;

/*
 * recv_cb — called by lwIP tick context every time bytes arrive
 * (or when the peer FIN-s, in which case p == NULL).
 */
static err_t echo_recv_cb(void *arg, struct tcp_pcb *pcb,
                          struct pbuf *p, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;

    if (p == NULL) {
        /* Peer half-closed; we close our side too. */
        (void) printf("[echo] peer FIN; closing\n");
        s->done = (int8_t) 1;
        return tcp_close(pcb);
    }

    if (err != ERR_OK) {
        (void) printf("[echo] recv_cb error: %d\n", (int) err);
        s->error = (int8_t) 1;
        pbuf_free(p);
        return err;
    }

    /* Copy the pbuf chain into recv_buf, capped at ECHO_BUF_SIZE-1. */
    uint16_t to_copy = p->tot_len;
    uint16_t cap     = (uint16_t) (ECHO_BUF_SIZE - 1U);
    if (to_copy > cap) {
        to_copy = cap;
    }
    (void) pbuf_copy_partial(p, s->recv_buf, to_copy, (u16_t) 0U);
    s->recv_buf[to_copy] = (uint8_t) '\0';
    s->bytes_recvd = (uint16_t) (s->bytes_recvd + to_copy);

    (void) printf("[echo] recv_cb: got %u bytes => \"%s\"\n",
                  (unsigned) to_copy, (const char *) s->recv_buf);

    /* Tell lwIP we have consumed p->tot_len bytes; advance window. */
    tcp_recved(pcb, p->tot_len);

    /* Release the pbuf back to the pool. EXACTLY ONCE. */
    pbuf_free(p);
    return ERR_OK;
}

/*
 * sent_cb — called when the peer ACKs our outbound data. The `len`
 * is the cumulative bytes acked since the last sent_cb invocation.
 */
static err_t echo_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void) pcb;
    echo_state_t *s = (echo_state_t *) arg;
    s->bytes_sent = (uint16_t) (s->bytes_sent + len);
    (void) printf("[echo] sent_cb: %u bytes acked\n", (unsigned) len);
    return ERR_OK;
}

/*
 * err_cb — called when lwIP has detected a fatal error. The pcb is
 * already freed; do NOT call tcp_close or any other pcb function here.
 */
static void echo_err_cb(void *arg, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;
    (void) printf("[echo] err_cb: %d (connection dead)\n", (int) err);
    s->error = (int8_t) 1;
    s->done  = (int8_t) 1;
}

/*
 * connected_cb — called when the SYN-ACK arrives and the connection
 * is established. From here we issue the first send.
 */
static err_t echo_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    echo_state_t *s = (echo_state_t *) arg;

    if (err != ERR_OK) {
        (void) printf("[echo] connect failed: %d\n", (int) err);
        s->error = (int8_t) 1;
        s->done  = (int8_t) 1;
        return err;
    }

    s->connected = (int8_t) 1;
    (void) printf("[echo] connected; sending %u bytes\n",
                  (unsigned) ECHO_MSG_LEN);

    err_t wrc = tcp_write(pcb, ECHO_MSG, ECHO_MSG_LEN,
                          TCP_WRITE_FLAG_COPY);
    if (wrc != ERR_OK) {
        (void) printf("[echo] tcp_write failed: %d\n", (int) wrc);
        s->error = (int8_t) 1;
        return wrc;
    }

    err_t orc = tcp_output(pcb);
    if (orc != ERR_OK) {
        (void) printf("[echo] tcp_output failed: %d\n", (int) orc);
        s->error = (int8_t) 1;
        return orc;
    }

    return ERR_OK;
}

/*
 * Issue one echo round-trip. Returns 0 on full success (sent + received
 * + clean close), -1 on any failure path. The actual byte count
 * received is in g_state.bytes_recvd; the buffer is in g_state.recv_buf.
 */
static int echo_once(const ip4_addr_t *server_ip, uint16_t port) {
    (void) memset(&g_state, 0, sizeof g_state);

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        (void) printf("[echo] tcp_new returned NULL (PCB pool exhausted)\n");
        return -1;
    }

    tcp_arg(pcb, &g_state);
    tcp_recv(pcb, echo_recv_cb);
    tcp_sent(pcb, echo_sent_cb);
    tcp_err (pcb, echo_err_cb);

    (void) printf("[echo] connecting...\n");
    ip_addr_t addr;
    ip_addr_copy_from_ip4(addr, *server_ip);
    err_t crc = tcp_connect(pcb, &addr, port, echo_connected_cb);
    if (crc != ERR_OK) {
        (void) printf("[echo] tcp_connect failed: %d\n", (int) crc);
        return -1;
    }

    /* Pump lwIP until done or 30 s have elapsed. */
    absolute_time_t deadline = make_timeout_time_ms(30000U);
    while (g_state.done == (int8_t) 0 && !time_reached(deadline)) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(100U));
    }

    if (g_state.done == (int8_t) 0) {
        (void) printf("[echo] timed out\n");
        return -1;
    }
    if (g_state.error != (int8_t) 0) {
        return -1;
    }
    return 0;
}

int main(void) {
    (void) stdio_init_all();
    sleep_ms(2000U);

    (void) printf("[boot] tcp-echo exercise\n");

    if (cyw43_arch_init() != 0) {
        (void) printf("[boot] cyw43_arch_init failed\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    int rc = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD,
        (uint32_t) WIFI_AUTH_MODE,
        WIFI_CONNECT_TIMEOUT_MS);
    if (rc != 0) {
        (void) printf("[boot] wifi connect failed: %d\n", rc);
        return 1;
    }

    const ip4_addr_t *my_ip = netif_ip4_addr(netif_default);
    (void) printf("[boot] wifi connected; ip=%s\n", ip4addr_ntoa(my_ip));

    ip4_addr_t server_ip;
    if (ip4addr_aton(ECHO_SERVER_IP, &server_ip) == 0) {
        (void) printf("[boot] bad ECHO_SERVER_IP literal\n");
        return 1;
    }

    (void) printf("[echo] resolving %s:%u...\n",
                  ECHO_SERVER_IP, (unsigned) ECHO_SERVER_PORT);

    int erc = echo_once(&server_ip, ECHO_SERVER_PORT);
    if (erc == 0) {
        (void) printf("[echo] done\n");
    } else {
        (void) printf("[echo] failed\n");
    }

    /* Spin so the UART output stays visible. */
    for (;;) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000U));
    }
}
