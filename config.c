#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

int config_border_width = 2;
unsigned long config_focused_color = 0xffffff;
unsigned long config_unfocused_color = 0x555555;
char config_terminal[256] = "alacritty";
unsigned int config_modifier = Mod4Mask;
char config_wallpaper[512] = "";
char config_launcher[256] = "nebula-launcher";

unsigned long get_color(const char *hex_str) {
    if (hex_str[0] == '#') hex_str++;
    return strtoul(hex_str, NULL, 16);
}

void load_config(void) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.config/Nebula/nebula.config", pw->pw_dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            val[strcspn(val, "\r\n")] = 0;
            
            if (strcmp(key, "border_width") == 0) {
                config_border_width = atoi(val);
            } else if (strcmp(key, "focused_color") == 0) {
                config_focused_color = get_color(val);
            } else if (strcmp(key, "unfocused_color") == 0) {
                config_unfocused_color = get_color(val);
            } else if (strcmp(key, "terminal") == 0) {
                strncpy(config_terminal, val, sizeof(config_terminal) - 1);
                config_terminal[sizeof(config_terminal) - 1] = '\0';
            } else if (strcmp(key, "modifier") == 0) {
                if (strcmp(val, "Mod4") == 0) config_modifier = Mod4Mask;
                else if (strcmp(val, "Mod1") == 0) config_modifier = Mod1Mask;
            } else if (strcmp(key, "wallpaper") == 0) {
                strncpy(config_wallpaper, val, sizeof(config_wallpaper) - 1);
                config_wallpaper[sizeof(config_wallpaper) - 1] = '\0';
            } else if (strcmp(key, "launcher") == 0) {
                strncpy(config_launcher, val, sizeof(config_launcher) - 1);
                config_launcher[sizeof(config_launcher) - 1] = '\0';
            }
        }
    }
    fclose(f);
}
