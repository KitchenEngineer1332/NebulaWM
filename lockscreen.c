#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <security/pam_appl.h>
#include "theme.h"

#define MAX_PASS_LEN 256

char config_font[256] = "monospace:size=32";
char config_date_font[256] = "monospace:size=16";
char config_background_type[32] = "color";
char config_background_image[512] = "";
int config_blur_radius = 10;
int config_pos_x = -1; // -1 means center
int config_pos_y = -1;

static Display *dpy;
static int screen;
static Window win;
static Pixmap pmap;
static GC gc;
static XftDraw *xft_draw;
static XftFont *font, *date_font;
static Colormap cmap;
static Visual *visual;
static Imlib_Image bg_image = NULL;

static char password[MAX_PASS_LEN];
static int pass_len = 0;
static int auth_failed = 0;
static int pass_hidden = 0;

static Theme lock_theme;
static XftColor color_bg, color_fg, color_indicator;

static XftColor xft_color(const char *hex) {
    XftColor c;
    XftColorAllocName(dpy, visual, cmap, hex, &c);
    return c;
}

static void load_config(void) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.config/Nebula/lockscreen.conf", pw->pw_dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            val[strcspn(val, "\r\n")] = 0;
            if (strcmp(key, "font") == 0) {
                strncpy(config_font, val, sizeof(config_font) - 1);
                config_font[sizeof(config_font) - 1] = '\0';
            } else if (strcmp(key, "date_font") == 0) {
                strncpy(config_date_font, val, sizeof(config_date_font) - 1);
                config_date_font[sizeof(config_date_font) - 1] = '\0';
            } else if (strcmp(key, "background_type") == 0) {
                strncpy(config_background_type, val, sizeof(config_background_type) - 1);
                config_background_type[sizeof(config_background_type) - 1] = '\0';
            } else if (strcmp(key, "background_image") == 0) {
                strncpy(config_background_image, val, sizeof(config_background_image) - 1);
                config_background_image[sizeof(config_background_image) - 1] = '\0';
            }
            else if (strcmp(key, "blur_radius") == 0) config_blur_radius = atoi(val);
            else if (strcmp(key, "pos_x") == 0 || strcmp(key, "clock_pos_x") == 0) {
                if (strcmp(val, "center") == 0) config_pos_x = -1;
                else config_pos_x = atoi(val);
            } else if (strcmp(key, "pos_y") == 0 || strcmp(key, "clock_pos_y") == 0) {
                if (strcmp(val, "center") == 0) config_pos_y = -1;
                else config_pos_y = atoi(val);
            }
        }
    }
    fclose(f);
}

static int pam_conv_func(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr) {
    struct pam_response *reply = calloc(num_msg, sizeof(struct pam_response));
    if (!reply) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            reply[i].resp = strdup((const char *)appdata_ptr);
            reply[i].resp_retcode = 0;
        }
    }
    *resp = reply;
    return PAM_SUCCESS;
}

static int authenticate(void) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return 0;

    const struct pam_conv local_conversation = { pam_conv_func, password };
    pam_handle_t *local_auth_handle = NULL;
    int retval;

    retval = pam_start("login", pw->pw_name, &local_conversation, &local_auth_handle);
    if (retval != PAM_SUCCESS) return 0;

    retval = pam_authenticate(local_auth_handle, 0);
    pam_end(local_auth_handle, retval);

    return (retval == PAM_SUCCESS);
}

static void draw(void) {
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    if (bg_image) {
        imlib_context_set_image(bg_image);
        imlib_context_set_drawable(pmap);
        imlib_render_image_on_drawable(0, 0);
    } else {
        XftDrawRect(xft_draw, &color_bg, 0, 0, sw, sh);
    }

    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    char time_str[64];
    char date_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    strftime(date_str, sizeof(date_str), "%A, %d %B %Y", timeinfo);

    XGlyphInfo time_extents, date_extents;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)time_str, strlen(time_str), &time_extents);
    XftTextExtentsUtf8(dpy, date_font, (FcChar8 *)date_str, strlen(date_str), &date_extents);

    int tx = config_pos_x == -1 ? (sw - time_extents.width) / 2 : config_pos_x;
    int ty = config_pos_y == -1 ? (sh / 2) - 50 : config_pos_y;

    int dx = config_pos_x == -1 ? (sw - date_extents.width) / 2 : config_pos_x;
    int dy = ty + 40;

    XftDrawStringUtf8(xft_draw, &color_fg, font, tx, ty, (FcChar8 *)time_str, strlen(time_str));
    XftDrawStringUtf8(xft_draw, &color_fg, date_font, dx, dy, (FcChar8 *)date_str, strlen(date_str));

    int ind_y = dy + 60;
    if (auth_failed == 1) {
        char *fail_msg = "Wrong password";
        XGlyphInfo fail_ext;
        XftTextExtentsUtf8(dpy, date_font, (FcChar8 *)fail_msg, strlen(fail_msg), &fail_ext);
        XftDrawStringUtf8(xft_draw, &color_indicator, date_font, (sw - fail_ext.width) / 2, ind_y, (FcChar8 *)fail_msg, strlen(fail_msg));
    } else if (pass_len > 0 && !pass_hidden) {
        int total_w = pass_len * 15;
        int ind_x = (sw - total_w) / 2;
        for (int i = 0; i < pass_len; i++) {
            XftDrawStringUtf8(xft_draw, &color_indicator, date_font, ind_x + i * 15, ind_y, (FcChar8 *)"*", 1);
        }
    }

    XCopyArea(dpy, pmap, win, gc, 0, 0, sw, sh, 0, 0);
}

int main(void) {
    load_config();

    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    screen = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.event_mask = ExposureMask | KeyPressMask;
    
    lock_theme = load_theme();
    color_bg = xft_color(lock_theme.bg_hex);
    color_fg = xft_color(lock_theme.fg_hex);
    color_indicator = xft_color(lock_theme.indicator_hex);

    swa.background_pixel = color_bg.pixel;

    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        0, 0, sw, sh, 0,
                        CopyFromParent, InputOutput, visual,
                        CWOverrideRedirect | CWEventMask | CWBackPixel,
                        &swa);

    pmap = XCreatePixmap(dpy, win, sw, sh, DefaultDepth(dpy, screen));
    gc = XCreateGC(dpy, win, 0, NULL);

    font = XftFontOpenName(dpy, screen, config_font);
    if (!font) font = XftFontOpenName(dpy, screen, "fixed");

    date_font = XftFontOpenName(dpy, screen, config_date_font);
    if (!date_font) date_font = XftFontOpenName(dpy, screen, "fixed");

    xft_draw = XftDrawCreate(dpy, pmap, visual, cmap);

    imlib_context_set_display(dpy);
    imlib_context_set_visual(visual);
    imlib_context_set_colormap(cmap);

    if (strcmp(config_background_type, "image") == 0 && strlen(config_background_image) > 0) {
        bg_image = imlib_load_image(config_background_image);
        if (bg_image) {
            imlib_context_set_image(bg_image);
            int w = imlib_image_get_width();
            int h = imlib_image_get_height();
            Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, w, h, sw, sh);
            imlib_free_image();
            bg_image = scaled;
        }
    } else if (strcmp(config_background_type, "blur") == 0) {
        imlib_context_set_drawable(RootWindow(dpy, screen));
        bg_image = imlib_create_image_from_drawable(0, 0, 0, sw, sh, 1);
        if (bg_image) {
            imlib_context_set_image(bg_image);
            imlib_image_blur(config_blur_radius);
        }
    }

    XMapRaised(dpy, win);
    XSync(dpy, False);

    // Grab keyboard and pointer completely from the root window
    int grabbed = 0;
    for (int i = 0; i < 1000; i++) {
        if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
            grabbed = 1;
            break;
        }
        usleep(10000);
    }
    if (!grabbed) {
        fprintf(stderr, "Cannot grab keyboard\n");
        return 1;
    }
    XGrabPointer(dpy, DefaultRootWindow(dpy), True, 0, GrabModeAsync, GrabModeAsync, win, None, CurrentTime);

    // Cursor for pointer to indicate locking? A blank cursor would be better, but we leave it as default or grabbed.

    int running = 1;
    int x11_fd = ConnectionNumber(dpy);
    XEvent ev;

    while (running) {
        int needs_draw = 0;
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
                needs_draw = 1;
            } else if (ev.type == KeyPress) {
                auth_failed = 0;
                char buf[32];
                KeySym ks;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

                if (ks == XK_Return || ks == XK_KP_Enter) {
                    password[pass_len] = '\0';
                    if (authenticate()) {
                        running = 0;
                    } else {
                        // blink 3 times
                        for (int i = 0; i < 3; i++) {
                            pass_hidden = 1;
                            draw();
                            XFlush(dpy);
                            usleep(150000);
                            pass_hidden = 0;
                            draw();
                            XFlush(dpy);
                            usleep(150000);
                        }
                        auth_failed = 1;
                        pass_len = 0;
                        memset(password, 0, sizeof(password));
                    }
                } else if (ks == XK_Escape) {
                    pass_len = 0;
                    memset(password, 0, sizeof(password));
                } else if (ks == XK_BackSpace) {
                    if (pass_len > 0) pass_len--;
                } else if (len > 0 && pass_len < MAX_PASS_LEN - 1) {
                    // Only printable characters
                    if (buf[0] >= 32 && buf[0] <= 126) {
                        password[pass_len++] = buf[0];
                    }
                }
                needs_draw = 1;
            }
        }

        static time_t last_t = 0;
        time_t now = time(NULL);
        if (now != last_t) {
            needs_draw = 1;
            last_t = now;
        }

        if (needs_draw) {
            draw();
            XFlush(dpy);
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        fd_set in_fds;
        FD_ZERO(&in_fds);
        FD_SET(x11_fd, &in_fds);

        select(x11_fd + 1, &in_fds, NULL, NULL, &tv);
    }

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    memset(password, 0, sizeof(password)); // Clear memory

    if (bg_image) {
        imlib_context_set_image(bg_image);
        imlib_free_image();
    }

    XftColorFree(dpy, visual, cmap, &color_bg);
    XftColorFree(dpy, visual, cmap, &color_fg);
    XftColorFree(dpy, visual, cmap, &color_indicator);

    XftDrawDestroy(xft_draw);
    XftFontClose(dpy, font);
    XftFontClose(dpy, date_font);
    XFreePixmap(dpy, pmap);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    return 0;
}
