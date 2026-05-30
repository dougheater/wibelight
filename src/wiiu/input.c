#include "../config.h"
#include "wiiu.h"

#include <malloc.h>

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/time.h>
#include <coreinit/alarm.h>
#include <coreinit/thread.h>

#define millis() OSTicksToMilliseconds(OSGetTime())

int disable_gamepad = 0;
int swap_buttons = 0;
int enable_rumble = 1;
int rumble_strength = 0; // 0=Low, 1=Medium, 2=High, 3=Full
int enable_nav_click = 0; // Menu rumble: subtle haptic feedback on navigation
mouse_modes mouse_mode = MOUSE_MODE_ABSOLUTE;

static char lastTouched = 0;
static char touched = 0;

static uint16_t last_x = 0;
static uint16_t last_y = 0;

#define TAP_MILLIS 100
#define DRAG_DISTANCE 10
static uint64_t touchDownMillis = 0;

#define TOUCH_WIDTH 1280
#define TOUCH_HEIGHT 720

static int thread_running;
static OSThread inputThread;
static OSAlarm inputAlarm;

// ~60 Hz
#define INPUT_UPDATE_RATE OSMillisecondsToTicks(16)

void handleTouch(VPADTouchData touch) {
  if (mouse_mode == MOUSE_MODE_ABSOLUTE) {
    if (touch.touched) {
      LiSendMousePositionEvent(touch.x, touch.y, TOUCH_WIDTH, TOUCH_HEIGHT);

      if (!touched) {
        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
        touched = 1;
      }
    }
    else if (touched) {
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
      touched = 0;
    }
  }
  else if (mouse_mode == MOUSE_MODE_TOUCHSCREEN) {
    if (touch.touched) {
      if (!touched) {
        LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, 0, (float) touch.x / TOUCH_WIDTH, (float) touch.y / TOUCH_HEIGHT, 0.0, 0.0, 0.0, 0.0);
        touched = 1;
      } else {
        LiSendTouchEvent(LI_TOUCH_EVENT_MOVE, 0, (float) touch.x / TOUCH_WIDTH, (float) touch.y / TOUCH_HEIGHT, 0.0, 0.0, 0.0, 0.0);
      }
    }
    else if (touched) {
      LiSendTouchEvent(LI_TOUCH_EVENT_UP, 0, (float) touch.x / TOUCH_WIDTH, (float) touch.y / TOUCH_HEIGHT, 0.0, 0.0, 0.0, 0.0);
      touched = 0;
    }
  }
  else {
    // Just pressed (run this twice to allow touch position to settle)
    if (lastTouched < 2 && touch.touched) {
      touchDownMillis = millis();
      last_x = touch.x;
      last_y = touch.y;

      lastTouched++;
      return; // We can't do much until we wait for a few hundred milliseconds
              // since we don't know if it's a tap, a tap-and-hold, or a drag
    }

    // Just released
    if (lastTouched && !touch.touched) {
      if (millis() - touchDownMillis < TAP_MILLIS) {
        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
      }
    }

    if (touch.touched) {
      // Holding & dragging screen, not just tapping
      if (millis() - touchDownMillis > TAP_MILLIS || touchDownMillis == 0) {
        if (touch.x != last_x || touch.y != last_y) // Don't send extra data if we don't need to
          LiSendMouseMoveEvent(touch.x - last_x, touch.y - last_y);
        last_x = touch.x;
        last_y = touch.y;
      } else {
        if (touch.x - last_x < -10 || touch.x - last_x > 10) touchDownMillis=0;
        if (touch.y - last_y < -10 || touch.y - last_y > 10) touchDownMillis=0;
        int16_t diff_x = touch.x - last_x;
        int16_t diff_y = touch.y - last_y;
        if (diff_x < 0) diff_x = -diff_x;
        if (diff_y < 0) diff_y = -diff_y;
        if (diff_x + diff_y > DRAG_DISTANCE) touchDownMillis = 0;
      }
    }

    lastTouched = touch.touched ? lastTouched : 0; // Keep value unless released
  }
}

void wiiu_input_init(void)
{
	KPADInit();
	WPADEnableURCC(1);
	// Enable fast button repeat for menu navigation (200ms delay, 50ms repeat)
	VPADSetBtnRepeat(VPAD_CHAN_0, 0.2f, 0.05f);
}

// Rumble support for GamePad (controller 0)
// wibelight sends uint16_t values (0-65535) for lowFreqMotor and highFreqMotor.
// The Wii U GamePad has a single vibration motor controlled by VPADControlMotor(),
// which takes a byte pattern where 0xFF = rumble, 0x00 = silent.
// Each byte represents a 20ms tick; the pattern loops.
// We map intensity to a PWM duty cycle: higher motor values = more 0xFF bytes.
//
// Pattern length: 16 ticks = 320ms cycle.
// Minimum duty: 2/16 (12.5%) so the motor has enough time to spin up and be felt.
// Maximum duty: 16/16 (100%) for full rumble.
//
// Intensity calculation:
//   combined = max(lowFreqMotor, highFreqMotor)  [0..65535]
//   duty     = 2 + (combined / 65535) * 14       [2..16 ticks on]
//
// To avoid spamming VPADControlMotor with identical patterns, we track the last
// intensity sent and only update when it changes by at least one tick.

#define RUMBLE_PATTERN_LEN  16
#define RUMBLE_MIN_DUTY     2
#define RUMBLE_MAX_DUTY     RUMBLE_PATTERN_LEN
#define RUMBLE_DUTY_RANGE   (RUMBLE_MAX_DUTY - RUMBLE_MIN_DUTY)  // 14

static uint8_t rumble_pattern[RUMBLE_PATTERN_LEN];
static uint8_t rumble_last_duty = 0;

static void rumble_build_pattern(uint8_t duty) {
  // Clamp duty to valid range
  if (duty < RUMBLE_MIN_DUTY) duty = RUMBLE_MIN_DUTY;
  if (duty > RUMBLE_MAX_DUTY) duty = RUMBLE_MAX_DUTY;
  for (int i = 0; i < RUMBLE_PATTERN_LEN; i++) {
    rumble_pattern[i] = (i < duty) ? 0xFF : 0x00;
  }
}

void wiiu_rumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor) {
  if (controllerNumber != 0 || !enable_rumble || disable_gamepad) {
    return;
  }

  // Combine motors: use the stronger of the two
  unsigned short combined = lowFreqMotor > highFreqMotor ? lowFreqMotor : highFreqMotor;

  if (combined == 0) {
    VPADStopMotor(VPAD_CHAN_0);
    rumble_last_duty = 0;
    return;
  }

  // Apply user-adjustable strength multiplier (promote to 32-bit to avoid overflow)
  // Aggressively compressed curve so Low is subtle and Full is the only maxed option:
  //   Low(0)=2/32, Medium(1)=4/32, High(2)=12/32, Full(3)=32/32
  static const uint8_t strength_mult[] = {2, 4, 12, 32};
  combined = (unsigned short)((combined * strength_mult[rumble_strength]) / 32);

  if (combined == 0) {
    VPADStopMotor(VPAD_CHAN_0);
    rumble_last_duty = 0;
    return;
  }

  // Map 0..65535 -> duty cycle RUMBLE_MIN_DUTY..RUMBLE_MAX_DUTY
  uint8_t duty = RUMBLE_MIN_DUTY + ((combined * RUMBLE_DUTY_RANGE) / 65535);

  // Only rebuild + send if duty changed (avoids API spam)
  if (duty != rumble_last_duty) {
    rumble_build_pattern(duty);
    VPADControlMotor(VPAD_CHAN_0, rumble_pattern, RUMBLE_PATTERN_LEN);
    rumble_last_duty = duty;
  }
}

void wiiu_rumble_stop(void) {
  VPADStopMotor(VPAD_CHAN_0);
  rumble_last_duty = 0;
}

// Menu rumble: 1 tick on (20ms) out of 8 ticks (160ms total)
// Short burst — ~62.5% weaker than the original 2-on-8 pattern.
void wiiu_nav_click(void) {
  if (!enable_nav_click || !enable_rumble || disable_gamepad) {
    return;
  }
  static uint8_t click_pattern[8] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  VPADControlMotor(VPAD_CHAN_0, click_pattern, 8);
}

// Track whether we've already sent a controller arrival event for each slot.
// controllerNumber 0 = GamePad, 1-4 = Pro/Classic controllers.
static int controller_arrived[5] = {0};

// Reset controller arrival tracking between streams so the host receives
// a fresh LiSendControllerArrivalEvent on each new connection.
void wiiu_input_reset(void)
{
  for (int i = 0; i < 5; i++)
    controller_arrived[i] = 0;
}

// Cached controller presence: avoids calling WPADProbe 8x per frame.
// Updated only once per second (see disconnect_check_counter below).
static int cached_present[5] = {0};
static short cached_gamepad_mask = 0;

void wiiu_input_update(void) {
  static uint64_t home_pressed[4] = {0};
  static int disconnect_check_counter = 0;
  static int initialized = 0;

  // Probe controllers on the very first call so gamepad_mask is valid
  // immediately — the controller arrival event depends on it.
  // After that, throttle to ~1 Hz (every 60 ticks) since hot-plug is rare.
  if (!initialized) {
    initialized = 1;
    disconnect_check_counter = 59; // triggers probe on this call
  }

  disconnect_check_counter++;
  if (disconnect_check_counter >= 60) {
    disconnect_check_counter = 0;
    int present[5] = {0};
    present[0] = !disable_gamepad;
    int presentCount = present[0];
    cached_gamepad_mask = 0;
    for (int i = 0; i < 4; i++) {
      WPADExtensionType type;
      if (WPADProbe((WPADChan) i, &type) == 0) {
        if (type == WPAD_EXT_PRO_CONTROLLER || type == WPAD_EXT_CLASSIC || type == WPAD_EXT_MPLUS_CLASSIC) {
          present[presentCount] = 1;
          presentCount++;
          cached_gamepad_mask |= 1 << presentCount;
        }
      }
    }
    // Also set bit 0 for GamePad
    if (present[0]) cached_gamepad_mask |= 1;
    for (int i = 0; i < 5; i++) {
      cached_present[i] = present[i];
      if (!present[i]) {
        controller_arrived[i] = 0;
      }
    }
  }

  short controllerNumber = 0;
  short gamepad_mask = cached_gamepad_mask;

  VPADStatus vpad;
  VPADReadError err;
  VPADRead(VPAD_CHAN_0, &vpad, 1, &err);
  if (err == VPAD_READ_SUCCESS && !disable_gamepad) {
    uint32_t btns = vpad.hold;
    short buttonFlags = 0;
#define CHECKBTN(v, f) if (btns & v) buttonFlags |= f;
    if (swap_buttons) {
      CHECKBTN(VPAD_BUTTON_A,       B_FLAG);
      CHECKBTN(VPAD_BUTTON_B,       A_FLAG);
      CHECKBTN(VPAD_BUTTON_X,       Y_FLAG);
      CHECKBTN(VPAD_BUTTON_Y,       X_FLAG);
    }
    else {
      CHECKBTN(VPAD_BUTTON_A,       A_FLAG);
      CHECKBTN(VPAD_BUTTON_B,       B_FLAG);
      CHECKBTN(VPAD_BUTTON_X,       X_FLAG);
      CHECKBTN(VPAD_BUTTON_Y,       Y_FLAG);
    }
    CHECKBTN(VPAD_BUTTON_UP,      UP_FLAG);
    CHECKBTN(VPAD_BUTTON_DOWN,    DOWN_FLAG);
    CHECKBTN(VPAD_BUTTON_LEFT,    LEFT_FLAG);
    CHECKBTN(VPAD_BUTTON_RIGHT,   RIGHT_FLAG);
    CHECKBTN(VPAD_BUTTON_L,       LB_FLAG);
    CHECKBTN(VPAD_BUTTON_R,       RB_FLAG);
    CHECKBTN(VPAD_BUTTON_STICK_L, LS_CLK_FLAG);
    CHECKBTN(VPAD_BUTTON_STICK_R, RS_CLK_FLAG);
    CHECKBTN(VPAD_BUTTON_PLUS,    PLAY_FLAG);
    CHECKBTN(VPAD_BUTTON_MINUS,   BACK_FLAG);
    CHECKBTN(VPAD_BUTTON_HOME,    SPECIAL_FLAG);
#undef CHECKBTN

    // If the button was just pressed, reset to current time
    if (vpad.trigger & VPAD_BUTTON_HOME) home_pressed[controllerNumber] = millis();

    if (btns & VPAD_BUTTON_HOME && millis() - home_pressed[controllerNumber] > 3000) {
      state = STATE_STOP_STREAM;
      return;
    }

    // Advertise controller capabilities on first event (GamePad with rumble)
    if (!controller_arrived[controllerNumber]) {
      uint32_t supportedButtons = A_FLAG | B_FLAG | X_FLAG | Y_FLAG |
                                  UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG |
                                  LB_FLAG | RB_FLAG | PLAY_FLAG | BACK_FLAG |
                                  LS_CLK_FLAG | RS_CLK_FLAG | SPECIAL_FLAG;
      uint16_t caps = enable_rumble ? LI_CCAP_RUMBLE : 0;
      LiSendControllerArrivalEvent(controllerNumber, gamepad_mask, LI_CTYPE_NINTENDO,
                                   supportedButtons, caps);
      controller_arrived[controllerNumber] = 1;
    }

    LiSendMultiControllerEvent(controllerNumber++, gamepad_mask, buttonFlags,
      (vpad.hold & VPAD_BUTTON_ZL) ? 0xFF : 0,
      (vpad.hold & VPAD_BUTTON_ZR) ? 0xFF : 0,
      vpad.leftStick.x * INT16_MAX, vpad.leftStick.y * INT16_MAX,
      vpad.rightStick.x * INT16_MAX, vpad.rightStick.y * INT16_MAX);

    VPADTouchData touch;
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &touch, &vpad.tpNormal);
    handleTouch(touch);
  }

  KPADStatus kpad_data = {0};
	int32_t kpad_err = -1;
	for (int i = 0; i < 4; i++) {
		KPADReadEx((KPADChan) i, &kpad_data, 1, &kpad_err);
		if (kpad_err == KPAD_ERROR_OK && controllerNumber < 4) {
      if (kpad_data.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        uint32_t btns = kpad_data.pro.hold;
        short buttonFlags = 0;
#define CHECKBTN(v, f) if (btns & v) buttonFlags |= f;
        if (swap_buttons) {
          CHECKBTN(WPAD_PRO_BUTTON_A,       B_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_B,       A_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_X,       Y_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_Y,       X_FLAG);
        }
        else {
          CHECKBTN(WPAD_PRO_BUTTON_A,       A_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_B,       B_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_X,       X_FLAG);
          CHECKBTN(WPAD_PRO_BUTTON_Y,       Y_FLAG);
        }
        CHECKBTN(WPAD_PRO_BUTTON_UP,      UP_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_DOWN,    DOWN_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_LEFT,    LEFT_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_RIGHT,   RIGHT_FLAG);
        CHECKBTN(WPAD_PRO_TRIGGER_L,      LB_FLAG);
        CHECKBTN(WPAD_PRO_TRIGGER_R,      RB_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_STICK_L, LS_CLK_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_STICK_R, RS_CLK_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_PLUS,    PLAY_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_MINUS,   BACK_FLAG);
        CHECKBTN(WPAD_PRO_BUTTON_HOME,    SPECIAL_FLAG);
#undef CHECKBTN

        // If the button was just pressed, reset to current time
        if (kpad_data.pro.trigger & WPAD_PRO_BUTTON_HOME)
          home_pressed[controllerNumber] = millis();

        if (btns & WPAD_PRO_BUTTON_HOME && millis() - home_pressed[controllerNumber] > 3000) {
          state = STATE_STOP_STREAM;
          return;
        }

        // Advertise controller capabilities on first event (Pro Controller, no rumble)
        if (!controller_arrived[controllerNumber]) {
          uint32_t supportedButtons = A_FLAG | B_FLAG | X_FLAG | Y_FLAG |
                                      UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG |
                                      LB_FLAG | RB_FLAG | PLAY_FLAG | BACK_FLAG |
                                      LS_CLK_FLAG | RS_CLK_FLAG | SPECIAL_FLAG;
          LiSendControllerArrivalEvent(controllerNumber, gamepad_mask, LI_CTYPE_NINTENDO,
                                       supportedButtons, LI_CCAP_ANALOG_TRIGGERS);
          controller_arrived[controllerNumber] = 1;
        }

        LiSendMultiControllerEvent(controllerNumber++, gamepad_mask, buttonFlags,
          (kpad_data.pro.hold & WPAD_PRO_TRIGGER_ZL) ? 0xFF : 0,
          (kpad_data.pro.hold & WPAD_PRO_TRIGGER_ZR) ? 0xFF : 0,
          kpad_data.pro.leftStick.x * INT16_MAX, kpad_data.pro.leftStick.y * INT16_MAX,
          kpad_data.pro.rightStick.x * INT16_MAX, kpad_data.pro.rightStick.y * INT16_MAX);
      }
      else if (kpad_data.extensionType == WPAD_EXT_CLASSIC || kpad_data.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
        uint32_t btns = kpad_data.classic.hold;
        short buttonFlags = 0;
#define CHECKBTN(v, f) if (btns & v) buttonFlags |= f;
        if (swap_buttons) {
          CHECKBTN(WPAD_CLASSIC_BUTTON_A,       B_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_B,       A_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_X,       Y_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_Y,       X_FLAG);
        }
        else {
          CHECKBTN(WPAD_CLASSIC_BUTTON_A,       A_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_B,       B_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_X,       X_FLAG);
          CHECKBTN(WPAD_CLASSIC_BUTTON_Y,       Y_FLAG);
        }
        CHECKBTN(WPAD_CLASSIC_BUTTON_UP,      UP_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_DOWN,    DOWN_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_LEFT,    LEFT_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_RIGHT,   RIGHT_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_L,       LB_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_R,       RB_FLAG);
        // don't have stick buttons on a classic controller
        // CHECKBTN(WPAD_CLASSIC_BUTTON_STICK_L, LS_CLK_FLAG);
        // CHECKBTN(WPAD_CLASSIC_BUTTON_STICK_R, RS_CLK_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_PLUS,    PLAY_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_MINUS,   BACK_FLAG);
        CHECKBTN(WPAD_CLASSIC_BUTTON_HOME,    SPECIAL_FLAG);
#undef CHECKBTN

        // If the button was just pressed, reset to current time
        if (kpad_data.classic.trigger & WPAD_CLASSIC_BUTTON_HOME)
          home_pressed[controllerNumber] = millis();

        if (btns & WPAD_CLASSIC_BUTTON_HOME && millis() - home_pressed[controllerNumber] > 3000) {
          state = STATE_STOP_STREAM;
          return;
        }

        // Advertise controller capabilities on first event (Classic Controller, no rumble, no stick clicks)
        if (!controller_arrived[controllerNumber]) {
          uint32_t supportedButtons = A_FLAG | B_FLAG | X_FLAG | Y_FLAG |
                                      UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG |
                                      LB_FLAG | RB_FLAG | PLAY_FLAG | BACK_FLAG | SPECIAL_FLAG;
          LiSendControllerArrivalEvent(controllerNumber, gamepad_mask, LI_CTYPE_NINTENDO,
                                       supportedButtons, LI_CCAP_ANALOG_TRIGGERS);
          controller_arrived[controllerNumber] = 1;
        }

        LiSendMultiControllerEvent(controllerNumber++, gamepad_mask, buttonFlags,
          (kpad_data.classic.hold & WPAD_CLASSIC_BUTTON_ZL) ? 0xFF : 0x00,
          (kpad_data.classic.hold & WPAD_CLASSIC_BUTTON_ZR) ? 0xFF : 0x00,
          kpad_data.classic.leftStick.x * INT16_MAX, kpad_data.classic.leftStick.y * INT16_MAX,
          kpad_data.classic.rightStick.x * INT16_MAX, kpad_data.classic.rightStick.y * INT16_MAX);
      }
    }
  }
}

uint32_t wiiu_input_num_controllers(void)
{
  uint32_t numControllers = !disable_gamepad;

  WPADExtensionType type;
  for (int i = 0; i < 4; i++) {
    if (WPADProbe((WPADChan) i, &type) == 0) {
      if (type == WPAD_EXT_PRO_CONTROLLER || type == WPAD_EXT_CLASSIC || type == WPAD_EXT_MPLUS_CLASSIC) {
        numControllers++;
      }
    }
  }

  if (numControllers > 4) {
    numControllers = 4;
  }

  return numControllers;
}

uint32_t wiiu_input_buttons_triggered(void)
{
  uint32_t btns = 0;

  VPADStatus vpad;
  VPADReadError vpad_err;
  VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_err);
  if (vpad_err == VPAD_READ_SUCCESS) {
    btns |= vpad.trigger;
  }

  KPADStatus kpad_data = {0};
	int32_t kpad_err = -1;
	for (int i = 0; i < 4; i++) {
		KPADReadEx((KPADChan) i, &kpad_data, 1, &kpad_err);
		if (kpad_err == KPAD_ERROR_OK) {
      if (kpad_data.extensionType == WPAD_EXT_PRO_CONTROLLER) {
#define MAPBTNS(b, v) if (kpad_data.pro.trigger & b) btns |= v;
        MAPBTNS(WPAD_PRO_BUTTON_UP,       VPAD_BUTTON_UP);
        MAPBTNS(WPAD_PRO_BUTTON_LEFT,     VPAD_BUTTON_LEFT);
        MAPBTNS(WPAD_PRO_TRIGGER_ZR,      VPAD_BUTTON_ZR);
        MAPBTNS(WPAD_PRO_BUTTON_X,        VPAD_BUTTON_X);
        MAPBTNS(WPAD_PRO_BUTTON_A,        VPAD_BUTTON_A);
        MAPBTNS(WPAD_PRO_BUTTON_Y,        VPAD_BUTTON_Y);
        MAPBTNS(WPAD_PRO_BUTTON_B,        VPAD_BUTTON_B);
        MAPBTNS(WPAD_PRO_TRIGGER_ZL,      VPAD_BUTTON_ZL);
        MAPBTNS(WPAD_PRO_TRIGGER_R,       VPAD_BUTTON_R);
        MAPBTNS(WPAD_PRO_BUTTON_PLUS,     VPAD_BUTTON_PLUS);
        MAPBTNS(WPAD_PRO_BUTTON_HOME,     VPAD_BUTTON_HOME);
        MAPBTNS(WPAD_PRO_BUTTON_MINUS,    VPAD_BUTTON_MINUS);
        MAPBTNS(WPAD_PRO_TRIGGER_L,       VPAD_BUTTON_L);
        MAPBTNS(WPAD_PRO_BUTTON_DOWN,     VPAD_BUTTON_DOWN);
        MAPBTNS(WPAD_PRO_BUTTON_RIGHT,    VPAD_BUTTON_RIGHT);
        MAPBTNS(WPAD_PRO_BUTTON_STICK_R,  VPAD_BUTTON_STICK_R);
        MAPBTNS(WPAD_PRO_BUTTON_STICK_L,  VPAD_BUTTON_STICK_L);
#undef MAPBTNS
      }
      else if (kpad_data.extensionType == WPAD_EXT_CLASSIC || kpad_data.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
#define MAPBTNS(b, v) if (kpad_data.classic.trigger & b) btns |= v;
        MAPBTNS(WPAD_CLASSIC_BUTTON_UP,     VPAD_BUTTON_UP);
        MAPBTNS(WPAD_CLASSIC_BUTTON_LEFT,   VPAD_BUTTON_LEFT);
        MAPBTNS(WPAD_CLASSIC_BUTTON_ZR,     VPAD_BUTTON_ZR);
        MAPBTNS(WPAD_CLASSIC_BUTTON_X,      VPAD_BUTTON_X);
        MAPBTNS(WPAD_CLASSIC_BUTTON_A,      VPAD_BUTTON_A);
        MAPBTNS(WPAD_CLASSIC_BUTTON_Y,      VPAD_BUTTON_Y);
        MAPBTNS(WPAD_CLASSIC_BUTTON_B,      VPAD_BUTTON_B);
        MAPBTNS(WPAD_CLASSIC_BUTTON_ZL,     VPAD_BUTTON_ZL);
        MAPBTNS(WPAD_CLASSIC_BUTTON_R,      VPAD_BUTTON_R);
        MAPBTNS(WPAD_CLASSIC_BUTTON_PLUS,   VPAD_BUTTON_PLUS);
        MAPBTNS(WPAD_CLASSIC_BUTTON_HOME,   VPAD_BUTTON_HOME);
        MAPBTNS(WPAD_CLASSIC_BUTTON_MINUS,  VPAD_BUTTON_MINUS);
        MAPBTNS(WPAD_CLASSIC_BUTTON_L,      VPAD_BUTTON_L);
        MAPBTNS(WPAD_CLASSIC_BUTTON_DOWN,   VPAD_BUTTON_DOWN);
        MAPBTNS(WPAD_CLASSIC_BUTTON_RIGHT,  VPAD_BUTTON_RIGHT);
#undef MAPBTNS
      }
      else {
        // meh we can't really map a wiimote to gamepad
      }
    }
  }

  return btns;
}

static void alarm_callback(OSAlarm* alarm, OSContext* ctx)
{
  wiiu_input_update();
}

static int input_thread_proc(int argc, const char **argv)
{
  (void)argc; (void)argv;
  OSCreateAlarm(&inputAlarm);
  OSSetPeriodicAlarm(&inputAlarm, 0, INPUT_UPDATE_RATE, alarm_callback);

  while (thread_running) {
    OSWaitAlarm(&inputAlarm);
  }
  return 0;
}

static void thread_deallocator(OSThread *thread, void *stack)
{
  free(stack);
}

void start_input_thread(void)
{
  const int stack_size = 4 * 1024 * 1024;
  uint8_t* stack = (uint8_t*)memalign(16, stack_size);
  if (!stack) {
    return;
  }

  if (!OSCreateThread(&inputThread,
                      input_thread_proc,
                      0, NULL,
                      stack + stack_size, stack_size,
                      0x10, OS_THREAD_ATTRIB_AFFINITY_ANY))
  {
    free(stack);
    return;
  }

  thread_running = 1;

  OSSetThreadName(&inputThread, "PadInput");
  OSSetThreadDeallocator(&inputThread, thread_deallocator);
  OSResumeThread(&inputThread);
}

void stop_input_thread(void)
{
  if (!thread_running) return; // thread was never started
  thread_running = 0;
  OSCancelAlarm(&inputAlarm);
  OSJoinThread(&inputThread, NULL);
}
