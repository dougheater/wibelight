/*
 * UI theme system for wibelight.
 *
 * Provides customizable background color, accent color, and text contrast.
 * All UI screens should use the theme_* variables instead of hardcoded colors.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include "../third_party/cJSON/cJSON.h"

// --- Preset indices ---
// Backgrounds (darkest → brightest)
#define THEME_BG_PURE_BLACK      0   // Pure Black (#000000)
#define THEME_BG_DEEP_NAVY       1   // Deep Navy (#000034)
#define THEME_BG_ICONIC_GREEN    2   // Iconic Green (#040E04)
#define THEME_BG_OBSIDIAN        3   // Obsidian (#0D0D0D)
#define THEME_BG_ESPRESSO        4   // Espresso (#210F04)
#define THEME_BG_DARK_BLUE       5   // Dark Blue (#021727)
#define THEME_BG_DARK_GREEN      6   // Dark Green (#0A1A0A)
#define THEME_BG_DEEP_PURPLE     7   // Deep Purple (#2C0E37)
#define THEME_BG_CHARCOAL        8   // Charcoal (#232323)
#define THEME_BG_DARK_TEAL       9   // Dark Teal (#0A2A2A)
#define THEME_BG_DARK_SLATE      10  // Dark Slate (#1A2A3A)
#define THEME_BG_PS_BLUE         11  // PS Blue (#003087)
#define THEME_BG_UMBER           12  // Umber (#453823)
#define THEME_BG_PURPLE          13  // Purple (#5A3D7A)
#define THEME_BG_GC_BERRY        14  // GC Berry (#8B3A62)
#define THEME_BG_CRIMSON         15  // Crimson (#BF211E)
#define THEME_BG_DC_RED          16  // DC Red (#CF3311)
#define THEME_BG_BURN_ORANGE     17  // Burnt Orange (#C44900)
#define THEME_BG_WII_FLAME       18  // Wii Flame (#C44A2F)
#define THEME_BG_WII_WAVE        19  // Wii Wave (#3A7CA5)
#define THEME_BG_WII_BAMBOO      20  // Wii Bamboo (#6B8E4E)
#define THEME_BG_FOREST_GREEN    21  // Forest Green (#08A045)
#define THEME_BG_DC_ORANGE       22  // DC Orange (#ED8432)
#define THEME_BG_PS1_GRAY        23  // PS1 Gray (#ACADB6)
#define THEME_BG_WARM_SILVER     24  // Warm Silver (#D2D2BA)
#define NUM_BG_PRESETS           25

#define THEME_ACCENT_RED_ROOM     0   // Red Room (#780000)
#define THEME_ACCENT_INDIGO       1   // Indigo (#3C32A0)
#define THEME_ACCENT_WIIBE        2   // Wiibe (#555B63)
#define THEME_ACCENT_STEEL_BLUE   3   // Steel Blue (#365A9B)
#define THEME_ACCENT_PURPLE       4   // Purple (#783CC8)
#define THEME_ACCENT_BLUE         5   // Default blue accent (#2864C8)
#define THEME_ACCENT_N7           6   // N7 (#B45000)
#define THEME_ACCENT_DC_RED       7   // DC Red (#CF3311)
#define THEME_ACCENT_PINK         8   // Pink (#C83C64)
#define THEME_ACCENT_DC_BLUE      9   // DC Blue (#3B76C2)
#define THEME_ACCENT_TEAL         10  // Teal (#1E8C8C)
#define THEME_ACCENT_CORAL        11  // Coral (#DC5046)
#define THEME_ACCENT_TEAL_GREEN   12  // Teal Green (#169873)
#define THEME_ACCENT_ORANGE       13  // Orange (#C87828)
#define THEME_ACCENT_CYAN         14  // Cyan (#28A0B4)
#define THEME_ACCENT_ICONIC_GREEN 15  // Iconic Green (#00B432)
#define THEME_ACCENT_GREEN        16  // Green (#28B450)
#define THEME_ACCENT_GOLD         17  // Gold (#C8961E)
#define THEME_ACCENT_DC_ORANGE    18  // DC Orange (#ED8432)
#define THEME_ACCENT_YELLOW       19  // Yellow (#B4A014)
#define THEME_ACCENT_PS1_GRAY     20  // PS1 Gray (#ACADB6)
#define THEME_ACCENT_LIME         21  // Lime (#28C850)
#define THEME_ACCENT_WARM_SILVER  22  // Warm Silver (#D2D2BA)
#define NUM_ACCENT_PRESETS        23

#define THEME_TEXT_WHITE        0   // Pure white (#FFFFFF)
#define THEME_TEXT_SOFT_WHITE   1   // Soft White (#EDEDED)
#define THEME_TEXT_WARM         2   // Warm off-white (#E8E0D8)
#define THEME_TEXT_ICONIC_GREEN 3   // Iconic Green (#00FF41)
#define THEME_TEXT_SILVER       4   // Silver (#D0D0D0)
#define THEME_TEXT_LIGHT_GRAY   5   // Light gray (#C8C8C8)
#define THEME_TEXT_AMBER        6   // Amber (#FFB300)
#define THEME_TEXT_PS1_GRAY     7   // PS1 Gray (#ACADB6)
#define THEME_TEXT_WIIBE        8   // Wiibe (#A0A8B0)
#define THEME_TEXT_DC_ORANGE    9   // DC Orange (#ED8432)
#define THEME_TEXT_TEAL_GREEN   10  // Teal Green (#169873)
#define THEME_TEXT_DC_BLUE      11  // DC Blue (#3B76C2)
#define THEME_TEXT_DC_RED       12  // DC Red (#CF3311)
#define THEME_TEXT_STEEL_BLUE   13  // Steel Blue (#365A9B)
#define THEME_TEXT_DARK_GRAY    14  // Dark gray (#323232)
#define THEME_TEXT_BLACK        15  // Black (#000000)
#define NUM_TEXT_PRESETS        16

// Button icon style (Colored first, then darkest → brightest)
#define THEME_BTN_COLORED       0   // Wii U colors (A=green, B=red, X=blue, Y=yellow)
#define THEME_BTN_RED_ROOM      1   // Red Room (#780000) circles, dark letters
#define THEME_BTN_DARK          2   // Dark gray (#404040) circles, white letters
#define THEME_BTN_WIIBE         3   // Wiibe (#555B63) circles, white letters
#define THEME_BTN_STEEL_BLUE    4   // Steel Blue (#365A9B) circles, white letters
#define THEME_BTN_N7            5   // N7 (#B45000) circles, dark letters
#define THEME_BTN_DC_RED        6   // DC Red (#CF3311) circles, dark letters
#define THEME_BTN_DC_BLUE       7   // DC Blue (#3B76C2) circles, white letters
#define THEME_BTN_TEAL_GREEN    8   // Teal Green (#169873) circles, dark letters
#define THEME_BTN_ICONIC_GREEN  9   // Iconic Green (#00B432) circles, black letters
#define THEME_BTN_GOURYELLA     10  // Gold (#C8961E) circles, dark letters
#define THEME_BTN_DC_ORANGE     11  // DC Orange (#ED8432) circles, dark letters
#define THEME_BTN_PS1_GRAY      12  // PS1 Gray (#ACADB6) circles, white letters
#define THEME_BTN_AMBER         13  // Amber (#FFB300) circles, dark letters
#define THEME_BTN_WHITE         14  // White (#FFFFFF) circles, black letters
#define NUM_BTN_PRESETS         15

// Logo style
#define LOGO_STYLE_NORMAL 0   // Full 296x80 logo
#define LOGO_STYLE_SMALL  1   // Small 80x80 logo
#define LOGO_STYLE_NONE   2   // No logo
#define NUM_LOGO_PRESETS  3

// --- Labels for UI ---
extern const char* bg_labels[];
extern const char* accent_labels[];
extern const char* text_labels[];
extern const char* btn_labels[];
extern const char* logo_labels[];

// --- Runtime theme state ---
extern int theme_bg_preset;
extern int theme_accent_preset;
extern int theme_text_preset;
extern int theme_btn_preset;
extern int theme_logo_preset;

// --- Derived color variables (set by theme_apply) ---
// Background clear color (for WHBGfxClearColor)
extern float theme_bg_r, theme_bg_g, theme_bg_b;

// Title text (large headers like "wibelight")
extern int theme_title_r, theme_title_g, theme_title_b, theme_title_a;

// Subtitle / label text (setting labels, descriptions)
extern int theme_label_r, theme_label_g, theme_label_b, theme_label_a;

// Divider lines (semi-transparent horizontal separators)
extern int theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a;

// Item dividers (very faint lines between list items)
extern int theme_item_div_r, theme_item_div_g, theme_item_div_b, theme_item_div_a;

// Selection highlight background (semi-transparent colored bar)
extern int theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a;

// Selected item text color (accent-tinted)
extern int theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a;

// Unselected item value text color
extern int theme_val_text_r, theme_val_text_g, theme_val_text_b, theme_val_text_a;

// Button label color (bottom bar)
extern int theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a;

// Error text
extern int theme_error_r, theme_error_g, theme_error_b, theme_error_a;

// Success text
extern int theme_ok_r, theme_ok_g, theme_ok_b, theme_ok_a;

// Spinner track (faint ring)
extern int theme_spinner_track_r, theme_spinner_track_g, theme_spinner_track_b, theme_spinner_track_a;

// Spinner arc (bright rotating arc)
extern int theme_spinner_arc_r, theme_spinner_arc_g, theme_spinner_arc_b, theme_spinner_arc_a;

// --- API ---

// Compute all derived colors from current presets. Call on init and after preset change.
void theme_apply(void);

// Find the text preset with the best contrast against the current background.
// Returns the preset index (0..NUM_TEXT_PRESETS-1).
// If the current text preset already passes min_ratio, returns it unchanged.
int theme_best_text_preset(float bg_r, float bg_g, float bg_b, int current_text_preset, float min_ratio);

// Load theme settings from JSON (returns true if found)
bool theme_load_json(const char *json_buf);

// Save theme settings into a cJSON object
void theme_save_json(cJSON *root);

#endif // UI_THEME_H
