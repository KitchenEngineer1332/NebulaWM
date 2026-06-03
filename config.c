#include "config.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>

int config_border_width = 2;
int config_bar_height = 32;
int config_wm_type = 0; // 0 for nogaps, 1 for float
unsigned long config_focused_color = 0x7aa2f7;
unsigned long config_unfocused_color = 0x414868;
char config_terminal[256] = "starlight";
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

    char config_dir[512];
    snprintf(config_dir, sizeof(config_dir), "%s/.config/Nebula", pw->pw_dir);
    
    // Create directory if it doesn't exist
    struct stat st = {0};
    if (stat(config_dir, &st) == -1) {
        mkdir(config_dir, 0755);
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/nebula.config", config_dir);

    FILE *f = fopen(path, "r");
    if (!f) {
        // Generate default config if it doesn't exist
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "border_width=2\n");
            fprintf(f, "wmtype=nogaps\n");
            fprintf(f, "primary_color=#ff0055\n");
            fprintf(f, "terminal=starlight\n");
            fprintf(f, "modifier=Mod4\n");
            fprintf(f, "wallpaper=\n");
            fprintf(f, "launcher=nebula-launcher\n");
            fclose(f);
        }
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *delimiter = strpbrk(line, "=:");
        if (!delimiter) continue;

        *delimiter = '\0';
        char *key = line;
        char *val = delimiter + 1;

        // Trim whitespace from key
        while (*key && isspace(*key)) key++;
        char *end = key + strlen(key) - 1;
        while (end > key && isspace(*end)) *end-- = '\0';

        // Trim whitespace from val
        while (*val && isspace(*val)) val++;
        end = val + strlen(val) - 1;
        while (end > val && isspace(*end)) *end-- = '\0';

        if (*key && *val) {
            if (strcmp(key, "border_width") == 0) {
                config_border_width = atoi(val);
            } else if (strcmp(key, "wmtype") == 0) {
                if (strcmp(val, "nogaps") == 0) config_wm_type = 0;
                else if (strcmp(val, "float") == 0) config_wm_type = 1;
            } else if (strcmp(key, "bar_height") == 0) {
                config_bar_height = atoi(val);
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
    
    Theme t = load_theme();
    config_focused_color = t.border;
    config_unfocused_color = t.dim;
}
