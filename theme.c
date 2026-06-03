#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#include <Imlib2.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

Theme current_theme;
static int theme_loaded = 0;

void theme_set_wallpaper(Display *dpy, Window root, const char *path) {
    if (!path || access(path, F_OK) == -1) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    Imlib_Image img = imlib_load_image(path);
    if (!img) return;

    imlib_context_set_image(img);
    int iw = imlib_image_get_width();
    int ih = imlib_image_get_height();

    Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, width, height);
    imlib_free_image();
    imlib_context_set_image(scaled);

    Pixmap pixmap = XCreatePixmap(dpy, root, width, height, DefaultDepth(dpy, screen));
    imlib_context_set_display(dpy);
    imlib_context_set_visual(DefaultVisual(dpy, screen));
    imlib_context_set_colormap(DefaultColormap(dpy, screen));
    imlib_context_set_drawable(pixmap);
    imlib_render_image_on_drawable(0, 0);

    XSetWindowBackgroundPixmap(dpy, root, pixmap);
    XClearWindow(dpy, root);

    /* Set properties so other apps can find it */
    Atom prop_root_pmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    Atom prop_eroot_pmap = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);
    XChangeProperty(dpy, root, prop_root_pmap, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pixmap, 1);
    XChangeProperty(dpy, root, prop_eroot_pmap, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pixmap, 1);

    imlib_free_image();
    XFlush(dpy);
}

void theme_color_to_hex(uint32_t color, char *out) {
    snprintf(out, 10, "#%06x", color & 0xffffff);
}

Lab rgb_to_lab(uint32_t color) {
    float r = ((color >> 16) & 0xff) / 255.0f;
    float g = ((color >> 8) & 0xff) / 255.0f;
    float b = (color & 0xff) / 255.0f;

    r = (r > 0.04045f) ? powf((r + 0.055f) / 1.055f, 2.4f) : (r / 12.92f);
    g = (g > 0.04045f) ? powf((g + 0.055f) / 1.055f, 2.4f) : (g / 12.92f);
    b = (b > 0.04045f) ? powf((b + 0.055f) / 1.055f, 2.4f) : (b / 12.92f);

    float x = (r * 0.4124f + g * 0.3576f + b * 0.1805f) / 0.95047f;
    float y = (r * 0.2126f + g * 0.7152f + b * 0.0722f) / 1.00000f;
    float z = (r * 0.0193f + g * 0.1192f + b * 0.9505f) / 1.08883f;

    x = (x > 0.008856f) ? powf(x, 1.0f/3.0f) : (7.787f * x + 16.0f/116.0f);
    y = (y > 0.008856f) ? powf(y, 1.0f/3.0f) : (7.787f * y + 16.0f/116.0f);
    z = (z > 0.008856f) ? powf(z, 1.0f/3.0f) : (7.787f * z + 16.0f/116.0f);

    return (Lab){(116.0f * y) - 16.0f, 500.0f * (x - y), 200.0f * (y - z)};
}

uint32_t lab_to_rgb(Lab lab) {
    float y = (lab.l + 16.0f) / 116.0f;
    float x = lab.a / 500.0f + y;
    float z = y - lab.b / 200.0f;

    x = 0.95047f * ((x * x * x > 0.008856f) ? x * x * x : (x - 16.0f/116.0f) / 7.787f);
    y = 1.00000f * ((y * y * y > 0.008856f) ? y * y * y : (y - 16.0f/116.0f) / 7.787f);
    z = 1.08883f * ((z * z * z > 0.008856f) ? z * z * z : (z - 16.0f/116.0f) / 7.787f);

    float r = x *  3.2406f + y * -1.5372f + z * -0.4986f;
    float g = x * -0.9689f + y *  1.8758f + z *  0.0415f;
    float b = x *  0.0557f + y * -0.2040f + z *  1.0570f;

    r = (r > 0.0031308f) ? (1.055f * powf(r, 1.0f/2.4f) - 0.055f) : (12.92f * r);
    g = (g > 0.0031308f) ? (1.055f * powf(g, 1.0f/2.4f) - 0.055f) : (12.92f * g);
    b = (b > 0.0031308f) ? (1.055f * powf(b, 1.0f/2.4f) - 0.055f) : (12.92f * b);

    int ir = fmax(0, fmin(255, (int)(r * 255.0f)));
    int ig = fmax(0, fmin(255, (int)(g * 255.0f)));
    int ib = fmax(0, fmin(255, (int)(b * 255.0f)));

    return (ir << 16) | (ig << 8) | ib;
}

uint32_t theme_blend(uint32_t c1, uint32_t c2, float w1) {
    Lab l1 = rgb_to_lab(c1);
    Lab l2 = rgb_to_lab(c2);
    Lab res = {
        l1.l * w1 + l2.l * (1.0f - w1),
        l1.a * w1 + l2.a * (1.0f - w1),
        l1.b * w1 + l2.b * (1.0f - w1)
    };
    return lab_to_rgb(res);
}

float theme_get_luminance(uint32_t color) {
    Lab lab = rgb_to_lab(color);
    return lab.l / 100.0f;
}

uint32_t theme_get_accent(uint32_t primary) {
    Lab lab = rgb_to_lab(primary);
    lab.l = fmin(lab.l + 20.0f, 90.0f);
    return lab_to_rgb(lab);
}

uint32_t theme_extract_from_wallpaper(const char *path) {
    if (!path || access(path, F_OK) == -1) return 0xff0055;

    Imlib_Image img = imlib_load_image(path);
    if (!img) return 0xff0055;

    imlib_context_set_image(img);
    int w = imlib_image_get_width();
    int h = imlib_image_get_height();

    /* Scale down to 64x64 to find dominant color efficiently */
    Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, w, h, 64, 64);
    imlib_free_image();
    imlib_context_set_image(scaled);

    uint32_t *data = imlib_image_get_data_for_reading_only();
    unsigned long r = 0, g = 0, b = 0;
    int count = 64 * 64;

    for (int i = 0; i < count; i++) {
        r += (data[i] >> 16) & 0xff;
        g += (data[i] >> 8) & 0xff;
        b += data[i] & 0xff;
    }

    imlib_free_image();
    
    uint32_t avg = ((r / count) << 16) | ((g / count) << 8) | (b / count);
    
    /* Boost saturation if too gray */
    Lab lab = rgb_to_lab(avg);
    float chroma = sqrtf(lab.a * lab.a + lab.b * lab.b);
    if (chroma < 10.0f) {
        lab.a *= 2.0f;
        lab.b *= 2.0f;
        avg = lab_to_rgb(lab);
    }

    return avg;
}

Theme load_theme(void) {
    if (theme_loaded) return current_theme;

    Theme t;
    char primary_hex[16] = "";
    char wallpaper_path[512] = "";
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/.config/Nebula/nebula.config", pw->pw_dir);
        FILE *f = fopen(path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char *delimiter = strpbrk(line, "=:");
                if (!delimiter) continue;
                *delimiter = '\0';
                char *key = line;
                char *val = delimiter + 1;
                
                while (*key && (unsigned char)*key <= 32) key++;
                char *end = key + strlen(key) - 1;
                while (end > key && (unsigned char)*end <= 32) *end-- = '\0';
                while (*val && (unsigned char)*val <= 32) val++;
                end = val + strlen(val) - 1;
                while (end > val && (unsigned char)*end <= 32) *end-- = '\0';

                if (strcmp(key, "primary_color") == 0) {
                    strncpy(primary_hex, val, sizeof(primary_hex) - 1);
                } else if (strcmp(key, "wallpaper") == 0) {
                    strncpy(wallpaper_path, val, sizeof(wallpaper_path) - 1);
                }
            }
            fclose(f);
        }
    }
    
    uint32_t primary;
    if (primary_hex[0] != '\0') {
        if (primary_hex[0] == '#') primary = (uint32_t)strtoul(primary_hex + 1, NULL, 16);
        else primary = (uint32_t)strtoul(primary_hex, NULL, 16);
    } else if (wallpaper_path[0] != '\0') {
        primary = theme_extract_from_wallpaper(wallpaper_path);
    } else {
        primary = 0xff0055;
    }
    
    float lum = theme_get_luminance(primary);
    int is_light = lum > 0.9f;
    
    uint32_t base_bg = is_light ? 0xf2f2f7 : 0x08080a;
    uint32_t base_fg = is_light ? 0x1c1c1e : 0xe5e5e7;
    
    /* Intelligent Generation */
    if (is_light) {
        t.bg = theme_blend(primary, base_bg, 0.05f);
        t.input_bg = theme_blend(primary, base_bg, 0.12f);
        t.dim = theme_blend(primary, base_bg, 0.22f);
        t.fg = theme_blend(primary, base_fg, 0.15f);
    } else {
        t.bg = theme_blend(primary, base_bg, 0.08f);
        t.input_bg = theme_blend(primary, base_bg, 0.15f);
        t.dim = theme_blend(primary, base_bg, 0.30f);
        t.fg = theme_blend(primary, base_fg, 0.20f);
    }
    
    t.sel_bg = primary;
    t.border = primary;
    t.active = primary;
    t.indicator = primary;
    t.prompt = primary;
    t.accent = theme_get_accent(primary);

    t.success = 0x4ade80;
    t.warning = 0xfacc15;
    t.error = 0xf87171;

    if (theme_get_luminance(primary) > 0.65f) {
        t.sel_fg = 0x1c1c1e;
    } else {
        t.sel_fg = 0xffffff;
    }
    
    t.inactive = t.dim;
    
    /* Generate Hex Strings */
    theme_color_to_hex(t.bg, t.bg_hex);
    theme_color_to_hex(t.input_bg, t.input_bg_hex);
    theme_color_to_hex(t.sel_bg, t.sel_bg_hex);
    theme_color_to_hex(t.fg, t.fg_hex);
    theme_color_to_hex(t.prompt, t.prompt_hex);
    theme_color_to_hex(t.sel_fg, t.sel_fg_hex);
    theme_color_to_hex(t.dim, t.dim_hex);
    theme_color_to_hex(t.border, t.border_hex);
    theme_color_to_hex(t.active, t.active_hex);
    theme_color_to_hex(t.inactive, t.inactive_hex);
    theme_color_to_hex(t.indicator, t.indicator_hex);
    theme_color_to_hex(t.success, t.success_hex);
    theme_color_to_hex(t.warning, t.warning_hex);
    theme_color_to_hex(t.error, t.error_hex);
    theme_color_to_hex(t.accent, t.accent_hex);

    current_theme = t;
    theme_loaded = 1;
    return t;
}

void init_theme(void) {
    load_theme();
}
