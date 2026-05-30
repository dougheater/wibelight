// A simple software font renderer using freetype + gx2, to replace OSScreen
#include "font.h"
#include "ui_theme.h"

#include <stdarg.h>
#include <string.h>
#include <math.h>


#include <coreinit/memory.h>
#include <whb/gfx.h>
#include <gx2/draw.h>

#include <gx2/registers.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <gx2r/draw.h>
#include <ft2build.h>
#include FT_FREETYPE_H

static void draw_freetype_bitmap(FT_Bitmap *bitmap, FT_Int x, FT_Int y, uint8_t* dst_buffer);

#include "shaders/font_texture.h"
#include "logo_bin.h"
#include "logo_sm_bin.h"

static WHBGfxShaderGroup fontShader = {};
static GX2Texture fontTexture = {};
static GX2Sampler fontSampler = {};
static FT_Library ft_lib = NULL;
static FT_Face ft_face   = NULL;

static uint8_t font_r = 255, font_g = 255, font_b = 255, font_a = 255;

// Current font selection: always 0 (system Standard)
static int current_font_id = 0;

// Batched surface access: when non-NULL, all draw functions share this lock
// instead of doing individual lock/unlock pairs.
static uint8_t* font_locked_pixels = NULL;
static int font_batch_depth = 0;

// Static buffer for Font_Print ASCII fast path (1024 chars = 2KB, covers all UI strings)
static wchar_t font_wcs_buffer[1024];

static const float font_vertex_buffer[] __attribute__ ((aligned (GX2_VERTEX_BUFFER_ALIGNMENT))) = {
   -1.0f, -1.0f,  0.0f, 1.0f,
    1.0f, -1.0f,  1.0f, 1.0f,
    1.0f,  1.0f,  1.0f, 0.0f,
   -1.0f,  1.0f,  0.0f, 0.0f,
};

// Font selection is fixed to system Standard font.
// Custom font support was removed (FreeType too slow on Broadway CPU).
int Font_CurrentId(void)
{
  return current_font_id;
}

int Font_UserCount(void)
{
  return 0; // no user fonts
}

const char* Font_UserName(int index)
{
  (void)index;
  return NULL;
}

void Font_Init(void)
{
  // Load the system font
  void *font = NULL;
  uint32_t size = 0;
  OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &font, &size);

    if (font && size) {
        FT_Init_FreeType(&ft_lib);
        FT_New_Memory_Face(ft_lib, (FT_Byte *) font, size, 0, &ft_face);
    }

    // Load the shader
    WHBGfxLoadGFDShaderGroup(&fontShader, 0, font_texture_gsh);

    // Initialize the shader attributes
    WHBGfxInitShaderAttribute(&fontShader, "in_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&fontShader, "in_texCoord", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);

    // Initialize the fetch shader
    WHBGfxInitFetchShader(&fontShader);

    // Create a texture to draw the text to
    fontTexture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    fontTexture.surface.use = GX2_SURFACE_USE_TEXTURE;
    fontTexture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    fontTexture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    fontTexture.surface.depth = 1;
    fontTexture.surface.width = FONT_BUFFER_WIDTH;
    fontTexture.surface.height = FONT_BUFFER_HEIGHT;
    fontTexture.surface.mipLevels = 1;
    fontTexture.viewNumSlices = 1;
    fontTexture.viewNumMips = 1;
    fontTexture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);

    GX2RCreateSurface(&fontTexture.surface, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
    GX2InitTextureRegs(&fontTexture);

    // Create a sampler
    GX2InitSampler(&fontSampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_MIP_FILTER_MODE_POINT);

    // Clear the texture buffer
    Font_Clear();
}

void Font_Deinit(void)
{
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_lib);

    WHBGfxFreeShaderGroup(&fontShader);
    GX2RDestroySurfaceEx(&fontTexture.surface, GX2R_RESOURCE_BIND_NONE);
}

void Font_Draw(void)
{
    GX2SetFetchShader(&fontShader.fetchShader);
    GX2SetVertexShader(fontShader.vertexShader);
    GX2SetPixelShader(fontShader.pixelShader);

    GX2SetPixelTexture(&fontTexture, 0);
    GX2SetPixelSampler(&fontSampler, 0);

    GX2SetAttribBuffer(0, sizeof(font_vertex_buffer), 4 * sizeof(float), font_vertex_buffer);
    GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

void Font_Draw_TVDRC(void)
{
    Font_Draw_TVDRC_Color(theme_bg_r, theme_bg_g, theme_bg_b);
}

void Font_Draw_TVDRC_Color(float r, float g, float b)
{
    WHBGfxBeginRender();

    WHBGfxBeginRenderTV();
    WHBGfxClearColor(r, g, b, 1.0f);
    Font_Draw();
    WHBGfxFinishRenderTV();

    WHBGfxBeginRenderDRC();
    WHBGfxClearColor(r, g, b, 1.0f);
    Font_Draw();
    WHBGfxFinishRenderDRC();

    WHBGfxFinishRender();
}

void Font_BeginDraw(void)
{
    font_batch_depth++;
    if (font_batch_depth == 1) {
        font_locked_pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

void Font_EndDraw(void)
{
    if (font_batch_depth > 0) {
        font_batch_depth--;
    }
    if (font_batch_depth == 0) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
        font_locked_pixels = NULL;
    }
}

void Font_Clear(void)
{
    uint8_t* pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    memset(pixels, 0, fontTexture.surface.imageSize);
    GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
}

void Font_DrawCircle(uint32_t cx, uint32_t cy, uint32_t radius)
{
    if (radius == 0) return;
    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    // Scanline-filled circle with edge anti-aliasing
    // For each row, compute the exact x extent and use fractional coverage for alpha
    uint32_t r = radius;
    uint32_t r2 = r * r;
    uint32_t pitch = fontTexture.surface.pitch;

    for (int32_t dy = -(int32_t)r; dy <= (int32_t)r; dy++) {
        // Exact half-width at this row: sqrt(r^2 - dy^2)
        float hw = sqrtf((float)r2 - (float)dy * dy);
        int32_t x_left = (int32_t)cx - (int32_t)hw;
        int32_t x_right = (int32_t)cx + (int32_t)hw;
        int32_t py = (int32_t)cy + dy;

        if (py < 0 || (uint32_t)py >= FONT_BUFFER_HEIGHT)
            continue;

        // Clamp to buffer bounds
        if ((uint32_t)x_left >= FONT_BUFFER_WIDTH) continue;
        if (x_right < 0) continue;
        int32_t x_start = x_left < 0 ? 0 : x_left;
        int32_t x_end = (uint32_t)x_right >= FONT_BUFFER_WIDTH ? FONT_BUFFER_WIDTH - 1 : x_right;

        // Edge coverage for anti-aliasing
        float frac_left = hw - (float)((int32_t)hw);
        float frac_right = hw - (float)((int32_t)hw);

        for (int32_t px = x_start; px <= x_end; px++) {
            if ((uint32_t)px >= FONT_BUFFER_WIDTH) break;
            uint32_t offset = ((uint32_t)px + (uint32_t)py * pitch) * 4;

            // Determine alpha: full inside, fractional at edges
            float alpha = 1.0f;
            if (px == x_left) {
                // Left edge: fraction of pixel inside circle
                alpha = frac_left;
            } else if (px == x_right) {
                // Right edge
                alpha = frac_right;
            }
            // For the very first/last row, both edges are the same pixel
            if (dy == -(int32_t)r || dy == (int32_t)r) {
                alpha = frac_left; // single pixel row
            }

            // Clamp
            if (alpha > 1.0f) alpha = 1.0f;
            if (alpha < 0.0f) alpha = 0.0f;

            uint8_t dst_a = pixels[offset + 3];
            float src_alpha = alpha;
            float inv_src = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
            pixels[offset    ] = (uint8_t)((font_r * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 1] = (uint8_t)((font_g * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 2] = (uint8_t)((font_b * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
    }

    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

void Font_DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
    uint32_t x_end = x + width;
    uint32_t y_end = y + height;
    if (x_end > FONT_BUFFER_WIDTH) x_end = FONT_BUFFER_WIDTH;
    if (y_end > FONT_BUFFER_HEIGHT) y_end = FONT_BUFFER_HEIGHT;
    float src_alpha = font_a / 255.0f;
    for (uint32_t py = y; py < y_end; py++) {
        uint32_t row_offset = py * fontTexture.surface.pitch * 4;
        for (uint32_t px = x; px < x_end; px++) {
            uint32_t offset = row_offset + px * 4;
            uint8_t dst_a = pixels[offset + 3];
            float inv_src = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
            pixels[offset    ] = (uint8_t)((font_r * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 1] = (uint8_t)((font_g * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 2] = (uint8_t)((font_b * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
    }
    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

void Font_DrawHLine(uint32_t x1, uint32_t x2, uint32_t y)
{
    if (x1 > x2) { uint32_t tmp = x1; x1 = x2; x2 = tmp; }
    if (y >= FONT_BUFFER_HEIGHT) return;
    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
    float src_alpha = font_a / 255.0f;
    uint32_t row_offset = y * fontTexture.surface.pitch * 4;
    for (uint32_t px = x1; px <= x2 && px < FONT_BUFFER_WIDTH; px++) {
        uint32_t offset = row_offset + px * 4;
        uint8_t dst_a = pixels[offset + 3];
        float inv_src = 1.0f - src_alpha;
        float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
        pixels[offset    ] = (uint8_t)((font_r * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
        pixels[offset + 1] = (uint8_t)((font_g * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
        pixels[offset + 2] = (uint8_t)((font_b * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
        pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
    }
    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

int Font_DrawLogo(uint32_t x, uint32_t y)
{
    uint32_t w = logo_width;
    uint32_t h = logo_height;
    if (w == 0 || h == 0) return 0;

    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    for (uint32_t sy = 0; sy < h; sy++) {
        uint32_t dy = y + sy;
        if (dy >= FONT_BUFFER_HEIGHT) break;
        for (uint32_t sx = 0; sx < w; sx++) {
            uint32_t dx = x + sx;
            if (dx >= FONT_BUFFER_WIDTH) break;
            const uint8_t* src = &logo_rgba[(sy * w + sx) * 4];
            float src_alpha = src[3] / 255.0f;
            if (src_alpha < 0.01f) continue;
            uint32_t offset = (dx + dy * fontTexture.surface.pitch) * 4;
            uint8_t dst_a = pixels[offset + 3];
            float inv_src = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
            pixels[offset    ] = (uint8_t)((src[0] * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 1] = (uint8_t)((src[1] * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 2] = (uint8_t)((src[2] * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
    }

    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    return (int)w;
}

int Font_DrawLogoSm(uint32_t x, uint32_t y)
{
    uint32_t w = logo_sm_width;
    uint32_t h = logo_sm_height;
    if (w == 0 || h == 0) return 0;

    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    for (uint32_t sy = 0; sy < h; sy++) {
        uint32_t dy = y + sy;
        if (dy >= FONT_BUFFER_HEIGHT) break;
        for (uint32_t sx = 0; sx < w; sx++) {
            uint32_t dx = x + sx;
            if (dx >= FONT_BUFFER_WIDTH) break;
            const uint8_t* src = &logo_sm_rgba[(sy * w + sx) * 4];
            float src_alpha = src[3] / 255.0f;
            if (src_alpha < 0.01f) continue;
            uint32_t offset = (dx + dy * fontTexture.surface.pitch) * 4;
            uint8_t dst_a = pixels[offset + 3];
            float inv_src = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
            pixels[offset    ] = (uint8_t)((src[0] * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 1] = (uint8_t)((src[1] * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 2] = (uint8_t)((src[2] * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
    }

    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    return (int)w;
}

void Font_DrawSpinner(uint32_t cx, uint32_t cy, uint32_t radius, int startAngle, int sweep, int lineWidth)
{
    if (radius == 0 || lineWidth == 0) return;
    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    int inner = (int)radius - lineWidth;
    if (inner < 0) inner = 0;

    // Use incremental rotation to avoid cosf/sinf in the hot path
    float start_rad = (float)startAngle * 3.14159265358979323846f / 180.0f;
    float cos_a = cosf(start_rad);
    float sin_a = sinf(start_rad);
    float dtheta = 3.14159265358979323846f / 180.0f;
    float cos_dt = cosf(dtheta);
    float sin_dt = sinf(dtheta);

    for (int i = 0; i < sweep; i++) {
        // Draw line segment from inner to outer radius
        for (int r = inner; r <= (int)radius; r++) {
            int32_t px = (int32_t)cx + (int)(cos_a * r);
            int32_t py = (int32_t)cy - (int)(sin_a * r); // Y is flipped in screen coords
            if (px < 0 || py < 0 || (uint32_t)px >= FONT_BUFFER_WIDTH || (uint32_t)py >= FONT_BUFFER_HEIGHT)
                continue;
            uint32_t offset = ((uint32_t)px + (uint32_t)py * fontTexture.surface.pitch) * 4;
            uint8_t dst_a = pixels[offset + 3];
            float src_alpha = font_a / 255.0f;
            float inv_src   = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;
            pixels[offset    ] = (uint8_t)((font_r * src_alpha + pixels[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 1] = (uint8_t)((font_g * src_alpha + pixels[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 2] = (uint8_t)((font_b * src_alpha + pixels[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            pixels[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
        // Incremental rotation: rotate (cos_a, sin_a) by dtheta
        float new_cos = cos_a * cos_dt - sin_a * sin_dt;
        float new_sin = cos_a * sin_dt + sin_a * cos_dt;
        cos_a = new_cos;
        sin_a = new_sin;
    }

    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

// Wii U GamePad button colors (A=green, B=red, X=blue, Y=yellow, +/-=gray)
#define BTN_A_R 85
#define BTN_A_G 182
#define BTN_A_B 69
#define BTN_B_R 231
#define BTN_B_G 76
#define BTN_B_B 60
#define BTN_X_R 52
#define BTN_X_G 152
#define BTN_X_B 219
#define BTN_Y_R 241
#define BTN_Y_G 196
#define BTN_Y_B 15
#define BTN_PLUS_R 153
#define BTN_PLUS_G 153
#define BTN_PLUS_B 153
#define BTN_MINUS_R 153
#define BTN_MINUS_G 153
#define BTN_MINUS_B 153

int Font_DrawButtonIcon(uint32_t x, uint32_t y, char button, const char* label)
{
    int radius = 20;
    int cx = (int)x + radius;
    int cy = (int)y + radius;

    // Save caller's color so we can restore it for the label text
    uint8_t caller_r = font_r, caller_g = font_g, caller_b = font_b, caller_a = font_a;

    // Determine button circle color and letter color based on theme
    uint8_t circle_r, circle_g, circle_b, letter_r, letter_g, letter_b;
    switch (theme_btn_preset) {
        case THEME_BTN_WHITE:
            circle_r = 230; circle_g = 230; circle_b = 230;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_DARK:
            circle_r = 60;  circle_g = 60;  circle_b = 60;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_WIIBE:
            circle_r = 85;  circle_g = 91;  circle_b = 99;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_ICONIC_GREEN:
            circle_r = 0;   circle_g = 170; circle_b = 51;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_AMBER:
            circle_r = 255; circle_g = 179; circle_b = 0;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_GOURYELLA:
            circle_r = 255; circle_g = 140; circle_b = 66;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_N7:
            circle_r = 255; circle_g = 106; circle_b = 0;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_RED_ROOM:
            circle_r = 120; circle_g = 0;   circle_b = 0;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_DC_RED:
            circle_r = 207; circle_g = 51;  circle_b = 17;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_DC_BLUE:
            circle_r = 59;  circle_g = 118; circle_b = 194;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_DC_ORANGE:
            circle_r = 237; circle_g = 132; circle_b = 50;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_TEAL_GREEN:
            circle_r = 22;  circle_g = 152; circle_b = 115;
            letter_r = 0;   letter_g = 0;   letter_b = 0;
            break;
        case THEME_BTN_STEEL_BLUE:
            circle_r = 54;  circle_g = 90;  circle_b = 155;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_PS1_GRAY:
            circle_r = 172; circle_g = 173; circle_b = 182;
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
        case THEME_BTN_COLORED:
        default:
            switch (button) {
                case 'A': circle_r = BTN_A_R; circle_g = BTN_A_G; circle_b = BTN_A_B; break;
                case 'B': circle_r = BTN_B_R; circle_g = BTN_B_G; circle_b = BTN_B_B; break;
                case 'X': circle_r = BTN_X_R; circle_g = BTN_X_G; circle_b = BTN_X_B; break;
                case 'Y': circle_r = BTN_Y_R; circle_g = BTN_Y_G; circle_b = BTN_Y_B; break;
                case '+': circle_r = BTN_PLUS_R; circle_g = BTN_PLUS_G; circle_b = BTN_PLUS_B; break;
                case '-': circle_r = BTN_MINUS_R; circle_g = BTN_MINUS_G; circle_b = BTN_MINUS_B; break;
                default:  circle_r = 180;    circle_g = 180;    circle_b = 180;     break;
            }
            letter_r = 255; letter_g = 255; letter_b = 255;
            break;
    }

    // Draw circle
    Font_SetColor(circle_r, circle_g, circle_b, 255);
    Font_DrawCircle(cx, cy, radius);

    // Draw letter centered in circle using glyph metrics
    Font_SetColor(letter_r, letter_g, letter_b, 255);
    Font_SetSize(30);
    FT_GlyphSlot slot = ft_face->glyph;
    FT_Load_Glyph(ft_face, FT_Get_Char_Index(ft_face, button), FT_LOAD_DEFAULT);
    FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);

    // Scan actual bitmap to find tight bounding box of non-transparent pixels
    FT_Bitmap *bm = &slot->bitmap;
    int min_p = bm->width, max_p = -1, min_q = bm->rows, max_q = -1;
    for (int q = 0; q < bm->rows; q++) {
        for (int p = 0; p < bm->width; p++) {
            if (bm->buffer[q * bm->pitch + p] > 10) {
                if (p < min_p) min_p = p;
                if (p > max_p) max_p = p;
                if (q < min_q) min_q = q;
                if (q > max_q) max_q = q;
            }
        }
    }
    if (max_p < 0) max_p = min_p;
    if (max_q < 0) max_q = min_q;

    // Tight bounding box in bitmap coords
    int bb_w = max_p - min_p + 1;
    int bb_h = max_q - min_q + 1;
    int bb_center_p = min_p + bb_w / 2;
    int bb_center_q = min_q + bb_h / 2;

    // draw_freetype_bitmap draws pixel (p,q) at (pen_x + p, pen_y + q)
    // So to center the tight bounding box at (cx, cy):
    int pen_x = cx - bb_center_p;
    int pen_y = cy - bb_center_q;

    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
    draw_freetype_bitmap(&slot->bitmap, pen_x, pen_y, pixels);
    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    // Draw label text to the right of the icon, vertically centered with circle
    int total_width = radius * 2 + 12;
    if (label && label[0]) {
        Font_SetColor(caller_r, caller_g, caller_b, caller_a);
        Font_SetSize(32);
        // Compute baseline so text visual center aligns with circle center cy
        // In screen coords: text top = baseline - ascender, text bottom = baseline + descender
        // Visual center = baseline - (ascender - descender) / 2
        // Set center = cy: baseline = cy + (ascender - descender) / 2
        FT_Size_Metrics *sm = &ft_face->size->metrics;
        int ascender = sm->ascender >> 6;    // positive
        int descender = sm->descender >> 6;  // negative
        int baseline_offset = (ascender + descender) / 2;
        Font_Print(x + total_width, cy + baseline_offset, label);
        total_width += Font_GetTextWidth(label);
    }

    return total_width;
}

static void draw_freetype_bitmap(FT_Bitmap *bitmap, FT_Int x, FT_Int y, uint8_t* dst_buffer) {
    FT_Int i, j, p, q;
    FT_Int x_max = x + bitmap->width;
    FT_Int y_max = y + bitmap->rows;

    for (i = x, p = 0; i < x_max; i++, p++) {
        for (j = y, q = 0; j < y_max; j++, q++) {
            if (i < 0 || j < 0 || i >= FONT_BUFFER_WIDTH || j >= FONT_BUFFER_HEIGHT) {
                continue;
            }

            float opacity = bitmap->buffer[q * bitmap->pitch + p] / 255.0f;
            if (opacity < 0.01f) continue; // Skip fully transparent pixels

            uint32_t offset = (i + j * fontTexture.surface.pitch) * 4;
            uint8_t dst_a = dst_buffer[offset + 3];

            // Alpha compositing: src over dst
            float src_alpha = font_a / 255.0f * opacity;
            float inv_src = 1.0f - src_alpha;
            float out_alpha = src_alpha + dst_a / 255.0f * inv_src;

            dst_buffer[offset    ] = (uint8_t)((font_r * src_alpha + dst_buffer[offset    ] * dst_a / 255.0f * inv_src) / out_alpha);
            dst_buffer[offset + 1] = (uint8_t)((font_g * src_alpha + dst_buffer[offset + 1] * dst_a / 255.0f * inv_src) / out_alpha);
            dst_buffer[offset + 2] = (uint8_t)((font_b * src_alpha + dst_buffer[offset + 2] * dst_a / 255.0f * inv_src) / out_alpha);
            dst_buffer[offset + 3] = (uint8_t)(out_alpha * 255.0f);
        }
    }
}

void Font_Printw(uint32_t x, uint32_t y, const wchar_t* string)
{
    FT_GlyphSlot slot = ft_face->glyph;
    FT_Vector pen = {(int) x, (int) y};
    uint8_t* pixels = font_locked_pixels;
    if (!pixels) {
        pixels = (uint8_t*) GX2RLockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }

    for (; *string; string++) {
        uint32_t charcode = *string;

        if (charcode == '\n') {
            pen.y += ft_face->size->metrics.height >> 6;
            pen.x = x;
            continue;
        }

        FT_Load_Glyph(ft_face, FT_Get_Char_Index(ft_face, charcode), FT_LOAD_DEFAULT);
        FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);

        draw_freetype_bitmap(&slot->bitmap, pen.x + slot->bitmap_left, pen.y - slot->bitmap_top, pixels);
        pen.x += slot->advance.x >> 6;
    }

    if (!font_locked_pixels) {
        GX2RUnlockSurfaceEx(&fontTexture.surface, 0, GX2R_RESOURCE_BIND_NONE);
    }
}

void Font_Print(uint32_t x, uint32_t y, const char* string)
{
    // Fast path: pure ASCII (covers 99% of UI text - no heap, no mbstowcs)
    size_t i = 0;
    for (; string[i] && i < 1023; i++) {
        if ((unsigned char)string[i] > 127) {
            goto slow_path;
        }
    }
    // All ASCII - direct copy, one wchar per byte
    for (size_t j = 0; j < i; j++) {
        font_wcs_buffer[j] = (wchar_t)(unsigned char)string[j];
    }
    font_wcs_buffer[i] = 0;
    Font_Printw(x, y, font_wcs_buffer);
    return;

slow_path:
    // Fallback: full multibyte conversion (for UTF-8 or long strings)
    size_t num = mbstowcs(font_wcs_buffer, string, 1023);
    if (num > 0) {
        font_wcs_buffer[num] = 0;
    } else {
        // mbstowcs failed - byte-by-byte fallback
        size_t k = 0;
        for (; string[k] && k < 1023; k++) {
            font_wcs_buffer[k] = (wchar_t)(unsigned char)string[k];
        }
        font_wcs_buffer[k] = 0;
    }
    Font_Printw(x, y, font_wcs_buffer);
}

void Font_Printf(uint32_t x, uint32_t y, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);

    char* tmp = NULL;
    if((vasprintf(&tmp, msg, args) >= 0) && tmp) {
        Font_Print(x, y, tmp);
    }

    va_end(args);
    free(tmp);
}

// Print text clipped to a maximum width. Truncates with "..." if needed.
void Font_PrintClipped(uint32_t x, uint32_t y, uint32_t max_width, const char* string)
{
    static char utf8_buf[1024];
    strncpy(utf8_buf, string, 1023);
    utf8_buf[1023] = 0;

    // If it already fits, print as-is
    if (Font_GetTextWidth(utf8_buf) <= max_width) {
        Font_Print(x, y, utf8_buf);
        return;
    }

    // Binary search for the longest UTF-8 byte prefix that fits.
    // We must not split multi-byte UTF-8 sequences, so after each
    // truncation we skip to the next character boundary.
    uint32_t lo = 0, hi = (uint32_t)strlen(utf8_buf);
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        utf8_buf[mid] = 0;
        // Skip partial UTF-8 trailing bytes
        while (mid > 0 && (utf8_buf[mid - 1] & 0xC0) == 0x80) {
            utf8_buf[--mid] = 0;
        }
        if (Font_GetTextWidth(utf8_buf) <= max_width)
            lo = mid;
        else
            hi = mid - 1;
    }

    // Add "..." if truncated
    if (lo < (uint32_t)strlen(string)) {
        uint32_t ellipsis_w = Font_GetTextWidth("...");
        uint32_t current_len = lo;
        if (ellipsis_w < max_width && Font_GetTextWidth(utf8_buf) + ellipsis_w > max_width) {
            // Back up characters until there's room for "..."
            while (current_len > 0 && Font_GetTextWidth(utf8_buf) + ellipsis_w > max_width) {
                utf8_buf[--current_len] = 0;
                // Skip partial UTF-8 trailing bytes
                while (current_len > 0 && (utf8_buf[current_len - 1] & 0xC0) == 0x80)
                    utf8_buf[--current_len] = 0;
            }
        }
        strcat(utf8_buf, "...");
    }

    Font_Print(x, y, utf8_buf);
}


void Font_SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    font_r = r;
    font_g = g;
    font_b = b;
    font_a = a;
}

void Font_SetSize(uint32_t size)
{
    FT_Set_Pixel_Sizes(ft_face, 0, size);
}

int Font_GetAscender(void)
{
    return ft_face->size->metrics.ascender >> 6; // positive
}

int Font_GetDescender(void)
{
    return ft_face->size->metrics.descender >> 6; // negative
}

int Font_GetCharAdvance(wchar_t ch)
{
    if (FT_Load_Char(ft_face, ch, FT_LOAD_DEFAULT) != 0)
        return 0;
    return ft_face->glyph->advance.x >> 6;
}

int Font_GetTextWidth(const char* string)
{
    int total = 0;
    for (; *string; string++) {
        uint32_t charcode = (uint8_t)*string;
        if (FT_Load_Char(ft_face, charcode, FT_LOAD_DEFAULT) != 0)
            continue;
        total += ft_face->glyph->advance.x >> 6;
    }
    return total;
}
