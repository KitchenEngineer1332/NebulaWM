#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

/* ── Appearance ─────────────────────────────────────────────── */
#define MAX_APPS      512
#define MAX_NAME      128
#define MAX_EXEC      256
#define MAX_QUERY     128
#define WIN_W         620
#define WIN_H         420
#define INPUT_H       48
#define ITEM_H        38
#define PAD_X         18
#define PAD_Y         12
#define FONT_NAME     "monospace:size=13"
#define PROMPT        "Launch > "

/* ── Colours (RRGGBB) ──────────────────────────────────────── */
#define COL_BG        "#1a1b26"
#define COL_INPUT_BG  "#24283b"
#define COL_SEL_BG    "#414868"
#define COL_FG        "#c0caf5"
#define COL_PROMPT    "#7aa2f7"
#define COL_SEL_FG    "#ffffff"
#define COL_DIM       "#565f89"
#define COL_BORDER    "#7aa2f7"

/* ── Data types ─────────────────────────────────────────────── */
typedef struct {
    char name[MAX_NAME];
    char exec[MAX_EXEC];
} App;

/* ── Globals ────────────────────────────────────────────────── */
static Display *dpy;
static int      screen;
static Window   win;
static GC       gc;
static XftFont *font;
static XftDraw *xft_draw;
static Colormap cmap;
static Visual  *visual;

static App  apps[MAX_APPS];
static int  app_count = 0;
static App *filtered[MAX_APPS];
static int  filtered_count = 0;

static char query[MAX_QUERY] = "";
static int  query_len = 0;
static int  selected = 0;
static int  scroll_off = 0;

/* ── Helpers ────────────────────────────────────────────────── */
static XftColor xft_color(const char *hex) {
    XftColor c;
    XftColorAllocName(dpy, visual, cmap, hex, &c);
    return c;
}

static void str_tolower(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++)
        dst[i] = tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* strip %f %F %u %U %d %D %n %N %i %c %k %v %m from Exec lines */
static void clean_exec(char *exec) {
    char *p = exec;
    while (*p) {
        if (*p == '%' && p[1] && strchr("fFuUdDnNickvm", p[1])) {
            memmove(p, p + 2, strlen(p + 2) + 1);
        } else {
            p++;
        }
    }
    /* trim trailing whitespace */
    int len = strlen(exec);
    while (len > 0 && exec[len - 1] == ' ') exec[--len] = '\0';
}

/* ── .desktop scanner ───────────────────────────────────────── */
static void scan_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && app_count < MAX_APPS) {
        int len = strlen(ent->d_name);
        if (len < 9 || strcmp(ent->d_name + len - 8, ".desktop") != 0)
            continue;

        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", path, ent->d_name);

        FILE *f = fopen(fpath, "r");
        if (!f) continue;

        char name[MAX_NAME] = "";
        char exec[MAX_EXEC] = "";
        int  no_display = 0;
        int  in_entry = 0;
        char line[512];

        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';

            if (line[0] == '[') {
                in_entry = (strcmp(line, "[Desktop Entry]") == 0);
                continue;
            }
            if (!in_entry) continue;

            if (strncmp(line, "Name=", 5) == 0 && name[0] == '\0') {
                strncpy(name, line + 5, MAX_NAME - 1);
            } else if (strncmp(line, "Exec=", 5) == 0 && exec[0] == '\0') {
                strncpy(exec, line + 5, MAX_EXEC - 1);
            } else if (strncmp(line, "NoDisplay=true", 14) == 0) {
                no_display = 1;
            }
        }
        fclose(f);

        if (name[0] && exec[0] && !no_display) {
            clean_exec(exec);
            strncpy(apps[app_count].name, name, MAX_NAME - 1);
            strncpy(apps[app_count].exec, exec, MAX_EXEC - 1);
            app_count++;
        }
    }
    closedir(d);
}

static int cmp_app(const void *a, const void *b) {
    return strcasecmp(((const App *)a)->name, ((const App *)b)->name);
}

static void load_apps(void) {
    scan_dir("/usr/share/applications");

    const char *home = getenv("HOME");
    if (home) {
        char local[512];
        snprintf(local, sizeof(local), "%s/.local/share/applications", home);
        scan_dir(local);
    }
    qsort(apps, app_count, sizeof(App), cmp_app);
}

/* ── Filter ─────────────────────────────────────────────────── */
static void filter_apps(void) {
    filtered_count = 0;
    char lq[MAX_QUERY];
    str_tolower(lq, query, MAX_QUERY);

    for (int i = 0; i < app_count && filtered_count < MAX_APPS; i++) {
        char ln[MAX_NAME];
        str_tolower(ln, apps[i].name, MAX_NAME);
        if (lq[0] == '\0' || strstr(ln, lq)) {
            filtered[filtered_count++] = &apps[i];
        }
    }
    if (selected >= filtered_count) selected = filtered_count - 1;
    if (selected < 0) selected = 0;
}

/* ── Draw ───────────────────────────────────────────────────── */
static void draw(void) {
    int w = WIN_W, h = WIN_H;

    /* background */
    XftColor bg = xft_color(COL_BG);
    XftDrawRect(xft_draw, &bg, 0, 0, w, h);

    /* input box background */
    XftColor input_bg = xft_color(COL_INPUT_BG);
    XftDrawRect(xft_draw, &input_bg, PAD_X, PAD_Y, w - 2 * PAD_X, INPUT_H);

    /* prompt */
    XftColor prompt_col = xft_color(COL_PROMPT);
    XftDrawStringUtf8(xft_draw, &prompt_col, font,
                      PAD_X + 12, PAD_Y + INPUT_H / 2 + font->ascent / 2,
                      (FcChar8 *)PROMPT, strlen(PROMPT));

    /* query text */
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)PROMPT, strlen(PROMPT), &ext);
    int qx = PAD_X + 12 + ext.xOff + 4;

    XftColor fg = xft_color(COL_FG);
    if (query_len > 0) {
        XftDrawStringUtf8(xft_draw, &fg, font,
                          qx, PAD_Y + INPUT_H / 2 + font->ascent / 2,
                          (FcChar8 *)query, query_len);
        /* cursor after text */
        XftTextExtentsUtf8(dpy, font, (FcChar8 *)query, query_len, &ext);
        qx += ext.xOff;
    }

    /* blinking cursor (always shown for simplicity) */
    XftDrawRect(xft_draw, &fg, qx, PAD_Y + 10, 2, INPUT_H - 20);

    /* separator line */
    XftColor dim = xft_color(COL_DIM);
    XftDrawRect(xft_draw, &dim, PAD_X, PAD_Y + INPUT_H + 6, w - 2 * PAD_X, 1);

    /* items */
    int list_y = PAD_Y + INPUT_H + 14;
    int max_visible = (h - list_y - PAD_Y) / ITEM_H;

    /* adjust scroll so selected is visible */
    if (selected < scroll_off) scroll_off = selected;
    if (selected >= scroll_off + max_visible) scroll_off = selected - max_visible + 1;

    XftColor sel_bg   = xft_color(COL_SEL_BG);
    XftColor sel_fg_c = xft_color(COL_SEL_FG);

    for (int i = 0; i < max_visible && i + scroll_off < filtered_count; i++) {
        int idx = i + scroll_off;
        int iy = list_y + i * ITEM_H;

        if (idx == selected) {
            XftDrawRect(xft_draw, &sel_bg, PAD_X, iy, w - 2 * PAD_X, ITEM_H);
            XftDrawStringUtf8(xft_draw, &sel_fg_c, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)filtered[idx]->name,
                              strlen(filtered[idx]->name));
        } else {
            XftDrawStringUtf8(xft_draw, &fg, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)filtered[idx]->name,
                              strlen(filtered[idx]->name));
        }
    }

    /* scrollbar (if needed) */
    if (filtered_count > max_visible && max_visible > 0) {
        int bar_h = (max_visible * (h - list_y - PAD_Y)) / filtered_count;
        if (bar_h < 20) bar_h = 20;
        int bar_y = list_y + (scroll_off * (h - list_y - PAD_Y - bar_h))
                    / (filtered_count - max_visible);
        XftDrawRect(xft_draw, &dim, w - PAD_X - 4, bar_y, 3, bar_h);
    }

    /* count indicator */
    char count[32];
    snprintf(count, sizeof(count), "%d/%d", filtered_count, app_count);
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)count, strlen(count), &ext);
    XftDrawStringUtf8(xft_draw, &dim, font,
                      w - PAD_X - ext.xOff - 8,
                      PAD_Y + INPUT_H / 2 + font->ascent / 2,
                      (FcChar8 *)count, strlen(count));
}

/* ── Launch ─────────────────────────────────────────────────── */
static void launch(const char *exec_cmd) {
    if (fork() == 0) {
        setsid();
        /* close X connection in child */
        close(ConnectionNumber(dpy));
        execlp("/bin/sh", "sh", "-c", exec_cmd, NULL);
        _exit(1);
    }
}

/* ── Main ───────────────────────────────────────────────────── */
int main(void) {
    load_apps();
    filter_apps();

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "nebula-launcher: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, screen);
    cmap   = DefaultColormap(dpy, screen);

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    /* create override-redirect window (no WM decorations) */
    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask
                   | FocusChangeMask;
    swa.border_pixel = 0;

    unsigned long bg_pixel = 0;
    XftColor bg_c;
    XftColorAllocName(dpy, visual, cmap, COL_BG, &bg_c);
    bg_pixel = bg_c.pixel;
    swa.background_pixel = bg_pixel;

    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        (sw - WIN_W) / 2, (sh - WIN_H) / 2,
                        WIN_W, WIN_H, 2,
                        CopyFromParent, InputOutput, visual,
                        CWOverrideRedirect | CWEventMask
                        | CWBackPixel | CWBorderPixel,
                        &swa);

    /* border colour */
    XftColor border_c;
    XftColorAllocName(dpy, visual, cmap, COL_BORDER, &border_c);
    XSetWindowBorder(dpy, win, border_c.pixel);

    /* Set window class hints */
    XClassHint class_hint;
    class_hint.res_name  = "nebula-launcher";
    class_hint.res_class = "Nebula-Launcher";
    XSetClassHint(dpy, win, &class_hint);

    /* font */
    font = XftFontOpenName(dpy, screen, FONT_NAME);
    if (!font) {
        fprintf(stderr, "nebula-launcher: cannot load font '%s'\n", FONT_NAME);
        font = XftFontOpenName(dpy, screen, "fixed");
    }

    gc = XCreateGC(dpy, win, 0, NULL);
    xft_draw = XftDrawCreate(dpy, win, visual, cmap);

    XMapRaised(dpy, win);

    /* grab keyboard so we get all keys */
    for (int i = 0; i < 50; i++) {
        if (XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync,
                          CurrentTime) == GrabSuccess)
            break;
        usleep(10000);
    }

    XEvent ev;
    int running = 1;

    while (running && !XNextEvent(dpy, &ev)) {
        if (ev.type == Expose && ev.xexpose.count == 0) {
            draw();
        } else if (ev.type == KeyPress) {
            KeySym ks = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            int redraw = 0;

            if (ks == XK_Escape) {
                running = 0;
            } else if (ks == XK_Return || ks == XK_KP_Enter) {
                if (filtered_count > 0) {
                    launch(filtered[selected]->exec);
                }
                running = 0;
            } else if (ks == XK_Up || (ks == XK_k && (ev.xkey.state & ControlMask))) {
                if (selected > 0) { selected--; redraw = 1; }
            } else if (ks == XK_Down || (ks == XK_j && (ev.xkey.state & ControlMask))) {
                if (selected < filtered_count - 1) { selected++; redraw = 1; }
            } else if (ks == XK_BackSpace) {
                if (query_len > 0) {
                    query[--query_len] = '\0';
                    selected = 0;
                    scroll_off = 0;
                    filter_apps();
                    redraw = 1;
                }
            } else if (ks == XK_Tab) {
                /* Tab cycles down like rofi */
                if (filtered_count > 0) {
                    selected = (selected + 1) % filtered_count;
                    redraw = 1;
                }
            } else {
                /* regular character input */
                char buf[8];
                int n = XLookupString(&ev.xkey, buf, sizeof(buf), NULL, NULL);
                if (n > 0 && query_len + n < MAX_QUERY && buf[0] >= 32) {
                    memcpy(query + query_len, buf, n);
                    query_len += n;
                    query[query_len] = '\0';
                    selected = 0;
                    scroll_off = 0;
                    filter_apps();
                    redraw = 1;
                }
            }

            if (redraw) draw();
        } else if (ev.type == FocusOut) {
            /* close if we lose focus */
            running = 0;
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XftDrawDestroy(xft_draw);
    XftFontClose(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
