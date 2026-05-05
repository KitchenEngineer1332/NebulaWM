#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "config.h"

Display *dpy;
Window root;
Window focused_win = 0;

void grab_keys() {
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Return")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("q")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("c")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("d")), config_modifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("Delete")), ControlMask | Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
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
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    root = DefaultRootWindow(dpy);

    grab_keys();
    grab_buttons();

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    int running = 1;
    while (running && !XNextEvent(dpy, &ev)) {
        if (ev.type == MapRequest) {
            Window w = ev.xmaprequest.window;
            XSelectInput(dpy, w, EnterWindowMask);
            XMapWindow(dpy, w);
            XSetWindowBorderWidth(dpy, w, config_border_width);
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
                    execlp(config_terminal, config_terminal, NULL);
                    exit(0);
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
                    execlp(config_launcher, config_launcher, NULL);
                    exit(0);
                }
            } else if (keysym == XStringToKeysym("Delete") && (ev.xkey.state & (ControlMask | Mod1Mask)) == (ControlMask | Mod1Mask)) {
                if (fork() == 0) {
                    setsid();
                    execlp("nebula-powermenu", "nebula-powermenu", NULL);
                    exit(0);
                }
            }
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.subwindow != None) {
                XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
                start = ev.xbutton;
                set_focus(ev.xbutton.subwindow);
            }
        } else if (ev.type == MotionNotify) {
            if (start.subwindow != None) {
                int xdiff = ev.xbutton.x_root - start.x_root;
                int ydiff = ev.xbutton.y_root - start.y_root;
                
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
