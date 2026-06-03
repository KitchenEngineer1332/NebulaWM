#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <math.h>
#include <ctype.h>
#include "theme.h"
#include <locale.h>

typedef struct {
    int height;
    char font[256];
    int margin;
    int padding;
} BarConfig;

typedef struct {
    Window win;
    Pixmap pixmap;
    XftDraw *xft_draw;
    GC gc;
    int x, y, width, height;
} BarWindow;

BarConfig config = {
    .height = 32,
    .font = "3270 Nerd Font Mono:size=10",
    .margin = 5,
    .padding = 12
};

static Display *dpy;
static int screen;
static BarWindow *bars = NULL;
static int num_bars = 0;
static XftFont *font;
static Colormap cmap;
static Visual *visual;
static int depth;
static XftColor color_bg, color_fg, color_active, color_inactive, color_pill;
static XftColor color_success, color_warning, color_error, color_accent;
static Picture corner_mask = 0;

/* Module boundaries for interaction */
static int vol_x_start = 0, vol_x_end = 0;
static int mic_x_start = 0, mic_x_end = 0;

typedef struct {
    Atom net_current_desktop;
    Atom net_client_list;
    Atom net_wm_desktop;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dock;
    Atom net_wm_strut_partial;
    Atom nebula_layout;
    Atom nebula_toggle_layout;
} Atoms;

static Atoms atoms;

static void init_atoms() {
    atoms.net_current_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    atoms.net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    atoms.net_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    atoms.net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atoms.net_wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms.net_wm_strut_partial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    atoms.nebula_layout = XInternAtom(dpy, "_NEBULA_CURRENT_LAYOUT", False);
    atoms.nebula_toggle_layout = XInternAtom(dpy, "_NEBULA_TOGGLE_LAYOUT", False);
}

/* Cached System Info */
static double cached_cpu = 0;
static int cached_ram = 0;
static int cached_vol = 0;
static int cached_vol_muted = 0;
static int cached_mic = 0;
static int cached_mic_muted = 0;
static int cached_bat = 0;
static time_t last_sys_update = 0;

static void alloc_rgba(const char *hex, float alpha, XftColor *dest) {
    unsigned long val = strtoul(hex + 1, NULL, 16);
    dest->color.red = ((val >> 16) & 0xff) * 257;
    dest->color.green = ((val >> 8) & 0xff) * 257;
    dest->color.blue = (val & 0xff) * 257;
    dest->color.alpha = (unsigned short)(alpha * 65535.0);
    XftColorAllocValue(dpy, visual, cmap, &dest->color, dest);
}

static void init_colors(void) {
    Theme t = load_theme();
    alloc_rgba(t.bg_hex, 1.0, &color_bg); 
    alloc_rgba(t.fg_hex, 1.0, &color_fg);
    alloc_rgba(t.active_hex, 1.0, &color_active);
    alloc_rgba(t.inactive_hex, 0.45, &color_inactive);
    alloc_rgba(t.bg_hex, 1.0, &color_pill);
    alloc_rgba(t.success_hex, 1.0, &color_success);
    alloc_rgba(t.warning_hex, 1.0, &color_warning);
    alloc_rgba(t.error_hex, 1.0, &color_error);
    alloc_rgba(t.accent_hex, 1.0, &color_accent);
}

void load_bar_config(void) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/Nebula/bar.config", pw->pw_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            val[strcspn(val, "\r\n")] = 0;
            while (*key && isspace((unsigned char)*key)) key++;
            while (*val && isspace((unsigned char)*val)) val++;

            char *end = key + strlen(key) - 1;
            while (end >= key && isspace((unsigned char)*end)) *end-- = '\0';

            end = val + strlen(val) - 1;
            while (end >= val && isspace((unsigned char)*end)) *end-- = '\0';

            if (strcmp(key, "height") == 0) {
                config.height = atoi(val);
            } else if (strcmp(key, "font") == 0) {
                strncpy(config.font, val, sizeof(config.font) - 1);
                config.font[sizeof(config.font) - 1] = '\0';
            }
        }
    }
    fclose(f);
}

static void update_audio_info() {
    FILE *f;
    // Volume
    f = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", "r");
    if (f) {
        char buf[512];
        if (fgets(buf, sizeof(buf), f)) {
            char *p = strchr(buf, '/');
            if (p) {
                p++;
                while (*p && isspace(*p)) p++;
                cached_vol = atoi(p);
            }
        }
        pclose(f);
    }
    // Mute
    f = popen("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", "r");
    if (f) {
        char buf[128];
        if (fgets(buf, sizeof(buf), f)) {
            cached_vol_muted = (strstr(buf, "yes") != NULL);
        }
        pclose(f);
    }

    // Mic Volume
    f = popen("pactl get-source-volume @DEFAULT_SOURCE@ 2>/dev/null", "r");
    if (f) {
        char buf[512];
        if (fgets(buf, sizeof(buf), f)) {
            char *p = strchr(buf, '/');
            if (p) {
                p++;
                while (*p && isspace(*p)) p++;
                cached_mic = atoi(p);
            }
        }
        pclose(f);
    }
    // Mic Mute
    f = popen("pactl get-source-mute @DEFAULT_SOURCE@ 2>/dev/null", "r");
    if (f) {
        char buf[128];
        if (fgets(buf, sizeof(buf), f)) {
            cached_mic_muted = (strstr(buf, "yes") != NULL);
        }
        pclose(f);
    }
}

static void update_sys_info() {
    time_t now = time(NULL);
    if (now - last_sys_update < 1) return;
    last_sys_update = now;

    FILE *f;
    /* CPU Usage: Read /proc/stat */
    static long long prev_idle = 0, prev_total = 0;
    f = fopen("/proc/stat", "r");
    if (f) {
        long long u, n, s, i, io, ir, si, st;
        if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld", &u, &n, &s, &i, &io, &ir, &si, &st) >= 4) {
            long long idle = i + io;
            long long total = u + n + s + i + io + ir + si + st;
            long long d_idle = idle - prev_idle;
            long long d_total = total - prev_total;
            if (d_total > 0) cached_cpu = (1.0 - (double)d_idle / d_total) * 100.0;
            prev_idle = idle;
            prev_total = total;
        }
        fclose(f);
    }

    /* RAM Usage: Read /proc/meminfo */
    f = fopen("/proc/meminfo", "r");
    if (f) {
        long long total = 0, avail = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            if (strncmp(buf, "MemTotal:", 9) == 0) sscanf(buf + 9, "%lld", &total);
            else if (strncmp(buf, "MemAvailable:", 13) == 0) sscanf(buf + 13, "%lld", &avail);
            if (total && avail) break;
        }
        fclose(f);
        if (total > 0) cached_ram = (int)((total - avail) * 100 / total);
    }

    /* Battery: Read /sys/class/power_supply */
    static const char *bat_paths[] = {
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/BAT1/capacity",
        NULL
    };
    for (int i = 0; bat_paths[i]; i++) {
        f = fopen(bat_paths[i], "r");
        if (f) {
            if (fscanf(f, "%d", &cached_bat) == 1) { fclose(f); break; }
            fclose(f);
        }
    }

    /* Volume: pactl (keep it for now but minimize calls) */
    static time_t last_vol_update = 0;
    if (now - last_vol_update >= 2) {
        update_audio_info();
        last_vol_update = now;
    }
}

void create_corner_mask(int r) {
    if (corner_mask) return;
    int size = r * 2;
    Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, screen), size, size, 8);
    XRenderPictFormat *format = XRenderFindStandardFormat(dpy, PictStandardA8);
    corner_mask = XRenderCreatePicture(dpy, pm, format, 0, NULL);
    
    XRenderColor transparent = {0, 0, 0, 0};
    XRenderFillRectangle(dpy, PictOpSrc, corner_mask, &transparent, 0, 0, size, size);
    
    GC gc = XCreateGC(dpy, pm, 0, NULL);
    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, pm, gc, 0, 0, size, size);
    XSetForeground(dpy, gc, 255);
    XFillArc(dpy, pm, gc, 0, 0, size, size, 0, 360 * 64);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pm);
}

void draw_rounded_rect(XftDraw *d, XftColor *color, int x, int y, int w, int h, int r) {
    Picture target = XftDrawPicture(d);
    XRenderColor xr_color = color->color;
    Picture source = XRenderCreateSolidFill(dpy, &xr_color);
    
    XRenderFillRectangle(dpy, PictOpOver, target, &xr_color, x + r, y, w - 2 * r, h);
    XRenderFillRectangle(dpy, PictOpOver, target, &xr_color, x, y + r, r, h - 2 * r);
    XRenderFillRectangle(dpy, PictOpOver, target, &xr_color, x + w - r, y + r, r, h - 2 * r);
    
    if (!corner_mask) create_corner_mask(r);
    
    XRenderComposite(dpy, PictOpOver, source, corner_mask, target, 0, 0, 0, 0, x, y, r, r); // TL
    XRenderComposite(dpy, PictOpOver, source, corner_mask, target, r, 0, 0, 0, x + w - r, y, r, r); // TR
    XRenderComposite(dpy, PictOpOver, source, corner_mask, target, 0, r, 0, 0, x, y + h - r, r, r); // BL
    XRenderComposite(dpy, PictOpOver, source, corner_mask, target, r, r, 0, 0, x + w - r, y + h - r, r, r); // BR
    
    XRenderFreePicture(dpy, source);
}

void switch_workspace(int ws) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = RootWindow(dpy, screen);
    ev.xclient.message_type = atoms.net_current_desktop;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ws;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, RootWindow(dpy, screen), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

void toggle_layout() {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = RootWindow(dpy, screen);
    ev.xclient.message_type = atoms.nebula_toggle_layout;
    ev.xclient.format = 32;
    XSendEvent(dpy, RootWindow(dpy, screen), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

void draw_bar(BarWindow *bar) {
    XRenderColor clear = color_bg.color;
    Picture pict = XftDrawPicture(bar->xft_draw);
    XRenderFillRectangle(dpy, PictOpSrc, pict, &clear, 0, 0, bar->width, bar->height);

    update_sys_info();

    int current_ws = 0;
    int layout_mode = 0;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, RootWindow(dpy, screen), atoms.net_current_desktop,
                           0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0) current_ws = *(unsigned long *)prop;
        XFree(prop);
    }

    if (XGetWindowProperty(dpy, RootWindow(dpy, screen), atoms.nebula_layout,
                           0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0) layout_mode = *(unsigned long *)prop;
        XFree(prop);
    }

    int occupied[9] = {0};
    unsigned char *prop_clients = NULL;
    if (XGetWindowProperty(dpy, RootWindow(dpy, screen), atoms.net_client_list,
                           0, 1024, False, XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after, &prop_clients) == Success && prop_clients) {
        Window *wins = (Window *)prop_clients;
        for (unsigned long i = 0; i < nitems; i++) {
            unsigned char *prop_ws = NULL;
            if (XGetWindowProperty(dpy, wins[i], atoms.net_wm_desktop,
                                   0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop_ws) == Success && prop_ws) {
                if (nitems > 0) {
                    int ws = *(unsigned long *)prop_ws;
                    if (ws >= 0 && ws < 9) occupied[ws] = 1;
                }
                XFree(prop_ws);
            }
        }
        XFree(prop_clients);
    }

    int pill_h = config.height - 2 * config.margin;
    int pill_y = config.margin;
    int text_y = (config.height + font->ascent - font->descent) / 2;

    // --- Workspaces ---
    int ws_w = 9 * 26 + 10;
    draw_rounded_rect(bar->xft_draw, &color_pill, 8, pill_y, ws_w, pill_h, 8);
    for (int i = 0; i < 9; i++) {
        // Example using standard Nerd Font Unicode hex values
        const char *icon = (i == current_ws) ? "\uF111" : (occupied[i] ? "\uF192" : "\uF10C");
        XftColor *c = (i == current_ws) ? &color_active : (occupied[i] ? &color_fg : &color_inactive);
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, font, (FcChar8 *)icon, strlen(icon), &ext);
        int ix = 13 + i * 26 + (26 - (int)ext.width) / 2;
        XftDrawStringUtf8(bar->xft_draw, c, font, ix, text_y, (FcChar8 *)icon, strlen(icon));
    }

    // --- Layout ---
    const char *lay_icon = layout_mode ? "\uF009 TILE" : "\uF2D0 FLOAT";
    XGlyphInfo lay_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)lay_icon, strlen(lay_icon), &lay_ext);
    int lay_w = lay_ext.width + 20;
    int lay_x = 8 + ws_w + 8;
    draw_rounded_rect(bar->xft_draw, &color_pill, lay_x, pill_y, lay_w, pill_h, 8);
    XftDrawStringUtf8(bar->xft_draw, &color_fg, font, lay_x + 10, text_y, (FcChar8 *)lay_icon, strlen(lay_icon));

    // --- Window Title ---
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    char *win_name = NULL;
    if (focused != None && focused != RootWindow(dpy, screen)) XFetchName(dpy, focused, &win_name);
    if (win_name) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, font, (FcChar8 *)win_name, strlen(win_name), &ext);
        int title_w = ext.width + 30;
        if (title_w > 400) title_w = 400;
        int title_x = (bar->width - title_w) / 2;
        draw_rounded_rect(bar->xft_draw, &color_pill, title_x, pill_y, title_w, pill_h, 8);
        XftDrawStringUtf8(bar->xft_draw, &color_fg, font, title_x + 15, text_y, (FcChar8 *)win_name, strlen(win_name));
        XFree(win_name);
    }

    // --- System Info ---
    time_t raw;
    time(&raw);
    struct tm *ti = localtime(&raw);
    char t_str[32], cpu_str[16], ram_str[16], vol_str[16], mic_str[16], bat_str[16];
    strftime(t_str, sizeof(t_str), "%H:%M:%S", ti);
    
    snprintf(cpu_str, sizeof(cpu_str), "\uF4BC %.0f%%", cached_cpu);
    snprintf(ram_str, sizeof(ram_str), "\uF85A %d%%", cached_ram);
    snprintf(vol_str, sizeof(vol_str), "%s %d%%", cached_vol_muted ? "\uF026" : "\uF028", cached_vol);
    snprintf(mic_str, sizeof(mic_str), "\uF130 %d%%", cached_mic);
    snprintf(bat_str, sizeof(bat_str), "\uF240 %d%%", cached_bat);

    int s_x = bar->width - 8;
    
    // Time
    XGlyphInfo t_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)t_str, strlen(t_str), &t_ext);
    int t_w = t_ext.width + 20;
    s_x -= t_w;
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, t_w, pill_h, 8);
    XftDrawStringUtf8(bar->xft_draw, &color_accent, font, s_x + 10, text_y, (FcChar8 *)t_str, strlen(t_str));

    // Battery
    XGlyphInfo b_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)bat_str, strlen(bat_str), &b_ext);
    int b_w = b_ext.width + 20;
    s_x -= (b_w + 8);
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, b_w, pill_h, 8);
    XftColor *bc = (cached_bat < 20) ? &color_error : (cached_bat < 50 ? &color_warning : &color_success);
    XftDrawStringUtf8(bar->xft_draw, bc, font, s_x + 10, text_y, (FcChar8 *)bat_str, strlen(bat_str));

    // Mic
    XGlyphInfo m_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)mic_str, strlen(mic_str), &m_ext);
    int m_w = m_ext.width + 20;
    s_x -= (m_w + 8);
    mic_x_start = s_x;
    mic_x_end = s_x + m_w;
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, m_w, pill_h, 8);
    XftColor *mc = cached_mic_muted ? &color_error : &color_fg;
    XftDrawStringUtf8(bar->xft_draw, mc, font, s_x + 10, text_y, (FcChar8 *)mic_str, strlen(mic_str));

    // Volume
    XGlyphInfo v_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)vol_str, strlen(vol_str), &v_ext);
    int v_w = v_ext.width + 20;
    s_x -= (v_w + 8);
    vol_x_start = s_x;
    vol_x_end = s_x + v_w;
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, v_w, pill_h, 8);
    XftColor *vc = cached_vol_muted ? &color_error : &color_fg;
    XftDrawStringUtf8(bar->xft_draw, vc, font, s_x + 10, text_y, (FcChar8 *)vol_str, strlen(vol_str));

    // RAM
    XGlyphInfo r_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)ram_str, strlen(ram_str), &r_ext);
    int r_w = r_ext.width + 20;
    s_x -= (r_w + 8);
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, r_w, pill_h, 8);
    XftColor *rc = (cached_ram > 80) ? &color_error : (cached_ram > 60 ? &color_warning : &color_fg);
    XftDrawStringUtf8(bar->xft_draw, rc, font, s_x + 10, text_y, (FcChar8 *)ram_str, strlen(ram_str));

    // CPU
    XGlyphInfo c_ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)cpu_str, strlen(cpu_str), &c_ext);
    int c_w = c_ext.width + 20;
    s_x -= (c_w + 8);
    draw_rounded_rect(bar->xft_draw, &color_pill, s_x, pill_y, c_w, pill_h, 8);
    XftColor *cc = (cached_cpu > 80) ? &color_error : (cached_cpu > 60 ? &color_warning : &color_fg);
    XftDrawStringUtf8(bar->xft_draw, cc, font, s_x + 10, text_y, (FcChar8 *)cpu_str, strlen(cpu_str));

    XCopyArea(dpy, bar->pixmap, bar->win, bar->gc, 0, 0, bar->width, bar->height, 0, 0);
}

int xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy; (void)ee;
    return 0;
}

static void change_volume(int delta) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %s%d%%", delta > 0 ? "+" : "-", abs(delta));
    system(cmd);
    update_audio_info();
}

static void toggle_volume_mute() {
    system("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    update_audio_info();
}

static void change_mic_volume(int delta) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pactl set-source-volume @DEFAULT_SOURCE@ %s%d%%", delta > 0 ? "+" : "-", abs(delta));
    system(cmd);
    update_audio_info();
}

static void toggle_mic_mute() {
    system("pactl set-source-mute @DEFAULT_SOURCE@ toggle");
    update_audio_info();
}

int main(void) {
    load_bar_config();
    if (!(dpy = XOpenDisplay(NULL))) return 1;
    XSetErrorHandler(xerror);
    screen = DefaultScreen(dpy);
    
    XVisualInfo vinfo;
    if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo)) {
        visual = DefaultVisual(dpy, screen);
        depth = DefaultDepth(dpy, screen);
        cmap = DefaultColormap(dpy, screen);
    } else {
        visual = vinfo.visual;
        depth = vinfo.depth;
        cmap = XCreateColormap(dpy, RootWindow(dpy, screen), visual, AllocNone);
    }

    init_colors();

    XineramaScreenInfo *info = NULL;
    int n = 0;
    if (XineramaIsActive(dpy)) info = XineramaQueryScreens(dpy, &n);
    if (n == 0) {
        num_bars = 1;
        bars = malloc(sizeof(BarWindow));
        bars[0].x = 0; bars[0].y = 0;
        bars[0].width = DisplayWidth(dpy, screen);
        bars[0].height = config.height;
    } else {
        num_bars = n;
        bars = malloc(sizeof(BarWindow) * n);
        for (int i = 0; i < n; i++) {
            bars[i].x = info[i].x_org; bars[i].y = info[i].y_org;
            bars[i].width = info[i].width; bars[i].height = config.height;
        }
        XFree(info);
    }

    {
        FcPattern *pattern = FcNameParse((const FcChar8 *)config.font);
        if (pattern) {
            if (depth == 32) {
                FcPatternDel(pattern, FC_RGBA);
                FcPatternAddInteger(pattern, FC_RGBA, FC_RGBA_NONE);
            }
            FcConfigSubstitute(NULL, pattern, FcMatchPattern);
            XftDefaultSubstitute(dpy, screen, pattern);
            FcResult res;
            FcPattern *match = FcFontMatch(NULL, pattern, &res);
            if (match) {
                font = XftFontOpenPattern(dpy, match);
                if (!font) FcPatternDestroy(match);
            }
            FcPatternDestroy(pattern);
        }
    }
    if (!font) font = XftFontOpenName(dpy, screen, config.font);
    if (!font) font = XftFontOpenName(dpy, screen, "monospace:size=10");

    init_atoms();

    XSelectInput(dpy, RootWindow(dpy, screen), PropertyChangeMask);

    for (int i = 0; i < num_bars; i++) {
        XSetWindowAttributes swa;
        swa.override_redirect = True;
        swa.background_pixel = 0;
        swa.border_pixel = 0;
        swa.colormap = cmap;
        swa.event_mask = ExposureMask | ButtonPressMask;
        
        bars[i].win = XCreateWindow(dpy, RootWindow(dpy, screen),
                            bars[i].x, bars[i].y, bars[i].width, bars[i].height, 0,
                            depth, InputOutput, visual,
                            CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
                            &swa);

        XChangeProperty(dpy, bars[i].win, atoms.net_wm_window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&atoms.net_wm_window_type_dock, 1);
        unsigned long struts[12] = {0,0,config.height,0, 0,0,0,0, bars[i].x, bars[i].x + bars[i].width - 1, 0,0};
        XChangeProperty(dpy, bars[i].win, atoms.net_wm_strut_partial, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)struts, 12);

        bars[i].pixmap = XCreatePixmap(dpy, bars[i].win, bars[i].width, bars[i].height, depth);
        bars[i].xft_draw = XftDrawCreate(dpy, bars[i].pixmap, visual, cmap);
        bars[i].gc = XCreateGC(dpy, bars[i].win, 0, NULL);
        XMapRaised(dpy, bars[i].win);
    }

    int x11_fd = ConnectionNumber(dpy);
    XEvent ev;
    int redraw_needed = 1;

    while (1) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
                redraw_needed = 1;
            } else if (ev.type == ButtonPress) {
                int x = ev.xbutton.x;
                int button = ev.xbutton.button;
                int ws_w = 9 * 26 + 10;
                
                if (button == Button1) {
                    if (x >= 8 && x < 8 + ws_w) {
                        int ws = (x - 13) / 26;
                        if (ws >= 0 && ws < 9) switch_workspace(ws);
                    } else if (x >= 8 + ws_w + 8 && x < 8 + ws_w + 8 + 80) {
                        toggle_layout();
                    } else if (x >= vol_x_start && x < vol_x_end) {
                        toggle_volume_mute();
                        redraw_needed = 1;
                    } else if (x >= mic_x_start && x < mic_x_end) {
                        toggle_mic_mute();
                        redraw_needed = 1;
                    }
                } else if (button == Button4 || button == Button5) {
                    if (x >= vol_x_start && x < vol_x_end) {
                        change_volume(button == Button4 ? 5 : -5);
                        redraw_needed = 1;
                    } else if (x >= mic_x_start && x < mic_x_end) {
                        change_mic_volume(button == Button4 ? 5 : -5);
                        redraw_needed = 1;
                    } else {
                        // Check if scroll is over system info area (fallback)
                        BarWindow *clicked_bar = NULL;
                        for (int i = 0; i < num_bars; i++) {
                            if (bars[i].win == ev.xbutton.window) {
                                clicked_bar = &bars[i];
                                break;
                            }
                        }
                        if (clicked_bar) {
                            time_t raw; time(&raw);
                            char t_str[32], s_str[256];
                            strftime(t_str, sizeof(t_str), "%H:%M:%S", localtime(&raw));
                            snprintf(s_str, sizeof(s_str), "CPU %.0f%%  RAM %d%%  VOL %d%%  MIC %d%%  BAT %d%%  %s", cached_cpu, cached_ram, cached_vol, cached_mic, cached_bat, t_str);
                            XGlyphInfo s_ext;
                            XftTextExtentsUtf8(dpy, font, (FcChar8 *)s_str, strlen(s_str), &s_ext);
                            int s_w = s_ext.width + 24;
                            int s_x = clicked_bar->width - s_w - 8;
                            
                            if (x >= s_x && x < s_x + s_w) {
                                change_volume(button == Button4 ? 5 : -5);
                                redraw_needed = 1;
                            }
                        }
                    }
                }
            } else if (ev.type == PropertyNotify) {
                redraw_needed = 1;
            }
        }
        
        time_t now = time(NULL);
        if (redraw_needed || (now - last_sys_update >= 1)) {
            for (int i = 0; i < num_bars; i++) draw_bar(&bars[i]);
            XFlush(dpy);
            redraw_needed = 0;
        }

        struct timeval tv = {0, 100000}; // 100ms
        fd_set fds; FD_ZERO(&fds); FD_SET(x11_fd, &fds);
        select(x11_fd + 1, &fds, NULL, NULL, &tv);
    }
    return 0;
}
