#ifndef CONFIG_H
#define CONFIG_H

#include <X11/Xlib.h>

extern int config_border_width;
extern int config_bar_height;
extern unsigned long config_focused_color;
extern unsigned long config_unfocused_color;
extern char config_terminal[256];
extern unsigned int config_modifier;
extern char config_wallpaper[512];
extern char config_launcher[256];

void load_config(void);
unsigned long get_color(const char *hex_str);

#endif
