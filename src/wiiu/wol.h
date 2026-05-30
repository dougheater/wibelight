#pragma once

#include <stdbool.h>

/*
 * Wake-on-LAN (WoL) magic packet sender.
 *
 * Builds a standard 102-byte magic packet and sends it via UDP broadcast
 * to port 9.  Returns true on success, false on any error (bad MAC format,
 * socket failure, send failure).
 *
 * mac_str   — MAC in "AA:BB:CC:DD:EE:FF" (case-insensitive, colons or dashes)
 * broadcast — destination IP; pass NULL or "" for 255.255.255.255
 */
bool wol_send(const char *mac_str, const char *broadcast);

/*
 * Convenience: send WoL and then sleep for the given number of seconds,
 * polling a caller-supplied callback each second so the UI can still
 * render / handle input.  Returns the result of wol_send().
 *
 * The callback is called once per second during the wait.  Return 0 to
 * continue waiting, non-zero to abort early.
 */
bool wol_send_and_wait(const char *mac_str, const char *broadcast,
                       int wait_seconds,
                       int (*tick_cb)(void));
