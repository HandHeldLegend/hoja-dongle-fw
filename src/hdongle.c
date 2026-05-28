#include <stdio.h>
#include <dhcpserver.h>
#include <dongle.h>

#include "cores/cores.h"
#include "dongle_wlan.h"
#include "dongle_wlan_core0.h"
#include "dongle_wlan_core1.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

static struct udp_pcb *pcb = NULL;

static void _udp_receive_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    dongle_wlan_core1_on_rx(pcb, p, addr, port);
    if (p)
    {
        pbuf_free(p);
    }
}

void _wlan_network_task(void)
{
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
    {
        printf("Wi-Fi init failed\n");
        return;
    }

    cyw43_wifi_ap_set_channel(&cyw43_state, 6);
    cyw43_arch_enable_ap_mode(WIFI_SSID_BASE, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    cyw43_wifi_set_roam_enabled(&cyw43_state, false);
    cyw43_wifi_set_interference_mode(&cyw43_state, CYW43_IFMODE_NONE);

    static dhcp_server_t dhcp_server;
    ip4_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_netmask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    pcb = udp_new();
    udp_bind(pcb, IP_ANY_TYPE, DONGLE_WLAN_UDP_PORT);
    udp_recv(pcb, _udp_receive_cb, NULL);

    dongle_wlan_core1_init(pcb);

    for (;;)
    {
        dongle_wlan_core1_poll(time_us_64());
        tight_loop_contents();
    }
}

int main(void)
{
    stdio_init_all();

    dongle_wlan_core0_init();
    multicore_launch_core1(_wlan_network_task);

    dongle_wlan_core0_set_boot_format((uint8_t)CORE_REPORTFORMAT_N64);
    core_init(CORE_REPORTFORMAT_N64, NULL);

    for (;;)
    {
        uint64_t now = time_us_64();
        dongle_wlan_core0_poll(now);
        core_task(now);
    }
}
