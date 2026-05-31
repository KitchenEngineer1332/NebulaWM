#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "theme.h"

typedef struct Client Client;
struct Client {
    Window win;
    int x, y, w, h;
    int ws;
    int is_dock;
    int is_floating;
    Client *next;
    Client *prev;
};

Display *dpy;
Window root;
Window focused_win = 0;
Window last_focused[9] = {0};
int current_workspace = 0;
int layout_modes[9] = {0};

Client *clients = NULL;

/* Focus Stack for MRU */
Window focus_stack[1024];
int focus_stack_count = 0;

void remove_from_stack(Window w) {
    for (int i = 0; i < focus_stack_count; i++) {
        if (focus_stack[i] == w) {
            for (int j = i; j < focus_stack_count - 1; j++) {
                focus_stack[j] = focus_stack[j+1];
            }
            focus_stack_count--;
            break;
        }
    }
}

void add_to_stack(Window w) {
    remove_from_stack(w);
    if (focus_stack_count < 1024) {
        for (int i = focus_stack_count; i > 0; i--) {
            focus_stack[i] = focus_stack[i-1];
        }
        focus_stack[0] = w;
        focus_stack_count++;
    }
}

Client* find_client(Window w) {
    for (Client *c = clients; c; c = c->next) {
        if (c->win == w) return c;
    }
    return NULL;
}

void update_client_list() {
    Atom prop = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    int count = 0;
    for (Client *c = clients; c; c = c->next) count++;
    
    Window *wins = malloc(sizeof(Window) * count);
    int i = 0;
    for (Client *c = clients; c; c = c->next) wins[i++] = c->win;
    
    XChangeProperty(dpy, root, prop, XA_WINDOW, 32, PropModeReplace, (unsigned char *)wins, count);
    free(wins);
}

void add_client(Window w) {
    if (find_client(w)) return;
    
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) return;
    if (wa.override_redirect) return;

    Client *c = calloc(1, sizeof(Client));
    c->win = w;
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;

    // Check if dock or floating type
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    Atom utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    Atom toolbar = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    Atom splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    Atom type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    
    if (XGetWindowProperty(dpy, w, type_atom, 0, 1, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        Atom t = *(Atom *)prop;
        if (t == dock) c->is_dock = 1;
        if (t == dialog || t == utility || t == toolbar || t == splash) c->is_floating = 1;
        XFree(prop);
    }

    Window trans;
    if (XGetTransientForHint(dpy, w, &trans)) c->is_floating = 1;

    // Get workspace
    c->ws = -1;
    if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                           0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0) c->ws = *(unsigned long *)prop;
        XFree(prop);
    }

    if (c->ws == -1 && !c->is_dock) {
        c->ws = current_workspace;
        unsigned long ws = c->ws;
        XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ws, 1);
    }

    c->next = clients;
    if (clients) clients->prev = c;
    clients = c;
    
    if (!c->is_dock && wa.map_state == IsViewable) {
        add_to_stack(w);
    }
    update_client_list();
}

void remove_client(Window w) {
    Client *c = find_client(w);
    if (!c) return;

    if (c->prev) c->prev->next = c->next;
    if (c->next) c->next->prev = c->prev;
    if (c == clients) clients = c->next;

    free(c);
    remove_from_stack(w);
    update_client_list();
}

/* Prototypes */
void update_workspace_hints();
void set_focus(Window w);
void show_hide_windows();
void tile_windows(int ws);

/* Alt+Tab Switcher */
typedef struct {
    Window win;
    char name[256];
    int workspace;
} SwitcherItem;

SwitcherItem *switcher_items = NULL;
int switcher_count = 0;
int switcher_selected = 0;
Window switcher_win = 0;
int is_switching = 0;
XftFont *switcher_font = NULL;
XftDraw *switcher_draw = NULL;
Colormap switcher_cmap;
Visual *switcher_visual;
Pixmap switcher_buffer = 0;
XftDraw *switcher_buffer_draw = NULL;

XftColor s_bg, s_fg, s_sel_bg, s_sel_fg, s_dim, s_border;

void alloc_switcher_colors() {
    Theme t = load_theme();
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.bg, &s_bg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.fg, &s_fg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.sel_bg, &s_sel_bg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.sel_fg, &s_sel_fg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.dim, &s_dim);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.border, &s_border);
}

void free_switcher_colors() {
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_bg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_fg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_sel_bg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_sel_fg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_dim);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &s_border);
}

void draw_switcher() {
    if (!switcher_win) return;

    int ww = 600;
    int wh = 400;

    // Main background
    XftDrawRect(switcher_buffer_draw, &s_bg, 0, 0, ww, wh);
    
    // Header
    XftDrawRect(switcher_buffer_draw, &s_sel_bg, 0, 0, ww, 60);
    char title[] = "Switch Window";
    XftDrawStringUtf8(switcher_buffer_draw, &s_sel_fg, switcher_font, 20, 40, (FcChar8 *)title, strlen(title));

    int item_h = 45;
    int start_y = 80;
    int list_w = 280;

    for (int i = 0; i < switcher_count; i++) {
        int iy = start_y + i * item_h;
        if (i == switcher_selected) {
            XftDrawRect(switcher_buffer_draw, &s_sel_bg, 10, iy - 32, list_w - 20, item_h);
            XftDrawStringUtf8(switcher_buffer_draw, &s_sel_fg, switcher_font, 25, iy, (FcChar8 *)switcher_items[i].name, strlen(switcher_items[i].name));
        } else {
            XftDrawStringUtf8(switcher_buffer_draw, &s_fg, switcher_font, 25, iy, (FcChar8 *)switcher_items[i].name, strlen(switcher_items[i].name));
        }
        
        char ws_info[16];
        snprintf(ws_info, sizeof(ws_info), "WS %d", switcher_items[i].workspace + 1);
        XftDrawStringUtf8(switcher_buffer_draw, &s_dim, switcher_font, list_w - 60, iy, (FcChar8 *)ws_info, strlen(ws_info));
    }

    // Preview area (right side)
    int px = 300;
    int py = 80;
    int pw = ww - px - 20;
    int ph = wh - py - 20;

    XftDrawRect(switcher_buffer_draw, &s_dim, px, py, pw, ph);
    XftDrawRect(switcher_buffer_draw, &s_bg, px + 2, py + 2, pw - 4, ph - 4);
    
    if (switcher_count > 0 && switcher_selected < switcher_count) {
        char *name = switcher_items[switcher_selected].name;
        int ws = switcher_items[switcher_selected].workspace;
        
        XftDrawStringUtf8(switcher_buffer_draw, &s_sel_bg, switcher_font, px + 20, py + 40, (FcChar8 *)"Preview", 7);
        XftDrawRect(switcher_buffer_draw, &s_dim, px + 20, py + 50, pw - 40, 1);
        
        char truncated[64];
        strncpy(truncated, name, 63);
        truncated[63] = '\0';
        XftDrawStringUtf8(switcher_buffer_draw, &s_fg, switcher_font, px + 20, py + 85, (FcChar8 *)truncated, strlen(truncated));
        
        char ws_text[32];
        snprintf(ws_text, sizeof(ws_text), "On Workspace %d", ws + 1);
        XftDrawStringUtf8(switcher_buffer_draw, &s_dim, switcher_font, px + 20, py + 120, (FcChar8 *)ws_text, strlen(ws_text));

        // Window mockup
        int mx = px + 30;
        int my = py + 160;
        int mw = pw - 60;
        int mh = ph - 180;
        XftDrawRect(switcher_buffer_draw, &s_dim, mx, my, mw, mh);
        XftDrawRect(switcher_buffer_draw, &s_bg, mx + 1, my + 1, mw - 2, mh - 2);
        XftDrawRect(switcher_buffer_draw, &s_sel_bg, mx + 1, my + 1, mw - 2, 20);
    }

    XCopyArea(dpy, switcher_buffer, switcher_win, DefaultGC(dpy, DefaultScreen(dpy)), 0, 0, ww, wh, 0, 0);
}

void update_switcher_items() {
    if (switcher_items) free(switcher_items);
    switcher_items = NULL;
    switcher_count = 0;

    switcher_items = malloc(sizeof(SwitcherItem) * focus_stack_count);
    for (int i = 0; i < focus_stack_count; i++) {
        Window w = focus_stack[i];
        Client *c = find_client(w);
        if (!c) continue;

        char *name;
        if (XFetchName(dpy, w, &name) && name) {
            strncpy(switcher_items[switcher_count].name, name, 255);
            XFree(name);
        } else {
            strcpy(switcher_items[switcher_count].name, "Unnamed Window");
        }
        switcher_items[switcher_count].win = w;
        switcher_items[switcher_count].workspace = c->ws;
        switcher_count++;
    }
}

void start_switching() {
    update_switcher_items();
    if (switcher_count == 0) return;

    is_switching = 1;
    switcher_selected = (switcher_count > 1) ? 1 : 0;

    int sw = DisplayWidth(dpy, DefaultScreen(dpy));
    int sh = DisplayHeight(dpy, DefaultScreen(dpy));
    int ww = 600;
    int wh = 400;

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = 0;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

    switcher_visual = DefaultVisual(dpy, DefaultScreen(dpy));
    switcher_cmap = DefaultColormap(dpy, DefaultScreen(dpy));

    switcher_win = XCreateWindow(dpy, root, (sw - ww) / 2, (sh - wh) / 2, ww, wh, 2,
                                 CopyFromParent, InputOutput, switcher_visual,
                                 CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

    XSetWindowBorder(dpy, switcher_win, config_focused_color);
    
    if (!switcher_font) {
        switcher_font = XftFontOpenName(dpy, DefaultScreen(dpy), "monospace:size=12");
        if (!switcher_font) switcher_font = XftFontOpenName(dpy, DefaultScreen(dpy), "fixed");
    }

    alloc_switcher_colors();
    
    switcher_buffer = XCreatePixmap(dpy, switcher_win, ww, wh, DefaultDepth(dpy, DefaultScreen(dpy)));
    switcher_buffer_draw = XftDrawCreate(dpy, switcher_buffer, switcher_visual, switcher_cmap);

    XMapRaised(dpy, switcher_win);
    XGrabKeyboard(dpy, switcher_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    draw_switcher();
}

void stop_switching() {
    if (!is_switching) return;
    is_switching = 0;
    XUngrabKeyboard(dpy, CurrentTime);
    
    if (switcher_count > 0 && switcher_selected < switcher_count) {
        Window w = switcher_items[switcher_selected].win;
        int ws = switcher_items[switcher_selected].workspace;
        
        if (ws != current_workspace) {
            current_workspace = ws;
            update_workspace_hints();
            show_hide_windows();
        }
        set_focus(w);
    }

    if (switcher_buffer_draw) XftDrawDestroy(switcher_buffer_draw);
    if (switcher_buffer) XFreePixmap(dpy, switcher_buffer);
    XDestroyWindow(dpy, switcher_win);
    switcher_win = 0;
    switcher_buffer = 0;
    switcher_buffer_draw = NULL;
    free_switcher_colors();
}

void update_workspace_hints() {
    Atom cur = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    Atom num = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    Atom layout_atom = XInternAtom(dpy, "_NEBULA_CURRENT_LAYOUT", False);
    unsigned long n = 9;
    unsigned long c = current_workspace;
    unsigned long layout = layout_modes[current_workspace];
    XChangeProperty(dpy, root, num, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
    XChangeProperty(dpy, root, cur, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&c, 1);
    XChangeProperty(dpy, root, layout_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&layout, 1);
}

void raise_docks() {
    for (Client *c = clients; c; c = c->next) {
        if (c->is_dock) {
            XRaiseWindow(dpy, c->win);
        }
    }
}

void tile_windows(int ws) {
    if (layout_modes[ws] == 0) return;

    int count = 0;
    for (Client *c = clients; c; c = c->next) {
        if (!c->is_dock && !c->is_floating && c->ws == ws) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(dpy, c->win, &wa) && wa.map_state == IsViewable)
                count++;
        }
    }

    if (count > 0) {
        int sx = 0, sy = 0, sw, sh;
        int n;
        XineramaScreenInfo *info = NULL;
        if (XineramaIsActive(dpy) && (info = XineramaQueryScreens(dpy, &n))) {
            sx = info[0].x_org;
            sy = info[0].y_org;
            sw = info[0].width;
            sh = info[0].height;
            XFree(info);
        } else {
            int screen = DefaultScreen(dpy);
            sw = DisplayWidth(dpy, screen);
            sh = DisplayHeight(dpy, screen);
        }

        int wy = sy + config_bar_height;
        int wh = sh - config_bar_height;
        int bw = config_border_width;
        int gap = (config_wm_type == 1) ? 10 : 0;

        Client *ws_clients[count];
        int real_count = 0;
        for (Client *c = clients; c; c = c->next) {
            if (!c->is_dock && !c->is_floating && c->ws == ws) {
                XWindowAttributes wa;
                if (XGetWindowAttributes(dpy, c->win, &wa) && wa.map_state == IsViewable) {
                    if (real_count < count) ws_clients[real_count++] = c;
                }
            }
        }

        if (real_count == 1) {
            XMoveResizeWindow(dpy, ws_clients[0]->win, sx + gap, wy + gap, sw - 2*bw - 2*gap, wh - 2*bw - 2*gap);
        } else if (real_count > 1) {
            int master_w = sw / 2;
            XMoveResizeWindow(dpy, ws_clients[0]->win, sx + gap, wy + gap, master_w - 2*bw - 2*gap, wh - 2*bw - 2*gap);

            int stack_w = sw - master_w;
            int stack_h = wh / (real_count - 1);
            for (int i = 1; i < real_count; i++) {
                XMoveResizeWindow(dpy, ws_clients[i]->win, sx + master_w + gap, wy + (i - 1) * stack_h + gap, stack_w - 2*bw - 2*gap, stack_h - 2*bw - 2*gap);
            }
        }
    }
}

void set_focus(Window w) {
    if (focused_win && focused_win != root) {
        XSetWindowBorder(dpy, focused_win, config_unfocused_color);
    }
    if (w && w != root) {
        Client *c = find_client(w);
        if (c && !c->is_dock) {
            XSetWindowBorder(dpy, w, config_focused_color);
            XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
            XRaiseWindow(dpy, w);
            raise_docks();
            last_focused[current_workspace] = w;
            add_to_stack(w);
        }
    }
    focused_win = w;
}

void show_hide_windows() {
    for (Client *c = clients; c; c = c->next) {
        if (c->is_dock) continue;
        if (c->ws == current_workspace) XMapWindow(dpy, c->win);
        else XUnmapWindow(dpy, c->win);
    }

    if (last_focused[current_workspace]) {
        set_focus(last_focused[current_workspace]);
    } else {
        set_focus(root);
    }
}

void grab_keys() {
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Return")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("q")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("q")), config_modifier | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("d")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Delete")), ControlMask | Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    
    for (int i = 1; i <= 9; i++) {
        char key[2];
        snprintf(key, 2, "%d", i);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym(key)), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym(key)), config_modifier | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    }
    
    // Alt+Tab
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Tab")), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);

    // Layout/Floating toggle
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("f")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("f")), config_modifier | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

void grab_buttons() {
    XGrabButton(dpy, 1, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, 3, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
}

int xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    (void)ee;
    return 0;
}

int main() {
    XEvent ev;
    XWindowAttributes attr;
    XButtonEvent start;
    start.subwindow = None;

    load_config();

    for (int i = 0; i < 9; i++) {
        layout_modes[i] = (config_wm_type == 1) ? 0 : 1;
    }

    if (config_wallpaper[0] != '\0') {
        if (fork() == 0) {
            setsid();
            execlp("feh", "feh", "--bg-scale", config_wallpaper, NULL);
            exit(0);
        }
    }

    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "NebulaWM: cannot open display\n");
        exit(1);
    }

    XSetErrorHandler(xerror);

    root = DefaultRootWindow(dpy);

    grab_keys();
    grab_buttons();
    update_workspace_hints();

    // Initialize clients
    unsigned int n;
    Window root_return, parent_return, *children;
    if (XQueryTree(dpy, root, &root_return, &parent_return, &children, &n)) {
        for (unsigned int i = 0; i < n; i++) {
            add_client(children[i]);
        }
        if (children) XFree(children);
    }

    // Start the bar
    if (fork() == 0) {
        setsid();
        execlp("nebula-bar", "nebula-bar", NULL);
        _exit(1);
    }

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    int running = 1;
    while (running && !XNextEvent(dpy, &ev)) {
        if (ev.type == MapRequest) {
            Window w = ev.xmaprequest.window;
            add_client(w);
            Client *c = find_client(w);
            if (c) {
                XSelectInput(dpy, w, EnterWindowMask);
                if (!c->is_dock && c->y < config_bar_height) {
                    c->y = config_bar_height;
                    XMoveWindow(dpy, w, c->x, c->y);
                }
                XMapWindow(dpy, w);
                XSetWindowBorderWidth(dpy, w, config_border_width);
                set_focus(w);
                tile_windows(current_workspace);
            }
        } else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != root) {
                set_focus(ev.xcrossing.window);
            }
        } else if (ev.type == DestroyNotify) {
            remove_client(ev.xdestroywindow.window);
            if (ev.xdestroywindow.window == focused_win) {
                focused_win = 0;
            }
            for (int i = 0; i < 9; i++) {
                if (last_focused[i] == ev.xdestroywindow.window) {
                    last_focused[i] = 0;
                }
            }
            tile_windows(current_workspace);
        } else if (ev.type == UnmapNotify) {
            tile_windows(current_workspace);
        } else if (ev.type == ConfigureRequest) {
            XWindowChanges wc;
            wc.x = ev.xconfigurerequest.x;
            wc.y = ev.xconfigurerequest.y;
            wc.width = ev.xconfigurerequest.width;
            wc.height = ev.xconfigurerequest.height;
            wc.border_width = ev.xconfigurerequest.border_width;
            wc.sibling = ev.xconfigurerequest.above;
            wc.stack_mode = ev.xconfigurerequest.detail;
            XConfigureWindow(dpy, ev.xconfigurerequest.window, ev.xconfigurerequest.value_mask, &wc);
            
            Client *c = find_client(ev.xconfigurerequest.window);
            if (c) {
                c->x = wc.x; c->y = wc.y; c->w = wc.width; c->h = wc.height;
                if (layout_modes[c->ws]) tile_windows(c->ws);
            }
        } else if (ev.type == KeyPress) {
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            if (keysym == XStringToKeysym("Return")) {
                if (fork() == 0) {
                    setsid();
                    execlp("sh", "sh", "-c", config_terminal, NULL);
                    _exit(1);
                }
            } else if (keysym == XStringToKeysym("q")) {
                if (ev.xkey.state & ShiftMask) {
                    running = 0;
                } else if (focused_win) {
                    XKillClient(dpy, focused_win);
                }
            } else if (keysym == XStringToKeysym("d")) {
                if (fork() == 0) {
                    setsid();
                    execlp("sh", "sh", "-c", config_launcher, NULL);
                    _exit(1);
                }
            } else if (keysym == XStringToKeysym("Delete") && (ev.xkey.state & (ControlMask | Mod1Mask)) == (ControlMask | Mod1Mask)) {
                if (fork() == 0) {
                    setsid();
                    execlp("nebula-powermenu", "nebula-powermenu", NULL);
                    _exit(1);
                }
            } else if (keysym >= XK_1 && keysym <= XK_9) {
                int ws = keysym - XK_1;
                if (ev.xkey.state & ShiftMask) {
                    if (focused_win && focused_win != root) {
                        Client *c = find_client(focused_win);
                        if (c) {
                            c->ws = ws;
                            unsigned long wsv = ws;
                            XChangeProperty(dpy, focused_win, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&wsv, 1);
                            XUnmapWindow(dpy, focused_win);
                            tile_windows(current_workspace);
                            tile_windows(ws);
                        }
                    }
                } else {
                    current_workspace = ws;
                    update_workspace_hints();
                    show_hide_windows();
                    tile_windows(current_workspace);
                }
            } else if (keysym == XStringToKeysym("f")) {
                if (ev.xkey.state & ShiftMask) {
                    if (focused_win && focused_win != root) {
                        Client *c = find_client(focused_win);
                        if (c) {
                            c->is_floating ^= 1;
                            tile_windows(current_workspace);
                        }
                    }
                } else {
                    layout_modes[current_workspace] ^= 1;
                    update_workspace_hints();
                    tile_windows(current_workspace);
                }
            } else if (keysym == XStringToKeysym("Tab") && (ev.xkey.state & Mod1Mask)) {
                if (!is_switching) {
                    start_switching();
                } else {
                    switcher_selected = (switcher_selected + 1) % switcher_count;
                    draw_switcher();
                }
            }
        } else if (ev.type == KeyRelease) {
            if (is_switching) {
                KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
                if (keysym == XK_Alt_L || keysym == XK_Alt_R || keysym == XK_Meta_L || keysym == XK_Meta_R) {
                    stop_switching();
                }
            }
        } else if (ev.type == Expose) {
            if (is_switching && ev.xexpose.window == switcher_win) {
                draw_switcher();
            }
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.subwindow != None) {
                XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
                if (attr.override_redirect) continue;
                Client *c = find_client(ev.xbutton.subwindow);
                if (c && c->is_dock) continue;
                start = ev.xbutton;
                set_focus(ev.xbutton.subwindow);
            }
        } else if (ev.type == MotionNotify) {
            while (XCheckTypedEvent(dpy, MotionNotify, &ev));
            if (start.subwindow != None) {
                int xdiff = ev.xmotion.x_root - start.x_root;
                int ydiff = ev.xmotion.y_root - start.y_root;
                
                int new_x = attr.x + (start.button == 1 ? xdiff : 0);
                int new_y = attr.y + (start.button == 1 ? ydiff : 0);
                
                if (new_y < config_bar_height) new_y = config_bar_height;

                int new_w = attr.width + (start.button == 3 ? xdiff : 0);
                int new_h = attr.height + (start.button == 3 ? ydiff : 0);

                if (new_w < 1) new_w = 1;
                if (new_h < 1) new_h = 1;

                XMoveResizeWindow(dpy, start.subwindow, new_x, new_y, new_w, new_h);
                Client *c = find_client(start.subwindow);
                if (c) {
                    c->x = new_x; c->y = new_y; c->w = new_w; c->h = new_h;
                }
            }
        } else if (ev.type == ButtonRelease) {
            if (start.button == 1 && start.subwindow != None) {
                Client *c = find_client(start.subwindow);
                if (c && !c->is_floating && !c->is_dock && layout_modes[current_workspace]) {
                    int mx = ev.xbutton.x_root;
                    int my = ev.xbutton.y_root;
                    Client *target = NULL;
                    for (Client *t = clients; t; t = t->next) {
                        if (t != c && t->ws == current_workspace && !t->is_floating && !t->is_dock) {
                            if (mx >= t->x && mx <= t->x + t->w &&
                                my >= t->y && my <= t->y + t->h) {
                                target = t;
                                break;
                            }
                        }
                    }

                    if (target) {
                        // Remove c from list
                        if (c->prev) c->prev->next = c->next;
                        if (c->next) c->next->prev = c->prev;
                        if (c == clients) clients = c->next;

                        // Find master to decide split logic
                        Client *master = NULL;
                        for (Client *t = clients; t; t = t->next) {
                            if (t->ws == current_workspace && !t->is_floating && !t->is_dock) {
                                master = t;
                                break;
                            }
                        }

                        int insert_after = 0;
                        if (target == master) {
                            if (mx > target->x + target->w / 2) insert_after = 1;
                        } else {
                            if (my > target->y + target->h / 2) insert_after = 1;
                        }

                        if (insert_after) {
                            c->next = target->next;
                            c->prev = target;
                            if (target->next) target->next->prev = c;
                            target->next = c;
                        } else {
                            c->next = target;
                            c->prev = target->prev;
                            if (target->prev) target->prev->next = c;
                            else clients = c;
                            target->prev = c;
                        }
                    }
                    tile_windows(current_workspace);
                }
            }
            start.subwindow = None;
        } else if (ev.type == ClientMessage) {
            if (ev.xclient.message_type == XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False)) {
                if (ev.xclient.data.l[0] >= 0 && ev.xclient.data.l[0] < 9) {
                    current_workspace = ev.xclient.data.l[0];
                    update_workspace_hints();
                    show_hide_windows();
                }
            } else if (ev.xclient.message_type == XInternAtom(dpy, "_NET_WM_DESKTOP", False)) {
                int ws = ev.xclient.data.l[0];
                if (ws >= 0 && ws < 9) {
                    Client *c = find_client(ev.xclient.window);
                    if (c) {
                        c->ws = ws;
                        XChangeProperty(dpy, ev.xclient.window, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ws, 1);
                        if (ws != current_workspace) XUnmapWindow(dpy, ev.xclient.window);
                        else XMapWindow(dpy, ev.xclient.window);
                        tile_windows(ws);
                        tile_windows(current_workspace);
                    }
                }
            } else if (ev.xclient.message_type == XInternAtom(dpy, "_NEBULA_TOGGLE_LAYOUT", False)) {
                layout_modes[current_workspace] ^= 1;
                update_workspace_hints();
                tile_windows(current_workspace);
            }
        }
    }

    XCloseDisplay(dpy);
    return 0;
}
