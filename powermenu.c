#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIN_W         400
#define WIN_H         280
#define INPUT_H       48
#define ITEM_H        38
#define PAD_X         18
#define PAD_Y         12
#define FONT_NAME     "monospace:size=13"
#define PROMPT        "Power Menu"

#define COL_BG        "#1a1b26"
#define COL_INPUT_BG  "#24283b"
#define COL_SEL_BG    "#f7768e"
#define COL_FG        "#c0caf5"
#define COL_PROMPT    "#f7768e"
#define COL_SEL_FG    "#1a1b26"
#define COL_DIM       "#565f89"
#define COL_BORDER    "#f7768e"

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
static GC       gc;
static XftFont *font;
static XftDraw *xft_draw;
static Colormap cmap;
static Visual  *visual;

static XftColor xft_color(const char *hex) {
    XftColor c;
    XftColorAllocName(dpy, visual, cmap, hex, &c);
    return c;
}

static void draw(void) {
    int w = WIN_W, h = WIN_H;
    XftColor bg = xft_color(COL_BG);
    XftDrawRect(xft_draw, &bg, 0, 0, w, h);

    XftColor input_bg = xft_color(COL_INPUT_BG);
    XftDrawRect(xft_draw, &input_bg, PAD_X, PAD_Y, w - 2 * PAD_X, INPUT_H);

    XftColor prompt_col = xft_color(COL_PROMPT);
    XftDrawStringUtf8(xft_draw, &prompt_col, font,
                      PAD_X + 12, PAD_Y + INPUT_H / 2 + font->ascent / 2,
                      (FcChar8 *)PROMPT, strlen(PROMPT));

    XftColor dim = xft_color(COL_DIM);
    XftDrawRect(xft_draw, &dim, PAD_X, PAD_Y + INPUT_H + 6, w - 2 * PAD_X, 1);

    int list_y = PAD_Y + INPUT_H + 14;
    XftColor sel_bg   = xft_color(COL_SEL_BG);
    XftColor sel_fg_c = xft_color(COL_SEL_FG);
    XftColor fg = xft_color(COL_FG);

    for (int i = 0; i < option_count; i++) {
        int iy = list_y + i * ITEM_H;
        if (i == selected) {
            XftDrawRect(xft_draw, &sel_bg, PAD_X, iy, w - 2 * PAD_X, ITEM_H);
            XftDrawStringUtf8(xft_draw, &sel_fg_c, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)options[i].name,
                              strlen(options[i].name));
        } else {
            XftDrawStringUtf8(xft_draw, &fg, font,
                              PAD_X + 14, iy + ITEM_H / 2 + font->ascent / 2,
                              (FcChar8 *)options[i].name,
                              strlen(options[i].name));
        }
    }
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

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.event_mask = ExposureMask | KeyPressMask | FocusChangeMask | PointerMotionMask | ButtonPressMask;
    swa.border_pixel = 0;
    
    XftColor bg_c;
    XftColorAllocName(dpy, visual, cmap, COL_BG, &bg_c);
    swa.background_pixel = bg_c.pixel;

    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        (sw - WIN_W) / 2, (sh - WIN_H) / 2,
                        WIN_W, WIN_H, 2,
                        CopyFromParent, InputOutput, visual,
                        CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel,
                        &swa);

    XftColor border_c;
    XftColorAllocName(dpy, visual, cmap, COL_BORDER, &border_c);
    XSetWindowBorder(dpy, win, border_c.pixel);

    font = XftFontOpenName(dpy, screen, FONT_NAME);
    if (!font) font = XftFontOpenName(dpy, screen, "fixed");

    gc = XCreateGC(dpy, win, 0, NULL);
    xft_draw = XftDrawCreate(dpy, win, visual, cmap);

    XMapRaised(dpy, win);

    for (int i = 0; i < 50; i++) {
        if (XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
            break;
        usleep(10000);
    }
    XGrabPointer(dpy, win, True, PointerMotionMask | ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

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
                launch(options[selected].exec);
                running = 0;
            } else if (ks == XK_Up || (ks == XK_k && (ev.xkey.state & ControlMask))) {
                if (selected > 0) { selected--; redraw = 1; }
            } else if (ks == XK_Down || (ks == XK_j && (ev.xkey.state & ControlMask))) {
                if (selected < option_count - 1) { selected++; redraw = 1; }
            }

            if (redraw) draw();
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
                launch(options[selected].exec);
                running = 0;
            }
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    XftDrawDestroy(xft_draw);
    XftFontClose(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
