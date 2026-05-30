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

#include "listener_callbacks.h"

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>

#ifdef __WIIU__
#include "wiiu/wiiu.h"
#include "config.h"
#include <coreinit/fastmutex.h>
extern FILE *debug_log; // from main.c
#endif

#ifdef HAVE_SDL
#include <SDL.h>
#endif

ConnListenerRumble rumble_handler = wiiu_rumble;

extern void benchmark_status(int status);
extern int disconnecting;
extern OSFastMutex stateMutex;
ConnListenerRumbleTriggers rumble_triggers_handler = NULL;
ConnListenerSetMotionEventState set_motion_event_state_handler = NULL;
ConnListenerSetControllerLED set_controller_led_handler = NULL;

static void connection_terminated(int errorCode) {
  // If we're in a user-initiated disconnect, suppress all error display.
  // The callback fires from the ENet background thread during/after
  // LiStopConnection() and would otherwise overwrite our clean state.
  // Lock stateMutex to avoid racing with the main loop.
  OSFastMutex_Lock(&stateMutex);
  int disc = disconnecting;
  OSFastMutex_Unlock(&stateMutex);

  if (disc) {
    printf("Connection terminated (expected, error=%d)\n", errorCode);
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN] terminated (expected, error=%d)\n", errorCode);
#endif
    return;
  }

  switch (errorCode) {
  case ML_ERROR_GRACEFUL_TERMINATION:
    printf("Connection has been terminated gracefully.\n");
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN] graceful termination\n");
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "Connection has been terminated gracefully.\n");
    is_error = 0;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  case ML_ERROR_NO_VIDEO_TRAFFIC:
    printf("No video received from host. Check the host PC's firewall and port forwarding rules.\n");
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN-ERROR] no video traffic\n");
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "No video received from host.\n Check the host PC's firewall and port forwarding rules.\n");
    is_error = 1;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  case ML_ERROR_NO_VIDEO_FRAME:
    printf("Your network connection isn't performing well. Reduce your video bitrate setting or try a faster connection.\n");
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN-ERROR] no video frame\n");
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "Your network connection isn't performing well.\n Reduce your video bitrate setting or try a faster connection.\n");
    is_error = 1;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:
    printf("The connection was unexpectedly terminated by the host due to a video capture error. Make sure no DRM-protected content is playing on the host.\n");
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN-ERROR] unexpected early termination (DRM?)\n");
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "The connection was unexpectedly terminated by the host due to a video capture error.\n Make sure no DRM-protected content is playing on the host.\n");
    is_error = 1;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  case ML_ERROR_PROTECTED_CONTENT:
    printf("The connection was terminated by the host due to DRM-protected content. Close any DRM-protected content on the host and try again.\n");
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN-ERROR] DRM protected content\n");
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "The connection was terminated by the host due to DRM-protected content.\n Close any DRM-protected content on the host and try again.\n");
    is_error = 1;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  default:
    printf("Connection terminated with error: %d\n", errorCode);
#ifdef __WIIU__
    if (debug_log)
      fprintf(debug_log, "[CONN-ERROR] terminated with error: %d\n", errorCode);
    OSFastMutex_Lock(&stateMutex);
    snprintf(message_buffer, sizeof(message_buffer), "Connection terminated with error: %d\n", errorCode);
    is_error = 1;
    disconnecting = 1;
    state = STATE_STOP_STREAM;
    OSFastMutex_Unlock(&stateMutex);
#endif
    break;
  }

#ifndef __WIIU__
  #ifdef HAVE_SDL
      SDL_Event event;
      event.type = SDL_QUIT;
      SDL_PushEvent(&event);
  #endif
#endif
}

static void connection_log_message(const char* format, ...) {
  va_list arglist;
  va_start(arglist, format);
  vprintf(format, arglist);
  va_end(arglist);
}

static void rumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor) {
  if (rumble_handler)
    rumble_handler(controllerNumber, lowFreqMotor, highFreqMotor);
}

static void rumble_triggers(unsigned short controllerNumber, unsigned short leftTrigger, unsigned short rightTrigger) {
  if (rumble_triggers_handler)
    rumble_triggers_handler(controllerNumber, leftTrigger, rightTrigger);
}

static void set_motion_event_state(unsigned short controllerNumber, unsigned char motionType, unsigned short reportRateHz) {
  if (set_motion_event_state_handler)
    set_motion_event_state_handler(controllerNumber, motionType, reportRateHz);
}

static void set_controller_led(unsigned short controllerNumber, unsigned char r, unsigned char g, unsigned char b) {
  if (set_controller_led_handler)
    set_controller_led_handler(controllerNumber, r, g, b);
}

static void connection_status_update(int status) {
  switch (status) {
    case CONN_STATUS_OKAY:
      printf("Connection is okay\n");
#ifdef __WIIU__
      if (debug_log)
        fprintf(debug_log, "[CONN] status: okay\n");
#endif
      break;
    case CONN_STATUS_POOR:
      printf("Connection is poor\n");
#ifdef __WIIU__
      if (debug_log)
        fprintf(debug_log, "[CONN] status: POOR\n");
#endif
      break;
  }
  // Forward to benchmark tracking (no-op if benchmark is not running)
  benchmark_status(status);
}

CONNECTION_LISTENER_CALLBACKS connection_callbacks = {
  .stageStarting = NULL,
  .stageComplete = NULL,
  .stageFailed = NULL,
  .connectionStarted = NULL,
  .connectionTerminated = connection_terminated,
  .logMessage = connection_log_message,
  .rumble = rumble,
  .connectionStatusUpdate = connection_status_update,
  .setHdrMode = NULL,
  .rumbleTriggers = rumble_triggers,
  .setMotionEventState = set_motion_event_state,
  .setControllerLED = set_controller_led,
};
