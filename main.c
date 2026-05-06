#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "config.h"

Display *dpy;
Window root;
Window focused_win = 0;
int current_workspace = 0;

void update_workspace_hints() {
    Atom cur = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    Atom num = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    unsigned long n = 9;
    unsigned long c = current_workspace;
    XChangeProperty(dpy, root, num, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
    XChangeProperty(dpy, root, cur, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&c, 1);
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
            ws = *(unsigned long *)prop;
            XFree(prop);
        }
        
        if (ws != -1) {
            if (ws == current_workspace) XMapWindow(dpy, children[i]);
            else XUnmapWindow(dpy, children[i]);
        }
    }
    if (children) XFree(children);
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
}

void grab_buttons() {
    XGrabButton(dpy, 1, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, 3, config_modifier, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
}

void set_focus(Window w) {
    if (focused_win && focused_win != root) {
        XSetWindowBorder(dpy, focused_win, config_unfocused_color);
    }
    if (w && w != root) {
        XSetWindowBorder(dpy, w, config_focused_color);
        XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
        XRaiseWindow(dpy, w);
    }
    focused_win = w;
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

    root = DefaultRootWindow(dpy);

    grab_keys();
    grab_buttons();
    update_workspace_hints();

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
            XMapWindow(dpy, w);
            XSetWindowBorderWidth(dpy, w, config_border_width);
            
            // Set workspace for new window
            unsigned long ws = current_workspace;
            XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ws, 1);
            
            set_focus(w);
        } else if (ev.type == EnterNotify) {
            if (ev.xcrossing.window != root) {
                set_focus(ev.xcrossing.window);
            }
        } else if (ev.type == DestroyNotify) {
            if (ev.xdestroywindow.window == focused_win) {
                focused_win = 0;
            }
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
                    }
                } else {
                    // Switch workspace
                    current_workspace = ws;
                    update_workspace_hints();
                    show_hide_windows();
                }
            }
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.subwindow != None) {
                XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
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
                int new_w = attr.width + (start.button == 3 ? xdiff : 0);
                int new_h = attr.height + (start.button == 3 ? ydiff : 0);

                if (new_w < 1) new_w = 1;
                if (new_h < 1) new_h = 1;

                XMoveResizeWindow(dpy, start.subwindow, new_x, new_y, new_w, new_h);
            }
        } else if (ev.type == ButtonRelease) {
            start.subwindow = None;
        }
    }

    XCloseDisplay(dpy);
    return 0;
}
