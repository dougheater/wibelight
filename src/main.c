/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2019 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "listener_callbacks.h"

#include <Limelight.h>

#include <SDL2/SDL.h>

#include <client.h>
#include <errors.h>

#include <arpa/inet.h>

#include <limits.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../third_party/cJSON/cJSON.h"
#include "wiiu/wiiu.h"
#include "wiiu/wol.h"
#include "ui_theme.h"
#include <coreinit/time.h>

// Draw the logo according to the current theme_logo_preset
static void draw_logo(void)
{
    switch (theme_logo_preset) {
        case LOGO_STYLE_NORMAL:
            Font_DrawLogo(40, 30);
            break;
        case LOGO_STYLE_SMALL:
            Font_DrawLogoSm(40, 30);
            break;
        case LOGO_STYLE_NONE:
        default:
            break;
    }
}
#include <coreinit/thread.h>
#include <coreinit/fastmutex.h>
#include <vpad/input.h>
#include <whb/gfx.h>

// External variables from input.c
extern int swap_buttons;
extern int enable_rumble;
extern mouse_modes mouse_mode;
extern ConnListenerRumble rumble_handler;

// External variables from audio.c / wiiu.c
extern int audio_buffer_samples;
extern int max_queued_frames;
extern int disable_gamepad;
extern int autostream;
extern int rumble_strength;
extern int enable_nav_click;

#ifdef DEBUG
void Debug_Init();
#endif

#define SCREEN_BAR                                                             \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501"   \
  "\u2501\u2501\u2501\u2501\n"

// Fixed button bar positions (all pages)
#define BAR_DIVIDER_Y  960
#define BAR_BUTTONS_Y  1000

// Synchronized state - accessed from main thread and ENet callback thread.
// A fast mutex protects writes from the callback; the main thread locks briefly
// to read or transition state atomically.
OSFastMutex stateMutex;
int state = STATE_INVALID;

int is_error = 0;
int disconnecting = 0;
char message_buffer[1024] = "\0";

int autostream = 0;
int autostart_connect = 0; // If 1, connect to last server on boot; if 0, show Disconnected screen

// Debug log file: writes to wibelight.log with 512KB rotation
FILE *debug_log = NULL; // exposed for other modules
#define DEBUG_LOG_MAX_SIZE (512 * 1024) // 512KB

static void debug_log_open(void)
{
  if (debug_log)
    return;
  debug_log = fopen("/vol/external01/wiiu/apps/wibelight/wibelight.log", "w");
}

static void debug_log_close(void)
{
  if (debug_log) {
    fclose(debug_log);
    debug_log = NULL;
  }
}

// Usage: DBG_LOG("message %d\n", 42);
// Writes to log file, auto-rotates when >512KB.
#define DBG_LOG(fmt, ...)                                                    \
  do {                                                                        \
    if (!debug_log) debug_log_open();                                        \
    if (debug_log) {                                                          \
      long pos = ftell(debug_log);                                            \
      if (pos >= DEBUG_LOG_MAX_SIZE) {                                        \
        /* Rotate: truncate and rewrite */                                     \
        fflush(debug_log);                                                    \
        freopen("/vol/external01/wiiu/apps/wibelight/wibelight.log", "w", debug_log); \
      }                                                                       \
      fprintf(debug_log, fmt, ##__VA_ARGS__);                                 \
      fflush(debug_log);                                                      \
    }                                                                         \
  } while (0)

// Profile system
profile_t profiles[MAX_PROFILES];
int active_profile = 0;

// Forward refs to globals defined later in this file
extern int wol_enabled;
extern char wol_mac[];
extern int wol_wait_seconds;

// Profile selector state
static int profile_cursor = 0; // Which profile row is selected
static int profile_selector_prev_state = STATE_DISCONNECTED;
static int sel_saved_bg, sel_saved_accent, sel_saved_text, sel_saved_btn; // theme presets saved on entry
static int sel_cursor_prev = -1; // last cursor that triggered a theme preview

// IP editor state (shared by profile selector and Settings->Network->IP)
static int ip_edit_cursor = 0; // Which octet is highlighted
static uint8_t ip_edit_buf[4] = {0}; // Temp IP being edited
static int ip_edit_return_state = STATE_DISCONNECTED;

// Helper: check if a profile has a configured IP (last octet != 0)
static bool profile_is_configured(int idx) {
    return profiles[idx].ip[3] != 0;
}

// Helper: build IP string from profile
static void profile_ip_to_str(int idx, char *buf, size_t len) {
    snprintf(buf, len, "%d.%d.%d.%d",
             profiles[idx].ip[0], profiles[idx].ip[1],
             profiles[idx].ip[2], profiles[idx].ip[3]);
}

// Helper: set defaults for a new profile
static void profile_set_defaults(profile_t *p, int idx) {
    snprintf(p->name, PROFILE_NAME_LEN, "Profile %d", idx + 1);
    p->ip[0] = 192; p->ip[1] = 168; p->ip[2] = 1; p->ip[3] = 0;
    memset(p->wol_mac, 0, PROFILE_WOL_MAC_LEN);
    p->wol_enabled = 0;
    p->wol_wait_seconds = 25;
    p->width = 854; p->height = 480; p->fps = 60; p->bitrate = 3000;
    p->packetSize = 1024;
    p->colorSpace = COLORSPACE_REC_709;
    p->colorRange = COLOR_RANGE_FULL;
    p->max_queued_frames = 4;
    p->rotate = 0;
    p->audio_configuration = AUDIO_CONFIGURATION_STEREO;
    p->audio_buffer_samples = 4096;
    p->localaudio = 0;
    p->swap_buttons = 0;
    p->mouse_mode = MOUSE_MODE_ABSOLUTE;
    p->enable_rumble = 1;
    p->rumble_strength = 0;
    p->enable_nav_click = 0;
    p->disable_gamepad = 0;
    strncpy(p->app, "Desktop", PROFILE_APP_LEN);
    p->quitappafter = 1;
    p->viewonly = 0;
    p->autostream = 0;
    p->autostart_connect = 0;
    p->theme_bg = THEME_BG_DEEP_NAVY;
    p->theme_accent = THEME_ACCENT_STEEL_BLUE;
    p->theme_text = THEME_TEXT_SOFT_WHITE;
    p->theme_btn = THEME_BTN_COLORED;
    p->theme_logo = LOGO_STYLE_SMALL;

    // Profile 1
    if (idx == 0) {
        snprintf(p->name, PROFILE_NAME_LEN, "Echo");
    }
    // Profile 2 gets Red Room theme (Twin Peaks)
    if (idx == 1) {
        snprintf(p->name, PROFILE_NAME_LEN, "Nightingale");
        p->theme_bg = THEME_BG_ESPRESSO;
        p->theme_accent = THEME_ACCENT_RED_ROOM;
        p->theme_text = THEME_TEXT_LIGHT_GRAY;
        p->theme_btn = THEME_BTN_RED_ROOM;
    }
    // Profile 3 gets N7 theme (Mass Effect)
    if (idx == 2) {
        snprintf(p->name, PROFILE_NAME_LEN, "Spectre");
        p->theme_bg = THEME_BG_DARK_BLUE;
        p->theme_accent = THEME_ACCENT_N7;
        p->theme_text = THEME_TEXT_WIIBE;
        p->theme_btn = THEME_BTN_N7;
    }
    // Profile 4 gets Gouryella theme (Tiesto Forever Today)
    if (idx == 3) {
        snprintf(p->name, PROFILE_NAME_LEN, "Today");
        p->theme_bg = THEME_BG_DEEP_PURPLE;
        p->theme_accent = THEME_ACCENT_GOLD;
        p->theme_text = THEME_TEXT_SILVER;
        p->theme_btn = THEME_BTN_GOURYELLA;
    }
    // Profile 5 gets Blade Runner theme
    if (idx == 4) {
        snprintf(p->name, PROFILE_NAME_LEN, "Rain");
        p->theme_bg = THEME_BG_PURE_BLACK;
        p->theme_accent = THEME_ACCENT_CYAN;
        p->theme_text = THEME_TEXT_AMBER;
        p->theme_btn = THEME_BTN_AMBER;
    }
    // Profile 6 gets Iconic Green theme and custom name
    if (idx == 5) {
        snprintf(p->name, PROFILE_NAME_LEN, "Dissolved");
        p->theme_bg = THEME_BG_ICONIC_GREEN;
        p->theme_accent = THEME_ACCENT_ICONIC_GREEN;
        p->theme_text = THEME_TEXT_LIGHT_GRAY;
        p->theme_btn = THEME_BTN_ICONIC_GREEN;
    }
}

// Load profile idx into all globals
static void profile_load(int idx, PCONFIGURATION cfg) {
    if (idx < 0 || idx >= MAX_PROFILES) return;
    active_profile = idx;
    const profile_t *p = &profiles[idx];

    // Address (configured = last octet != 0)
    if (p->ip[3] != 0) {
        char ip_str[24];
        profile_ip_to_str(idx, ip_str, sizeof(ip_str));
        if (cfg->address) free((void*)cfg->address);
        cfg->address = strdup(ip_str);
    } else {
        if (cfg->address) free((void*)cfg->address);
        cfg->address = NULL;
    }

    // Video
    cfg->stream.width = p->width;
    cfg->stream.height = p->height;
    cfg->stream.fps = p->fps;
    cfg->stream.bitrate = p->bitrate;
    cfg->stream.packetSize = p->packetSize;
    cfg->stream.colorSpace = p->colorSpace;
    cfg->stream.colorRange = p->colorRange;
    cfg->stream.audioConfiguration = p->audio_configuration;
    cfg->rotate = p->rotate;
    max_queued_frames = p->max_queued_frames;

    // Audio
    cfg->localaudio = p->localaudio;
    audio_buffer_samples = p->audio_buffer_samples;

    // Input
    swap_buttons = p->swap_buttons;
    mouse_mode = p->mouse_mode;
    enable_rumble = p->enable_rumble;
    rumble_strength = p->rumble_strength;
    enable_nav_click = p->enable_nav_click;
    disable_gamepad = p->disable_gamepad;

    // Behavior
    cfg->quitappafter = p->quitappafter;
    cfg->viewonly = p->viewonly;
    if (cfg->app) free((void*)cfg->app);
    cfg->app = strdup(p->app);
    autostream = p->autostream;
    autostart_connect = p->autostart_connect;

    // WoL
    wol_enabled = p->wol_enabled;
    strncpy(wol_mac, p->wol_mac, PROFILE_WOL_MAC_LEN - 1);
    wol_mac[PROFILE_WOL_MAC_LEN - 1] = '\0';
    wol_wait_seconds = p->wol_wait_seconds;

    // Theme
    theme_bg_preset = p->theme_bg;
    theme_accent_preset = p->theme_accent;
    theme_text_preset = p->theme_text;
    theme_btn_preset = p->theme_btn;
    theme_logo_preset = p->theme_logo;
    theme_apply();
}

// Save all globals into profile[idx]
static void profile_save(int idx, const PCONFIGURATION cfg) {
    if (idx < 0 || idx >= MAX_PROFILES) return;
    profile_t *p = &profiles[idx];

    if (cfg->address) {
        sscanf(cfg->address, "%hhu.%hhu.%hhu.%hhu",
               &p->ip[0], &p->ip[1], &p->ip[2], &p->ip[3]);
    } else {
        memset(p->ip, 0, 4);
    }
    strncpy(p->wol_mac, wol_mac, PROFILE_WOL_MAC_LEN);
    p->wol_enabled = wol_enabled;
    p->wol_wait_seconds = wol_wait_seconds;

    p->width = cfg->stream.width;
    p->height = cfg->stream.height;
    p->fps = cfg->stream.fps;
    p->bitrate = cfg->stream.bitrate;
    p->packetSize = cfg->stream.packetSize;
    p->colorSpace = cfg->stream.colorSpace;
    p->colorRange = cfg->stream.colorRange;
    p->audio_configuration = cfg->stream.audioConfiguration;
    p->rotate = cfg->rotate;
    p->max_queued_frames = max_queued_frames;

    p->localaudio = cfg->localaudio;
    p->audio_buffer_samples = audio_buffer_samples;

    p->swap_buttons = swap_buttons;
    p->mouse_mode = mouse_mode;
    p->enable_rumble = enable_rumble;
    p->rumble_strength = rumble_strength;
    p->enable_nav_click = enable_nav_click;
    p->disable_gamepad = disable_gamepad;

    p->quitappafter = cfg->quitappafter;
    p->viewonly = cfg->viewonly;
    strncpy(p->app, cfg->app ? cfg->app : "Desktop", PROFILE_APP_LEN - 1);
    p->app[PROFILE_APP_LEN - 1] = '\0';
    p->autostream = autostream;
    p->autostart_connect = autostart_connect;

    p->theme_bg = theme_bg_preset;
    p->theme_accent = theme_accent_preset;
    p->theme_text = theme_text_preset;
    p->theme_btn = theme_btn_preset;
    p->theme_logo = theme_logo_preset;
}

// Auto-save: save active profile to JSON
static void auto_save_settings(PCONFIGURATION cfg) {
    profile_save(active_profile, cfg);
    char main_conf[PATH_MAX];
    snprintf(main_conf, sizeof(main_conf),
             "/vol/external01/wiiu/apps/wibelight/wibelight.json");
    config_write_settings(main_conf, cfg, NULL, 0);
}
// Saved servers state
static int settings_prev_state = STATE_DISCONNECTED;       // where B should return to

// Settings state - expandable sections
static int settings_cursor = 0; // visible-row index into the dynamic list

// Section definitions: name, child count, expanded flag
#define NUM_SECTIONS 6

enum {
  SEC_VIDEO = 0,   // Resolution, FPS, Bitrate, Color Space, Color Range
  SEC_AUDIO = 1,   // Surround
  SEC_INPUT = 2,   // Swap Buttons, Mouse Mode, Enable Rumble
  SEC_NETWORK = 3, // Packet Size, Server IP
  SEC_BEHAVIOR = 4,// Quit After, View Only, App
  SEC_APPEARANCE = 5 // Background, Accent Color, Text Contrast, Button Color
};

static const char *section_names[NUM_SECTIONS] = {
    "Video", "Audio", "Input", "Network", "Behavior", "Appearance"};

// Child counts per section
static const int section_child_count[NUM_SECTIONS] = {
    7, // Video: Resolution, FPS, Bitrate, Color Space, Color Range, Max Queued Frames, Rotate
    3, // Audio: Surround, Audio Buffer, Local Audio
    6, // Input: Swap Buttons, Mouse Mode, Enable Rumble, Rumble Strength, Menu Rumble, Disable GamePad
    5, // Network: Packet Size, Server IP, Wake-on-LAN, MAC Address, Wait Time
    5, // Behavior: Quit After, View Only, App, Auto Stream, Auto Connect
    5  // Appearance: Background, Accent Color, Text Contrast, Button Color, Logo
};

static int section_expanded[NUM_SECTIONS] = {0}; // all collapsed by default

// Child labels per section (indexed as section_children[sec][child])
static const char *section_children[NUM_SECTIONS][7] = {
    // Video
    {"Resolution:", "FPS:", "Bitrate:", "Color Space:", "Color Range:", "Max Queued Frames:", "Rotate:"},
    // Audio
    {"Surround:", "Audio Buffer:", "Local Audio:"},
    // Input
    {"Swap Buttons:", "Mouse Mode:", "Enable Rumble:", "Rumble Strength:", "Menu Rumble:", "Disable GamePad:"},
    // Network
    {"Packet Size:", "Server IP:", "Wake-on-LAN:", "MAC Address:", "WoL Wait:"},
    // Behavior
    {"Quit After:", "View Only:", "App:", "Auto Stream:", "Auto Connect:"},
    // Appearance
    {"Background:", "Accent Color:", "Text Contrast:", "Button Color:", "Logo:"},
};

// Helper: compute total visible rows (headers + expanded children)
static int settings_visible_count(void)
{
  int total = NUM_SECTIONS; // every header is always visible
  for (int s = 0; s < NUM_SECTIONS; s++) {
    if (section_expanded[s])
      total += section_child_count[s];
  }
  return total;
}

// Helper: map visible-row index -> (section, child)
// child == -1 means the header row itself
static void settings_visible_to_section(int visible_idx, int *out_section, int *out_child)
{
  int row = 0;
  for (int s = 0; s < NUM_SECTIONS; s++) {
    if (row == visible_idx) {
      *out_section = s;
      *out_child = -1; // header
      return;
    }
    row++; // header
    if (section_expanded[s]) {
      for (int c = 0; c < section_child_count[s]; c++) {
        if (row == visible_idx) {
          *out_section = s;
          *out_child = c;
          return;
        }
        row++;
      }
    }
  }
  // fallback
  *out_section = NUM_SECTIONS - 1;
  *out_child = -1;
}

// Helper: given (section, child), return the setting index 0-24
static int section_child_to_setting_idx(int sec, int child)
{
  // Cumulative offsets: each section's first child maps to this flat index
  // Video:0-6   Resolution,FPS,Bitrate,ColorSpace,ColorRange,MaxQueuedFrames,Rotate
  // Audio:7-9   Surround,AudioBuffer,LocalAudio
  // Input:10-15 SwapButtons,MouseMode,Rumble,RumbleStrength,NavClick,DisableGamePad
  // Network:16-20 PacketSize,ServerIP,WakeOnLAN,MACAddress,WaitTime
  // Behavior:21-25 QuitAfter,ViewOnly,App,AutoStream,AutoConnect
  // Appearance:26-30 Background,AccentColor,TextContrast,ButtonColor,Logo
  static const int section_child_offset[NUM_SECTIONS] = {
      0,   // Video
      7,   // Audio
      10,  // Input
      16,  // Network
      21,  // Behavior
      26   // Appearance
  };
  return section_child_offset[sec] + child;
}

// Track where we entered streaming from so disconnect returns to the right
// screen. 0 = STATE_CONNECTED (default), 1 = STATE_DISCONNECTED (benchmark from
// disconnected)
static int stream_entry_state = 0;

// Cooldown after error disconnect: host may need time to recover its session.
// Set to a future OSGetTime() tick value; reconnect is blocked until it expires.
static uint64_t reconnect_cooldown_deadline = 0;
#define RECONNECT_COOLDOWN_TICKS OSSecondsToTicks(5) // 5 seconds in ticks (OSGetTime returns ticks, not ns)

// Custom bitrate state
static int custom_br_value = 5000;

// Benchmark state
static int benchmark_duration = 60;    // seconds
static int benchmark_duration_idx = 1; // index into preset array
static const int benchmark_presets[] = {30, 60, 90, 120};
#define NUM_BENCHMARK_PRESETS 4
static uint64_t benchmark_start_time = 0;
int benchmark_running = 0; // non-static: used from decode.c
// Benchmark results
static uint32_t benchmark_rtt_avg = 0;
static uint32_t benchmark_rtt_variance = 0;
static int benchmark_poor_count = 0;
static int benchmark_okay_count = 0;
int benchmark_total_frames = 0;       // non-static: used from decode.c
int benchmark_decode_errors = 0;      // non-static: used from decode.c
static int custom_bitrate_return_to_benchmark = 0; // return to benchmark results after setting bitrate
uint64_t benchmark_total_bytes = 0;   // non-static: used from decode.c
static int benchmark_was_from_disconnected =
    0; // Track entry point for correct return

// Wake-on-LAN state
#define WOL_MAC_LEN PROFILE_WOL_MAC_LEN
#define WOL_WAIT_DEFAULT 25          // Default wait time if not yet loaded from profile
#define WOL_MAX_ATTEMPTS 3           // Maximum WoL resend attempts before giving up
int wol_enabled = 0;             // non-static: read by config.c
char wol_mac[WOL_MAC_LEN] = {0}; // Auto-detected from serverinfo on first connect
int wol_wait_seconds = WOL_WAIT_DEFAULT; // User-adjustable WoL wait (seconds)
static int wol_source_state = STATE_DISCONNECTED; // Where to return after WoL
static uint64_t wol_start_time = 0;
static int wol_attempt = 0;          // Current WoL attempt (1-based)

void benchmark_status(int status) {
  if (!benchmark_running)
    return;
  if (status == CONN_STATUS_POOR)
    benchmark_poor_count++;
  else if (status == CONN_STATUS_OKAY)
    benchmark_okay_count++;
}
#define CUSTOM_BR_STEP 250

// Surround labels and presets
static const char *SURROUND_LABELS[] = {"Stereo", "5.1 Surround",
                                        "7.1 Surround"};
static const int SURROUND_PRESETS[] = {AUDIO_CONFIGURATION_STEREO,
                                       AUDIO_CONFIGURATION_51_SURROUND,
                                       AUDIO_CONFIGURATION_71_SURROUND};
#define NUM_SURROUND 3

// Mouse mode labels and presets
static const char *MOUSE_MODE_LABELS[] = {"relative", "absolute",
                                          "touchscreen"};
static const mouse_modes MOUSE_MODE_PRESETS[] = {
    MOUSE_MODE_RELATIVE, MOUSE_MODE_ABSOLUTE, MOUSE_MODE_TOUCHSCREEN};
#define NUM_MOUSE_MODES 3

// App labels and presets (sent to host by name)
static const char *APP_LABELS[] = {
    "Desktop", "Steam", "Big Picture", "RetroArch",
    "Citron", "Emulator", "Application"};
static const char *APP_PRESETS[] = {
    "Desktop", "Steam", "Big Picture", "RetroArch",
    "Citron", "Emulator", "Application"};
#define NUM_APPS 7

// Bool labels
static const char *BOOL_LABELS[] = {"false", "true"};

// Color space labels and presets
static const char *COLOR_SPACE_LABELS[] = {"Rec. 601", "Rec. 709"};
#define NUM_COLOR_SPACES 2

// Color range labels and presets
static const char *COLOR_RANGE_LABELS[] = {"Limited", "Full"};
#define NUM_COLOR_RANGES 2

static int find_surround_index(int audio_cfg) {
  for (int i = 0; i < NUM_SURROUND; i++) {
    if (SURROUND_PRESETS[i] == audio_cfg)
      return i;
  }
  return 0;
}

static int find_mouse_mode_index(mouse_modes mode) {
  for (int i = 0; i < NUM_MOUSE_MODES; i++) {
    if (MOUSE_MODE_PRESETS[i] == mode)
      return i;
  }
  return 1; // default to absolute
}

static int find_app_index(const char *app) {
  if (!app)
    return 0;
  for (int i = 0; i < NUM_APPS; i++) {
    if (strcmp(app, APP_PRESETS[i]) == 0)
      return i;
  }
  return 0; // default to Desktop
}



// Convenience: draw a centered animated spinner with track ring
static void draw_spinner_centered(int cx, int cy, int radius) {
  uint64_t ticks = OSGetTime();
  int angle = (int)((ticks % OSSecondsToTicks(2)) * 360 / OSSecondsToTicks(2));
  // Faint background ring
  Font_SetColor(theme_spinner_track_r, theme_spinner_track_g, theme_spinner_track_b, theme_spinner_track_a);
  Font_DrawSpinner(cx, cy, radius, 0, 360, 6);
  // Bright rotating arc
  Font_SetColor(theme_spinner_arc_r, theme_spinner_arc_g, theme_spinner_arc_b, theme_spinner_arc_a);
  Font_DrawSpinner(cx, cy, radius, angle, 270, 6);
}

// Connection step text based on elapsed time
static const char *connection_step_text(uint64_t startTicks) {
  uint64_t elapsed = OSGetTime() - startTicks;
  // Step thresholds in ticks (OSGetTime returns ticks, not ns)
  if (elapsed < OSMillisecondsToTicks(500))
    return "Resolving host...";
  if (elapsed < OSMillisecondsToTicks(1500))
    return "Handshaking...";
  if (elapsed < OSMillisecondsToTicks(3000))
    return "Negotiating stream...";
  return "Connecting...";
}

#include "xml.h"

static int get_app_id(GS_CLIENT client, PSERVER_DATA server, const char *name) {
  PAPP_LIST list = NULL;
  if (gs_applist(client, server, &list) != GS_OK) {
    fprintf(stderr, "Can't get app list\n");
    DBG_LOG("[ERROR] Can't get app list\n");
    return -1;
  }

  int appId = -1;
  PAPP_LIST node = list;
  while (node != NULL) {
    if (strcmp(node->name, name) == 0) {
      appId = node->id;
      break;
    }
    node = node->next;
  }
  free_applist(list);
  return appId;
}

// Send gs_quit_app() with retries.  On error disconnects the host may be in
// a half-zombie state, so a single fire-and-forget HTTP request is unreliable.
// Retries with a short delay between attempts, logging each one.
// Returns 0 on success, last error code on failure.
static int quit_app_reliable(GS_CLIENT client, PSERVER_DATA server,
                             int max_retries, uint64_t retry_delay_ns)
{
  int ret = GS_OK;
  for (int attempt = 1; attempt <= max_retries; attempt++) {
    ret = gs_quit_app(client, server);
    if (ret == GS_OK) {
      DBG_LOG("[QUIT] success on attempt %d\n", attempt);
      return 0;
    }
    DBG_LOG("[QUIT] attempt %d/%d failed (err=%d): %s\n",
            attempt, max_retries, ret, gs_get_error_message());
    if (attempt < max_retries) {
      // Brief delay before retry, keep ProcUI responsive
      uint64_t deadline = OSGetTime() + OSMillisecondsToTicks(retry_delay_ns / 1000);
      while (OSGetTime() < deadline) {
        wiiu_proc_running();
        usleep(100000); // 100ms
      }
    }
  }
  DBG_LOG("[QUIT] ALL %d attempts failed, host may be in zombie state\n",
          max_retries);
  return ret;
}

static int stream(GS_CLIENT client, PSERVER_DATA server,
                  PCONFIGURATION config) {
  int appId = get_app_id(client, server, config->app);
  if (appId < 0) {
    fprintf(stderr, "Can't find app %s\n", config->app);
    DBG_LOG("[ERROR] Can't find app %s\n", config->app);
    snprintf(message_buffer, sizeof(message_buffer), "Can't find app %s\n",
             config->app);
    is_error = 1;
    return -1;
  }

  int gamepads = wiiu_input_num_controllers();
  int gamepad_mask = 0;
  for (int i = 0; i < gamepads; i++)
    gamepad_mask = (gamepad_mask << 1) + 1;

  // Refresh server status (modes, currentGame) before starting the app.
  // After a disconnect the server struct may have stale display mode data,
  // causing GS_NOT_SUPPORTED_MODE (-8) on the next stream attempt.
  int pre_status = gs_get_status(client, server, config->address, config->unsupported);
  /* Auto-save MAC if not yet detected — only trust when paired */
  if (pre_status == GS_OK && server->paired && server->mac && server->mac[0] != '\0' && wol_mac[0] == '\0') {
    strncpy(wol_mac, server->mac, WOL_MAC_LEN - 1);
    wol_mac[WOL_MAC_LEN - 1] = '\0';
    DBG_LOG("[WoL] Auto-detected MAC: %s\n", wol_mac);
    auto_save_settings(config);
  }
  if (pre_status == GS_OK && server->currentGame != 0) {
    // Host still has an app running - quit it before starting a new one.
    printf("[STREAM] Host has active app (game=%d), quitting...\n",
           server->currentGame);
    DBG_LOG("[STREAM] Host has active app (game=%d), quitting...\n",
            server->currentGame);
    quit_app_reliable(client, server, 3, 500000);
    // Poll-wait 1.5s so the host has time to stop the app.
    // Do NOT block with usleep() - keep ProcUI messages flowing.
    {
      uint64_t deadline = OSGetTime() + OSMillisecondsToTicks(1500); // 1.5s in ticks
      while (OSGetTime() < deadline) {
        wiiu_proc_running(); // pump ProcUI messages
        usleep(100000);      // 100ms sleep between polls
      }
    }
  }

  int ret = gs_start_app(client, server, &config->stream, appId, server->isGfe,
                         config->sops, config->localaudio, gamepad_mask);
  if (ret < 0) {
    const char *err_msg = NULL;
    if (ret == GS_NOT_SUPPORTED_4K) {
      fprintf(stderr, "Server doesn't support 4K\n");
      err_msg = "Server doesn't support 4K\n";
    } else if (ret == GS_NOT_SUPPORTED_MODE) {
      fprintf(stderr,
              "Server doesn't support %dx%d (%d fps) or remove --nounsupported "
              "option\n",
              config->stream.width, config->stream.height, config->stream.fps);
      snprintf(message_buffer, sizeof(message_buffer),
               "Mode %dx%d @%dfps not supported\n",
               config->stream.width, config->stream.height, config->stream.fps);
      err_msg = message_buffer;
    } else if (ret == GS_NOT_SUPPORTED_SOPS_RESOLUTION) {
      fprintf(stderr,
              "Optimal Playable Settings isn't supported for the resolution "
              "%dx%d, use supported resolution or add --nosops option\n",
              config->stream.width, config->stream.height);
      err_msg = "Optimal Playable Settings not supported for this resolution\n";
    } else if (ret == GS_ERROR) {
      fprintf(stderr, "Gamestream error: %s\n", gs_get_error_message());
      snprintf(message_buffer, sizeof(message_buffer),
               "Gamestream error:\n%s\n", gs_get_error_message());
      err_msg = message_buffer;
    } else if (ret == GS_IO_ERROR) {
      fprintf(stderr, "I/O error starting app\n");
      err_msg = "I/O error starting app\n";
    } else if (ret == GS_WRONG_STATE) {
      fprintf(stderr, "Wrong state error starting app\n");
      err_msg = "Server in wrong state\n";
    } else if (ret == GS_OUT_OF_MEMORY) {
      fprintf(stderr, "Not enough memory\n");
      err_msg = "Not enough memory\n";
    } else {
      fprintf(stderr, "Errorcode starting app: %d\n", ret);
      snprintf(message_buffer, sizeof(message_buffer),
               "Error starting app: %d\n", ret);
      err_msg = message_buffer;
    }
    if (err_msg && (message_buffer[0] == '\0' || err_msg != message_buffer))
      snprintf(message_buffer, sizeof(message_buffer), "%s", err_msg);
    DBG_LOG("[ERROR] gs_start_app failed: ret=%d, msg=%s\n", ret, message_buffer);
    is_error = 1;
    return -1;
  }

  if (config->debug_level > 0) {
    printf("Stream %d x %d, %d fps, %d kbps\n", config->stream.width,
           config->stream.height, config->stream.fps, config->stream.bitrate);
  }

  rumble_handler = wiiu_rumble; // Restore after teardown set it to NULL

  // Re-init SDL audio subsystem before each stream.
  // On Wii U, SDL_CloseAudioDevice() can leave the subsystem in a
  // state where re-opening the device fails without a fresh init.
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  SDL_InitSubSystem(SDL_INIT_AUDIO);

  if (LiStartConnection(&server->serverInfo, &config->stream,
                        &connection_callbacks, &decoder_callbacks_wiiu,
                        &audio_callbacks_wiiu, NULL, 0, config->audio_device,
                        0) != 0) {
    fprintf(stderr, "Failed to start connection\n");
    DBG_LOG("[ERROR] Failed to start connection\n");
    snprintf(message_buffer, sizeof(message_buffer),
             "Failed to start connection\n");
    is_error = 1;
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[]) {  
  wiiu_proc_init();

  // Open debug log file (512KB limit, managed by DBG_LOG macro)
  debug_log_open();
  DBG_LOG("wibelight started\n");

#ifdef DEBUG
  Debug_Init();
  printf("wibelight started\n");
#endif

  // Seed PRNG for pairing PIN generation
  srandom((unsigned)OSGetTime());

  // Init mutex for thread-safe state transitions (ENet callback ↔ main loop)
  OSFastMutex_Init(&stateMutex, "State");

  WHBGfxInit();
  wiiu_setup_renderstate();

  SDL_InitSubSystem(SDL_INIT_AUDIO);

  wiiu_net_init();

  wiiu_input_init();

  Font_Init();

  Font_SetSize(48);
  Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
  Font_Print(40, 70, "Reading configuration...");
  Font_Draw_TVDRC();

  CONFIGURATION config;

  // Init profile defaults BEFORE parsing JSON so defaults are the base
  for (int i = 0; i < MAX_PROFILES; i++)
      profile_set_defaults(&profiles[i], i);

  // config_parse calls config_json_parse internally on WiiU
  bool has_config = config_parse(argc, argv, &config);

  // WiiU-specific overrides
  config.unsupported = true;
  config.sops = false;

  // Apply theme colors
  theme_apply();

  if (!has_config) {
    // First boot — go to profile selector
    DBG_LOG("[INFO] First boot, showing Profile Selector\n");
    state = STATE_PROFILE_SELECTOR;
  } else {
    // Normal boot — load last active profile
    profile_load(active_profile, &config);

    if (!profile_is_configured(active_profile)) {
      state = STATE_DISCONNECTED;
    } else if (autostream) {
      state = STATE_CONNECTING;
    } else if (autostart_connect) {
      state = STATE_CONNECTING;
    } else {
      state = STATE_DISCONNECTED;
    }
  }

  wiiu_stream_init(config.stream.width, config.stream.height);

  GS_CLIENT client = gs_new(config.key_dir);
  if (client == NULL && gs_get_error(NULL) == GS_BAD_CONF) {
    // Show a friendly message while generating keys (blocking, ~2-5s)
    Font_Clear();
    Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
    draw_logo();
    Font_SetSize(28);
    Font_Print(1920 - 40 - Font_GetTextWidth("First run setup"), 78, "First run setup");
    Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
    Font_DrawHLine(0, 1920, 140);
    Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
    Font_SetSize(32);
    Font_Print(40, 200, "Generating secure certificates and");
    Font_Print(40, 245, "encryption keys for pairing.");
    Font_Print(40, 340, "This is a one-time process and");
    Font_Print(40, 385, "may take a few moments.");
    Font_Draw_TVDRC();

    uint64_t keys_start = OSGetTime();
    if (gs_conf_init(config.key_dir) != GS_OK) {
      fprintf(stderr, "Failed to create client info: %s\n",
              gs_get_error_message());
      DBG_LOG("[FATAL] Failed to create client info: %s\n",
              gs_get_error_message());
      Font_Clear();
      Font_Printf(40, 170, "Failed to create client info:\n %s.",
                  gs_get_error_message());
      Font_Draw_TVDRC();
      state = STATE_INVALID;
    } else {
      // Keep the screen visible for at least 5 seconds so the user reads it
      uint64_t elapsed = OSGetTime() - keys_start;
      uint64_t min_display = OSSecondsToTicks(5);
      if (elapsed < min_display) {
        uint64_t remaining_ns = min_display - elapsed;
        usleep(remaining_ns / 1000);
      }
      client = gs_new(config.key_dir);
    }
  }

  if (client == NULL) {
    fprintf(stderr, "Failed to create GameStream client: %s\n",
            gs_get_error_message());
    DBG_LOG("[FATAL] Failed to create GameStream client: %s\n",
            gs_get_error_message());
    Font_Clear();
    Font_Printf(40, 170, "Failed to create GameStream client:\n %s.",
                gs_get_error_message());
    Font_Draw_TVDRC();
    state = STATE_INVALID;
  }

  SERVER_DATA server;
  while (wiiu_proc_running()) {
    switch (state) {
    case STATE_INVALID: {
      Font_Draw_TVDRC();
      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_HOME) {
        DBG_LOG("[INFO] User pressed HOME, shutting down\n");
        break;
      }
      break;
    }
    case STATE_MENU: {
      state = STATE_PROFILE_SELECTOR;
      break;
    }
    case STATE_PROFILE_SELECTOR: {
      // Save theme presets on first entry, then preview hovered profile's theme
      if (sel_cursor_prev < 0) {
        sel_saved_bg = theme_bg_preset;
        sel_saved_accent = theme_accent_preset;
        sel_saved_text = theme_text_preset;
        sel_saved_btn = theme_btn_preset;
        sel_cursor_prev = profile_cursor;
      }
      if (profile_cursor != sel_cursor_prev) {
        theme_bg_preset = profiles[profile_cursor].theme_bg;
        theme_accent_preset = profiles[profile_cursor].theme_accent;
        theme_text_preset = profiles[profile_cursor].theme_text;
        theme_btn_preset = profiles[profile_cursor].theme_btn;
        theme_apply();
        sel_cursor_prev = profile_cursor;
      }

      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);

      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Profile Selector"), 78, "Profile Selector");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      int y = 280;
      for (int slot = 0; slot < MAX_PROFILES; slot++) {
        /* divider line above this slot (skip first) */
        if (slot > 0) {
          Font_SetColor(theme_item_div_r, theme_item_div_g, theme_item_div_b, theme_item_div_a);
          Font_DrawHLine(40, 1880, y - 12);
        }

        Font_SetSize(48);
        if (slot == profile_cursor) {
          /* highlight bar */
          Font_SetColor(theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a);
          {
            int asc = Font_GetAscender();
            int desc = Font_GetDescender();
            int pad = 8;
            int text_top = y - asc;
            int text_bottom = y - desc;
            int text_height = text_bottom - text_top;
            Font_DrawRect(20, text_top - pad, 1880, text_height + 2 * pad);
          }
        }

        // Profile name (left)
        if (slot == profile_cursor)
          Font_SetColor(theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a);
        else
          Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
        Font_Print(40, y, profiles[slot].name);

        // IP or "Not configured" (right)
        bool configured = profile_is_configured(slot);
        char right_text[32];
        if (configured)
          profile_ip_to_str(slot, right_text, sizeof(right_text));
        else
          snprintf(right_text, sizeof(right_text), "Not configured");

        if (slot == profile_cursor)
          Font_SetColor(theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a);
        else
          Font_SetColor(theme_val_text_r, theme_val_text_g, theme_val_text_b, theme_val_text_a);
        Font_Print(1920 - 40 - Font_GetTextWidth(right_text), y, right_text);

        y += 70;
      }

      // Bottom bar
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Select");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Back");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & (VPAD_BUTTON_UP | VPAD_BUTTON_STICK_L)) {
        profile_cursor = (profile_cursor + MAX_PROFILES - 1) % MAX_PROFILES;
        wiiu_nav_click();
      } else if (btns & (VPAD_BUTTON_DOWN | VPAD_BUTTON_STICK_L)) {
        profile_cursor = (profile_cursor + 1) % MAX_PROFILES;
        wiiu_nav_click();
      } else if (btns & VPAD_BUTTON_A) {
        // Restore original theme presets before saving (preview may have changed them)
        theme_bg_preset = sel_saved_bg;
        theme_accent_preset = sel_saved_accent;
        theme_text_preset = sel_saved_text;
        theme_btn_preset = sel_saved_btn;
        theme_apply();
        // Save current profile, then load selected
        profile_save(active_profile, &config);
        if (profile_is_configured(profile_cursor)) {
          // Configured profile — load it and return
          profile_load(profile_cursor, &config);
          auto_save_settings(&config);
          state = profile_selector_prev_state;
        } else {
          // Not configured — load it and go to IP editor
          profile_load(profile_cursor, &config);
          ip_edit_buf[0] = 192; ip_edit_buf[1] = 168; ip_edit_buf[2] = 1; ip_edit_buf[3] = 0;
          ip_edit_cursor = 0;
          ip_edit_return_state = STATE_PROFILE_SELECTOR;
          state = STATE_PROFILE_IP_EDIT;
        }
        sel_cursor_prev = -1;
      } else if (btns & VPAD_BUTTON_B) {
        // Restore original theme
        theme_bg_preset = sel_saved_bg;
        theme_accent_preset = sel_saved_accent;
        theme_text_preset = sel_saved_text;
        theme_btn_preset = sel_saved_btn;
        theme_apply();
        sel_cursor_prev = -1;
        state = profile_selector_prev_state;
      }
      break;
    }
    case STATE_PROFILE_IP_EDIT: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);

      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Edit Server IP"), 78, "Edit Server IP");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      // Render IP centered
      Font_SetSize(72);
      int px = (1920 - (Font_GetTextWidth("255.255.255.255"))) / 2;
      for (int oct = 0; oct < 4; oct++) {
        char oct_str[4];
        snprintf(oct_str, sizeof(oct_str), "%d", ip_edit_buf[oct]);
        if (oct == ip_edit_cursor)
          Font_SetColor(theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a);
        else
          Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
        Font_Print(px, 440, oct_str);
        px += Font_GetTextWidth(oct_str);
        if (oct < 3) {
          Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
          Font_Print(px, 440, ".");
          px += Font_GetTextWidth(".");
        }
      }

      // Bottom bar
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Set");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Cancel");
      Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', "Inc +1");
      Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Dec -1");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & (VPAD_BUTTON_LEFT | VPAD_BUTTON_STICK_L)) {
        ip_edit_cursor = (ip_edit_cursor + 3) % 4;
      } else if (btns & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_STICK_R)) {
        ip_edit_cursor = (ip_edit_cursor + 1) % 4;
      } else if (btns & VPAD_BUTTON_X) {
        ip_edit_buf[ip_edit_cursor] = (ip_edit_buf[ip_edit_cursor] + 1) % 256;
      } else if (btns & VPAD_BUTTON_Y) {
        ip_edit_buf[ip_edit_cursor] = (ip_edit_buf[ip_edit_cursor] + 255) % 256;
      } else if (btns & VPAD_BUTTON_PLUS) {
        ip_edit_buf[ip_edit_cursor] = (ip_edit_buf[ip_edit_cursor] + 1) % 256;
      } else if (btns & VPAD_BUTTON_MINUS) {
        ip_edit_buf[ip_edit_cursor] = (ip_edit_buf[ip_edit_cursor] + 255) % 256;
      } else if (btns & VPAD_BUTTON_A) {
        // Apply IP
        profiles[active_profile].ip[0] = ip_edit_buf[0];
        profiles[active_profile].ip[1] = ip_edit_buf[1];
        profiles[active_profile].ip[2] = ip_edit_buf[2];
        profiles[active_profile].ip[3] = ip_edit_buf[3];
        char ip_str[24];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 ip_edit_buf[0], ip_edit_buf[1], ip_edit_buf[2], ip_edit_buf[3]);
        if (config.address) free((void*)config.address);
        config.address = strdup(ip_str);
        auto_save_settings(&config);
        if (ip_edit_return_state == STATE_PROFILE_SELECTOR) {
          state = STATE_PROFILE_SELECTOR;
        } else if (ip_edit_return_state == STATE_SETTINGS) {
          state = STATE_SETTINGS;
        } else {
          state = STATE_DISCONNECTED;
        }
      } else if (btns & VPAD_BUTTON_B) {
        state = ip_edit_return_state;
      }
      break;
    }
    case STATE_SETTINGS: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);

      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Settings"), 78, "Settings");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      int y = 240;
      const int row_height = 54;
      const int header_indent = 40;
      const int label_x = 100;
      int visible_count = settings_visible_count();

      // Clamp cursor
      if (visible_count > 0 && settings_cursor >= visible_count)
        settings_cursor = visible_count - 1;

      // Render each section
      {
        int vis_row = 0;
        y = 240;
        for (int s = 0; s < NUM_SECTIONS; s++) {
          int sel = (settings_cursor == vis_row);

          // Section header
          if (s > 0) {
            Font_SetColor(theme_item_div_r, theme_item_div_g, theme_item_div_b, theme_item_div_a);
            Font_DrawHLine(40, 1880, y - 8);
          }
          if (sel) {
            Font_SetSize(36);
            Font_SetColor(theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a);
            {
              int asc = Font_GetAscender();
              int desc = Font_GetDescender();
              int pad = 6;
              int text_top = y - asc;
              int text_bottom = y - desc;
              int text_height = text_bottom - text_top;
              Font_DrawRect(20, text_top - pad, 1880, text_height + 2 * pad);
            }
          }
          Font_SetColor(sel ? theme_sel_text_r : theme_label_r,
                        sel ? theme_sel_text_g : theme_label_g,
                        sel ? theme_sel_text_b : theme_label_b, 255);
          Font_SetSize(36);
          // Use '>' and 'v' - system font lacks Unicode block element glyphs
          char header_text[64];
          if (section_expanded[s])
            snprintf(header_text, sizeof(header_text), "v  %s", section_names[s]);
          else
            snprintf(header_text, sizeof(header_text), ">  %s", section_names[s]);
          Font_Print(header_indent, y, header_text);
          y += row_height;
          vis_row++;

          // Children (only if expanded)
          if (section_expanded[s]) {
            for (int c = 0; c < section_child_count[s]; c++) {
              sel = (settings_cursor == vis_row);

              Font_SetColor(theme_item_div_r, theme_item_div_g, theme_item_div_b, theme_item_div_a);
              Font_DrawHLine(40, 1880, y - 8);

              if (sel) {
                Font_SetSize(36);
                Font_SetColor(theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a);
                {
                  int asc = Font_GetAscender();
                  int desc = Font_GetDescender();
                  int pad = 6;
                  int text_top = y - asc;
                  int text_bottom = y - desc;
                  int text_height = text_bottom - text_top;
                  Font_DrawRect(20, text_top - pad, 1880, text_height + 2 * pad);
                }
              }

              // Label
              Font_SetColor(sel ? theme_sel_text_r : theme_label_r,
                            sel ? theme_sel_text_g : theme_label_g,
                            sel ? theme_sel_text_b : theme_label_b, 255);
              Font_SetSize(36);
              Font_Print(label_x, y, section_children[s][c]);

              // Value � right-aligned
              char val[64] = "";
              int setting_idx = section_child_to_setting_idx(s, c);
              switch (setting_idx) {
              case 0: // Resolution
                snprintf(val, sizeof(val), "%dx%d", config.stream.width, config.stream.height);
                break;
              case 1: // FPS
                snprintf(val, sizeof(val), "%d", config.stream.fps);
                break;
              case 2: // Bitrate
                snprintf(val, sizeof(val), "%d kbps", config.stream.bitrate);
                break;
              case 3: // Color Space
                snprintf(val, sizeof(val), "%s", COLOR_SPACE_LABELS[(config.stream.colorSpace == COLORSPACE_REC_709) ? 1 : 0]);
                break;
              case 4: // Color Range
                snprintf(val, sizeof(val), "%s", COLOR_RANGE_LABELS[(config.stream.colorRange == COLOR_RANGE_FULL) ? 1 : 0]);
                break;
              case 5: // Max Queued Frames
                snprintf(val, sizeof(val), "%d", max_queued_frames);
                break;
              case 6: // Rotate
                snprintf(val, sizeof(val), "%d deg", config.rotate);
                break;
              case 7: // Surround
                snprintf(val, sizeof(val), "%s", SURROUND_LABELS[find_surround_index(config.stream.audioConfiguration)]);
                break;
              case 8: // Audio Buffer
                snprintf(val, sizeof(val), "%d", audio_buffer_samples);
                break;
              case 9: // Local Audio
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[config.localaudio]);
                break;
              case 10: // Swap Buttons
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[swap_buttons]);
                break;
              case 11: // Mouse Mode
                snprintf(val, sizeof(val), "%s", MOUSE_MODE_LABELS[find_mouse_mode_index(mouse_mode)]);
                break;
              case 12: // Enable Rumble
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[enable_rumble]);
                break;
              case 13: // Rumble Strength
                static const char *rumble_labels[] = {"Low", "Medium", "High", "Full"};
                snprintf(val, sizeof(val), "%s", rumble_labels[rumble_strength]);
                break;
              case 14: // Menu Rumble
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[enable_nav_click]);
                break;
              case 15: // Disable GamePad
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[disable_gamepad]);
                break;
              case 16: // Packet Size
                snprintf(val, sizeof(val), "%d", config.stream.packetSize);
                break;
              case 17: // Server IP
                snprintf(val, sizeof(val), "%s", config.address ? config.address : "(none)");
                break;
              case 18: // Wake-on-LAN
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[wol_enabled]);
                break;
              case 19: // MAC Address
                snprintf(val, sizeof(val), "%s", wol_mac[0] != '\0' ? wol_mac : "(not detected)");
                break;
              case 20: // WoL Wait Time
                snprintf(val, sizeof(val), "%d s", wol_wait_seconds);
                break;
              case 21: // Quit After
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[config.quitappafter]);
                break;
              case 22: // View Only
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[config.viewonly]);
                break;
              case 23: // App
                snprintf(val, sizeof(val), "%s", APP_LABELS[find_app_index(config.app)]);
                break;
              case 24: // Auto Stream
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[autostream]);
                break;
              case 25: // Auto Connect
                snprintf(val, sizeof(val), "%s", BOOL_LABELS[autostart_connect]);
                break;
              case 26: // Background
                snprintf(val, sizeof(val), "%s", bg_labels[theme_bg_preset]);
                break;
              case 27: // Accent Color
                snprintf(val, sizeof(val), "%s", accent_labels[theme_accent_preset]);
                break;
              case 28: // Text Contrast
                snprintf(val, sizeof(val), "%s", text_labels[theme_text_preset]);
                break;
              case 29: // Button Color
                snprintf(val, sizeof(val), "%s", btn_labels[theme_btn_preset]);
                break;
              case 30: // Logo
                snprintf(val, sizeof(val), "%s", logo_labels[theme_logo_preset]);
                break;
              default:
                snprintf(val, sizeof(val), "?");
                break;
              }

              Font_SetColor(sel ? theme_sel_text_r : theme_val_text_r,
                            sel ? theme_sel_text_g : theme_val_text_g,
                            sel ? theme_sel_text_b : theme_val_text_b, 255);
              Font_SetSize(36);
              Font_Print(1800 - Font_GetTextWidth(val), y, val);
              y += row_height;
              vis_row++;
            }
          }
        }
      }

      /* Bottom bar - fixed position at bottom of screen */
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Change");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Back");
      Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', "Connect");
      Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Adj. Bitrate");

      /* Version number - lower right corner */
      Font_SetColor(theme_val_text_r, theme_val_text_g, theme_val_text_b, 120);
      Font_SetSize(20);
      Font_Print(1920 - 12 - Font_GetTextWidth(APP_VERSION), 1068, APP_VERSION);

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & (VPAD_BUTTON_UP | VPAD_BUTTON_STICK_L)) {
        visible_count = settings_visible_count();
        if (visible_count > 0) {
          settings_cursor = (settings_cursor + visible_count - 1) % visible_count;
          wiiu_nav_click();
        }

      } else if (btns & (VPAD_BUTTON_DOWN | VPAD_BUTTON_STICK_L)) {
        visible_count = settings_visible_count();
        if (visible_count > 0) {
          settings_cursor = (settings_cursor + 1) % visible_count;
          wiiu_nav_click();
        }

      } else if (btns & VPAD_BUTTON_A) {
        // Resolve cursor to (section, child)
        int sec, child;
        settings_visible_to_section(settings_cursor, &sec, &child);

        if (child == -1) {
          // Header row - toggle expand/collapse
          // Only one section expanded at a time: collapse all others first
          if (!section_expanded[sec]) {
            // About to expand - collapse every other section
            for (int _s = 0; _s < NUM_SECTIONS; _s++)
              section_expanded[_s] = 0;
          }
          section_expanded[sec] = !section_expanded[sec];
          if (section_expanded[sec]) {
            settings_cursor++; // move to first child
          } else {
            // Collapsed - clamp cursor to new visible count
            visible_count = settings_visible_count();
            if (settings_cursor >= visible_count)
              settings_cursor = visible_count - 1;
          }
        } else {
          // Child row � toggle the setting value
          int setting_idx = section_child_to_setting_idx(sec, child);
          switch (setting_idx) {
          case 0: { // Resolution
            if (config.stream.width * config.stream.height <= 854 * 480) {
              config.stream.width = 1280;
              config.stream.height = 720;
            } else if (config.stream.width * config.stream.height <= 1280 * 720) {
              config.stream.width = 1920;
              config.stream.height = 1080;
            } else {
              config.stream.width = 854;
              config.stream.height = 480;
            }
            break;
          }
          case 1: { // FPS
            config.stream.fps = (config.stream.fps == 60) ? 30 : 60;
            break;
          }
          case 2: { // Bitrate
            static const int br_presets[] = {1000, 3000, 5000, 8000, 10000, 15000, 20000};
#define NUM_BR_PRESETS 7
            int cur_idx = 0;
            for (int i = 0; i < NUM_BR_PRESETS; i++) {
              if (config.stream.bitrate == br_presets[i]) {
                cur_idx = (i + 1) % NUM_BR_PRESETS;
                break;
              }
              if (i == NUM_BR_PRESETS - 1) {
                cur_idx = 0;
              } else if (config.stream.bitrate > br_presets[i] &&
                         config.stream.bitrate < br_presets[i + 1]) {
                cur_idx = i + 1;
              }
            }
            config.stream.bitrate = br_presets[cur_idx];
#undef NUM_BR_PRESETS
            break;
          }
          case 3: { // Color Space
            config.stream.colorSpace =
                (config.stream.colorSpace == COLORSPACE_REC_709)
                    ? COLORSPACE_REC_601
                    : COLORSPACE_REC_709;
            break;
          }
          case 4: { // Color Range
            config.stream.colorRange =
                (config.stream.colorRange == COLOR_RANGE_FULL)
                    ? COLOR_RANGE_LIMITED
                    : COLOR_RANGE_FULL;
            break;
          }
          case 5: { // Max Queued Frames - cycle 2 → 4 → 8 → 2
            static const int qf_presets[] = {2, 4, 8};
            int qi = 0;
            if (max_queued_frames == 4) qi = 1;
            else if (max_queued_frames == 8) qi = 2;
            qi = (qi + 1) % 3;
            max_queued_frames = qf_presets[qi];
            break;
          }
          case 6: { // Rotate - cycle 0 -> 90 -> 180 -> 270 -> 0 deg
            config.rotate = ((config.rotate + 90) % 360);
            break;
          }
          case 7: { // Surround
            {
              int surr_idx = find_surround_index(config.stream.audioConfiguration);
              surr_idx = (surr_idx + 1) % NUM_SURROUND;
              config.stream.audioConfiguration = SURROUND_PRESETS[surr_idx];
            }
            break;
          }
          case 8: { // Audio Buffer - toggle 4096 ↔ 2048
            audio_buffer_samples = (audio_buffer_samples == 4096) ? 2048 : 4096;
            break;
          }
          case 9: { // Local Audio
            config.localaudio = !config.localaudio;
            break;
          }
          case 10: { // Swap Buttons
            swap_buttons = !swap_buttons;
            break;
          }
          case 11: { // Mouse Mode
            {
              int mm_idx = find_mouse_mode_index(mouse_mode);
              mm_idx = (mm_idx + 1) % NUM_MOUSE_MODES;
              mouse_mode = MOUSE_MODE_PRESETS[mm_idx];
            }
            break;
          }
          case 12: { // Enable Rumble
            enable_rumble = !enable_rumble;
            break;
          }
          case 13: { // Rumble Strength - cycle Low → Medium → High → Full → Low
            rumble_strength = (rumble_strength + 1) % 4;
            break;
          }
          case 14: { // Menu Rumble
            enable_nav_click = !enable_nav_click;
            break;
          }
          case 15: { // Disable GamePad
            disable_gamepad = !disable_gamepad;
            break;
          }
          case 16: { // Packet Size - cycle 1024 → 1232 → 1392 → 1472 → 1024
            static const int ps_presets[] = {1024, 1232, 1392, 1472};
            int pi = 0;
            for (int i = 0; i < 4; i++) {
              if (config.stream.packetSize == ps_presets[i]) {
                pi = (i + 1) % 4;
                break;
              }
            }
            config.stream.packetSize = ps_presets[pi];
            break;
          }
          case 17: { // Server IP — open dedicated IP editor
            if (config.address)
                sscanf(config.address, "%hhu.%hhu.%hhu.%hhu",
                       &ip_edit_buf[0], &ip_edit_buf[1],
                       &ip_edit_buf[2], &ip_edit_buf[3]);
            else
                memset(ip_edit_buf, 0, 4);
            ip_edit_cursor = 0;
            ip_edit_return_state = STATE_SETTINGS;
            state = STATE_PROFILE_IP_EDIT;
            break;
          }
          case 18: { // Wake-on-LAN
            wol_enabled = !wol_enabled;
            break;
          }
          case 19: { // MAC Address — read-only display, no toggle
            break;
          }
          case 20: { // WoL Wait Time — cycle 10 → 15 → 20 → 25 → 30 → 45 → 60
            static const int wait_presets[] = {10, 15, 20, 25, 30, 45, 60};
            static const int num_wait_presets = 7;
            int wi = 0;
            for (int i = 0; i < num_wait_presets; i++) {
                if (wol_wait_seconds == wait_presets[i]) {
                    wi = (i + 1) % num_wait_presets;
                    break;
                }
                if (i == num_wait_presets - 1) {
                    wi = 0;
                }
            }
            wol_wait_seconds = wait_presets[wi];
            break;
          }
          case 21: { // Quit After
            config.quitappafter = !config.quitappafter;
            break;
          }
          case 22: { // View Only
            config.viewonly = !config.viewonly;
            break;
          }
          case 23: { // App
            {
              int app_idx = find_app_index(config.app);
              app_idx = (app_idx + 1) % NUM_APPS;
              if (config.app)
                free((void *)config.app);
              config.app = strdup(APP_PRESETS[app_idx]);
            }
            break;
          }
          case 24: { // Auto Stream
            autostream = !autostream;
            break;
          }
          case 25: { // Auto Connect
            autostart_connect = !autostart_connect;
            break;
          }
          case 26: { // Background
            theme_bg_preset = (theme_bg_preset + 1) % NUM_BG_PRESETS;
            // Auto-adjust text contrast to maintain readability
            theme_apply();
            theme_text_preset = theme_best_text_preset(theme_bg_r, theme_bg_g, theme_bg_b,
                                                       theme_text_preset, 3.0f);
            theme_apply();
            break;
          }
          case 27: { // Accent Color
            theme_accent_preset = (theme_accent_preset + 1) % NUM_ACCENT_PRESETS;
            theme_apply();
            break;
          }
          case 28: { // Text Contrast
            theme_text_preset = (theme_text_preset + 1) % NUM_TEXT_PRESETS;
            theme_apply();
            break;
          }
          case 29: { // Button Color
            theme_btn_preset = (theme_btn_preset + 1) % NUM_BTN_PRESETS;
            theme_apply();
            break;
          }
          case 30: { // Logo
            theme_logo_preset = (theme_logo_preset + 1) % NUM_LOGO_PRESETS;
            break;
          }
          default:
            break;
          }
        }
        auto_save_settings(&config);
        theme_apply();
      } else if (btns & VPAD_BUTTON_Y) {
        custom_br_value =
            (config.stream.bitrate < 500) ? 5000 : config.stream.bitrate;
        state = STATE_CUSTOM_BITRATE;
      } else if (btns & VPAD_BUTTON_B) {
        state = settings_prev_state;
      } else if (btns & VPAD_BUTTON_X) {
        if (config.address) {
          auto_save_settings(&config);
          state = STATE_CONNECTING;
        }
      }
      break;
    }
    case STATE_CUSTOM_BITRATE: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);

      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Adjust Bitrate"), 78, "Adjust Bitrate");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      // Render bitrate value centered on screen
      Font_SetSize(72);
      char br_line[16];
      snprintf(br_line, sizeof(br_line), "%d kbps", custom_br_value);
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      Font_Print((1920 - Font_GetTextWidth(br_line)) / 2, 340, br_line);

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Set");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Cancel");
      Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', "Inc +250");
      Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Dec -250");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_X) {
        custom_br_value += CUSTOM_BR_STEP;
        if (custom_br_value > 99999)
          custom_br_value = 99999;
      } else if (btns & VPAD_BUTTON_Y) {
        custom_br_value -= CUSTOM_BR_STEP;
        if (custom_br_value < 500)
          custom_br_value = 500;
      } else if (btns & VPAD_BUTTON_PLUS) {
        custom_br_value += CUSTOM_BR_STEP;
        if (custom_br_value > 99999)
          custom_br_value = 99999;
      } else if (btns & VPAD_BUTTON_MINUS) {
        custom_br_value -= CUSTOM_BR_STEP;
        if (custom_br_value < 500)
          custom_br_value = 500;
      } else if (btns & VPAD_BUTTON_A) {
        // Apply custom bitrate
        config.stream.bitrate = custom_br_value;
        auto_save_settings(&config);
        if (custom_bitrate_return_to_benchmark) {
          custom_bitrate_return_to_benchmark = 0;
          state = STATE_BENCHMARK_RESULTS;
        } else {
          state = STATE_SETTINGS;
        }
      } else if (btns & VPAD_BUTTON_B) {
        // Cancel
        if (custom_bitrate_return_to_benchmark) {
          custom_bitrate_return_to_benchmark = 0;
          state = STATE_BENCHMARK_RESULTS;
        } else {
          state = STATE_SETTINGS;
        }
      }
      break;
    }
    case STATE_SENDING_WOL: {
      /* Send WoL packet once on entry, then wait */
      if (wol_start_time == 0) {
        wol_start_time = OSGetTime();
        if (!wol_send(wol_mac, NULL)) {
          DBG_LOG("[WoL] Send failed\n");
          wol_start_time = 0;
          wol_attempt = 0;
          snprintf(message_buffer, sizeof(message_buffer),
                   "Failed to send Wake-on-LAN\n");
          is_error = 1;
          state = STATE_DISCONNECTED;
          break;
        }
      }

      uint64_t elapsed = OSGetTime() - wol_start_time;
      int remaining = wol_wait_seconds - (int)(elapsed / OSSecondsToTicks(1));
      if (remaining < 0) remaining = 0;

      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      {
        char wol_label[128];
        snprintf(wol_label, sizeof(wol_label), "Waking up %s", config.address ? config.address : "host");
        Font_SetSize(28);
        Font_Print(1920 - 40 - Font_GetTextWidth(wol_label), 78, wol_label);
      }
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      /* Animated spinner */
      draw_spinner_centered(960, 440, 65);

      /* Status text */
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(36);
      if (wol_attempt == 1)
          Font_Print(40, 540, "WoL magic packet sent");
      else {
          char status_str[64];
          snprintf(status_str, sizeof(status_str),
                   "Resending WoL (attempt %d/%d)", wol_attempt, WOL_MAX_ATTEMPTS);
          Font_Print(40, 540, status_str);
      }

      Font_SetSize(28);
      Font_Print(40, 590, "Waiting for host to boot...");

      /* Countdown */
      char countdown_str[64];
      snprintf(countdown_str, sizeof(countdown_str), "%d seconds remaining", remaining);
      Font_SetColor(theme_val_text_r, theme_val_text_g, theme_val_text_b, 255);
      Font_SetSize(28);
      Font_Print(40, 630, countdown_str);

      /* MAC shown for confirmation */
      if (wol_mac[0] != '\0') {
        char mac_line[128];
        snprintf(mac_line, sizeof(mac_line), "MAC: %s", wol_mac);
        Font_SetColor(theme_label_r, theme_label_g, theme_label_b, 180);
        Font_SetSize(24);
        Font_Print(40, 680, mac_line);
      }

      /* Bottom bar */
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'B', "Cancel");

      Font_EndDraw();
      Font_Draw_TVDRC();

      /* Check if wait time has elapsed */
      if (elapsed >= OSSecondsToTicks(wol_wait_seconds)) {
        wol_start_time = 0;
        wol_attempt++;
        if (wol_attempt <= WOL_MAX_ATTEMPTS) {
          DBG_LOG("[WoL] Wait complete (attempt %d/%d), retrying connection\n",
                  wol_attempt, WOL_MAX_ATTEMPTS);
          state = wol_source_state;
          break;
        }
        /* Exhausted all WoL attempts */
        DBG_LOG("[WoL] All %d attempts exhausted, host did not respond\n", WOL_MAX_ATTEMPTS);
        wol_attempt = 0;
        snprintf(message_buffer, sizeof(message_buffer),
                 "Host did not respond after %d Wake-on-LAN attempts\n", WOL_MAX_ATTEMPTS);
        is_error = 1;
        state = STATE_DISCONNECTED;
        break;
      }

      /* Allow user to cancel with B */
      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_B) {
        wol_start_time = 0;
        wol_attempt = 0;
        DBG_LOG("[WoL] User cancelled\n");
        snprintf(message_buffer, sizeof(message_buffer),
                 "Wake-on-LAN cancelled\n");
        is_error = 0;
        state = STATE_DISCONNECTED;
      }
      break;
    }
    case STATE_DISCONNECTED: {
      // Update cooldown state: show message while active, clear UI when expired
      if (reconnect_cooldown_deadline > 0 && OSGetTime() < reconnect_cooldown_deadline) {
        snprintf(message_buffer, sizeof(message_buffer),
                 "Waiting for host to clean up...\n");
        is_error = 1;
      } else if (reconnect_cooldown_deadline > 0) {
        // Cooldown just expired — clear the message for a clean UI
        reconnect_cooldown_deadline = 0;
        message_buffer[0] = '\0';
        is_error = 0;
      }

      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);

      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("(Disconnected)"), 78, "(Disconnected)");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(is_error ? 255 : 0, is_error ? 0 : 255, 0, 255);
      Font_SetSize(32);
      Font_PrintClipped(40, 200, 1840, message_buffer);

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      if (config.address) {
        Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Connect");
        Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Settings");
        Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', "Pair");
        Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Benchmark");
        Font_DrawButtonIcon(1648, BAR_BUTTONS_Y, '-', "Profile");
      } else {
        Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Enter IP");
      }

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_A) {
        if (config.address) {
          // Block connect while cooldown is active
          if (reconnect_cooldown_deadline > 0 && OSGetTime() < reconnect_cooldown_deadline) {
            break;
          }
          if (reconnect_cooldown_deadline > 0) {
            reconnect_cooldown_deadline = 0;
          }
          message_buffer[0] = '\0';
          is_error = 0;
          stream_entry_state = 1; // from Disconnected
          state = STATE_CONNECTING;
        } else {
          is_error = 0;
          message_buffer[0] = '\0';
          profile_selector_prev_state = STATE_DISCONNECTED;
          state = STATE_PROFILE_SELECTOR;
        }
      } else if (btns & VPAD_BUTTON_B) {
        if (config.address) {
          is_error = 0;
          message_buffer[0] = '\0';
          settings_prev_state = STATE_DISCONNECTED;
          state = STATE_SETTINGS;
        } else {
          is_error = 0;
          message_buffer[0] = '\0';
          profile_selector_prev_state = STATE_DISCONNECTED;
          state = STATE_PROFILE_SELECTOR;
        }
      } else if (btns & VPAD_BUTTON_X) {
        if (config.address) {
          is_error = 0;
          message_buffer[0] = '\0';
          stream_entry_state = 1; // from Disconnected
          state = STATE_CONNECTING;
        }
      } else if (btns & VPAD_BUTTON_Y) {
        is_error = 0;
        message_buffer[0] = '\0';
        if (config.address) {
          benchmark_duration_idx = 1;
          benchmark_was_from_disconnected = 1;
          state = STATE_BENCHMARK;
        }
      } else if (btns & VPAD_BUTTON_MINUS) {
        is_error = 0;
        message_buffer[0] = '\0';
        profile_selector_prev_state = STATE_DISCONNECTED;
        state = STATE_PROFILE_SELECTOR;
      }
      break;
    }
        case STATE_CONNECTING: {
      printf("Connecting to %s...\n", config.address);

      static uint64_t connect_start = 0;
      if (connect_start == 0)
        connect_start = OSGetTime();

      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      {
        char conn_label[128];
        snprintf(conn_label, sizeof(conn_label), "Connecting to %s", config.address);
        Font_SetSize(28);
        Font_Print(1920 - 40 - Font_GetTextWidth(conn_label), 78, conn_label);
      }

      // Animated spinner
      draw_spinner_centered(960, 560, 65);

      // Connection step text
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(36);
      Font_Print(40, 540, connection_step_text(connect_start));

      Font_EndDraw();
      Font_Draw_TVDRC();

      int ret;
      if ((ret = gs_get_status(client, &server, config.address,
                               config.unsupported)) == GS_OUT_OF_MEMORY) {
        fprintf(stderr, "Not enough memory\n");
        snprintf(message_buffer, sizeof(message_buffer), "Not enough memory\n");
        is_error = 1;
        connect_start = 0;
        state = STATE_DISCONNECTED;
        break;
      } else if (ret == GS_ERROR) {
        fprintf(stderr, "Gamestream error: %s\n", gs_get_error_message());
        snprintf(message_buffer, sizeof(message_buffer),
                 "Gamestream error:\n%s\n", gs_get_error_message());
        is_error = 1;
        connect_start = 0;
        state = STATE_DISCONNECTED;
        break;
      } else if (ret == GS_INVALID) {
        fprintf(stderr, "Invalid data from server: %s\n",
                gs_get_error_message());
        snprintf(message_buffer, sizeof(message_buffer),
                 "Invalid data from server:\n%s\n", gs_get_error_message());
        is_error = 1;
        connect_start = 0;
        state = STATE_DISCONNECTED;
        break;
      } else if (ret == GS_UNSUPPORTED_VERSION) {
        fprintf(stderr, "Unsupported version: %s\n", gs_get_error_message());
        snprintf(message_buffer, sizeof(message_buffer),
                 "Unsupported version:\n%s\n", gs_get_error_message());
        is_error = 1;
        connect_start = 0;
        state = STATE_DISCONNECTED;
        break;
      } else if (ret == GS_IO_ERROR) {
        fprintf(stderr, "I/O error connecting to %s\n", config.address);
        DBG_LOG("[ERROR] I/O error connecting to %s\n", config.address);
        connect_start = 0;

        /* Try Wake-on-LAN if enabled and we have a MAC */
        if (wol_enabled && wol_mac[0] != '\0') {
          DBG_LOG("[WoL] Connect failed, sending magic packet for %s\n", wol_mac);
          wol_source_state = STATE_CONNECTING;
          if (wol_attempt == 0)
              wol_attempt = 1;
          snprintf(message_buffer, sizeof(message_buffer),
                   "Sending Wake-on-LAN...\n");
          is_error = 0;
          state = STATE_SENDING_WOL;
          break;
        }

        snprintf(message_buffer, sizeof(message_buffer),
                 "Connection failed: %s\n", gs_get_error_message());
        is_error = 1;
        state = STATE_DISCONNECTED;
        break;
      } else if (ret == GS_WRONG_STATE) {
        fprintf(stderr, "Server in wrong state: %s\n", gs_get_error_message());
        DBG_LOG("[ERROR] Server in wrong state: %s\n", gs_get_error_message());
        connect_start = 0;
        /* Likely not paired — go straight to pairing */
        message_buffer[0] = '\0';
        is_error = 0;
        state = STATE_PAIRING;
        break;
      } else if (ret != GS_OK) {
        fprintf(stderr, "Can't connect to server %s\n", config.address);
        DBG_LOG("[ERROR] Can't connect to server %s (ret=%d)\n", config.address, ret);
        snprintf(message_buffer, sizeof(message_buffer),
                 "Can't connect to server\n");
        is_error = 1;
        connect_start = 0;
        state = STATE_DISCONNECTED;
        break;
      }

      if (config.debug_level > 0) {
        printf("NVIDIA %s, GFE %s (%s, %s)\n", server.gpuType,
               server.serverInfo.serverInfoGfeVersion, server.gsVersion,
               server.serverInfo.serverInfoAppVersion);
      }

      DBG_LOG("[CONN] server.mac = '%s', server.paired = %d\n", server.mac ? server.mac : "(null)", server.paired);

      /* Auto-save MAC from serverinfo — only trust it when paired,
       * because unpaired HTTP responses return 00:00:00:00:00:00 */
      if (server.paired && server.mac && server.mac[0] != '\0' && wol_mac[0] == '\0') {
        strncpy(wol_mac, server.mac, WOL_MAC_LEN - 1);
        wol_mac[WOL_MAC_LEN - 1] = '\0';
        DBG_LOG("[WoL] Auto-detected MAC: %s\n", wol_mac);
        auto_save_settings(&config);
      }

      connect_start = 0;

      /* If not paired, redirect to pairing */
      if (!server.paired) {
        DBG_LOG("[CONN] Server reports not paired, entering pairing flow\n");
        message_buffer[0] = '\0';
        is_error = 0;
        state = STATE_PAIRING;
        break;
      }

      if (autostream) {
        state = STATE_START_STREAM;
        break;
      }
      /* Clear any stale message from WoL or previous errors */
      message_buffer[0] = '\0';
      is_error = 0;
      wol_attempt = 0; // Reset WoL attempt counter on successful connection
      state = STATE_CONNECTED;
      break;
    }
    case STATE_CONNECTED: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      {
        char conn_label[128];
        snprintf(conn_label, sizeof(conn_label), "(Connected to %s)", config.address);
        Font_Print(1920 - 40 - Font_GetTextWidth(conn_label), 78, conn_label);
      }
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(is_error ? 255 : 0, is_error ? 0 : 255, 0, 255);
      Font_SetSize(32);

      // Update cooldown state: show message while active, clear UI when expired
      if (reconnect_cooldown_deadline > 0 && OSGetTime() < reconnect_cooldown_deadline) {
        snprintf(message_buffer, sizeof(message_buffer),
                 "Waiting for host to clean up...\n");
        is_error = 1;
      } else if (reconnect_cooldown_deadline > 0) {
        // Cooldown just expired — clear the message for a clean UI
        reconnect_cooldown_deadline = 0;
        message_buffer[0] = '\0';
        is_error = 0;
      }

      Font_PrintClipped(40, 200, 1840, message_buffer);

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Stream");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Settings");
      Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', server.paired ? "Unpair" : "Pair");
      Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Benchmark");
      Font_DrawButtonIcon(1648, BAR_BUTTONS_Y, '-', "Disconnect");
      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_A) {
        // Block stream while cooldown is active
        if (reconnect_cooldown_deadline > 0 && OSGetTime() < reconnect_cooldown_deadline) {
          break;
        }
        if (reconnect_cooldown_deadline > 0) {
          reconnect_cooldown_deadline = 0;
        }
        message_buffer[0] = '\0';
        is_error = 0;
        stream_entry_state = 0; // from Connected
        state = STATE_START_STREAM;
      } else if (btns & VPAD_BUTTON_B) {
        is_error = 0;
        message_buffer[0] = '\0';
        settings_prev_state = STATE_CONNECTED;
        state = STATE_SETTINGS;
      } else if (btns & VPAD_BUTTON_X) {
        if (server.paired) {
          // Show unpair confirmation
          message_buffer[0] = '\0';
          is_error = 0;
          state = STATE_UNPAIR_CONFIRM;
        } else {
          message_buffer[0] = '\0';
          is_error = 0;
          state = STATE_PAIRING;
        }
      } else if (btns & VPAD_BUTTON_Y) {
        is_error = 0;
        message_buffer[0] = '\0';
        benchmark_duration_idx = 1;
        benchmark_was_from_disconnected = 0;
        state = STATE_BENCHMARK;
      } else if (btns & VPAD_BUTTON_MINUS) {
        // Manual disconnect: tear down the server status connection
        is_error = 0;
        message_buffer[0] = '\0';
        snprintf(message_buffer, sizeof(message_buffer),
                 "Disconnected\n");
        state = STATE_DISCONNECTED;
      }
      break;
    }
    case STATE_BENCHMARK: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Benchmark Connection"), 78, "Benchmark Connection");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 175, "Starts a short stream to measure");
      Font_Print(40, 210, "connection quality.");

      // Hint box
      Font_SetColor(theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, 40);
      Font_DrawRect(40, 250, 900, 160);
      Font_SetColor(theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a);
      Font_SetSize(28);
      Font_Print(54, 280, "Tip: For the most accurate results,");
      Font_Print(54, 312, "make sure your game or app is already");
      Font_Print(54, 344, "running at the target framerate");
      Font_Print(54, 376, "before starting the benchmark.");

      // Duration presets
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(36);
      int dur_y = 500;
      Font_Print(40, dur_y, "Duration:");

      // Draw duration buttons
      for (int i = 0; i < NUM_BENCHMARK_PRESETS; i++) {
        char dur_label[16];
        snprintf(dur_label, sizeof(dur_label), "%ds", benchmark_presets[i]);
        int selected = (i == benchmark_duration_idx);
        int btn_x = 40 + Font_GetTextWidth("Duration:") + 30 + i * 160;
        int btn_y = dur_y;

        // Background bar for selected
        if (selected) {
          Font_SetSize(36);
          Font_SetColor(theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a);
          {
            int asc = Font_GetAscender();
            int desc = Font_GetDescender();
            int pad = 6;
            int text_top = btn_y - asc;
            int text_bottom = btn_y - desc;
            int text_height = text_bottom - text_top;
            Font_DrawRect(btn_x - 10, text_top - pad, 120, text_height + 2 * pad);
          }
        }

        Font_SetColor(selected ? theme_sel_text_r : theme_label_r,
                      selected ? theme_sel_text_g : theme_label_g,
                      selected ? theme_sel_text_b : theme_label_b, 255);
        Font_SetSize(36);
        Font_Print(btn_x, btn_y, dur_label);
      }

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Start");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Back");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_A) {
        // Start benchmark stream
        benchmark_duration = benchmark_presets[benchmark_duration_idx];
        benchmark_running = 1;
        benchmark_start_time = 0;
        benchmark_poor_count = 0;
        benchmark_okay_count = 0;
        benchmark_rtt_avg = 0;
        benchmark_rtt_variance = 0;
        benchmark_total_frames = 0;
        benchmark_decode_errors = 0;
        benchmark_total_bytes = 0;
        message_buffer[0] = '\0';
        if (benchmark_was_from_disconnected) {
          state = STATE_BENCHMARK_CONNECTING;
        } else {
          state = STATE_START_STREAM;
        }
      } else if (btns & VPAD_BUTTON_B) {
        if (benchmark_was_from_disconnected) {
          state = STATE_DISCONNECTED;
        } else {
          state = STATE_CONNECTED;
        }
      } else if (btns & VPAD_BUTTON_X) {
        // Cycle duration
        benchmark_duration_idx =
            (benchmark_duration_idx + 1) % NUM_BENCHMARK_PRESETS;

      } else if (btns & (VPAD_BUTTON_LEFT | VPAD_BUTTON_STICK_L)) {
        benchmark_duration_idx =
            (benchmark_duration_idx + NUM_BENCHMARK_PRESETS - 1) %
            NUM_BENCHMARK_PRESETS;

      } else if (btns & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_STICK_L)) {
        benchmark_duration_idx =
            (benchmark_duration_idx + 1) % NUM_BENCHMARK_PRESETS;
      }
      break;
    }
    case STATE_UNPAIR_CONFIRM: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Unpair from Server"), 78, "Unpair from Server");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 200, "This will remove the pairing");
      Font_Print(40, 240, "between this Wii U and");
      Font_Printf(40, 280, "%s.", config.address);
      Font_Print(40, 340, "You will need to pair again");
      Font_Print(40, 380, "to stream from this server.");

      if (!server.isGfe) {
        // Sunshine doesn't support remote unpair
        Font_SetColor(255, 200, 0, 255);
        Font_SetSize(28);
        Font_Print(40, 440, "Note: Sunshine does not support");
        Font_Print(40, 470, "remote unpairing. This removes");
        Font_Print(40, 500, "the pairing locally only.");
      } else {
        Font_SetColor(255, 0, 0, 255);
        Font_SetSize(32);
        Font_Print(40, 460, "This cannot be undone.");
      }

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Unpair");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Cancel");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_A) {
        // Confirm unpair
        gs_set_timeout(client, 30);
        if (gs_unpair(client, &server) == GS_OK) {
          snprintf(message_buffer, sizeof(message_buffer),
                   "Successfully unpaired\n");
          is_error = 0;
        } else {
          snprintf(message_buffer, sizeof(message_buffer),
                   "Failed to unpair:\n%s\n", gs_get_error_message());
          is_error = 1;
        }
        gs_set_timeout(client, 5);
        state = STATE_CONNECTED;
      } else if (btns & VPAD_BUTTON_B) {
        // Cancel
        message_buffer[0] = '\0';
        is_error = 0;
        state = STATE_CONNECTED;
      }
      break;
    }
    case STATE_BENCHMARK_CONNECTING: {
      // Connect to the server, quit any running app, then start benchmark
      // stream
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      {
        char conn_label[128];
        snprintf(conn_label, sizeof(conn_label), "Connecting to %s", config.address);
        Font_SetSize(28);
        Font_Print(1920 - 40 - Font_GetTextWidth(conn_label), 78, conn_label);
      }
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(36);
      Font_Print(40, 200, "Benchmark stream");

      draw_spinner_centered(960, 560, 65);

      Font_EndDraw();
      Font_Draw_TVDRC();

      int ret = gs_get_status(client, &server, config.address, false);
      if (ret == GS_OK) {
        /* Auto-save MAC if not yet detected — only trust when paired */
        if (server.paired && server.mac && server.mac[0] != '\0' && wol_mac[0] == '\0') {
          strncpy(wol_mac, server.mac, WOL_MAC_LEN - 1);
          wol_mac[WOL_MAC_LEN - 1] = '\0';
          DBG_LOG("[WoL] Auto-detected MAC: %s\n", wol_mac);
          auto_save_settings(&config);
        }

        benchmark_running = 1;
        benchmark_start_time = 0;
        benchmark_poor_count = 0;
        benchmark_okay_count = 0;
        benchmark_rtt_avg = 0;
        benchmark_rtt_variance = 0;
        benchmark_total_frames = 0;
        benchmark_decode_errors = 0;
        benchmark_total_bytes = 0;
        message_buffer[0] = '\0';
        state = STATE_START_STREAM;
      } else {
        snprintf(message_buffer, sizeof(message_buffer),
                 "Can't connect to server\n");
        is_error = 1;
        state = STATE_DISCONNECTED;
      }
      break;
    }
    case STATE_PAIRING: {
      static int pairing_active = 0;
      static char pair_pin[5];
      static uint64_t pairing_start_time = 0;

      if (!pairing_active) {
        // First entry: generate PIN and start pairing
        sprintf(pair_pin, "%d%d%d%d", (unsigned)random() % 10, (unsigned)random() % 10,
                (unsigned)random() % 10, (unsigned)random() % 10);
        printf("Please enter the following PIN on the target PC: %s\n", pair_pin);
        DBG_LOG("[PAIR] PIN: %s\n", pair_pin);
        pairing_active = 1;
        pairing_start_time = OSGetTime();
      }

      // Total pairing timeout: 90 seconds
      uint64_t pairing_elapsed = (OSGetTime() - pairing_start_time) / OSSecondsToTicks(1);
      if (pairing_elapsed >= 90) {
        pairing_active = 0;
        gs_set_timeout(client, 5);
        snprintf(message_buffer, sizeof(message_buffer),
                 "Pairing timed out\n");
        is_error = 1;
        state = STATE_DISCONNECTED;
        break;
      }

      // Short timeout so the main loop can check buttons each iteration
      gs_set_timeout(client, 5);

      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Pairing with server"), 78, "Pairing with server");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(36);
      Font_Print(40, 200, "Enter this PIN on your PC:");
      Font_SetSize(48);
      Font_Printf(40, 270, "%s", pair_pin);

      Font_SetSize(32);
      Font_Print(40, 540, "Waiting for host confirmation...");

      // Bottom bar - cancel
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'B', "Hold to Cancel");

      Font_EndDraw();
      Font_Draw_TVDRC();

      // Hold-to-cancel: B must be held for 1.5 seconds
      static uint64_t b_hold_start = 0;
      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_B && b_hold_start == 0) {
        b_hold_start = OSGetTime();
      }
      // Check if B is still held
      VPADStatus vpad_hold;
      VPADRead(VPAD_CHAN_0, &vpad_hold, 1, false);
      if ((vpad_hold.hold & VPAD_BUTTON_B) && b_hold_start != 0) {
        if ((OSGetTime() - b_hold_start) >= OSSecondsToTicks(1) + OSMillisecondsToTicks(500)) {
          pairing_active = 0;
          b_hold_start = 0;
          gs_set_timeout(client, 5);
          snprintf(message_buffer, sizeof(message_buffer),
                   "Pairing cancelled\n");
          is_error = 0;
          state = STATE_DISCONNECTED;
          break;
        }
      }
      if (!(vpad_hold.hold & VPAD_BUTTON_B)) {
        b_hold_start = 0;
      }

      // Poll pairing result
      if (gs_pair(client, &server, &pair_pin[0]) != GS_OK) {
        int err = gs_get_error(NULL);
        // GS_IO_ERROR with timeout means still waiting; otherwise fail
        if (err != GS_IO_ERROR) {
          fprintf(stderr, "Failed to pair to server: %s\n",
                  gs_get_error_message());
          DBG_LOG("[ERROR] Failed to pair: %s\n", gs_get_error_message());
          snprintf(message_buffer, sizeof(message_buffer),
                   "Failed to pair:\n%s\n", gs_get_error_message());
          is_error = 1;
          pairing_active = 0;
          gs_set_timeout(client, 5);
          state = STATE_DISCONNECTED;
          break;
        }
      } else {
        snprintf(message_buffer, sizeof(message_buffer),
                 "Successfully paired\n");
        is_error = 0;
        pairing_active = 0;
        gs_set_timeout(client, 5);

        /* Fetch serverinfo over HTTPS now that we're paired —
         * this gives us the real MAC (HTTP returns all zeros) */
        int post_status = gs_get_status(client, &server, config.address,
                                        config.unsupported);
        if (post_status == GS_OK && server.mac && server.mac[0] != '\0'
            && wol_mac[0] == '\0') {
          strncpy(wol_mac, server.mac, WOL_MAC_LEN - 1);
          wol_mac[WOL_MAC_LEN - 1] = '\0';
          DBG_LOG("[WoL] Auto-detected MAC after pairing: %s\n", wol_mac);
          auto_save_settings(&config);
        }

        if (server.currentGame != 0) {
          state = STATE_DISCONNECTED;
          break;
        }
        state = STATE_CONNECTED;
        break;
      }

      // Small delay to avoid busy-waiting
      usleep(200000); // 200ms
      break;
    }
    case STATE_START_STREAM: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Starting stream"), 78, "Starting stream");

      // Animated spinner
      draw_spinner_centered(960, 560, 65);

      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 540, "Initializing video decoder...");

      Font_EndDraw();
      Font_Draw_TVDRC();

      if (server.paired) {
        // Clear cooldown once we're past the UI gate
        reconnect_cooldown_deadline = 0;
        config.stream.supportedVideoFormats = VIDEO_FORMAT_H264;
        if (stream(client, &server, &config) == 0) {
          wiiu_proc_set_home_enabled(0);
          start_input_thread();
          state = STATE_STREAMING;
          break;
        }
      } else {
        snprintf(message_buffer, sizeof(message_buffer),
                 "You must pair with the PC first\n");
        is_error = 1;
      }
      // stream() failed - reset benchmark flags so normal streaming isn't
      // polluted with benchmark counters.
      benchmark_running = 0;
      state = STATE_CONNECTED;
      break;
    }
    case STATE_STREAMING: {
      wiiu_stream_draw();

      // Benchmark timer and data collection
      if (benchmark_running) {
        static uint64_t last_rtt_sample = 0;
        if (benchmark_start_time == 0) {
          benchmark_start_time = OSGetTime();
          last_rtt_sample = 0; // Reset RTT timer on benchmark start
        } else {
          uint64_t elapsed_ms =
              (OSGetTime() - benchmark_start_time) / (OSTimerClockSpeed / 1000);
          if (elapsed_ms >= benchmark_duration * 1000) {
            // Time's up - stop the stream
            state = STATE_STOP_STREAM;
            break;
          }
          // Sample RTT every ~500ms using a timer
          if (last_rtt_sample == 0 ||
              (OSGetTime() - last_rtt_sample) >= (OSTimerClockSpeed / 2)) {
            last_rtt_sample = OSGetTime();
            uint32_t rtt, variance;
            if (LiGetEstimatedRttInfo(&rtt, &variance)) {
              if (benchmark_rtt_avg == 0) {
                benchmark_rtt_avg = rtt;
                benchmark_rtt_variance = variance;
              } else {
                // Running average
                benchmark_rtt_avg = (benchmark_rtt_avg * 3 + rtt) / 4;
                benchmark_rtt_variance =
                    (benchmark_rtt_variance * 3 + variance) / 4;
              }
            }
          }
        }
      }
      break;
    }
    case STATE_STOP_STREAM: {
      // Check if the ENet callback already triggered this (unexpected disconnect).
      // If so, preserve the error message it wrote.  Only clear on user-initiated
      // disconnects (where we set disconnecting=1 below before teardown).
      int was_callback_disconnect;
      {
        OSFastMutex_Lock(&stateMutex);
        was_callback_disconnect = disconnecting;
        disconnecting = 1; // prevent further callback writes
        OSFastMutex_Unlock(&stateMutex);

        stop_input_thread();
        wiiu_input_reset();       // Reset controller_arrived[] for next stream
        rumble_handler = NULL;    // Prevent rumble callbacks during teardown

        // Send quit request BEFORE tearing down the ENet connection,
        // so the host actually receives it.  Critical: if this fails the
        // host stays in a zombie streaming state and refuses new connections.
        bool should_quit = benchmark_running || config.quitappafter;
        // On error disconnects always attempt quit regardless of quitappafter
        // because the host session is in an undefined state.
        if (was_callback_disconnect)
          should_quit = true;

        if (should_quit) {
          if (benchmark_running) {
            printf("[BENCHMARK] Sending app quit request ...\n");
            DBG_LOG("[QUIT] benchmark quit requested\n");
          } else if (was_callback_disconnect) {
            printf("[ERROR DISCONNECT] Sending app quit request ...\n");
            DBG_LOG("[QUIT] error-disconnect quit requested\n");
          } else {
            printf("Sending app quit request ...\n");
            DBG_LOG("[QUIT] user quit requested\n");
          }
          quit_app_reliable(client, &server, 3, 1000000);
        }

        LiStopConnection();
        wiiu_rumble_stop(); // Ensure motor is off after connection is gone

        // Reset frame queue for next stream (benchmark re-stream or normal)
        wiiu_stream_reset();

        // Clear disconnecting flag; only wipe error state on user-initiated
        // disconnects.  Callback-triggered disconnects keep their message.
        OSFastMutex_Lock(&stateMutex);
        disconnecting = 0;
        if (!was_callback_disconnect) {
          is_error = 0;
          message_buffer[0] = '\0';
        }
        OSFastMutex_Unlock(&stateMutex);

        // After an error disconnect, force currentGame=0 so the next
        // connection attempt uses /launch instead of /resume.  The host
        // session is in an unknown state and /resume will fail.
        // Also impose a cooldown to give the host time to fully recover.
        if (was_callback_disconnect) {
          server.currentGame = 0;
          reconnect_cooldown_deadline = OSGetTime() + RECONNECT_COOLDOWN_TICKS;
          DBG_LOG("[QUIT] forced currentGame=0, cooldown %llds after error disconnect\n",
                  (long long)OSTicksToSeconds(RECONNECT_COOLDOWN_TICKS));
        }
      }

      wiiu_proc_set_home_enabled(1);

      if (benchmark_running) {
        benchmark_running = 0;
        state = STATE_BENCHMARK_RESULTS;
      } else {
        // Error disconnects always go to Disconnected (connection is broken).
        // User-initiated disconnects also go to Disconnected — the stream is
        // gone and showing "Connected" is misleading.
        state = STATE_DISCONNECTED;
      }
      break;
    }
    case STATE_BENCHMARK_RESULTS: {
      Font_Clear();
      Font_BeginDraw();
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      draw_logo();
      Font_SetSize(28);
      Font_Print(1920 - 40 - Font_GetTextWidth("Benchmark Results"), 78, "Benchmark Results");
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, 140);

      // Current stream settings header
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 175, "Stream Settings:");
      Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
      Font_SetSize(32);
      Font_Printf(40, 215, "Bitrate: %d kbps", config.stream.bitrate);
      Font_Printf(40, 255, "FPS:     %d", config.stream.fps);
      Font_Printf(40, 295, "Res:     %dx%d", config.stream.width, config.stream.height);

      // Compute average bitrate from decoded frame bytes
      int avg_bitrate_kbps = 0;
      if (benchmark_duration > 0 && benchmark_total_bytes > 0) {
        avg_bitrate_kbps =
            (int)((benchmark_total_bytes * 8) / (benchmark_duration * 1000));
      }

      int total_reports = benchmark_poor_count + benchmark_okay_count;
      int poor_pct =
          total_reports > 0 ? (benchmark_poor_count * 100) / total_reports : 0;

      // RTT
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 355, "Network Latency (RTT):");
      {
        char rtt_str[64];
        if (benchmark_rtt_avg > 0) {
          snprintf(rtt_str, sizeof(rtt_str), "%u ms (+/- %u ms)",
                   benchmark_rtt_avg, benchmark_rtt_variance);
        } else {
          snprintf(rtt_str, sizeof(rtt_str), "N/A");
        }
        Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
        Font_SetSize(32);
        Font_Print(40, 395, rtt_str);
      }

      // Average bitrate
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 455, "Average Bitrate:");
      {
        char br_str[64];
        if (avg_bitrate_kbps > 0) {
          snprintf(br_str, sizeof(br_str), "%d kbps", avg_bitrate_kbps);
        } else {
          snprintf(br_str, sizeof(br_str), "N/A");
        }
        Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
        Font_SetSize(32);
        Font_Print(40, 495, br_str);
      }

      // Connection quality
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 555, "Connection Quality:");
      {
        char quality_str[64];
        int quality_color_r, quality_color_g, quality_color_b;
        if (total_reports == 0) {
          // Fallback: use decode errors as quality indicator
          if (benchmark_total_frames > 0 && benchmark_decode_errors == 0) {
            snprintf(quality_str, sizeof(quality_str),
                     "Good (no decode errors)");
            quality_color_r = 120;
            quality_color_g = 255;
            quality_color_b = 120;
          } else if (benchmark_total_frames > 0) {
            int err_pct =
                (benchmark_decode_errors * 100) / benchmark_total_frames;
            snprintf(quality_str, sizeof(quality_str),
                     "Fair (%d%% decode errors)", err_pct);
            quality_color_r = 255;
            quality_color_g = 200;
            quality_color_b = 100;
          } else {
            snprintf(quality_str, sizeof(quality_str), "No data collected");
            quality_color_r = 160;
            quality_color_g = 160;
            quality_color_b = 160;
          }
        } else {
          if (poor_pct <= 10) {
            snprintf(quality_str, sizeof(quality_str),
                     "Excellent (%d%% poor intervals)", poor_pct);
            quality_color_r = 120;
            quality_color_g = 255;
            quality_color_b = 120;
          } else if (poor_pct <= 30) {
            snprintf(quality_str, sizeof(quality_str),
                     "Good (%d%% poor intervals)", poor_pct);
            quality_color_r = 200;
            quality_color_g = 220;
            quality_color_b = 120;
          } else if (poor_pct <= 60) {
            snprintf(quality_str, sizeof(quality_str),
                     "Fair (%d%% poor intervals)", poor_pct);
            quality_color_r = 255;
            quality_color_g = 200;
            quality_color_b = 100;
          } else {
            snprintf(quality_str, sizeof(quality_str),
                     "Poor (%d%% poor intervals)", poor_pct);
            quality_color_r = 255;
            quality_color_g = 120;
            quality_color_b = 100;
          }
        }
        Font_SetColor(quality_color_r, quality_color_g, quality_color_b, 255);
        Font_SetSize(32);
        Font_Print(40, 595, quality_str);
      }

      // Average framerate / decode errors
      Font_SetColor(theme_label_r, theme_label_g, theme_label_b, theme_label_a);
      Font_SetSize(32);
      Font_Print(40, 655, "Average Framerate:");
      {
        char frame_str[64];
        if (benchmark_duration > 0 && benchmark_total_frames > 0) {
          int avg_fps = benchmark_total_frames / benchmark_duration;
          snprintf(frame_str, sizeof(frame_str), "%d fps (%d errors)", avg_fps,
                   benchmark_decode_errors);
        } else {
          snprintf(frame_str, sizeof(frame_str), "N/A");
        }
        Font_SetColor(theme_title_r, theme_title_g, theme_title_b, theme_title_a);
        Font_SetSize(32);
        Font_Print(40, 695, frame_str);
      }

      // Bottom bar - fixed position
      Font_SetColor(theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a);
      Font_DrawHLine(0, 1920, BAR_DIVIDER_Y);
      Font_SetColor(theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a);
      Font_SetSize(28);
      Font_DrawButtonIcon(40, BAR_BUTTONS_Y, 'A', "Stream");
      Font_DrawButtonIcon(442, BAR_BUTTONS_Y, 'B', "Back");
      Font_DrawButtonIcon(844, BAR_BUTTONS_Y, 'X', "Re-test");
      Font_DrawButtonIcon(1246, BAR_BUTTONS_Y, 'Y', "Adj. Bitrate");

      Font_EndDraw();
      Font_Draw_TVDRC();

      uint32_t btns = wiiu_input_buttons_triggered();
      if (btns & VPAD_BUTTON_A) {
        // Stream with current bitrate
        message_buffer[0] = '\0';
        state = STATE_START_STREAM;
      } else if (btns & VPAD_BUTTON_Y) {
        // Go to custom bitrate screen; return here after setting
        custom_br_value = config.stream.bitrate;
        if (custom_br_value < 500) custom_br_value = 500;
        custom_bitrate_return_to_benchmark = 1;
        state = STATE_CUSTOM_BITRATE;
      } else if (btns & VPAD_BUTTON_B) {
        // Go back - determine correct destination
        if (benchmark_was_from_disconnected) {
          state = STATE_DISCONNECTED;
        } else {
          state = STATE_CONNECTED;
        }
      } else if (btns & VPAD_BUTTON_X) {
        // Re-run benchmark
        benchmark_duration_idx = 1;
        if (benchmark_was_from_disconnected) {
          state = STATE_BENCHMARK_CONNECTING;
        } else {
          state = STATE_BENCHMARK;
        }
      }
      break;
    }
    }
  }

  // If we exited while connected/streaming, tear down the connection first.
  // All streaming threads must be stopped before ProcUI shutdown.
  {
    int st;
    OSFastMutex_Lock(&stateMutex);
    disconnecting = 1;
    st = state;
    OSFastMutex_Unlock(&stateMutex);
    if (st == STATE_CONNECTED || st == STATE_STREAMING ||
        st == STATE_START_STREAM || st == STATE_CONNECTING ||
        st == STATE_PAIRING || st == STATE_SENDING_WOL) {
      stop_input_thread();
      wiiu_input_reset();
      rumble_handler = NULL; // Prevent rumble callbacks during teardown
      LiStopConnection();
      wiiu_rumble_stop(); // Ensure motor is off after connection is gone
    }
  }

  // CRITICAL: ProcUIShutdown() MUST come BEFORE WHBGfxShutdown().
  // WHBGfxShutdown() calls GX2WaitForFlip() which waits for the current
  // frame to be scanned out. After ProcUIDrawDoneRelease() (called in
  // wiiu_proc_running() on PROCUI_STATUS_RELEASE_FOREGROUND or implicitly
  // during PROCUI_STATUS_EXITING), the OS has taken back the display and
  // flips may never complete, causing WHBGfxShutdown() to hang forever.
  // ProcUIShutdown() properly releases the display context first.

  DBG_LOG("[SHUTDOWN] Font_Deinit\n");
  Font_Deinit();

  DBG_LOG("[SHUTDOWN] wiiu_stream_fini\n");
  wiiu_stream_fini();

  DBG_LOG("[SHUTDOWN] wiiu_net_shutdown\n");
  wiiu_net_shutdown();

  DBG_LOG("[SHUTDOWN] SDL_QuitSubSystem(AUDIO)\n");
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  DBG_LOG("[SHUTDOWN] ProcUIShutdown\n");
  wiiu_proc_shutdown_procui();
  DBG_LOG("[SHUTDOWN] ProcUIShutdown done\n");

  DBG_LOG("[SHUTDOWN] WHBGfxShutdown\n");
  WHBGfxShutdown();
  DBG_LOG("[SHUTDOWN] WHBGfxShutdown done\n");

  // Clean up GameStream client
  DBG_LOG("[SHUTDOWN] gs_destroy\n");
  if (client)
    gs_destroy(client);

  // Clean up allocated config strings
  if (config.address)
    free(config.address);
  if (config.app)
    free(config.app);

  if (config.mapping)
    free(config.mapping);
  if (config.platform)
    free(config.platform);

  if (config.audio_device)
    free(config.audio_device);

  if (config.action)
    free(config.action);
  for (int i = 0; i < config.inputsCount; i++)
    free(config.inputs[i]);

  if (config.config_file)
    free(config.config_file);

  DBG_LOG("[SHUTDOWN] wiiu_proc_shutdown\n");
  debug_log_close();
  wiiu_proc_shutdown();

  return 0;
}
