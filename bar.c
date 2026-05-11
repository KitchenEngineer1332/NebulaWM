#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xinerama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/select.h>
#include "theme.h"

typedef struct {
    int height;
    char font[256];
} BarConfig;

typedef struct {
    Window win;
    Pixmap pixmap;
    XftDraw *xft_draw;
    int x, y, width, height;
} BarWindow;

BarConfig config = {
    .height = 30,
    .font = "monospace:size=10"
};

static Display *dpy;
static int screen;
static BarWindow *bars = NULL;
static int num_bars = 0;
static XftFont *font;
static Colormap cmap;
static Visual *visual;
static XftColor color_bg, color_fg, color_active, color_inactive;

static void init_colors(void) {
    Theme t = load_theme();
    XftColorAllocName(dpy, visual, cmap, t.bg, &color_bg);
    XftColorAllocName(dpy, visual, cmap, t.fg, &color_fg);
    XftColorAllocName(dpy, visual, cmap, t.active, &color_active);
    XftColorAllocName(dpy, visual, cmap, t.inactive, &color_inactive);
}

void load_bar_config(void) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.config/Nebula/bar.config", pw->pw_dir);

    FILE *f = fopen(path, "r");
    if (!f) {
        // Create default config
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "height=30\n");
            fprintf(f, "font=monospace:size=10\n");
            fclose(f);
        }
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            val[strcspn(val, "\r\n")] = 0;
            if (strcmp(key, "height") == 0) config.height = atoi(val);
            else if (strcmp(key, "font") == 0) strncpy(config.font, val, sizeof(config.font) - 1);
        }
    }
    fclose(f);
}

int get_battery_capacity() {
    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!f) f = fopen("/sys/class/power_supply/BAT1/capacity", "r");
    if (!f) return -1;
    int cap = 0;
    fscanf(f, "%d", &cap);
    fclose(f);
    return cap;
}

char* get_battery_status() {
    static char status[32];
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!f) f = fopen("/sys/class/power_supply/BAT1/status", "r");
    if (!f) return "Unknown";
    fscanf(f, "%s", status);
    fclose(f);
    return status;
}

double get_cpu_usage() {
    static long long prev_idle = 0, prev_total = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0.0;
    long long user, nice, system, idle, iowait, irq, softirq, steal;
    if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4) {
        fclose(f);
        return 0.0;
    }
    fclose(f);
    long long current_idle = idle + iowait;
    long long current_total = user + nice + system + idle + iowait + irq + softirq + steal;
    long long diff_idle = current_idle - prev_idle;
    long long diff_total = current_total - prev_total;
    double usage = 0.0;
    if (diff_total > 0) usage = (1.0 - (double)diff_idle / diff_total) * 100.0;
    prev_idle = current_idle;
    prev_total = current_total;
    return usage;
}

int get_ram_usage() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    long long total = 0, available = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line + 9, "%lld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line + 13, "%lld", &available);
    }
    fclose(f);
    if (total == 0) return 0;
    return (int)((total - available) * 100 / total);
}

int get_volume() {
    FILE *f = popen("amixer get Master | awk -F\"[][]\" '/Left:/ { print $2 }' | head -n1 | sed 's/%//'", "r");
    if (!f) return -1;
    int vol = -1;
    if (fscanf(f, "%d", &vol) == 0) vol = -1;
    pclose(f);
    return vol;
}

void switch_workspace(int ws) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = RootWindow(dpy, screen);
    ev.xclient.message_type = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ws;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, RootWindow(dpy, screen), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

void draw_bar(BarWindow *bar) {
    XftDrawRect(bar->xft_draw, &color_bg, 0, 0, bar->width, config.height);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    int current_ws = 0;
    if (XGetWindowProperty(dpy, RootWindow(dpy, screen), XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False),
                           0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0) current_ws = *(unsigned long *)prop;
        XFree(prop);
    }

    int current_layout = 0;
    unsigned char *prop_layout = NULL;
    if (XGetWindowProperty(dpy, RootWindow(dpy, screen), XInternAtom(dpy, "_NEBULA_CURRENT_LAYOUT", False),
                           0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop_layout) == Success && prop_layout) {
        if (nitems > 0) current_layout = *(unsigned long *)prop_layout;
        XFree(prop_layout);
    }

    // Identify occupied workspaces
    int occupied[9] = {0};
    unsigned int n;
    Window root_return, parent_return, *children;
    XQueryTree(dpy, RootWindow(dpy, screen), &root_return, &parent_return, &children, &n);
    for (unsigned int i = 0; i < n; i++) {
        unsigned char *prop_ws = NULL;
        if (XGetWindowProperty(dpy, children[i], XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                               0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop_ws) == Success && prop_ws) {
            if (nitems > 0) {
                int ws = *(unsigned long *)prop_ws;
                if (ws >= 0 && ws < 9) occupied[ws] = 1;
            }
            XFree(prop_ws);
        }
    }
    if (children) XFree(children);

    XftColor *active = &color_active;
    XftColor *inactive = &color_inactive;
    XftColor *fg = &color_fg;

    int x = 10;
    for (int i = 0; i < 9; i++) {
        char ws_name[2];
        snprintf(ws_name, 2, "%d", i + 1);
        XftColor *c = (i == current_ws) ? active : (occupied[i] ? fg : inactive);
        XftDrawStringUtf8(bar->xft_draw, c, font, x, (config.height + font->ascent) / 2, (FcChar8 *)ws_name, 1);
        
        // Add a dot for occupied workspaces if not current
        if (occupied[i] && i != current_ws) {
            XftDrawRect(bar->xft_draw, fg, x + 4, (config.height + font->ascent) / 2 + 2, 4, 2);
        }
        
        x += 20;
    }

    char *layout_str = current_layout == 0 ? "[F]" : "[T]";
    XftColor *layout_c = current_layout == 0 ? inactive : active;
    XftDrawStringUtf8(bar->xft_draw, layout_c, font, x, (config.height + font->ascent) / 2, (FcChar8 *)layout_str, 3);
    x += 40;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused != None && focused != RootWindow(dpy, screen)) {
        char *name = NULL;
        if (XFetchName(dpy, focused, &name) && name) {
            XftDrawStringUtf8(bar->xft_draw, fg, font, x + 20, (config.height + font->ascent) / 2, (FcChar8 *)name, strlen(name));
            XFree(name);
        }
    }

    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char status_str[256];
    strftime(status_str, sizeof(status_str), "%a %d %b %H:%M:%S", timeinfo);
    
    char sys_str[128];
    snprintf(sys_str, sizeof(sys_str), " | CPU: %.1f%% | RAM: %d%%", get_cpu_usage(), get_ram_usage());
    
    int vol = get_volume();
    if (vol != -1) {
        char vol_str[16];
        snprintf(vol_str, sizeof(vol_str), " | Vol: %d%%", vol);
        strncat(sys_str, vol_str, sizeof(sys_str) - strlen(sys_str) - 1);
    }
    
    strncat(status_str, sys_str, sizeof(status_str) - strlen(status_str) - 1);

    int bat = get_battery_capacity();
    if (bat != -1) {
        char bat_str[32];
        snprintf(bat_str, sizeof(bat_str), " | Bat: %d%% (%s)", bat, get_battery_status());
        strncat(status_str, bat_str, sizeof(status_str) - strlen(status_str) - 1);
    }

    XGlyphInfo extents;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)status_str, strlen(status_str), &extents);
    XftDrawStringUtf8(bar->xft_draw, fg, font, bar->width - extents.width - 10, (config.height + font->ascent) / 2, (FcChar8 *)status_str, strlen(status_str));

    XCopyArea(dpy, bar->pixmap, bar->win, DefaultGC(dpy, screen), 0, 0, bar->width, bar->height, 0, 0);
}

int xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    (void)ee;
    return 0;
}

int main(void) {
    load_bar_config();

    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    XSetErrorHandler(xerror);

    screen = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);

    init_colors();

    XineramaScreenInfo *info = NULL;
    int n = 0;
    if (XineramaIsActive(dpy)) {
        info = XineramaQueryScreens(dpy, &n);
    }

    if (n == 0) {
        num_bars = 1;
        bars = malloc(sizeof(BarWindow));
        bars[0].x = 0;
        bars[0].y = 0;
        bars[0].width = DisplayWidth(dpy, screen);
        bars[0].height = config.height;
    } else {
        num_bars = n;
        bars = malloc(sizeof(BarWindow) * n);
        for (int i = 0; i < n; i++) {
            bars[i].x = info[i].x_org;
            bars[i].y = info[i].y_org;
            bars[i].width = info[i].width;
            bars[i].height = config.height;
        }
        XFree(info);
    }

    font = XftFontOpenName(dpy, screen, config.font);
    if (!font) font = XftFontOpenName(dpy, screen, "fixed");

    Atom window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom strut = XInternAtom(dpy, "_NET_WM_STRUT", False);
    Atom strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);

    for (int i = 0; i < num_bars; i++) {
        XSetWindowAttributes swa;
        swa.override_redirect = True;
        swa.event_mask = ExposureMask | ButtonPressMask;
        
        bars[i].win = XCreateWindow(dpy, RootWindow(dpy, screen),
                            bars[i].x, bars[i].y, bars[i].width, bars[i].height, 0,
                            CopyFromParent, InputOutput, visual,
                            CWOverrideRedirect | CWEventMask,
                            &swa);

        XChangeProperty(dpy, bars[i].win, window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock, 1);
        XChangeProperty(dpy, bars[i].win, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&above, 1);

        unsigned long strut_vals[4] = {0, 0, config.height, 0};
        XChangeProperty(dpy, bars[i].win, strut, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut_vals, 4);
        
        unsigned long strut_p_vals[12] = {0, 0, config.height, 0, 0, 0, 0, 0, bars[i].x, bars[i].x + bars[i].width - 1, 0, 0};
        XChangeProperty(dpy, bars[i].win, strut_partial, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut_p_vals, 12);

        bars[i].pixmap = XCreatePixmap(dpy, bars[i].win, bars[i].width, bars[i].height, DefaultDepth(dpy, screen));
        bars[i].xft_draw = XftDrawCreate(dpy, bars[i].pixmap, visual, cmap);
        XMapRaised(dpy, bars[i].win);
    }

    int x11_fd = ConnectionNumber(dpy);
    XEvent ev;
    time_t last_draw = 0;

    while (1) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
                for (int i = 0; i < num_bars; i++) {
                    if (ev.xexpose.window == bars[i].win) draw_bar(&bars[i]);
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.x >= 10 && ev.xbutton.x < 190) {
                    int ws = (ev.xbutton.x - 10) / 20;
                    if (ws >= 0 && ws < 9) switch_workspace(ws);
                } else if (ev.xbutton.x >= 190 && ev.xbutton.x < 230) {
                    XEvent toggle_ev;
                    memset(&toggle_ev, 0, sizeof(toggle_ev));
                    toggle_ev.type = ClientMessage;
                    toggle_ev.xclient.window = RootWindow(dpy, screen);
                    toggle_ev.xclient.message_type = XInternAtom(dpy, "_NEBULA_TOGGLE_LAYOUT", False);
                    toggle_ev.xclient.format = 32;
                    XSendEvent(dpy, RootWindow(dpy, screen), False, SubstructureRedirectMask | SubstructureNotifyMask, &toggle_ev);
                    XFlush(dpy);
                }
            }
        }

        time_t now = time(NULL);
        if (now != last_draw) {
            for (int i = 0; i < num_bars; i++) {
                draw_bar(&bars[i]);
            }
            last_draw = now;
            XFlush(dpy);
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // Check every 100ms for responsiveness

        fd_set in_fds;
        FD_ZERO(&in_fds);
        FD_SET(x11_fd, &in_fds);

        select(x11_fd + 1, &in_fds, NULL, NULL, &tv);
    }

    return 0;
}
