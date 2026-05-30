#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>

// Legacy compat — use theme_bg_* in new code
#define GX2_CLEAR_COLOR 0.004f, 0.086f, 0.153f, 1.0f

#define FONT_BUFFER_WIDTH 1920
#define FONT_BUFFER_HEIGHT 1080

void Font_Init(void);

void Font_Deinit(void);

void Font_Draw(void);

// Draw TV+DRC with theme background color
void Font_Draw_TVDRC(void);

// Draw TV+DRC with explicit background color
void Font_Draw_TVDRC_Color(float r, float g, float b);

void Font_Clear(void);

// Batched rendering: lock the font surface once, draw multiple primitives,
// then unlock. Reduces GPU sync from N pairs to 1 pair per frame.
// Call Font_BeginDraw() before a series of draw calls, then Font_EndDraw().
// Safe to nest; only the outermost pair actually locks/unlocks.
// Existing code that doesn't use batching still works (auto lock/unlock per call).
void Font_BeginDraw(void);
void Font_EndDraw(void);

void Font_Printw(uint32_t x, uint32_t y, const wchar_t* string);

void Font_Print(uint32_t x, uint32_t y, const char* string);

void Font_Printf(uint32_t x, uint32_t y, const char* msg, ...);
void Font_PrintClipped(uint32_t x, uint32_t y, uint32_t max_width, const char* string);

void Font_SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void Font_SetSize(uint32_t size);
int  Font_GetCharAdvance(wchar_t ch);
int  Font_GetTextWidth(const char* string);

// Font info — fixed to system Standard font.
int     Font_CurrentId(void);
int     Font_UserCount(void);

// Return ascender (positive, pixels above baseline) and descender (negative,
// pixels below baseline) for the currently set font size.
// Use to compute rect top = baseline - ascender + padding
//            rect height = ascender - descender + 2*padding
int Font_GetAscender(void);
int Font_GetDescender(void);

// Draw a filled circle (uses current Font_SetColor for RGBA)
void Font_DrawCircle(uint32_t cx, uint32_t cy, uint32_t radius);

// Draw a spinner arc: rotating arc with given start angle (degrees) and sweep (degrees)
// Uses current Font_SetColor for RGBA
void Font_DrawSpinner(uint32_t cx, uint32_t cy, uint32_t radius, int startAngle, int sweep, int lineWidth);

// Draw a Wii U-style button icon (A=green, B=red, X=blue, Y=yellow)
// Draws a colored circle with the letter inside, then optional label text.
// Returns the total width consumed so you can chain icons horizontally.
int Font_DrawButtonIcon(uint32_t x, uint32_t y, char button, const char* label);

// Draw a filled rectangle (uses current Font_SetColor for RGBA)
void Font_DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Draw a horizontal line at y from x1 to x2 (uses current Font_SetColor for RGBA)
void Font_DrawHLine(uint32_t x1, uint32_t x2, uint32_t y);

// Draw the embedded logo texture at (x, y) top-left.
// Returns the logo width so callers can offset adjacent text.
int  Font_DrawLogo(uint32_t x, uint32_t y);

// Draw the small embedded logo texture at (x, y) top-left.
// Returns the logo width so callers can offset adjacent text.
int  Font_DrawLogoSm(uint32_t x, uint32_t y);
