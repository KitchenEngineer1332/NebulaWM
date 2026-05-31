#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "theme.h"

#define WIN_W         400
#define WIN_H         280
#define INPUT_H       48
#define ITEM_H        38
#define PAD_X         18
#define PAD_Y         12
#define FONT_NAME     "monospace:size=13"
#define PROMPT        "Power Menu"

typedef struct {
    char name[32];
    char exec[64];
} MenuOption;

static MenuOption options[] = {
    {"Lock", "nebula-lockscreen"},
    {"Logout", "killall nebulawm"},
    {"Restart", "systemctl reboot"},
    {"Shutdown", "systemctl poweroff"}
};
static int option_count = 4;
static int selected = 0;

static Display *dpy;
static int      screen;
static Window   win;
static Pixmap   pmap;
static GC       gc;
static XftFont *font;
static XftDraw *xft_draw;
static Colormap cmap;
static Visual  *visual;

typedef struct {
    XftColor bg, input_bg, sel_bg, fg, prompt, sel_fg, dim, border;
} ThemeColors;

static ThemeColors colors;

static XftColor get_xft_color(const char *hex) {
    XftColor c;
    XftColorAllocName(dpy, visual, cmap, hex, &c);
    return c;
}

static void update_colors(Theme t) {
    colors.bg       = get_xft_color(t.bg);
    colors.input_bg = get_xft_color(t.input_bg);
    colors.sel_bg   = get_xft_color(t.sel_bg);
    colors.fg       = get_xft_color(t.fg);
    colors.prompt   = get_xft_color(t.prompt);
    colors.sel_fg   = get_xft_color(t.sel_fg);
    colors.dim      = get_xft_color(t.dim);
    colors.border   = get_xft_color(t.border);
}

static void free_colors() {
    XftColorFree(dpy, visual, cmap, &colors.bg);
    XftColorFree(dpy, visual, cmap, &colors.input_bg);
    XftColorFree(dpy, visual, cmap, &colors.sel_bg);
    XftColorFree(dpy, visual, cmap, &colors.fg);
    XftColorFree(dpy, visual, cmap, &colors.prompt);
    XftColorFree(dpy, visual, cmap, &colors.sel_fg);
    XftColorFree(dpy, visual, cmap, &colors.dim);
    XftColorFree(dpy, visual, cmap, &colors.border);
}

static void draw(void) {
    int w = WIN_W, h = WIN_H;
    
    /* Clear Pixmap */
    XftDrawRect(xft_draw, &colors.bg, 0, 0, w, h);

    /* Draw Input Area */
    XftDrawRect(xft_draw, &colors.input_bg, PAD_X, PAD_Y, w - 2 * PAD_X, INPUT_H);
    XftDrawStringUtf8(xft_draw, &colors.prompt, font,
                      PAD_X + 12, PAD_Y + INPUT_H / 2 + font->ascent / 2,
                      (FcChar8 *)PROMPT, strlen(PROMPT));

    /* Separator */
    XftDrawRect(xft_draw, &colors.dim, PAD_X, PAD_Y + INPUT_H + 6, w - 2 * PAD_X, 1);

    /* Menu Items */
    int list_y = PAD_Y + INPUT_H + 14;
    for (int i = 0; i < option_count; i++) {
        int iy = list_y + i * ITEM_H;
        if (i == selected) {
            XftDrawRect(xft_draw, &colors.sel_bg, PAD_X, iy, w - 2 * PAD_X, ITEM_H);
            XftDrawStringUtf8(xft_draw, &colors.sel_fg, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)options[i].name,
                              strlen(options[i].name));
        } else {
            XftDrawStringUtf8(xft_draw, &colors.fg, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)options[i].name,
                              strlen(options[i].name));
        }
    }

    /* Copy Pixmap to Window */
    XCopyArea(dpy, pmap, win, gc, 0, 0, w, h, 0, 0);
    XFlush(dpy);
}

static void launch(const char *exec_cmd) {
    if (fork() == 0) {
        setsid();
        close(ConnectionNumber(dpy));
        execlp("/bin/sh", "sh", "-c", exec_cmd, NULL);
        _exit(1);
    }
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    screen = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, screen);
    cmap   = DefaultColormap(dpy, screen);

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    Theme t = load_theme();
    update_colors(t);

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.event_mask = ExposureMask | KeyPressMask | FocusChangeMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask;
    swa.background_pixel = colors.bg.pixel;
    swa.border_pixel = colors.border.pixel;

    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        (sw - WIN_W) / 2, (sh - WIN_H) / 2,
                        WIN_W, WIN_H, 2,
                        CopyFromParent, InputOutput, visual,
                        CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel,
                        &swa);

    XSetWindowBorder(dpy, win, colors.border.pixel);

    font = XftFontOpenName(dpy, screen, FONT_NAME);
    if (!font) font = XftFontOpenName(dpy, screen, "fixed");

    gc = XCreateGC(dpy, win, 0, NULL);
    pmap = XCreatePixmap(dpy, win, WIN_W, WIN_H, DefaultDepth(dpy, screen));
    xft_draw = XftDrawCreate(dpy, pmap, visual, cmap);

    XMapRaised(dpy, win);

    /* Grabs */
    for (int i = 0; i < 100; i++) {
        if (XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
            break;
        usleep(1000);
    }
    XGrabPointer(dpy, win, False, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, 
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    XEvent ev;
    int running = 1;

    while (running && !XNextEvent(dpy, &ev)) {
        if (ev.type == Expose && ev.xexpose.count == 0) {
            draw();
        } else if (ev.type == KeyPress) {
            KeySym ks = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            if (ks == XK_Escape) {
                running = 0;
            } else if (ks == XK_Return || ks == XK_KP_Enter) {
                launch(options[selected].exec);
                running = 0;
            } else if (ks == XK_Up || (ks == XK_k && (ev.xkey.state & ControlMask))) {
                if (selected > 0) { selected--; draw(); }
            } else if (ks == XK_Down || (ks == XK_j && (ev.xkey.state & ControlMask))) {
                if (selected < option_count - 1) { selected++; draw(); }
            }
        } else if (ev.type == FocusOut) {
            running = 0;
        } else if (ev.type == MotionNotify) {
            int mx = ev.xmotion.x;
            int my = ev.xmotion.y;
            int list_y = PAD_Y + INPUT_H + 14;
            if (mx >= PAD_X && mx <= WIN_W - PAD_X && my >= list_y && my < list_y + option_count * ITEM_H) {
                int new_sel = (my - list_y) / ITEM_H;
                if (new_sel != selected) {
                    selected = new_sel;
                    draw();
                }
            }
        } else if (ev.type == ButtonPress) {
            int mx = ev.xbutton.x;
            int my = ev.xbutton.y;
            int list_y = PAD_Y + INPUT_H + 14;
            
            if (mx < 0 || mx >= WIN_W || my < 0 || my >= WIN_H) {
                running = 0;
            } else if (mx >= PAD_X && mx <= WIN_W - PAD_X && my >= list_y && my < list_y + option_count * ITEM_H) {
                int new_sel = (my - list_y) / ITEM_H;
                selected = new_sel;
                draw(); // Update visual before launching
                launch(options[selected].exec);
                running = 0;
            }
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    XftDrawDestroy(xft_draw);
    XFreePixmap(dpy, pmap);
    XftFontClose(dpy, font);
    XFreeGC(dpy, gc);
    free_colors();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

