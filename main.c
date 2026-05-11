#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "theme.h"

Display *dpy;
Window root;
Window focused_win = 0;
Window last_focused[9] = {0};
int current_workspace = 0;
int layout_modes[9] = {0};

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

/* Prototypes */
void update_workspace_hints();
int is_dock(Window w);
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

void draw_switcher() {
    if (!switcher_win) return;

    int ww = 600;
    int wh = 400;

    Theme t = load_theme();
    XftColor bg, fg, sel_bg, sel_fg, dim, border;
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.bg, &bg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.fg, &fg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.sel_bg, &sel_bg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.sel_fg, &sel_fg);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.dim, &dim);
    XftColorAllocName(dpy, switcher_visual, switcher_cmap, t.border, &border);

    // Main background
    XftDrawRect(switcher_draw, &bg, 0, 0, ww, wh);
    
    // Header
    XftDrawRect(switcher_draw, &sel_bg, 0, 0, ww, 60);
    char title[] = "Switch Window";
    XftDrawStringUtf8(switcher_draw, &sel_fg, switcher_font, 20, 40, (FcChar8 *)title, strlen(title));

    int item_h = 45;
    int start_y = 80;
    int list_w = 280;

    for (int i = 0; i < switcher_count; i++) {
        int iy = start_y + i * item_h;
        if (i == switcher_selected) {
            XftDrawRect(switcher_draw, &sel_bg, 10, iy - 32, list_w - 20, item_h);
            XftDrawStringUtf8(switcher_draw, &sel_fg, switcher_font, 25, iy, (FcChar8 *)switcher_items[i].name, strlen(switcher_items[i].name));
        } else {
            XftDrawStringUtf8(switcher_draw, &fg, switcher_font, 25, iy, (FcChar8 *)switcher_items[i].name, strlen(switcher_items[i].name));
        }
        
        char ws_info[16];
        snprintf(ws_info, sizeof(ws_info), "WS %d", switcher_items[i].workspace + 1);
        XftDrawStringUtf8(switcher_draw, &dim, switcher_font, list_w - 60, iy, (FcChar8 *)ws_info, strlen(ws_info));
    }

    // Preview area (right side)
    int px = 300;
    int py = 80;
    int pw = ww - px - 20;
    int ph = wh - py - 20;

    XftDrawRect(switcher_draw, &dim, px, py, pw, ph);
    XftDrawRect(switcher_draw, &bg, px + 2, py + 2, pw - 4, ph - 4);
    
    if (switcher_count > 0 && switcher_selected < switcher_count) {
        char *name = switcher_items[switcher_selected].name;
        int ws = switcher_items[switcher_selected].workspace;
        
        XftDrawStringUtf8(switcher_draw, &sel_bg, switcher_font, px + 20, py + 40, (FcChar8 *)"Preview", 7);
        XftDrawRect(switcher_draw, &dim, px + 20, py + 50, pw - 40, 1);
        
        char truncated[64];
        strncpy(truncated, name, 63);
        truncated[63] = '\0';
        XftDrawStringUtf8(switcher_draw, &fg, switcher_font, px + 20, py + 85, (FcChar8 *)truncated, strlen(truncated));
        
        char ws_text[32];
        snprintf(ws_text, sizeof(ws_text), "On Workspace %d", ws + 1);
        XftDrawStringUtf8(switcher_draw, &dim, switcher_font, px + 20, py + 120, (FcChar8 *)ws_text, strlen(ws_text));

        // Window mockup
        int mx = px + 30;
        int my = py + 160;
        int mw = pw - 60;
        int mh = ph - 180;
        XftDrawRect(switcher_draw, &dim, mx, my, mw, mh);
        XftDrawRect(switcher_draw, &bg, mx + 1, my + 1, mw - 2, mh - 2);
        XftDrawRect(switcher_draw, &sel_bg, mx + 1, my + 1, mw - 2, 20);
    }

    XftColorFree(dpy, switcher_visual, switcher_cmap, &bg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &fg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &sel_bg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &sel_fg);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &dim);
    XftColorFree(dpy, switcher_visual, switcher_cmap, &border);
}

void update_switcher_items() {
    if (switcher_items) free(switcher_items);
    switcher_items = NULL;
    switcher_count = 0;

    // Use focus stack for MRU order
    switcher_items = malloc(sizeof(SwitcherItem) * focus_stack_count);
    for (int i = 0; i < focus_stack_count; i++) {
        Window w = focus_stack[i];
        XWindowAttributes wa;
        if (!XGetWindowAttributes(dpy, w, &wa)) continue;
        if (wa.map_state != IsViewable) continue;

        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        int ws = -1;
        if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                               0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            if (nitems > 0) ws = *(unsigned long *)prop;
            XFree(prop);
        }
        
        if (ws != -1) {
            char *name;
            if (XFetchName(dpy, w, &name) && name) {
                strncpy(switcher_items[switcher_count].name, name, 255);
                XFree(name);
            } else {
                strcpy(switcher_items[switcher_count].name, "Unnamed Window");
            }
            switcher_items[switcher_count].win = w;
            switcher_items[switcher_count].workspace = ws;
            switcher_count++;
        }
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

    switcher_draw = XftDrawCreate(dpy, switcher_win, switcher_visual, switcher_cmap);
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

    if (switcher_draw) XftDrawDestroy(switcher_draw);
    XDestroyWindow(dpy, switcher_win);
    switcher_win = 0;
    switcher_draw = NULL;
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

int is_dock(Window w) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    int dock_found = 0;
    
    if (XGetWindowProperty(dpy, w, type_atom, 0, 1, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0 && *(Atom *)prop == dock) dock_found = 1;
        XFree(prop);
    }
    return dock_found;
}

void raise_docks() {
    unsigned int n;
    Window root_return, parent_return, *children;
    XQueryTree(dpy, root, &root_return, &parent_return, &children, &n);
    for (unsigned int i = 0; i < n; i++) {
        if (is_dock(children[i])) {
            XRaiseWindow(dpy, children[i]);
        }
    }
    if (children) XFree(children);
}

void tile_windows(int ws) {
    if (layout_modes[ws] == 0) return;

    unsigned int n;
    Window root_return, parent_return, *children;
    XQueryTree(dpy, root, &root_return, &parent_return, &children, &n);

    int count = 0;
    Window *ws_windows = malloc(sizeof(Window) * n);
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes wa;
        XGetWindowAttributes(dpy, children[i], &wa);
        if (wa.override_redirect || is_dock(children[i])) continue;
        
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        int win_ws = -1;
        if (XGetWindowProperty(dpy, children[i], XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                               0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            if (nitems > 0) win_ws = *(unsigned long *)prop;
            XFree(prop);
        }
        if (win_ws == ws) {
            ws_windows[count++] = children[i];
        }
    }

    if (count > 0) {
        int screen = DefaultScreen(dpy);
        int sw = DisplayWidth(dpy, screen);
        int sh = DisplayHeight(dpy, screen);
        int wy = config_bar_height;
        int wh = sh - config_bar_height;
        int bw = config_border_width;

        if (count == 1) {
            XMoveResizeWindow(dpy, ws_windows[0], 0, wy, sw - 2*bw, wh - 2*bw);
        } else {
            int master_w = sw / 2;
            XMoveResizeWindow(dpy, ws_windows[0], 0, wy, master_w - 2*bw, wh - 2*bw);

            int stack_w = sw - master_w;
            int stack_h = wh / (count - 1);
            for (int i = 1; i < count; i++) {
                XMoveResizeWindow(dpy, ws_windows[i], master_w, wy + (i - 1) * stack_h, stack_w - 2*bw, stack_h - 2*bw);
            }
        }
    }
    free(ws_windows);
    if (children) XFree(children);
}

void set_focus(Window w) {
    if (focused_win && focused_win != root) {
        XSetWindowBorder(dpy, focused_win, config_unfocused_color);
    }
    if (w && w != root && !is_dock(w)) {
        XSetWindowBorder(dpy, w, config_focused_color);
        XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
        XRaiseWindow(dpy, w);
        raise_docks();
        last_focused[current_workspace] = w;
        add_to_stack(w);
    }
    focused_win = w;
}

void show_hide_windows() {
    unsigned int n;
    Window root_return, parent_return, *children;
    XQueryTree(dpy, root, &root_return, &parent_return, &children, &n);
    for (unsigned int i = 0; i < n; i++) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        int ws = -1;
        if (XGetWindowProperty(dpy, children[i], XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                               0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            if (nitems > 0) ws = *(unsigned long *)prop;
            XFree(prop);
        }
        
        if (ws != -1) {
            if (ws == current_workspace) XMapWindow(dpy, children[i]);
            else XUnmapWindow(dpy, children[i]);
        }
    }
    if (children) XFree(children);

    if (last_focused[current_workspace]) {
        set_focus(last_focused[current_workspace]);
    } else {
        set_focus(root);
    }
}

void grab_keys() {
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Return")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("q")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("c")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
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
}

void grab_buttons() {
    XGrabButton(dpy, 1, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, 3, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
}

int xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    (void)ee;
    // Ignore X errors to prevent the WM from crashing
    return 0;
}

int main() {
    XEvent ev;
    XWindowAttributes attr;
    XButtonEvent start;
    start.subwindow = None;

    load_config();

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

    // Initialize focus stack
    unsigned int n;
    Window root_return, parent_return, *children;
    if (XQueryTree(dpy, root, &root_return, &parent_return, &children, &n)) {
        for (unsigned int i = 0; i < n; i++) {
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, children[i], &wa);
            if (!wa.override_redirect && !is_dock(children[i]) && wa.map_state == IsViewable) {
                add_to_stack(children[i]);
            }
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
            XSelectInput(dpy, w, EnterWindowMask);
            
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, w, &wa);
            
            if (!is_dock(w)) {
                if (wa.y < config_bar_height) {
                    XMoveWindow(dpy, w, wa.x, config_bar_height);
                }
            }

            XMapWindow(dpy, w);
            XSetWindowBorderWidth(dpy, w, config_border_width);
            
            // Set workspace for new window
            unsigned long ws = current_workspace;
            XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ws, 1);
            
            set_focus(w);
            tile_windows(current_workspace);
        } else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != root) {
                set_focus(ev.xcrossing.window);
            }
        } else if (ev.type == DestroyNotify) {
            remove_from_stack(ev.xdestroywindow.window);
            if (ev.xdestroywindow.window == focused_win) {
                focused_win = 0;
            }
            for (int i = 0; i < 9; i++) {
                if (last_focused[i] == ev.xdestroywindow.window) {
                    last_focused[i] = 0;
                }
            }
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
        } else if (ev.type == KeyPress) {
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            if (keysym == XStringToKeysym("Return")) {
                if (fork() == 0) {
                    setsid();
                    execlp("sh", "sh", "-c", config_terminal, NULL);
                    _exit(1);
                }
            } else if (keysym == XStringToKeysym("q")) {
                running = 0;
            } else if (keysym == XStringToKeysym("c")) {
                if (focused_win) {
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
                    // Move focused window to workspace
                    if (focused_win && focused_win != root) {
                        unsigned long wsv = ws;
                        XChangeProperty(dpy, focused_win, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&wsv, 1);
                        XUnmapWindow(dpy, focused_win);
                        tile_windows(current_workspace);
                        tile_windows(ws);
                    }
                } else {
                    // Switch workspace
                    current_workspace = ws;
                    update_workspace_hints();
                    show_hide_windows();
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
                if (attr.override_redirect || is_dock(ev.xbutton.subwindow)) continue;
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
            }
        } else if (ev.type == ButtonRelease) {
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
                    XChangeProperty(dpy, ev.xclient.window, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ws, 1);
                    if (ws != current_workspace) XUnmapWindow(dpy, ev.xclient.window);
                    else XMapWindow(dpy, ev.xclient.window);
                    tile_windows(ws);
                    tile_windows(current_workspace);
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
