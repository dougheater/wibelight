/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
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
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include <Limelight.h>

#include <stdbool.h>

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

#define MAX_INPUTS 6
#define MAX_PROFILES 6
#define PROFILE_NAME_LEN 32
#define PROFILE_APP_LEN 32
#define PROFILE_WOL_MAC_LEN 18

enum codecs { CODEC_UNSPECIFIED, CODEC_H264, CODEC_HEVC, CODEC_AV1 };

typedef enum mouse_modes {
  MOUSE_MODE_RELATIVE,
  MOUSE_MODE_ABSOLUTE,
  MOUSE_MODE_TOUCHSCREEN
} mouse_modes;

typedef struct {
    char name[PROFILE_NAME_LEN];
    uint8_t ip[4];
    char wol_mac[PROFILE_WOL_MAC_LEN];
    int wol_enabled;
    // Video
    int width, height, fps, bitrate, packetSize;
    int colorSpace, colorRange;
    int max_queued_frames, rotate;
    // Audio
    int audio_configuration, audio_buffer_samples;
    int localaudio;
    // Input
    int swap_buttons, mouse_mode, enable_rumble, rumble_strength, enable_nav_click, disable_gamepad;
    // Behavior
    int quitappafter, viewonly;
    char app[PROFILE_APP_LEN];
    int autostream, autostart_connect;
    // Appearance
    int theme_bg, theme_accent, theme_text, theme_btn, theme_logo;
} profile_t;

typedef struct _CONFIGURATION {
  STREAM_CONFIGURATION stream;
  int debug_level;
  char* app;
  char* action;
  char* address;
  char* mapping;
  char* platform;
  char* audio_device;
  char* config_file;
  char key_dir[4096];
  bool sops;
  bool localaudio;
  bool fullscreen;
  int rotate;
  bool unsupported;
  bool quitappafter;
  bool viewonly;
  bool mouse_emulation;
  char* inputs[MAX_INPUTS];
  int inputsCount;
  enum codecs codec;
  bool hdr;
  int pin;
  unsigned short port;
} CONFIGURATION, *PCONFIGURATION;

extern bool inputAdded;

bool config_json_parse(char* filename, PCONFIGURATION config);
bool config_parse(int argc, char* argv[], PCONFIGURATION config);  // returns true if JSON loaded
void config_write_settings(char* filename, PCONFIGURATION config, const uint8_t ip_octets[][4], int max_ips);
