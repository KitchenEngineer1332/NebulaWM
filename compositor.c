#define GL_GLEXT_PROTOTYPES
#include "compositor.h"
#include "shaders.h"
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>

static Display *dpy;
static int screen;
static Window root;
static Window overlay;
static GLXContext ctx;
static GLXFBConfig *fbconfigs;
static int numfbconfigs;

static Window *stacking_order = NULL;
static int num_stacked = 0;

static void update_stacking_order() {
    Window root_ret, parent;
    Window *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, root, &root_ret, &parent, &children, &nchildren)) {
        if (stacking_order) XFree(stacking_order);
        stacking_order = children;
        num_stacked = nchildren;
    }
}

static int damage_event, damage_error;
static int composite_event, composite_error;
static int render_event, render_error;
static int shape_event, shape_error;

static WindowInfo *windows = NULL;

static PFNGLXBINDTEXIMAGEEXTPROC glXBindTexImageEXT_ptr = NULL;
static PFNGLXRELEASETEXIMAGEEXTPROC glXReleaseTexImageEXT_ptr = NULL;
static PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT_ptr = NULL;

static GLuint shader_program;
static GLint alpha_loc;

static void compile_shaders() {
    GLint success;
    char info_log[512];

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertex_shader_source, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vs, 512, NULL, info_log);
        fprintf(stderr, "Vertex shader compilation failed: %s\n", info_log);
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragment_shader_source, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fs, 512, NULL, info_log);
        fprintf(stderr, "Fragment shader compilation failed: %s\n", info_log);
    }

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vs);
    glAttachShader(shader_program, fs);
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program, 512, NULL, info_log);
        fprintf(stderr, "Shader program linking failed: %s\n", info_log);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    alpha_loc = glGetUniformLocation(shader_program, "alpha");
}

static WindowInfo *find_window(Window id) {
    for (WindowInfo *w = windows; w; w = w->next) {
        if (w->id == id) return w;
    }
    return NULL;
}

static void add_window(Window id) {
    if (find_window(id)) return;
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, id, &attr) || attr.class == InputOnly) return;

    WindowInfo *w = calloc(1, sizeof(WindowInfo));
    w->id = id;
    w->attr = attr;
    w->mapped = attr.map_state == IsViewable;
    w->opacity = w->mapped ? 1.0f : 0.0f;
    w->target_opacity = w->opacity;

    if (w->mapped) {
        w->damage = XDamageCreate(dpy, id, XDamageReportNonEmpty);
        
        if (id == root) {
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char *prop;
            Atom prop_root_pmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
            if (XGetWindowProperty(dpy, root, prop_root_pmap, 0, 1, False, XA_PIXMAP,
                                   &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
                w->pixmap = *(Pixmap *)prop;
                XFree(prop);
            } else {
                w->pixmap = XCompositeNameWindowPixmap(dpy, id);
            }
        } else {
            w->pixmap = XCompositeNameWindowPixmap(dpy, id);
        }
        
        if (w->pixmap) {
            int pixmap_attr[] = {
                GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                GLX_TEXTURE_FORMAT_EXT, (w->attr.depth == 32) ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
                None
            };
            w->glx_pixmap = glXCreatePixmap(dpy, fbconfigs[0], w->pixmap, pixmap_attr);
            glGenTextures(1, &w->texture);
            glBindTexture(GL_TEXTURE_2D, w->texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }

    if (!windows) {
        windows = w;
    } else {
        WindowInfo *last = windows;
        while (last->next) last = last->next;
        last->next = w;
    }
    update_stacking_order();
}

static void remove_window(Window id) {
    WindowInfo **prev = &windows;
    for (WindowInfo *w = windows; w; w = w->next) {
        if (w->id == id) {
            *prev = w->next;
            if (w->damage) XDamageDestroy(dpy, w->damage);
            if (w->glx_pixmap) glXDestroyPixmap(dpy, w->glx_pixmap);
            if (w->pixmap) XFreePixmap(dpy, w->pixmap);
            if (w->texture) glDeleteTextures(1, &w->texture);
            free(w);
            update_stacking_order();
            return;
        }
        prev = &w->next;
    }
}

static void paint_all() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shader_program);

    // Always paint root first
    WindowInfo *root_w = find_window(root);
    if (root_w && root_w->opacity > 0.0f && root_w->pixmap) {
        glBindTexture(GL_TEXTURE_2D, root_w->texture);
        if (glXBindTexImageEXT_ptr) {
            glXBindTexImageEXT_ptr(dpy, root_w->glx_pixmap, GLX_FRONT_LEFT_EXT, NULL);
        }
        glUniform1f(alpha_loc, root_w->opacity);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(root_w->attr.x, root_w->attr.y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(root_w->attr.x + root_w->attr.width, root_w->attr.y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(root_w->attr.x + root_w->attr.width, root_w->attr.y + root_w->attr.height);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(root_w->attr.x, root_w->attr.y + root_w->attr.height);
        glEnd();
        if (glXReleaseTexImageEXT_ptr) {
            glXReleaseTexImageEXT_ptr(dpy, root_w->glx_pixmap, GLX_FRONT_LEFT_EXT);
        }
    }

    // Paint children in stacking order
    if (stacking_order) {
        for (int i = 0; i < num_stacked; i++) {
            WindowInfo *w = find_window(stacking_order[i]);
            if (!w || w->opacity <= 0.0f || !w->pixmap || w->id == overlay || w->id == root) continue;

            glBindTexture(GL_TEXTURE_2D, w->texture);
            if (glXBindTexImageEXT_ptr) {
                glXBindTexImageEXT_ptr(dpy, w->glx_pixmap, GLX_FRONT_LEFT_EXT, NULL);
            }

            glUniform1f(alpha_loc, w->opacity);

            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(w->attr.x, w->attr.y);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(w->attr.x + w->attr.width, w->attr.y);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(w->attr.x + w->attr.width, w->attr.y + w->attr.height);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(w->attr.x, w->attr.y + w->attr.height);
            glEnd();

            if (glXReleaseTexImageEXT_ptr) {
                glXReleaseTexImageEXT_ptr(dpy, w->glx_pixmap, GLX_FRONT_LEFT_EXT);
            }
        }
    }

    glXSwapBuffers(dpy, overlay);
}

void init_compositor(Display *display, int scr) {
    dpy = display;
    screen = scr;
    root = RootWindow(dpy, screen);

    if (!XCompositeQueryExtension(dpy, &composite_event, &composite_error)) {
        fprintf(stderr, "Xcomposite extension not found\n");
        exit(1);
    }
    if (!XDamageQueryExtension(dpy, &damage_event, &damage_error)) {
        fprintf(stderr, "Xdamage extension not found\n");
        exit(1);
    }
    XRenderQueryExtension(dpy, &render_event, &render_error);
    XShapeQueryExtension(dpy, &shape_event, &shape_error);

    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);

    overlay = XCompositeGetOverlayWindow(dpy, root);
    
    // Make overlay window input-transparent
    XShapeCombineRectangles(dpy, overlay, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);

    XMapWindow(dpy, overlay);
    
    int attr[] = {
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT | GLX_PIXMAP_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DOUBLEBUFFER, True,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
        None
    };

    fbconfigs = glXChooseFBConfig(dpy, screen, attr, &numfbconfigs);
    if (!fbconfigs) {
        fprintf(stderr, "No suitable FB config found\n");
        exit(1);
    }

    XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, fbconfigs[0]);
    ctx = glXCreateContext(dpy, vi, NULL, True);
    glXMakeCurrent(dpy, overlay, ctx);
    XFree(vi);

    glXBindTexImageEXT_ptr = (PFNGLXBINDTEXIMAGEEXTPROC)glXGetProcAddressARB((const GLubyte *)"glXBindTexImageEXT");
    glXReleaseTexImageEXT_ptr = (PFNGLXRELEASETEXIMAGEEXTPROC)glXGetProcAddressARB((const GLubyte *)"glXReleaseTexImageEXT");
    glXSwapIntervalEXT_ptr = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");

    if (glXSwapIntervalEXT_ptr) {
        glXSwapIntervalEXT_ptr(dpy, overlay, 1);
    }

    compile_shaders();

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glViewport(0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    XSelectInput(dpy, root, SubstructureNotifyMask | ExposureMask | PropertyChangeMask);

    add_window(root);
    Window root_ret, parent;
    Window *children;
    unsigned int nchildren;
    XQueryTree(dpy, root, &root_ret, &parent, &children, &nchildren);
    for (unsigned int i = 0; i < nchildren; i++) {
        add_window(children[i]);
    }
    if (children) XFree(children);
}

static double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

void run_compositor() {
    XEvent ev;
    int redraw_needed = 1;
    struct pollfd pfd = { .fd = ConnectionNumber(dpy), .events = POLLIN };
    double last_time = get_time();

    while (1) {
        int animating = 0;
        for (WindowInfo *w = windows; w; w = w->next) {
            if (w->opacity != w->target_opacity) {
                animating = 1;
                break;
            }
        }

        if (!XPending(dpy)) {
            poll(&pfd, 1, animating ? 10 : -1);
        }

        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == CreateNotify) {
                add_window(ev.xcreatewindow.window);
            } else if (ev.type == DestroyNotify) {
                remove_window(ev.xdestroywindow.window);
                redraw_needed = 1;
            } else if (ev.type == MapNotify) {
                WindowInfo *w = find_window(ev.xmap.window);
                if (w) {
                    w->mapped = true;
                    w->target_opacity = 1.0f;
                    if (!w->pixmap) {
                        XWindowAttributes attr;
                        XGetWindowAttributes(dpy, w->id, &attr);
                        w->attr = attr;
                        w->damage = XDamageCreate(dpy, w->id, XDamageReportNonEmpty);
                        w->pixmap = XCompositeNameWindowPixmap(dpy, w->id);
                        int pixmap_attr[] = {
                            GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                            GLX_TEXTURE_FORMAT_EXT, (w->attr.depth == 32) ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
                            None
                        };
                        w->glx_pixmap = glXCreatePixmap(dpy, fbconfigs[0], w->pixmap, pixmap_attr);
                        glGenTextures(1, &w->texture);
                        glBindTexture(GL_TEXTURE_2D, w->texture);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    }
                    redraw_needed = 1;
                }
            } else if (ev.type == UnmapNotify) {
                WindowInfo *w = find_window(ev.xunmap.window);
                if (w) {
                    w->mapped = false;
                    w->target_opacity = 0.0f;
                    redraw_needed = 1;
                }
            } else if (ev.type == ConfigureNotify) {
                WindowInfo *w = find_window(ev.xconfigure.window);
                if (w) {
                    if (w->attr.width != ev.xconfigure.width || w->attr.height != ev.xconfigure.height) {
                        // Re-create pixmap on resize
                        if (w->glx_pixmap) glXDestroyPixmap(dpy, w->glx_pixmap);
                        if (w->pixmap) XFreePixmap(dpy, w->pixmap);
                        
                        w->attr.x = ev.xconfigure.x;
                        w->attr.y = ev.xconfigure.y;
                        w->attr.width = ev.xconfigure.width;
                        w->attr.height = ev.xconfigure.height;
                        
                        if (w->mapped) {
                            w->pixmap = XCompositeNameWindowPixmap(dpy, w->id);
                            int pixmap_attr[] = {
                                GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                                GLX_TEXTURE_FORMAT_EXT, (w->attr.depth == 32) ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
                                None
                            };
                            w->glx_pixmap = glXCreatePixmap(dpy, fbconfigs[0], w->pixmap, pixmap_attr);
                        }
                    } else {
                        w->attr.x = ev.xconfigure.x;
                        w->attr.y = ev.xconfigure.y;
                    }
                    redraw_needed = 1;
                }
                update_stacking_order();
            } else if (ev.type == damage_event + XDamageNotify) {
                XDamageNotifyEvent *dev = (XDamageNotifyEvent *)&ev;
                XDamageSubtract(dpy, dev->damage, None, None);
                redraw_needed = 1;
            } else if (ev.type == PropertyNotify && ev.xproperty.window == root) {
                Atom prop_root_pmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
                if (ev.xproperty.atom == prop_root_pmap) {
                    WindowInfo *w = find_window(root);
                    if (w) {
                        if (w->glx_pixmap) glXDestroyPixmap(dpy, w->glx_pixmap);
                        if (w->texture) glDeleteTextures(1, &w->texture);
                        w->pixmap = None;
                        w->glx_pixmap = None;
                        w->texture = 0;

                        Atom actual_type;
                        int actual_format;
                        unsigned long nitems, bytes_after;
                        unsigned char *prop;
                        if (XGetWindowProperty(dpy, root, prop_root_pmap, 0, 1, False, XA_PIXMAP,
                                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
                            w->pixmap = *(Pixmap *)prop;
                            XFree(prop);
                        }

                        if (w->pixmap) {
                            int pixmap_attr[] = {
                                GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                                GLX_TEXTURE_FORMAT_EXT, (w->attr.depth == 32) ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
                                None
                            };
                            w->glx_pixmap = glXCreatePixmap(dpy, fbconfigs[0], w->pixmap, pixmap_attr);
                            glGenTextures(1, &w->texture);
                            glBindTexture(GL_TEXTURE_2D, w->texture);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        }
                        redraw_needed = 1;
                    }
                }
            }
        }
        
        double current_time = get_time();
        double dt = current_time - last_time;
        last_time = current_time;

        animating = 0;
        for (WindowInfo *w = windows; w; w = w->next) {
            if (w->opacity != w->target_opacity) {
                animating = 1;
                float step = 5.0f * dt; // Fade in/out over 0.2s
                if (w->opacity < w->target_opacity) {
                    w->opacity += step;
                    if (w->opacity > w->target_opacity) w->opacity = w->target_opacity;
                } else {
                    w->opacity -= step;
                    if (w->opacity < w->target_opacity) w->opacity = w->target_opacity;
                }
            }

            if (!w->mapped && w->opacity <= 0.0f && w->pixmap) {
                if (w->damage) XDamageDestroy(dpy, w->damage);
                if (w->glx_pixmap) glXDestroyPixmap(dpy, w->glx_pixmap);
                XFreePixmap(dpy, w->pixmap);
                if (w->texture) glDeleteTextures(1, &w->texture);
                w->damage = None;
                w->glx_pixmap = None;
                w->pixmap = None;
                w->texture = 0;
            }
        }

        if (animating || redraw_needed) {
            paint_all();
            redraw_needed = 0;
        }
    }
}

void cleanup_compositor() {
    XCompositeReleaseOverlayWindow(dpy, overlay);
    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, ctx);
    XFree(fbconfigs);
}

static int x11_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    (void)e;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    
    XSetErrorHandler(x11_error_handler);
    
    init_compositor(display, DefaultScreen(display));
    run_compositor();
    cleanup_compositor();
    XCloseDisplay(display);
    return 0;
}
