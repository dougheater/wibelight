#include "ui_theme.h"
#include "../third_party/cJSON/cJSON.h"
#include <string.h>

// --- Default presets ---
int theme_bg_preset = THEME_BG_DEEP_NAVY;
int theme_accent_preset = THEME_ACCENT_STEEL_BLUE;
int theme_text_preset = THEME_TEXT_SOFT_WHITE;
int theme_btn_preset = THEME_BTN_COLORED;
int theme_logo_preset = LOGO_STYLE_SMALL;

// --- Labels for UI ---
const char* bg_labels[] = {
    "Pure Black", "Deep Navy", "Iconic Green", "Obsidian", "Espresso",
    "Dark Blue", "Dark Green", "D. Purple", "Charcoal", "Dark Teal",
    "Dark Slate", "PS Blue", "Umber", "Purple", "GC Berry",
    "Crimson", "DC Red", "B. Orange", "Wii Flame", "Wii Wave",
    "Wii Bamboo", "Forest Green", "DC Orange", "PS1 Gray", "Warm Silver"
};
const char* accent_labels[] = {
    "Red Room", "Indigo", "Wiibe", "Steel Blue", "Purple",
    "Blue", "N7", "DC Red", "Pink", "DC Blue",
    "Teal", "Coral", "Teal Green", "Orange", "Cyan",
    "Iconic Green", "Green", "Gold", "DC Orange", "Yellow",
    "PS1 Gray", "Lime", "Warm Silver"
};
const char* text_labels[] = {
    "White", "Soft White", "Warm", "Iconic Green", "Silver",
    "Light Gray", "Amber", "PS1 Gray", "Wiibe", "DC Orange",
    "Teal Green", "DC Blue", "DC Red", "Steel Blue",
    "Dark Gray", "Black"
};
const char* btn_labels[] = {
    "Colored", "Red Room", "Dark", "Wiibe", "Steel Blue",
    "N7", "DC Red", "DC Blue", "Teal Green", "Iconic Green",
    "Gouryella", "DC Orange", "PS1 Gray", "Amber", "White"
};
const char* logo_labels[] = {"Normal", "Small", "None"};

// --- Derived colors ---
float theme_bg_r, theme_bg_g, theme_bg_b;

int theme_title_r, theme_title_g, theme_title_b, theme_title_a;
int theme_label_r, theme_label_g, theme_label_b, theme_label_a;
int theme_divider_r, theme_divider_g, theme_divider_b, theme_divider_a;
int theme_item_div_r, theme_item_div_g, theme_item_div_b, theme_item_div_a;
int theme_sel_bg_r, theme_sel_bg_g, theme_sel_bg_b, theme_sel_bg_a;
int theme_sel_text_r, theme_sel_text_g, theme_sel_text_b, theme_sel_text_a;
int theme_val_text_r, theme_val_text_g, theme_val_text_b, theme_val_text_a;
int theme_btn_r, theme_btn_g, theme_btn_b, theme_btn_a;
int theme_error_r, theme_error_g, theme_error_b, theme_error_a;
int theme_ok_r, theme_ok_g, theme_ok_b, theme_ok_a;
int theme_spinner_track_r, theme_spinner_track_g, theme_spinner_track_b, theme_spinner_track_a;
int theme_spinner_arc_r, theme_spinner_arc_g, theme_spinner_arc_b, theme_spinner_arc_a;

// Accent color palettes: [preset][6] = {sel_bg_r, sel_bg_g, sel_bg_b, sel_text_r, sel_text_g, sel_text_b}
// Sorted darkest → brightest
static const uint8_t accent_palettes[NUM_ACCENT_PRESETS][6] = {
    // Red Room (#780000)
    { 120,   0,   0, 200,  60,  40},
    // Indigo (#3C32A0)
    {  60,  50, 160, 150, 140, 255},
    // Wiibe (#555B63)
    {  85,  91,  99, 160, 168, 176},
    // Steel Blue (#365A9B)
    {  54,  90, 155, 160, 190, 255},
    // Purple (#783CC8)
    { 120,  60, 200, 200, 140, 255},
    // Blue (#2864C8)
    {  40, 100, 200, 120, 200, 255},
    // N7 (#B45000)
    { 180,  80,   0, 255, 106,   0},
    // DC Red (#CF3311)
    { 207,  51,  17, 255, 180, 160},
    // Pink (#C83C64)
    { 200,  60, 100, 255, 160, 200},
    // DC Blue (#3B76C2)
    {  59, 118, 194, 140, 200, 255},
    // Teal (#1E8C8C)
    {  30, 140, 140, 130, 220, 220},
    // Coral (#DC5046)
    { 220,  80,  70, 255, 190, 185},
    // Teal Green (#169873)
    {  22, 152, 115, 120, 240, 210},
    // Orange (#C87828)
    { 200, 120,  40, 255, 200, 120},
    // Cyan (#28A0B4)
    {  40, 160, 180, 120, 240, 255},
    // Iconic Green (#00B432)
    {   0, 180,  50,   0, 255,  65},
    // Green (#28B450)
    {  40, 180,  80, 120, 255, 180},
    // Gold (#C8961E)
    { 200, 150,  30, 255, 230, 140},
    // DC Orange (#ED8432)
    { 237, 132,  50, 255, 210, 160},
    // Yellow (#B4A014)
    { 180, 160,  20, 255, 240, 100},
    // PS1 Gray (#ACADB6)
    { 172, 173, 182, 220, 220, 230},
    // Lime (#28C850)
    {  40, 200,  80, 160, 255, 140},
    // Warm Silver (#D2D2BA)
    { 210, 210, 186, 232, 232, 224},
};

// Text color palettes (file-scope for theme_best_text_preset)
// Format: {title_r,g,b label_r,g,b val_r,g,b btn_r,g,b div_r,g,b,a idiv_r,g,b,a}
// Sorted brightest → darkest
static const uint8_t text_palettes[NUM_TEXT_PRESETS][20] = {
    // White
    { 255, 255, 255,  240, 240, 240,  255, 255, 255,  240, 240, 240,  255, 255, 255, 120,  255, 255, 255, 30 },
    // Soft White
    { 237, 237, 237,  210, 210, 210,  237, 237, 237,  228, 228, 228,  237, 237, 237, 110,  237, 237, 237, 28 },
    // Warm
    { 232, 224, 216,  200, 192, 184,  232, 224, 216,  224, 216, 208,  232, 224, 216, 110,  232, 224, 216, 25 },
    // Iconic Green
    {   0, 255,  65,     0, 204,  51,     0, 255,  65,     0, 204,  51,     0, 180,  50, 100,    0, 120,  30, 25 },
    // Silver
    { 208, 208, 208,  176, 176, 176,  208, 208, 208,  200, 200, 200,  208, 208, 208, 110,  208, 208, 208, 28 },
    // Light Gray
    { 200, 200, 200,  170, 170, 170,  200, 200, 200,  190, 190, 190,  200, 200, 200, 100,  200, 200, 200, 25 },
    // Amber
    { 255, 179,   0,   232, 200,  64,   255, 179,   0,   240, 200,  60,   255, 179,   0, 100,   255, 179,   0, 25 },
    // PS1 Gray
    { 172, 173, 182, 142, 143, 152,   172, 173, 182,   162, 163, 172,   172, 173, 182, 100,   142, 143, 152, 25 },
    // Wiibe
    { 160, 168, 176,  136, 144, 160,  160, 168, 176,  150, 158, 168,  160, 168, 176, 100,  160, 168, 176, 25 },
    // DC Orange
    { 237, 132,  50,   210, 110,  40,   237, 132,  50,   220, 120,  45,   237, 132,  50, 100,   200, 100,  35, 25 },
    // Teal Green
    {  22, 152, 115,   22, 122,  92,    22, 152, 115,    22, 122,  92,    22, 152, 115, 100,    22, 122,  92, 25 },
    // DC Blue
    {  59, 118, 194,   59,  94, 155,    59, 118, 194,    59,  94, 155,    59, 118, 194, 100,    59,  94, 155, 25 },
    // DC Red
    { 207,  51,  17,   180,  40,  12,   207,  51,  17,   180,  40,  12,   207,  51,  17, 100,   180,  40,  12, 25 },
    // Steel Blue
    {  54,  90, 155,   54,  72, 124,    54,  90, 155,    54,  72, 124,    54,  90, 155, 100,    54,  72, 124, 25 },
    // Dark Gray
    {  50,  50,  50,    80,  80,  80,    50,  50,  50,    60,  60,  60,   50,  50,  50,  80,   50,  50,  50, 20 },
    // Black
    {   0,   0,   0,    30,  30,  30,     0,   0,   0,    20,  20,  20,    0,   0,   0, 100,    0,   0,   0, 30 },
};

// Linearize a single sRGB channel (0.0-1.0) to linear light
static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : ((c + 0.055f) / 1.055f) * 0.02043375f;
    // 0.02043375 = pow(0.055/1.055, 2.4)
}

// Compute WCAG contrast ratio between two sRGB colors (0.0-1.0 range).
// Returns ratio >= 1.0.  Higher = more contrast.
static float contrast_ratio(float r1, float g1, float b1, float r2, float g2, float b2)
{
    float l1 = 0.2126f * srgb_to_linear(r1) + 0.7152f * srgb_to_linear(g1) + 0.0722f * srgb_to_linear(b1);
    float l2 = 0.2126f * srgb_to_linear(r2) + 0.7152f * srgb_to_linear(g2) + 0.0722f * srgb_to_linear(b2);

    float lighter = (l1 > l2) ? l1 : l2;
    float darker  = (l1 > l2) ? l2 : l1;
    return (lighter + 0.05f) / (darker + 0.05f);
}

// Find the text preset with the best contrast against the given background.
// Uses the title color (indices 0-2) of each text palette as the representative text color.
// If current_text_preset already passes min_ratio, returns it unchanged.
int theme_best_text_preset(float bg_r, float bg_g, float bg_b, int current_text_preset, float min_ratio)
{
    // Check if current preset is acceptable
    const uint8_t *cur = text_palettes[current_text_preset];
    float cur_ratio = contrast_ratio(
        bg_r, bg_g, bg_b,
        cur[0] / 255.0f, cur[1] / 255.0f, cur[2] / 255.0f);
    if (cur_ratio >= min_ratio)
        return current_text_preset;

    // Find best
    int best = current_text_preset;
    float best_ratio = cur_ratio;
    for (int i = 0; i < NUM_TEXT_PRESETS; i++) {
        const uint8_t *tp = text_palettes[i];
        float ratio = contrast_ratio(
            bg_r, bg_g, bg_b,
            tp[0] / 255.0f, tp[1] / 255.0f, tp[2] / 255.0f);
        if (ratio > best_ratio) {
            best_ratio = ratio;
            best = i;
        }
    }
    return best;
}

void theme_apply(void)
{
    // Clamp presets
    if (theme_bg_preset < 0)            theme_bg_preset = 0;
    if (theme_bg_preset >= NUM_BG_PRESETS)  theme_bg_preset = 0;
    if (theme_accent_preset < 0)        theme_accent_preset = 0;
    if (theme_accent_preset >= NUM_ACCENT_PRESETS) theme_accent_preset = 0;
    if (theme_text_preset < 0)          theme_text_preset = 0;
    if (theme_text_preset >= NUM_TEXT_PRESETS) theme_text_preset = 0;
    if (theme_btn_preset < 0)           theme_btn_preset = 0;
    if (theme_btn_preset >= NUM_BTN_PRESETS) theme_btn_preset = 0;

    // Background (ordered darkest → brightest)
    switch (theme_bg_preset) {
        case THEME_BG_PURE_BLACK:
            theme_bg_r = 0.000f; theme_bg_g = 0.000f; theme_bg_b = 0.000f; break;
        case THEME_BG_DEEP_NAVY:
            theme_bg_r = 0.000f; theme_bg_g = 0.000f; theme_bg_b = 0.204f; break;
        case THEME_BG_ICONIC_GREEN:
            theme_bg_r = 0.016f; theme_bg_g = 0.055f; theme_bg_b = 0.016f; break;
        case THEME_BG_OBSIDIAN:
            theme_bg_r = 0.051f; theme_bg_g = 0.051f; theme_bg_b = 0.051f; break;
        case THEME_BG_ESPRESSO:
            theme_bg_r = 0.129f; theme_bg_g = 0.059f; theme_bg_b = 0.016f; break;
        case THEME_BG_DARK_BLUE:
            theme_bg_r = 0.004f; theme_bg_g = 0.086f; theme_bg_b = 0.153f; break;
        case THEME_BG_DARK_GREEN:
            theme_bg_r = 0.039f; theme_bg_g = 0.102f; theme_bg_b = 0.039f; break;
        case THEME_BG_DEEP_PURPLE:
            theme_bg_r = 0.173f; theme_bg_g = 0.055f; theme_bg_b = 0.216f; break;
        case THEME_BG_CHARCOAL:
            theme_bg_r = 0.137f; theme_bg_g = 0.137f; theme_bg_b = 0.137f; break;
        case THEME_BG_DARK_TEAL:
            theme_bg_r = 0.039f; theme_bg_g = 0.165f; theme_bg_b = 0.165f; break;
        case THEME_BG_DARK_SLATE:
            theme_bg_r = 0.102f; theme_bg_g = 0.165f; theme_bg_b = 0.227f; break;
        case THEME_BG_PS_BLUE:
            theme_bg_r = 0.000f; theme_bg_g = 0.188f; theme_bg_b = 0.529f; break;
        case THEME_BG_UMBER:
            theme_bg_r = 0.271f; theme_bg_g = 0.220f; theme_bg_b = 0.137f; break;
        case THEME_BG_PURPLE:
            theme_bg_r = 0.353f; theme_bg_g = 0.239f; theme_bg_b = 0.478f; break;
        case THEME_BG_GC_BERRY:
            theme_bg_r = 0.545f; theme_bg_g = 0.227f; theme_bg_b = 0.384f; break;
        case THEME_BG_CRIMSON:
            theme_bg_r = 0.749f; theme_bg_g = 0.129f; theme_bg_b = 0.118f; break;
        case THEME_BG_DC_RED:
            theme_bg_r = 0.812f; theme_bg_g = 0.200f; theme_bg_b = 0.067f; break;
        case THEME_BG_BURN_ORANGE:
            theme_bg_r = 0.769f; theme_bg_g = 0.286f; theme_bg_b = 0.000f; break;
        case THEME_BG_WII_FLAME:
            theme_bg_r = 0.769f; theme_bg_g = 0.290f; theme_bg_b = 0.184f; break;
        case THEME_BG_WII_WAVE:
            theme_bg_r = 0.227f; theme_bg_g = 0.486f; theme_bg_b = 0.647f; break;
        case THEME_BG_WII_BAMBOO:
            theme_bg_r = 0.420f; theme_bg_g = 0.557f; theme_bg_b = 0.306f; break;
        case THEME_BG_FOREST_GREEN:
            theme_bg_r = 0.031f; theme_bg_g = 0.627f; theme_bg_b = 0.271f; break;
        case THEME_BG_DC_ORANGE:
            theme_bg_r = 0.929f; theme_bg_g = 0.518f; theme_bg_b = 0.196f; break;
        case THEME_BG_PS1_GRAY:
            theme_bg_r = 0.675f; theme_bg_g = 0.678f; theme_bg_b = 0.714f; break;
        case THEME_BG_WARM_SILVER:
            theme_bg_r = 0.824f; theme_bg_g = 0.824f; theme_bg_b = 0.729f; break;
    }

    const uint8_t *tp = text_palettes[theme_text_preset];

    theme_title_r = tp[0]; theme_title_g = tp[1]; theme_title_b = tp[2]; theme_title_a = 255;
    theme_label_r = tp[3]; theme_label_g = tp[4]; theme_label_b = tp[5]; theme_label_a = 255;
    theme_val_text_r = tp[6]; theme_val_text_g = tp[7]; theme_val_text_b = tp[8]; theme_val_text_a = 255;
    theme_btn_r = tp[9]; theme_btn_g = tp[10]; theme_btn_b = tp[11]; theme_btn_a = 255;
    theme_divider_r = tp[12]; theme_divider_g = tp[13]; theme_divider_b = tp[14]; theme_divider_a = tp[15];
    theme_item_div_r = tp[16]; theme_item_div_g = tp[17]; theme_item_div_b = tp[18]; theme_item_div_a = tp[19];

    // Accent colors (selection highlight + selected text)
    const uint8_t *pal = accent_palettes[theme_accent_preset];
    theme_sel_bg_r = pal[0]; theme_sel_bg_g = pal[1]; theme_sel_bg_b = pal[2]; theme_sel_bg_a = 50;
    theme_sel_text_r = pal[3]; theme_sel_text_g = pal[4]; theme_sel_text_b = pal[5]; theme_sel_text_a = 255;

    // Error / OK
    theme_error_r = 255; theme_error_g = 0;   theme_error_b = 0;   theme_error_a = 255;
    theme_ok_r    = 0;   theme_ok_g = 255; theme_ok_b = 0;   theme_ok_a = 255;

    // Spinner - track follows dividers, arc follows title color
    theme_spinner_track_r = tp[12]; theme_spinner_track_g = tp[13]; theme_spinner_track_b = tp[14]; theme_spinner_track_a = tp[15];
    theme_spinner_arc_r = tp[0]; theme_spinner_arc_g = tp[1]; theme_spinner_arc_b = tp[2]; theme_spinner_arc_a = 200;
}

bool theme_load_json(const char *json_buf)
{
    cJSON *root = cJSON_Parse(json_buf);
    if (!root) return false;

    bool found = false;
    cJSON *item;

    item = cJSON_GetObjectItem(root, "theme_bg");
    if (item && item->type == cJSON_Number) {
        theme_bg_preset = item->valueint;
        found = true;
    }
    item = cJSON_GetObjectItem(root, "theme_accent");
    if (item && item->type == cJSON_Number) {
        theme_accent_preset = item->valueint;
        found = true;
    }
    item = cJSON_GetObjectItem(root, "theme_text");
    if (item && item->type == cJSON_Number) {
        theme_text_preset = item->valueint;
        found = true;
    }
    item = cJSON_GetObjectItem(root, "theme_btn");
    if (item && item->type == cJSON_Number) {
        theme_btn_preset = item->valueint;
        found = true;
    }
    item = cJSON_GetObjectItem(root, "theme_logo");
    if (item && item->type == cJSON_Number) {
        theme_logo_preset = item->valueint;
        found = true;
    }

    cJSON_Delete(root);
    return found;
}

void theme_save_json(cJSON *root)
{
    cJSON_AddNumberToObject(root, "theme_bg", theme_bg_preset);
    cJSON_AddNumberToObject(root, "theme_accent", theme_accent_preset);
    cJSON_AddNumberToObject(root, "theme_text", theme_text_preset);
    cJSON_AddNumberToObject(root, "theme_btn", theme_btn_preset);
    cJSON_AddNumberToObject(root, "theme_logo", theme_logo_preset);
}
