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

#include "config.h"
#include "ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <pwd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <limits.h>

#ifdef __WIIU__
extern int disable_gamepad;
extern int swap_buttons;
extern int enable_rumble;
extern mouse_modes mouse_mode;
extern int autostream;
extern int autostart_connect;
extern int audio_buffer_samples;
extern int max_queued_frames;
extern int rumble_strength;
extern int enable_nav_click;

#define wibelight_WIIU_PATH "/vol/external01/wiiu/apps/wibelight"
#endif

#include "../third_party/cJSON/cJSON.h"

#define wibelight_PATH "/wibelight"
#define USER_PATHS "."
#define DEFAULT_CONFIG_DIR "/.config"
#define DEFAULT_CACHE_DIR "/.cache"

bool inputAdded = false;

#ifndef __WIIU__ // unused on WiiU, used by getopt_long_only below
static struct option long_options[] = {
  {"720", no_argument, NULL, 'a'},
  {"1080", no_argument, NULL, 'b'},
  {"4k", no_argument, NULL, '0'},
  {"width", required_argument, NULL, 'c'},
  {"height", required_argument, NULL, 'd'},
  {"bitrate", required_argument, NULL, 'g'},
  {"packetsize", required_argument, NULL, 'h'},
  {"app", required_argument, NULL, 'i'},
  {"input", required_argument, NULL, 'j'},
  {"mapping", required_argument, NULL, 'k'},
  {"nosops", no_argument, NULL, 'l'},
  {"audio", required_argument, NULL, 'm'},
  {"localaudio", no_argument, NULL, 'n'},
  {"config", required_argument, NULL, 'o'},
  {"platform", required_argument, NULL, 'p'},
  {"save", required_argument, NULL, 'q'},
  {"keydir", required_argument, NULL, 'r'},
  {"remote", required_argument, NULL, 's'},
  {"windowed", no_argument, NULL, 't'},
  {"surround", required_argument, NULL, 'u'},
  {"fps", required_argument, NULL, 'v'},
  {"codec", required_argument, NULL, 'x'},
  {"nounsupported", no_argument, NULL, 'y'},
  {"quitappafter", no_argument, NULL, '1'},
  {"viewonly", no_argument, NULL, '2'},
  {"rotate", required_argument, NULL, '3'},
  {"verbose", no_argument, NULL, 'z'},
  {"debug", no_argument, NULL, 'Z'},
#ifdef __WIIU__
  {"disable_gamepad", no_argument, NULL, 'A'},
  {"swap_buttons", no_argument, NULL, 'B'},
  {"autostream", no_argument, NULL, 'C'},
  {"mouse_mode", required_argument, NULL, 'D'},
#endif
  {"nomouseemulation", no_argument, NULL, '4'},
  {"pin", required_argument, NULL, '5'},
  {"port", required_argument, NULL, '6'},
  {"hdr", no_argument, NULL, '7'},
  {0, 0, 0, 0},
};
#endif // __WIIU__

#ifndef __WIIU__
char* get_path(char* name, char* extra_data_dirs) {
  const char *xdg_config_dir = getenv("XDG_CONFIG_DIR");
  const char *home_dir = getenv("HOME");

  if (access(name, R_OK) != -1) {
      return name;
  }

  if (!home_dir) {
    struct passwd *pw = getpwuid(getuid());
    home_dir = pw->pw_dir;
  }

  if (!extra_data_dirs)
    extra_data_dirs = "/usr/share:/usr/local/share";
  if (!xdg_config_dir)
    xdg_config_dir = home_dir;

  char *data_dirs = malloc(strlen(USER_PATHS) + 1 + strlen(xdg_config_dir) + 1 + strlen(home_dir) + 1 + strlen(DEFAULT_CONFIG_DIR) + 1 + strlen(extra_data_dirs) + 2);
  sprintf(data_dirs, USER_PATHS ":%s:%s/" DEFAULT_CONFIG_DIR ":%s/", xdg_config_dir, home_dir, extra_data_dirs);

  char *path = malloc(strlen(data_dirs)+strlen(wibelight_PATH)+strlen(name)+2);
  if (path == NULL) {
    fprintf(stderr, "Not enough memory\n");
    exit(-1);
  }

  char* data_dir = data_dirs;
  char* end;
  do {
    end = strstr(data_dir, ":");
    int length = end != NULL ? end - data_dir:strlen(data_dir);
    memcpy(path, data_dir, length);
    if (path[0] == '/')
      sprintf(path+length, wibelight_PATH "/%s", name);
    else
      sprintf(path+length, "/%s", name);

    if(access(path, R_OK) != -1) {
      free(data_dirs);
      return path;
    }

    data_dir = end + 1;
  } while (end != NULL);

  free(data_dirs);
  free(path);
  return NULL;
}
#endif

#ifndef __WIIU__
static void parse_argument(int c, char* value, PCONFIGURATION config) {
  switch (c) {
  case 'a':
    config->stream.width = 1280;
    config->stream.height = 720;
    break;
  case 'b':
    config->stream.width = 1920;
    config->stream.height = 1080;
    break;
  case '0':
    config->stream.width = 3840;
    config->stream.height = 2160;
    break;
  case 'c':
    config->stream.width = atoi(value);
    break;
  case 'd':
    config->stream.height = atoi(value);
    break;
  case 'g':
    config->stream.bitrate = atoi(value);
    break;
  case 'h':
    config->stream.packetSize = atoi(value);
    break;
  case 'i':
    config->app = strdup(value);
    break;
  case 'j':
    if (config->inputsCount >= MAX_INPUTS) {
      perror("Too many inputs specified");
      exit(-1);
    }
    config->inputs[config->inputsCount] = strdup(value);
    config->inputsCount++;
    inputAdded = true;
    break;
  case 'k':
#ifndef __WIIU__
    config->mapping = get_path(value, getenv("XDG_DATA_DIRS"));
    if (config->mapping == NULL) {
      fprintf(stderr, "Unable to open custom mapping file: %s\n", value);
      exit(-1);
    }
    break;
#endif
  case 'l':
    config->sops = false;
    break;
  case 'm':
    config->audio_device = strdup(value);
    break;
  case 'n':
    config->localaudio = true;
    break;
  case 'o':
    if (!config_json_parse(value, config))
      exit(EXIT_FAILURE);

    break;
  case 'p':
    config->platform = strdup(value);
    break;
  case 'q':
    config->config_file = strdup(value);
    break;
  case 'r':
    strcpy(config->key_dir, value);
    break;
  case 's':
    if (strcasecmp(value, "auto") == 0)
      config->stream.streamingRemotely = STREAM_CFG_AUTO;
    else if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0)
      config->stream.streamingRemotely = STREAM_CFG_REMOTE;
    else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0)
      config->stream.streamingRemotely = STREAM_CFG_LOCAL;
    break;

  case 't':
    config->fullscreen = false;
    break;
  case 'u':
    if (strcasecmp(value, "5.1") == 0)
      config->stream.audioConfiguration = AUDIO_CONFIGURATION_51_SURROUND;
    else if (strcasecmp(value, "7.1") == 0)
      config->stream.audioConfiguration = AUDIO_CONFIGURATION_71_SURROUND;
    break;
  case 'v':
    config->stream.fps = atoi(value);
    break;
  case 'x':
    if (strcasecmp(value, "auto") == 0)
      config->codec = CODEC_UNSPECIFIED;
    else if (strcasecmp(value, "h264") == 0)
      config->codec = CODEC_H264;
    else if (strcasecmp(value, "h265") == 0 || strcasecmp(value, "hevc") == 0)
      config->codec = CODEC_HEVC;
    else if (strcasecmp(value, "av1") == 0)
      config->codec = CODEC_AV1;
    break;
  case 'y':
    config->unsupported = false;
    break;
  case '1':
    config->quitappafter = true;
    break;
  case '2':
    config->viewonly = true;
    break;
  case '3':
    config->rotate = atoi(value);
    break;
  case 'z':
    config->debug_level = 1;
    break;
  case 'Z':
    config->debug_level = 2;
    break;
#ifdef __WIIU__
  case 'A':
    disable_gamepad = true;
    break;
  case 'B':
    swap_buttons = true;
    break;
  case 'C':
    autostream = true;
    break;
  case 'D':
    if (strcasecmp(value, "relative") == 0)
      mouse_mode = MOUSE_MODE_RELATIVE;
    else if (strcasecmp(value, "absolute") == 0)
      mouse_mode = MOUSE_MODE_ABSOLUTE;
    else if (strcasecmp(value, "touchscreen") == 0)
      mouse_mode = MOUSE_MODE_TOUCHSCREEN;
    break;
#endif
  case '4':
    config->mouse_emulation = false;
    break;
  case '5':
    config->pin = atoi(value);
    break;
  case '6':
    config->port = atoi(value);
    break;
  case '7':
    config->hdr = true;
    break;
  case 1:
    if (config->action == NULL)
      config->action = strdup(value);
    else if (config->address == NULL)
      config->address = strdup(value);
    else {
      perror("Too many options");
      exit(-1);
    }
  }
}
#endif // __WIIU__



// Serialize a single profile into a cJSON object
static cJSON *profile_to_json(const profile_t *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;

    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddNumberToObject(obj, "ip0", p->ip[0]);
    cJSON_AddNumberToObject(obj, "ip1", p->ip[1]);
    cJSON_AddNumberToObject(obj, "ip2", p->ip[2]);
    cJSON_AddNumberToObject(obj, "ip3", p->ip[3]);
    if (p->wol_mac[0] != '\0')
        cJSON_AddStringToObject(obj, "wol_mac", p->wol_mac);
    cJSON_AddBoolToObject(obj, "wol_enabled", p->wol_enabled);

    // Video
    cJSON_AddNumberToObject(obj, "width", p->width);
    cJSON_AddNumberToObject(obj, "height", p->height);
    cJSON_AddNumberToObject(obj, "fps", p->fps);
    cJSON_AddNumberToObject(obj, "bitrate", p->bitrate);
    cJSON_AddNumberToObject(obj, "packetsize", p->packetSize);
    cJSON_AddNumberToObject(obj, "colorSpace", p->colorSpace);
    cJSON_AddNumberToObject(obj, "colorRange", p->colorRange);
    cJSON_AddNumberToObject(obj, "max_queued_frames", p->max_queued_frames);
    cJSON_AddNumberToObject(obj, "rotate", p->rotate);

    // Audio
    if (p->audio_configuration == AUDIO_CONFIGURATION_51_SURROUND)
        cJSON_AddStringToObject(obj, "surround", "5.1");
    else if (p->audio_configuration == AUDIO_CONFIGURATION_71_SURROUND)
        cJSON_AddStringToObject(obj, "surround", "7.1");
    cJSON_AddNumberToObject(obj, "audio_buffer_samples", p->audio_buffer_samples);
    cJSON_AddBoolToObject(obj, "localaudio", p->localaudio);

    // Input
    cJSON_AddBoolToObject(obj, "swap_buttons", p->swap_buttons);
    const char *mm = "relative";
    if (p->mouse_mode == MOUSE_MODE_ABSOLUTE) mm = "absolute";
    else if (p->mouse_mode == MOUSE_MODE_TOUCHSCREEN) mm = "touchscreen";
    cJSON_AddStringToObject(obj, "mouse_mode", mm);
    cJSON_AddBoolToObject(obj, "enable_rumble", p->enable_rumble);
    cJSON_AddNumberToObject(obj, "rumble_strength", p->rumble_strength);
    cJSON_AddBoolToObject(obj, "enable_nav_click", p->enable_nav_click);
    cJSON_AddBoolToObject(obj, "disable_gamepad", p->disable_gamepad);

    // Behavior
    cJSON_AddStringToObject(obj, "app", p->app);
    cJSON_AddBoolToObject(obj, "quitappafter", p->quitappafter);
    cJSON_AddBoolToObject(obj, "viewonly", p->viewonly);
    cJSON_AddBoolToObject(obj, "autostream", p->autostream);
    cJSON_AddBoolToObject(obj, "autostart_connect", p->autostart_connect);

    // Appearance
    cJSON_AddNumberToObject(obj, "theme_bg", p->theme_bg);
    cJSON_AddNumberToObject(obj, "theme_accent", p->theme_accent);
    cJSON_AddNumberToObject(obj, "theme_text", p->theme_text);
    cJSON_AddNumberToObject(obj, "theme_btn", p->theme_btn);
    cJSON_AddNumberToObject(obj, "theme_logo", p->theme_logo);

    return obj;
}

// Parse a cJSON object into a profile (defaults already set by caller)
static void json_to_profile(cJSON *obj, profile_t *p) {
    cJSON *item;

    item = cJSON_GetObjectItem(obj, "name");
    if (item && item->type == cJSON_String) {
        strncpy(p->name, item->valuestring, PROFILE_NAME_LEN - 1);
        p->name[PROFILE_NAME_LEN - 1] = '\0';
    }

    item = cJSON_GetObjectItem(obj, "ip0");
    if (item && item->type == cJSON_Number) p->ip[0] = item->valueint;
    item = cJSON_GetObjectItem(obj, "ip1");
    if (item && item->type == cJSON_Number) p->ip[1] = item->valueint;
    item = cJSON_GetObjectItem(obj, "ip2");
    if (item && item->type == cJSON_Number) p->ip[2] = item->valueint;
    item = cJSON_GetObjectItem(obj, "ip3");
    if (item && item->type == cJSON_Number) p->ip[3] = item->valueint;

    item = cJSON_GetObjectItem(obj, "wol_mac");
    if (item && item->type == cJSON_String) {
        strncpy(p->wol_mac, item->valuestring, PROFILE_WOL_MAC_LEN - 1);
        p->wol_mac[PROFILE_WOL_MAC_LEN - 1] = '\0';
    }
    item = cJSON_GetObjectItem(obj, "wol_enabled");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->wol_enabled = item->valueint;

    // Video
    item = cJSON_GetObjectItem(obj, "width");
    if (item && item->type == cJSON_Number) p->width = item->valueint;
    item = cJSON_GetObjectItem(obj, "height");
    if (item && item->type == cJSON_Number) p->height = item->valueint;
    item = cJSON_GetObjectItem(obj, "fps");
    if (item && item->type == cJSON_Number) p->fps = item->valueint;
    item = cJSON_GetObjectItem(obj, "bitrate");
    if (item && item->type == cJSON_Number) {
      p->bitrate = item->valueint;
      if (p->bitrate < 0) p->bitrate = 3000; // migrate old "Auto" (-1)
    }
    item = cJSON_GetObjectItem(obj, "packetsize");
    if (item && item->type == cJSON_Number) p->packetSize = item->valueint;
    item = cJSON_GetObjectItem(obj, "colorSpace");
    if (item && item->type == cJSON_Number) p->colorSpace = item->valueint;
    item = cJSON_GetObjectItem(obj, "colorRange");
    if (item && item->type == cJSON_Number) p->colorRange = item->valueint;
    item = cJSON_GetObjectItem(obj, "max_queued_frames");
    if (item && item->type == cJSON_Number) p->max_queued_frames = item->valueint;
    item = cJSON_GetObjectItem(obj, "rotate");
    if (item && item->type == cJSON_Number) p->rotate = item->valueint;

    // Audio
    item = cJSON_GetObjectItem(obj, "surround");
    if (item && item->type == cJSON_String) {
        if (strcmp(item->valuestring, "5.1") == 0)
            p->audio_configuration = AUDIO_CONFIGURATION_51_SURROUND;
        else if (strcmp(item->valuestring, "7.1") == 0)
            p->audio_configuration = AUDIO_CONFIGURATION_71_SURROUND;
    }
    item = cJSON_GetObjectItem(obj, "audio_buffer_samples");
    if (item && item->type == cJSON_Number) p->audio_buffer_samples = item->valueint;
    item = cJSON_GetObjectItem(obj, "localaudio");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->localaudio = item->valueint;

    // Input
    item = cJSON_GetObjectItem(obj, "swap_buttons");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->swap_buttons = item->valueint;
    item = cJSON_GetObjectItem(obj, "mouse_mode");
    if (item && item->type == cJSON_String) {
        if (strcmp(item->valuestring, "absolute") == 0) p->mouse_mode = MOUSE_MODE_ABSOLUTE;
        else if (strcmp(item->valuestring, "touchscreen") == 0) p->mouse_mode = MOUSE_MODE_TOUCHSCREEN;
        else p->mouse_mode = MOUSE_MODE_RELATIVE;
    }
    item = cJSON_GetObjectItem(obj, "enable_rumble");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->enable_rumble = item->valueint;
    item = cJSON_GetObjectItem(obj, "rumble_strength");
    if (item && item->type == cJSON_Number) p->rumble_strength = item->valueint;
    item = cJSON_GetObjectItem(obj, "enable_nav_click");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->enable_nav_click = item->valueint;
    item = cJSON_GetObjectItem(obj, "disable_gamepad");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->disable_gamepad = item->valueint;

    // Behavior
    item = cJSON_GetObjectItem(obj, "app");
    if (item && item->type == cJSON_String) {
        strncpy(p->app, item->valuestring, PROFILE_APP_LEN - 1);
        p->app[PROFILE_APP_LEN - 1] = '\0';
    }
    item = cJSON_GetObjectItem(obj, "quitappafter");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->quitappafter = item->valueint;
    item = cJSON_GetObjectItem(obj, "viewonly");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->viewonly = item->valueint;
    item = cJSON_GetObjectItem(obj, "autostream");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->autostream = item->valueint;
    item = cJSON_GetObjectItem(obj, "autostart_connect");
    if (item && (item->type & (cJSON_False|cJSON_True))) p->autostart_connect = item->valueint;

    // Appearance
    item = cJSON_GetObjectItem(obj, "theme_bg");
    if (item && item->type == cJSON_Number) p->theme_bg = item->valueint;
    item = cJSON_GetObjectItem(obj, "theme_accent");
    if (item && item->type == cJSON_Number) p->theme_accent = item->valueint;
    item = cJSON_GetObjectItem(obj, "theme_text");
    if (item && item->type == cJSON_Number) p->theme_text = item->valueint;
    item = cJSON_GetObjectItem(obj, "theme_btn");
    if (item && item->type == cJSON_Number) p->theme_btn = item->valueint;
    item = cJSON_GetObjectItem(obj, "theme_logo");
    if (item && item->type == cJSON_Number) p->theme_logo = item->valueint;
}

// Write all profiles to wibelight.json
void config_write_settings(char* filename, PCONFIGURATION config, const uint8_t ip_octets[][4], int max_ips) {
    (void)config; (void)ip_octets; (void)max_ips; // profile-based on WiiU

    extern profile_t profiles[];
    extern int active_profile;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "last_active_profile", active_profile);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < MAX_PROFILES; i++) {
        cJSON *pobj = profile_to_json(&profiles[i]);
        if (pobj) cJSON_AddItemToArray(arr, pobj);
    }
    cJSON_AddItemToObject(root, "profiles", arr);

    char *json_str = cJSON_Print(root);
    if (json_str) {
        FILE *fd = fopen(filename, "w");
        if (fd) {
            fprintf(fd, "%s\n", json_str);
            fsync(fileno(fd));
            fclose(fd);
        }
        free(json_str);
    }
    cJSON_Delete(root);
}

// Read profiles from wibelight.json
// Returns true if profiles were loaded, false if file not found (first boot)
bool config_json_parse(char* filename, PCONFIGURATION config) {
    extern profile_t profiles[];
    extern int active_profile;

    FILE *fd = fopen(filename, "r");
    if (!fd) return false;

    fseek(fd, 0, SEEK_END);
    long fsize = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fd); return false; }

    char *json_buf = (char*)malloc(fsize + 1);
    if (!json_buf) { fclose(fd); return false; }
    fread(json_buf, 1, fsize, fd);
    json_buf[fsize] = '\0';
    fclose(fd);

    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);
    if (!root) return false;

    // Parse last_active_profile
    cJSON *item = cJSON_GetObjectItem(root, "last_active_profile");
    if (item && item->type == cJSON_Number) active_profile = item->valueint;

    // Parse profiles array
    cJSON *profiles_arr = cJSON_GetObjectItem(root, "profiles");
    if (profiles_arr && profiles_arr->type == cJSON_Array) {
        int count = cJSON_GetArraySize(profiles_arr);
        for (int i = 0; i < count && i < MAX_PROFILES; i++) {
            cJSON *pobj = cJSON_GetArrayItem(profiles_arr, i);
            if (pobj) json_to_profile(pobj, &profiles[i]);
        }
    }

    cJSON_Delete(root);
    return true;
}

bool config_parse(int argc, char* argv[], PCONFIGURATION config) {
  LiInitializeStreamConfiguration(&config->stream);

  config->stream.width = 854;
  config->stream.height = 480;
  config->stream.fps = 60;
  config->stream.clientRefreshRateX100 = 5994; // Wii U TV refresh rate (59.94 Hz)
  config->stream.bitrate = 3000;
  config->stream.packetSize = 1024; // Safe for WiFi MTU (802.11 overhead ~276 bytes)
  config->stream.streamingRemotely = STREAM_CFG_AUTO;
  config->stream.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
  config->stream.supportedVideoFormats = SCM_H264;
  config->stream.encryptionFlags = ENCFLG_AUDIO;
  config->stream.colorSpace = COLORSPACE_REC_709;
  config->stream.colorRange = COLOR_RANGE_FULL;

#ifdef __arm__
  char cpuinfo[4096] = {};
  if (read_file("/proc/cpuinfo", cpuinfo, sizeof(cpuinfo) - 1) > 0) {
    // If this is a ARMv6 CPU (like the Pi 1), we'll assume it's not
    // powerful enough to handle audio encryption. The Pi 1 could
    // barely handle Opus decoding alone.
    if (strstr(cpuinfo, "ARMv6")) {
      config->stream.encryptionFlags = ENCFLG_NONE;
      printf("Disabling audio encryption on low performance CPU\n");
    }
  }
#endif

#ifdef __WIIU__
  // Audio encryption too slow on Broadway CPU
  config->stream.encryptionFlags = ENCFLG_NONE;
  config->stream.fps = 60;
#endif

  config->debug_level = 0;
  config->platform = strdup("auto");
  config->app = strdup("Desktop");
  config->action = NULL;
  config->address = NULL;
  config->config_file = NULL;
  config->audio_device = NULL;
  config->sops = true;
  config->localaudio = false;
  config->fullscreen = true;
  config->unsupported = true;
  config->quitappafter = true;
  config->viewonly = false;
  config->mouse_emulation = true;
  config->rotate = 0;
  config->codec = CODEC_UNSPECIFIED;
  config->hdr = false;
  config->pin = 0;
  config->port = 47989;

  config->inputsCount = 0;

#ifndef __WIIU__
  config->codec = CODEC_UNSPECIFIED;
  config->mapping = get_path("gamecontrollerdb.txt", getenv("XDG_DATA_DIRS"));
  config->key_dir[0] = 0;
#else
  config->codec = CODEC_H264;
  config->mapping = strdup("");
  strcpy(config->key_dir, wibelight_WIIU_PATH "/keys");
#endif

#ifndef __WIIU__
  char* config_file = get_path("wibelight.json", "/etc");
  if (config_file)
    config_json_parse(config_file, config);

  if (argc == 2 && access(argv[1], F_OK) == 0) {
    config->action = "stream";
    if (!config_json_parse(argv[1], config))
      exit(EXIT_FAILURE);

  } else {
    int option_index = 0;
    int c;
    while ((c = getopt_long_only(argc, argv, "-abc:d:efg:h:i:j:k:lm:no:p:q:r:s:tu:v:w:xy45:6:7", long_options, &option_index)) != -1) {
      parse_argument(c, optarg, config);
    }
  }

  if (config->config_file != NULL)
    config_write_settings(config->config_file, config, NULL, 0);

  if (config->key_dir[0] == 0x0) {
    struct passwd *pw = getpwuid(getuid());
    const char *dir;
    if ((dir = getenv("XDG_CACHE_DIR")) != NULL)
      sprintf(config->key_dir, "%s" wibelight_PATH, dir);
    else if ((dir = getenv("HOME")) != NULL)
      sprintf(config->key_dir, "%s" DEFAULT_CACHE_DIR wibelight_PATH, dir);
    else
      sprintf(config->key_dir, "%s" DEFAULT_CACHE_DIR wibelight_PATH, pw->pw_dir);
  }
#else
  // Load JSON config (returns true if file exists, false on first boot)
  char json_file[512];
  snprintf(json_file, sizeof(json_file), "%s/wibelight.json", wibelight_WIIU_PATH);
  bool has_config = config_json_parse(json_file, config);
#endif

  // Clamp bitrate: -1 (old "Auto") is no longer supported, default to 3000
  if (config->stream.bitrate < 0) {
    config->stream.bitrate = 3000;
  }

#ifndef __WIIU__
  return true;
#else
  return has_config;
#endif
}

