#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>
#include <wchar.h>
#include "theme.h"
#include <stdint.h>

#define SCROLLBACK 2048
#define MAX_ESC 64
#define TAB_WIDTH 8

enum { ATTR_BOLD=1, ATTR_UNDERLINE=2, ATTR_REVERSE=4 };

typedef struct { char ch[4]; uint8_t len; uint8_t fg, bg, attr; } Cell;

static Display *dpy;
static int scr;
static Window win;
static Pixmap buf;
static XftDraw *xdraw;
static XftFont *fnt;
static Visual *vis;
static Colormap cmap;
static GC xgc;
static int master_fd = -1;
static pid_t child_pid;

static int cols, rows, cw, ch;
static int cx, cy; /* cursor pos */
static int save_cx, save_cy;
static int scroll_top, scroll_bot;
static int win_w, win_h;

static Cell *screen_buf;
static Cell *scroll_buf;
static int scroll_lines = 0;
static int scroll_off = 0;

static XftColor colors[18]; /* 0-15 ansi, 16=fg, 17=bg */
static int cur_fg = 16, cur_bg = 17, cur_attr = 0;

static char esc_buf[MAX_ESC];
static int esc_len = 0;
static int esc_state = 0; /* 0=normal, 1=ESC, 2=CSI, 3=OSC */

static int running = 1;
static int dirty = 1;

static void init_colors(void) {
    Theme t = load_theme();
    unsigned long p = 0;
    if (t.border[0]=='#') p = strtoul(t.border+1,NULL,16);
    unsigned long bk = 0x101014;
    char hex[10];
    /* ANSI 0-7: dark variants */
    blend_color(p, bk, 0.10f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[0]);
    blend_color(0xf7768e, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[1]);
    blend_color(0x9ece6a, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[2]);
    blend_color(0xe0af68, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[3]);
    blend_color(p, bk, 0.70f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[4]);
    blend_color(0xbb9af7, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[5]);
    blend_color(0x7dcfff, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[6]);
    blend_color(0xc0caf5, bk, 0.85f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[7]);
    /* ANSI 8-15: bright */
    blend_color(p, bk, 0.30f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[8]);
    blend_color(0xf7768e, 0xffffff, 0.90f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[9]);
    blend_color(0x9ece6a, 0xffffff, 0.90f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[10]);
    blend_color(0xe0af68, 0xffffff, 0.90f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[11]);
    blend_color(p, 0xffffff, 0.70f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[12]);
    blend_color(0xbb9af7, 0xffffff, 0.90f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[13]);
    blend_color(0x7dcfff, 0xffffff, 0.90f, hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[14]);
    XftColorAllocName(dpy,vis,cmap,"#c0caf5",&colors[15]);
    XftColorAllocName(dpy,vis,cmap,t.fg,&colors[16]);
    XftColorAllocName(dpy,vis,cmap,t.bg,&colors[17]);
}

static Cell blank_cell(void) {
    Cell c;
    c.ch[0]=' '; c.ch[1]=0; c.ch[2]=0; c.ch[3]=0;
    c.len=1; c.fg=(uint8_t)cur_fg; c.bg=(uint8_t)cur_bg; c.attr=0;
    return c;
}

static Cell* cell_at(int x, int y) { return &screen_buf[y*cols+x]; }

static void clear_region(int x1,int y1,int x2,int y2) {
    Cell b = blank_cell();
    for (int y=y1;y<=y2;y++)
        for (int x=x1;x<=x2;x++)
            if (y>=0&&y<rows&&x>=0&&x<cols) screen_buf[y*cols+x]=b;
}

static void scroll_up(int top, int bot) {
    if (top==0) {
        int idx = scroll_lines % SCROLLBACK;
        memcpy(&scroll_buf[idx*cols], &screen_buf[top*cols], cols*sizeof(Cell));
        scroll_lines++;
    }
    for (int y=top; y<bot; y++)
        memcpy(&screen_buf[y*cols], &screen_buf[(y+1)*cols], cols*sizeof(Cell));
    Cell b = blank_cell();
    for (int x=0;x<cols;x++) screen_buf[bot*cols+x]=b;
}

static void scroll_down(int top, int bot) {
    for (int y=bot; y>top; y--)
        memcpy(&screen_buf[y*cols], &screen_buf[(y-1)*cols], cols*sizeof(Cell));
    Cell b = blank_cell();
    for (int x=0;x<cols;x++) screen_buf[top*cols+x]=b;
}

static void newline(void) {
    if (cy >= scroll_bot) scroll_up(scroll_top, scroll_bot);
    else cy++;
}

static void put_char(const char *utf8, int len) {
    if (cx >= cols) { cx=0; newline(); }
    Cell *c = cell_at(cx, cy);
    memcpy(c->ch, utf8, len);
    c->len = len;
    c->fg = cur_fg; c->bg = cur_bg; c->attr = cur_attr;
    cx++;
}

static void draw(void) {
    XftDrawRect(xdraw, &colors[17], 0, 0, win_w, win_h);
    Cell b_cell = blank_cell();
    for (int y=0; y<rows; y++) {
        for (int x=0; x<cols; x++) {
            Cell *c;
            if (scroll_off > 0) {
                long abs_y = (long)scroll_lines - scroll_off + y;
                if (abs_y < scroll_lines) {
                    if (abs_y >= 0 && abs_y >= (long)scroll_lines - SCROLLBACK) {
                        c = &scroll_buf[(abs_y % SCROLLBACK)*cols + x];
                    } else c = &b_cell;
                } else {
                    int sy = abs_y - scroll_lines;
                    if (sy >= 0 && sy < rows) c = &screen_buf[sy*cols + x];
                    else c = &b_cell;
                }
            } else {
                c = cell_at(x, y);
            }
            int fg_i = c->fg, bg_i = c->bg;
            if (c->attr & ATTR_REVERSE) { int t=fg_i; fg_i=bg_i; bg_i=t; }
            if (bg_i != 17)
                XftDrawRect(xdraw, &colors[bg_i], x*cw, y*ch, cw, ch);
            if (c->len > 0 && c->ch[0] != ' ') {
                XftColor *fc = &colors[fg_i];
                if (c->attr & ATTR_BOLD && fg_i < 8) fc = &colors[fg_i+8];
                XftDrawStringUtf8(xdraw, fc, fnt, x*cw, y*ch+fnt->ascent,
                    (FcChar8*)c->ch, c->len);
            }
            if (c->attr & ATTR_UNDERLINE)
                XftDrawRect(xdraw, &colors[fg_i], x*cw, y*ch+ch-1, cw, 1);
        }
    }
    /* cursor */
    if (scroll_off == 0) {
        int cxp = cx < cols ? cx : cols-1;
        XftDrawRect(xdraw, &colors[16], cxp*cw, cy*ch, cw, ch);
        Cell *cc = cell_at(cxp, cy);
        if (cc->len > 0)
            XftDrawStringUtf8(xdraw, &colors[17], fnt, cxp*cw, cy*ch+fnt->ascent,
                (FcChar8*)cc->ch, cc->len);
    }
    XCopyArea(dpy, buf, win, xgc, 0, 0, win_w, win_h, 0, 0);
}

static int parse_params(const char *s, int *params, int max) {
    int n=0;
    while (*s && n<max) {
        if (*s==';') { n++; s++; continue; }
        if (isdigit(*s)) { params[n]=params[n]*10+(*s-'0'); s++; }
        else break;
    }
    return n+1;
}

static void handle_sgr(void) {
    int params[16]={0}; int np=parse_params(esc_buf,params,16);
    for (int i=0;i<np;i++) {
        int p=params[i];
        if (p==0) { cur_fg=16; cur_bg=17; cur_attr=0; }
        else if (p==1) cur_attr|=ATTR_BOLD;
        else if (p==4) cur_attr|=ATTR_UNDERLINE;
        else if (p==7) cur_attr|=ATTR_REVERSE;
        else if (p==22) cur_attr&=~ATTR_BOLD;
        else if (p==24) cur_attr&=~ATTR_UNDERLINE;
        else if (p==27) cur_attr&=~ATTR_REVERSE;
        else if (p>=30&&p<=37) cur_fg=p-30;
        else if (p==39) cur_fg=16;
        else if (p>=40&&p<=47) cur_bg=p-40;
        else if (p==49) cur_bg=17;
        else if (p>=90&&p<=97) cur_fg=p-90+8;
        else if (p>=100&&p<=107) cur_bg=p-100+8;
    }
}

static void handle_csi(char cmd) {
    int params[8]={0}; parse_params(esc_buf,params,8);
    int a=params[0], b=params[1];
    switch(cmd) {
    case 'A': cy -= a?a:1; if(cy<scroll_top) cy=scroll_top; break;
    case 'B': cy += a?a:1; if(cy>scroll_bot) cy=scroll_bot; break;
    case 'C': cx += a?a:1; if(cx>=cols) cx=cols-1; break;
    case 'D': cx -= a?a:1; if(cx<0) cx=0; break;
    case 'E': cx=0; cy+=a?a:1; if(cy>scroll_bot) cy=scroll_bot; break;
    case 'F': cx=0; cy-=a?a:1; if(cy<scroll_top) cy=scroll_top; break;
    case 'G': cx=(a?a:1)-1; if(cx>=cols) cx=cols-1; break;
    case 'H': case 'f':
        cy=(a?a:1)-1; cx=(b?b:1)-1;
        if(cy>=rows) cy=rows-1;
        if(cx>=cols) cx=cols-1;
        if(cy<0) cy=0;
        if(cx<0) cx=0;
        break;
    case 'J':
        if(a==0) { clear_region(cx,cy,cols-1,cy); clear_region(0,cy+1,cols-1,rows-1); }
        else if(a==1) { clear_region(0,0,cols-1,cy-1); clear_region(0,cy,cx,cy); }
        else if(a==2||a==3) clear_region(0,0,cols-1,rows-1);
        break;
    case 'K':
        if(a==0) clear_region(cx,cy,cols-1,cy);
        else if(a==1) clear_region(0,cy,cx,cy);
        else if(a==2) clear_region(0,cy,cols-1,cy);
        break;
    case 'L': {
        int n2=a?a:1; for(int i=0;i<n2;i++) scroll_down(cy,scroll_bot);
    } break;
    case 'M': {
        int n2=a?a:1; for(int i=0;i<n2;i++) scroll_up(cy,scroll_bot);
    } break;
    case 'P': {
        int n=a?a:1;
        for(int x=cx;x<cols-n;x++) screen_buf[cy*cols+x]=screen_buf[cy*cols+x+n];
        Cell bl=blank_cell();
        for(int x=cols-n;x<cols;x++) screen_buf[cy*cols+x]=bl;
    } break;
    case 'S': for(int i=0;i<(a?a:1);i++) scroll_up(scroll_top,scroll_bot); break;
    case 'T': for(int i=0;i<(a?a:1);i++) scroll_down(scroll_top,scroll_bot); break;
    case 'd': cy=(a?a:1)-1; if(cy>=rows) cy=rows-1; break;
    case 'm': handle_sgr(); break;
    case 'r':
        scroll_top=(a?a:1)-1; scroll_bot=(b?b:rows)-1;
        if(scroll_top<0) scroll_top=0;
        if(scroll_bot>=rows) scroll_bot=rows-1;
        cx=0; cy=0; break;
    case 's': save_cx=cx; save_cy=cy; break;
    case 'u': cx=save_cx; cy=save_cy; break;
    case '@': {
        int n=a?a:1;
        for(int x=cols-1;x>=cx+n;x--) screen_buf[cy*cols+x]=screen_buf[cy*cols+x-n];
        Cell bl=blank_cell();
        for(int x=cx;x<cx+n&&x<cols;x++) screen_buf[cy*cols+x]=bl;
    } break;
    case 'X': { Cell bl=blank_cell(); for(int i=0;i<(a?a:1)&&cx+i<cols;i++) screen_buf[cy*cols+cx+i]=bl; } break;
    case 'n':
        if(a==6) { char r[32]; snprintf(r,sizeof(r),"\033[%d;%dR",cy+1,cx+1); write(master_fd,r,strlen(r)); }
        break;
    case 'c': {
        char r[32]; snprintf(r,sizeof(r),"\033[?6c"); write(master_fd,r,strlen(r));
    } break;
    case 'l': case 'h': break; /* mode set/reset - ignore for now */
    }
}

static void process_byte(unsigned char c) {
    scroll_off = 0;
    if (esc_state==3) { /* OSC - eat until ST or BEL */
        if (c==7 || c==0x9c) { esc_state=0; esc_len=0; }
        else if (c==27) esc_state=4; /* possible ST */
        return;
    }
    if (esc_state==4) { esc_state=(c=='\\')?0:3; esc_len=0; return; }
    if (esc_state==2) { /* CSI */
        if (c>='@' && c<='~') { handle_csi(c); esc_state=0; esc_len=0; }
        else if (esc_len<MAX_ESC-1) esc_buf[esc_len++]=c;
        else { esc_state=0; esc_len=0; }
        return;
    }
    if (esc_state==1) { /* after ESC */
        esc_state=0; esc_len=0;
        if (c=='[') { esc_state=2; memset(esc_buf,0,MAX_ESC); }
        else if (c==']') { esc_state=3; }
        else if (c=='D') newline();
        else if (c=='M') { if(cy<=scroll_top) scroll_down(scroll_top,scroll_bot); else cy--; }
        else if (c=='7') { save_cx=cx; save_cy=cy; }
        else if (c=='8') { cx=save_cx; cy=save_cy; }
        else if (c=='c') { clear_region(0,0,cols-1,rows-1); cx=cy=0; cur_fg=16; cur_bg=17; cur_attr=0; }
        return;
    }
    /* normal */
    if (c==27) { esc_state=1; return; }
    if (c=='\r') { cx=0; return; }
    if (c=='\n') { newline(); return; }
    if (c=='\b') { if(cx>0) cx--; return; }
    if (c=='\t') { cx=((cx/TAB_WIDTH)+1)*TAB_WIDTH; if(cx>=cols) cx=cols-1; return; }
    if (c==7) return; /* bell */
    if (c<32) return; /* other control */
    /* UTF-8 handling */
    char utf[4]={(char)c,0,0,0}; int ulen=1;
    put_char(utf, ulen);
}

static void resize_term(void) {
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    win_w = wa.width; win_h = wa.height;
    int new_cols = win_w / cw;
    int new_rows = win_h / ch;
    if (new_cols<2) new_cols=2;
    if (new_rows<2) new_rows=2;
    if (new_cols==cols && new_rows==rows) return;

    Cell *nb = calloc(new_rows*new_cols, sizeof(Cell));
    Cell b = blank_cell();
    for (int i=0;i<new_rows*new_cols;i++) nb[i]=b;
    int copy_r = rows<new_rows?rows:new_rows;
    int copy_c = cols<new_cols?cols:new_cols;
    if (screen_buf) {
        for (int y=0;y<copy_r;y++)
            for (int x=0;x<copy_c;x++)
                nb[y*new_cols+x] = screen_buf[y*cols+x];
        free(screen_buf);
    }
    screen_buf = nb;
    
    Cell *nsb = calloc(SCROLLBACK*new_cols, sizeof(Cell));
    if (scroll_buf) free(scroll_buf);
    scroll_buf = nsb;
    scroll_lines = 0;
    scroll_off = 0;

    cols=new_cols; rows=new_rows;
    scroll_top=0; scroll_bot=rows-1;
    if(cx>=cols) cx=cols-1;
    if(cy>=rows) cy=rows-1;

    if (buf) XFreePixmap(dpy, buf);
    buf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy,scr));
    if (xdraw) XftDrawDestroy(xdraw);
    xdraw = XftDrawCreate(dpy, buf, vis, cmap);

    struct winsize ws = {.ws_row=rows, .ws_col=cols, .ws_xpixel=win_w, .ws_ypixel=win_h};
    ioctl(master_fd, TIOCSWINSZ, &ws);
}

static void send_key(const char *s) { write(master_fd, s, strlen(s)); }

static void handle_key(XKeyEvent *e) {
    KeySym ks = XkbKeycodeToKeysym(dpy, e->keycode, 0, e->state&ShiftMask?1:0);
    char kbuf[32]; int n = XLookupString(e, kbuf, sizeof(kbuf), &ks, NULL);

    if (e->state & ShiftMask) {
        if (ks==XK_Page_Up) { scroll_off+=rows/2; if(scroll_off>scroll_lines) scroll_off=scroll_lines; dirty=1; return; }
        if (ks==XK_Page_Down) { scroll_off-=rows/2; if(scroll_off<0) scroll_off=0; dirty=1; return; }
    }
    scroll_off = 0;

    switch(ks) {
    case XK_Return: case XK_KP_Enter: send_key("\r"); return;
    case XK_BackSpace: send_key("\177"); return;
    case XK_Tab: send_key("\t"); return;
    case XK_Escape: send_key("\033"); return;
    case XK_Up: send_key("\033[A"); return;
    case XK_Down: send_key("\033[B"); return;
    case XK_Right: send_key("\033[C"); return;
    case XK_Left: send_key("\033[D"); return;
    case XK_Home: send_key("\033[H"); return;
    case XK_End: send_key("\033[F"); return;
    case XK_Insert: send_key("\033[2~"); return;
    case XK_Delete: send_key("\033[3~"); return;
    case XK_Page_Up: send_key("\033[5~"); return;
    case XK_Page_Down: send_key("\033[6~"); return;
    case XK_F1: send_key("\033OP"); return;
    case XK_F2: send_key("\033OQ"); return;
    case XK_F3: send_key("\033OR"); return;
    case XK_F4: send_key("\033OS"); return;
    case XK_F5: send_key("\033[15~"); return;
    case XK_F6: send_key("\033[17~"); return;
    case XK_F7: send_key("\033[18~"); return;
    case XK_F8: send_key("\033[19~"); return;
    case XK_F9: send_key("\033[20~"); return;
    case XK_F10: send_key("\033[21~"); return;
    case XK_F11: send_key("\033[23~"); return;
    case XK_F12: send_key("\033[24~"); return;
    }
    if (n > 0) write(master_fd, kbuf, n);
}

static void sigchld(int s) { (void)s; int st; while(waitpid(-1,&st,WNOHANG)>0); running=0; }

int main(void) {
    setlocale(LC_ALL, "");
    signal(SIGCHLD, sigchld);

    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "starlight: cannot open display\n"); return 1; }
    scr = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, scr);
    cmap = DefaultColormap(dpy, scr);

    init_colors();

    fnt = XftFontOpenName(dpy, scr, "monospace:size=11");
    if (!fnt) fnt = XftFontOpenName(dpy, scr, "fixed");
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, fnt, (const FcChar8*)"M", 1, &ext);
    cw = ext.xOff ? ext.xOff : fnt->max_advance_width;
    ch = fnt->ascent + fnt->descent;

    win_w = 80*cw; win_h = 24*ch;
    cols = 80; rows = 24;

    XSetWindowAttributes swa;
    swa.event_mask = ExposureMask|KeyPressMask|StructureNotifyMask|FocusChangeMask;
    swa.background_pixel = colors[17].pixel;
    win = XCreateWindow(dpy, RootWindow(dpy,scr), 100,100, win_w,win_h, 0,
        CopyFromParent, InputOutput, vis, CWEventMask|CWBackPixel, &swa);

    XClassHint cls = { .res_name="starlight", .res_class="Starlight" };
    XSetClassHint(dpy, win, &cls);
    XStoreName(dpy, win, "Starlight");

    Atom wm_name = XInternAtom(dpy,"_NET_WM_NAME",False);
    Atom utf8 = XInternAtom(dpy,"UTF8_STRING",False);
    XChangeProperty(dpy,win,wm_name,utf8,8,PropModeReplace,(unsigned char*)"Starlight",9);

    Theme t = load_theme();
    XftColor bc; XftColorAllocName(dpy,vis,cmap,t.border,&bc);
    XSetWindowBorder(dpy, win, bc.pixel);
    XSetWindowBorderWidth(dpy, win, 2);

    buf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy,scr));
    xdraw = XftDrawCreate(dpy, buf, vis, cmap);
    xgc = XCreateGC(dpy, win, 0, NULL);

    screen_buf = calloc(rows*cols, sizeof(Cell));
    scroll_buf = calloc(SCROLLBACK*cols, sizeof(Cell));
    Cell b = blank_cell();
    for (int i=0;i<rows*cols;i++) screen_buf[i]=b;
    scroll_top=0; scroll_bot=rows-1;

    /* Fork PTY */
    struct winsize ws = {.ws_row=rows,.ws_col=cols,.ws_xpixel=(unsigned short)win_w,.ws_ypixel=(unsigned short)win_h};
    child_pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (child_pid < 0) { perror("forkpty"); return 1; }
    if (child_pid == 0) {
        const char *sh = getenv("SHELL");
        if (!sh) sh = "/bin/bash";
        unsetenv("COLUMNS"); unsetenv("LINES");
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        execl(sh, sh, NULL);
        _exit(1);
    }

    XMapRaised(dpy, win);

    int xfd = ConnectionNumber(dpy);
    struct timespec last = {0,0};

    while (running) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        if (master_fd >= 0) FD_SET(master_fd, &rfds);
        int maxfd = xfd > master_fd ? xfd : master_fd;
        struct timeval tv = {0, 16000};
        select(maxfd+1, &rfds, NULL, NULL, &tv);

        /* Read PTY */
        if (master_fd >= 0 && FD_ISSET(master_fd, &rfds)) {
            unsigned char rbuf[4096];
            int nr = read(master_fd, rbuf, sizeof(rbuf));
            if (nr <= 0) { running = 0; break; }
            for (int i=0; i<nr; i++) process_byte(rbuf[i]);
            dirty = 1;
        }

        /* X11 events */
        while (XPending(dpy)) {
            XEvent ev; XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) dirty = 1;
            else if (ev.type == KeyPress) { handle_key(&ev.xkey); dirty = 1; }
            else if (ev.type == ConfigureNotify) { resize_term(); dirty = 1; }
        }

        /* Redraw at ~60fps */
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec-last.tv_sec)*1000 + (now.tv_nsec-last.tv_nsec)/1000000;
        if (dirty && ms >= 16) { draw(); last = now; dirty = 0; }
    }

    close(master_fd);
    XftDrawDestroy(xdraw);
    XFreePixmap(dpy, buf);
    XftFontClose(dpy, fnt);
    XFreeGC(dpy, xgc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    free(screen_buf);
    free(scroll_buf);
    return 0;
}
