#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <stdbool.h>

typedef struct WindowInfo {
    Window id;
    XWindowAttributes attr;
    Damage damage;
    Pixmap pixmap;
    GLXPixmap glx_pixmap;
    GLuint texture;
    bool mapped;
    float opacity;
    float target_opacity;
    struct WindowInfo *next;
} WindowInfo;

void init_compositor(Display *dpy, int screen);
void run_compositor();
void cleanup_compositor();

#endif
