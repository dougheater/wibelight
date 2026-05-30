/*
 * Wake-on-LAN (WoL) magic packet sender for wibelight.
 *
 * Sends a standard WoL "magic packet" — 6 × 0xFF followed by the target
 * MAC address repeated 16 times — as a UDP broadcast to port 9.
 *
 * Public domain / GPL v3 (matches project license).
 */

#include "wol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <coreinit/time.h>

/* ── helpers ─────────────────────────────────────────────────────── */

/* Normalise a MAC string: accept "AA:BB:CC:DD:EE:FF" or
 * "AA-BB-CC-DD-EE-FF", case-insensitive.  Returns true on success. */
static bool parse_mac(const char *str, uint8_t mac[6])
{
    int consumed = 0;
    for (int i = 0; i < 6; i++) {
        char byte_str[3] = {0, 0, 0};
        /* Skip optional separator (colon or dash) before each byte except first */
        if (i > 0 && *str && (*str == ':' || *str == '-'))
            str++;
        if (str[0] == '\0' || str[1] == '\0')
            return false;
        byte_str[0] = str[0];
        byte_str[1] = str[1];
        char *end;
        long val = strtol(byte_str, &end, 16);
        if (*end != '\0' || val < 0 || val > 255)
            return false;
        mac[i] = (uint8_t)val;
        str += 2;
        consumed++;
    }
    return consumed == 6;
}

/* ── public API ──────────────────────────────────────────────────── */

bool wol_send(const char *mac_str, const char *broadcast)
{
    if (!mac_str || strlen(mac_str) < 12) {
        fprintf(stderr, "[WoL] Invalid MAC string\n");
        return false;
    }

    uint8_t mac[6];
    if (!parse_mac(mac_str, mac)) {
        fprintf(stderr, "[WoL] Failed to parse MAC: %s\n", mac_str);
        return false;
    }

    /* Build magic packet: 6 × 0xFF + 16 × MAC = 102 bytes */
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++)
        memcpy(packet + 6 + i * 6, mac, 6);

    /* Create UDP socket */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("[WoL] socket");
        return false;
    }

    /* Enable broadcast */
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("[WoL] setsockopt SO_BROADCAST");
        close(sock);
        return false;
    }

    /* Destination */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9); /* Standard WoL port */

    if (broadcast && broadcast[0] != '\0')
        addr.sin_addr.s_addr = inet_addr(broadcast);
    else
        addr.sin_addr.s_addr = INADDR_BROADCAST;

    /* Send */
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    printf("[WoL] Sending magic packet to %s:9 for MAC %s\n", ip_str, mac_str);

    ssize_t sent = sendto(sock, packet, sizeof(packet), 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    close(sock);

    if (sent != (ssize_t)sizeof(packet)) {
        perror("[WoL] sendto");
        return false;
    }

    printf("[WoL] Magic packet sent (%zd bytes)\n", sent);
    return true;
}

bool wol_send_and_wait(const char *mac_str, const char *broadcast,
                       int wait_seconds, int (*tick_cb)(void))
{
    if (!wol_send(mac_str, broadcast))
        return false;

    /* Wait, calling tick_cb once per second so UI can keep rendering */
    for (int s = 0; s < wait_seconds; s++) {
        if (tick_cb && tick_cb())
            break; /* Abort early */
        /* Sleep 1 second using OS timers */
        uint64_t deadline = OSGetTime() + OSMillisecondsToTicks(1000);
        while (OSGetTime() < deadline) {
            /* Busy-wait with small yield — main loop drives rendering */
        }
    }
    return true;
}
