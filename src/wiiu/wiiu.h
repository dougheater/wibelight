#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

enum {
  STATE_INVALID,
  STATE_MENU,
  STATE_PROFILE_SELECTOR,
  STATE_PROFILE_IP_EDIT,
  STATE_SETTINGS,
  STATE_CUSTOM_BITRATE,
  STATE_DISCONNECTED,
  STATE_CONNECTING,
  STATE_CONNECTED,
  STATE_PAIRING,
  STATE_START_STREAM,
  STATE_STOP_STREAM,
  STATE_STREAMING,
  STATE_BENCHMARK,
  STATE_BENCHMARK_CONNECTING,
  STATE_BENCHMARK_RESULTS,
  STATE_UNPAIR_CONFIRM,
  STATE_SENDING_WOL,
};

#include "font.h"
#include <Limelight.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <gx2/texture.h>

extern int state;
extern int is_error;
extern int disconnecting;       // Set before LiStopConnection() so callbacks can suppress errors
extern char message_buffer[1024];

#define FRAME_BUFFER 12

void wiiu_stream_init(uint32_t width, uint32_t height);
void wiiu_stream_draw(void);
void wiiu_stream_fini(void);
void wiiu_stream_reset(void);          // Reset frame queue between streams
void wiiu_setup_renderstate(void);

#define NUM_BUFFERS 2
#define MAX_QUEUEMESSAGES_MAX 8        // Hard max for the ring buffer allocation

// Runtime queue depth (set from UI, defaults to 4)
// Presets: 2 (low latency), 4 (default), 8 (max buffering)
extern int max_queued_frames;

typedef struct {
  GX2Texture yTex;
  GX2Texture uvTex;
} yuv_texture_t;

void *get_frame(void);
void add_frame(yuv_texture_t *msg);

extern volatile uint32_t nextFrame;

extern AUDIO_RENDERER_CALLBACKS audio_callbacks_wiiu;
extern DECODER_RENDERER_CALLBACKS decoder_callbacks_wiiu;

// Tunable runtime parameters (set from UI before stream starts)
extern int audio_buffer_samples;  // SDL audio buffer size (2048 or 4096)
extern int rumble_strength;       // 0=Low, 1=Medium, 2=High, 3=Full

// input
void wiiu_input_init(void);
void wiiu_input_update(void); // this is only relevant while streaming
void wiiu_input_reset(void);  // reset controller_arrived[] between streams
uint32_t wiiu_input_num_controllers(void);
uint32_t wiiu_input_buttons_triggered(void); // only really used for the menu
void start_input_thread(void);
void stop_input_thread(void);
void wiiu_rumble(unsigned short controllerNumber, unsigned short lowFreqMotor,
                 unsigned short highFreqMotor);
void wiiu_rumble_stop(void);
void wiiu_nav_click(void);

// proc
void wiiu_proc_init(void);
void wiiu_proc_shutdown_procui(void);
void wiiu_proc_shutdown(void);
void wiiu_proc_register_home_callback(void);
int wiiu_proc_running(void);
void wiiu_proc_stop_running(void);
void wiiu_proc_set_home_enabled(int enabled);

// net
void wiiu_net_init(void);
void wiiu_net_shutdown(void);

// (adaptive bitrate removed)
