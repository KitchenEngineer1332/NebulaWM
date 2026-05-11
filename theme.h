#ifndef THEME_H
#define THEME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

typedef struct {
    char bg[10];
    char input_bg[10];
    char sel_bg[10];
    char fg[10];
    char prompt[10];
    char sel_fg[10];
    char dim[10];
    char border[10];
    
    /* Additional for bar/lockscreen */
    char active[10];
    char inactive[10];
    char indicator[10];
} Theme;

static inline void blend_color(unsigned long c1, unsigned long c2, float w1, char *out) {
    unsigned int r1 = (c1 >> 16) & 0xff;
    unsigned int g1 = (c1 >> 8) & 0xff;
    unsigned int b1 = c1 & 0xff;
    
    unsigned int r2 = (c2 >> 16) & 0xff;
    unsigned int g2 = (c2 >> 8) & 0xff;
    unsigned int b2 = c2 & 0xff;
    
    unsigned int r = (unsigned int)(r1 * w1 + r2 * (1.0f - w1));
    unsigned int g = (unsigned int)(g1 * w1 + g2 * (1.0f - w1));
    unsigned int b = (unsigned int)(b1 * w1 + b2 * (1.0f - w1));
    
    snprintf(out, 10, "#%02x%02x%02x", r, g, b);
}

static inline Theme load_theme(void) {
    Theme t;
    char primary_hex[16] = "#ff0055"; /* Default primary */
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/.config/Nebula/nebula.config", pw->pw_dir);
        FILE *f = fopen(path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char *key = strtok(line, "=");
                char *val = strtok(NULL, "\n");
                if (key && val) {
                    val[strcspn(val, "\r\n")] = 0;
                    if (strcmp(key, "primary_color") == 0) {
                        strncpy(primary_hex, val, sizeof(primary_hex) - 1);
                        primary_hex[sizeof(primary_hex) - 1] = '\0';
                        break;
                    }
                }
            }
            fclose(f);
        }
    }
    
    unsigned long primary = 0xff0055;
    if (primary_hex[0] == '#') {
        primary = strtoul(primary_hex + 1, NULL, 16);
    } else {
        primary = strtoul(primary_hex, NULL, 16);
        snprintf(primary_hex, sizeof(primary_hex), "#%06lx", primary);
    }
    
    unsigned long black = 0x101014;
    unsigned long white = 0xffffff;
    
    blend_color(primary, black, 0.08f, t.bg);
    blend_color(primary, black, 0.15f, t.input_bg);
    blend_color(primary, white, 0.15f, t.fg);
    blend_color(primary, black, 0.40f, t.dim);
    
    strncpy(t.sel_bg, primary_hex, sizeof(t.sel_bg));
    blend_color(primary, black, 0.05f, t.sel_fg);
    strncpy(t.border, primary_hex, sizeof(t.border));
    strncpy(t.prompt, primary_hex, sizeof(t.prompt));
    strncpy(t.active, primary_hex, sizeof(t.active));
    strncpy(t.inactive, t.dim, sizeof(t.inactive));
    strncpy(t.indicator, primary_hex, sizeof(t.indicator));
    
    return t;
}

#endif // THEME_H
