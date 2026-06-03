#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

typedef struct {
    uint32_t bg;
    uint32_t input_bg;
    uint32_t sel_bg;
    uint32_t fg;
    uint32_t prompt;
    uint32_t sel_fg;
    uint32_t dim;
    uint32_t border;
    
    /* Additional for bar/lockscreen */
    uint32_t active;
    uint32_t inactive;
    uint32_t indicator;

    /* Status colors */
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    uint32_t accent;

    /* Hex strings for Xft compatibility */
    char bg_hex[10];
    char input_bg_hex[10];
    char sel_bg_hex[10];
    char fg_hex[10];
    char prompt_hex[10];
    char sel_fg_hex[10];
    char dim_hex[10];
    char border_hex[10];
    char active_hex[10];
    char inactive_hex[10];
    char indicator_hex[10];
    char success_hex[10];
    char warning_hex[10];
    char error_hex[10];
    char accent_hex[10];
} Theme;

Theme load_theme(void);
void theme_color_to_hex(uint32_t color, char *out);
uint32_t theme_blend(uint32_t c1, uint32_t c2, float w1);
float theme_get_luminance(uint32_t color);
uint32_t theme_get_accent(uint32_t primary);

/* Color space conversions */
typedef struct { float l, a, b; } Lab;
typedef struct { float r, g, b; } RGB;
Lab rgb_to_lab(uint32_t color);
uint32_t lab_to_rgb(Lab lab);

/* Intelligent extraction */
uint32_t theme_extract_from_wallpaper(const char *path);
void theme_set_wallpaper(Display *dpy, Window root, const char *path);

/* Global theme instance for the process */
extern Theme current_theme;
void init_theme(void);

#endif // THEME_H
