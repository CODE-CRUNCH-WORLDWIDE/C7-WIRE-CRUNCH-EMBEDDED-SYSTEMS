/*
 * exercise-01-wifi-join-and-dhcp.c — Join a WPA2/WPA3 network and print
 * the SSID, RSSI, and DHCP-assigned IPv4 address. On disconnect (forced
 * by powering the AP off and on, or by the AP roaming this device to
 * another BSSID), reconnect and resume.
 *
 * Build:
 *   target_link_libraries(exercise-01
 *     pico_stdlib
 *     pico_cyw43_arch_lwip_poll)
 *   target_compile_definitions(exercise-01 PRIVATE
 *     WIFI_SSID="\"my-ap\"" WIFI_PASSWORD="\"my-password\"")
 *
 * Expected console output (UART, 115200 8N1):
 *
 *   [boot] pico-w wifi-join exercise
 *   [boot] cyw43_arch_init... ok (1487 ms)
 *   [boot] enabling STA mode
 *   [boot] connecting to "my-ap" (WPA2/WPA3)...
 *   [boot] connected; ip=192.168.1.42 rssi=-58 dBm bssid=28:cd:c1:0d:fe:8c
 *   [run]  link up uptime=  1.5 s
 *   [run]  link up uptime=  6.5 s
 *   ...
 *   [run]  link DOWN — reconnecting
 *   [boot] connecting to "my-ap" (WPA2/WPA3)...
 *   [boot] connected; ip=192.168.1.42 rssi=-61 dBm bssid=28:cd:c1:0d:fe:8c
 *
 * Cite: pico-sdk pico_cyw43_arch docs at
 * https://www.raspberrypi.com/documentation/pico-sdk/networking.html
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"

#include "wifi_common.h"

/*
 * Print the current netif's IPv4 address, the link state, and the
 * RSSI of the AP we are currently associated with. Called once per
 * second from the main loop.
 */
static void print_link_status(uint64_t boot_us) {
    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    int32_t rssi = 0;
    (void) cyw43_wifi_get_rssi(&cyw43_state, &rssi);

    int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    const char *link_str = "unknown";
    switch (link_status) {
        case CYW43_LINK_DOWN:    link_str = "DOWN";     break;
        case CYW43_LINK_JOIN:    link_str = "joining";  break;
        case CYW43_LINK_NOIP:    link_str = "no-ip";    break;
        case CYW43_LINK_UP:      link_str = "UP";       break;
        case CYW43_LINK_FAIL:    link_str = "FAIL";     break;
        case CYW43_LINK_NONET:   link_str = "no-net";   break;
        case CYW43_LINK_BADAUTH: link_str = "bad-auth"; break;
        default:                 link_str = "?";        break;
    }

    uint64_t now_us = time_us_64();
    double uptime_s = (double) (now_us - boot_us) / 1.0e6;

    char ipstr[16] = {0};
    if (ip != NULL && !ip4_addr_isany(ip)) {
        (void) snprintf(ipstr, sizeof ipstr, "%s", ip4addr_ntoa(ip));
    } else {
        (void) snprintf(ipstr, sizeof ipstr, "0.0.0.0");
    }

    (void) printf("[run]  link %-4s uptime=%6.1f s ip=%-15s rssi=%4ld dBm\n",
                  link_str, uptime_s, ipstr, (long) rssi);
}

/*
 * Connect (or reconnect) to the configured AP, blocking until
 * association + DHCP completes or the timeout elapses. Returns 0
 * on success, the cyw43_arch_wifi_connect_timeout_ms error code
 * on failure.
 */
static int wifi_connect_blocking(void) {
    (void) printf("[boot] connecting to \"%s\" (WPA2/WPA3)...\n", WIFI_SSID);

    int rc = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD,
        (uint32_t) WIFI_AUTH_MODE,
        WIFI_CONNECT_TIMEOUT_MS);

    if (rc != 0) {
        const char *why = "unknown";
        switch (rc) {
            case PICO_ERROR_TIMEOUT:         why = "timeout";        break;
            case PICO_ERROR_CONNECT_FAILED:  why = "connect-failed"; break;
            case PICO_ERROR_BADAUTH:         why = "bad-auth";       break;
            default:                         why = "other";          break;
        }
        (void) printf("[boot] connect failed: %d (%s)\n", rc, why);
        return rc;
    }

    const ip4_addr_t *ip = netif_ip4_addr(netif_default);
    int32_t rssi = 0;
    (void) cyw43_wifi_get_rssi(&cyw43_state, &rssi);

    uint8_t bssid[6] = {0};
    (void) cyw43_wifi_get_bssid(&cyw43_state, bssid);

    (void) printf(
        "[boot] connected; ip=%s rssi=%ld dBm bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
        ip4addr_ntoa(ip), (long) rssi,
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return 0;
}

int main(void) {
    (void) stdio_init_all();
    sleep_ms(2000U); /* Let USB-CDC come up so we can see boot prints. */

    (void) printf("[boot] pico-w wifi-join exercise\n");

    uint64_t before = time_us_64();
    int init_rc = cyw43_arch_init();
    uint64_t after = time_us_64();
    if (init_rc != 0) {
        (void) printf("[boot] cyw43_arch_init failed: %d\n", init_rc);
        return 1;
    }
    (void) printf("[boot] cyw43_arch_init... ok (%llu ms)\n",
                  (after - before) / 1000ULL);

    (void) printf("[boot] enabling STA mode\n");
    cyw43_arch_enable_sta_mode();

    /* Initial connect; retry on failure with a 5-second pause. */
    while (wifi_connect_blocking() != 0) {
        sleep_ms(5000U);
    }

    uint64_t boot_us = time_us_64();
    absolute_time_t next_status = make_timeout_time_ms(5000U);

    for (;;) {
        cyw43_arch_poll();

        int link_status = cyw43_wifi_link_status(&cyw43_state,
                                                  CYW43_ITF_STA);
        if (link_status == CYW43_LINK_DOWN ||
            link_status == CYW43_LINK_FAIL ||
            link_status == CYW43_LINK_NONET) {
            (void) printf("[run]  link DOWN — reconnecting\n");
            while (wifi_connect_blocking() != 0) {
                sleep_ms(5000U);
            }
        }

        if (time_reached(next_status)) {
            print_link_status(boot_us);
            next_status = make_timeout_time_ms(5000U);
        }

        cyw43_arch_wait_for_work_until(next_status);
    }

    /* Unreachable. */
    cyw43_arch_deinit();
    return 0;
}
