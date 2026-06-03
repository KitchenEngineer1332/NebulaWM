#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
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

typedef struct { char ch[4]; uint8_t len; uint16_t fg, bg; uint8_t attr; } Cell;

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
static Cell *pri_buf;
static Cell *alt_buf;
static int use_alt_buf = 0;
static int pri_cx = 0, pri_cy = 0;

static int pending_wrap = 0;
static int cursor_visible = 1;

static Cell *scroll_buf;
static int scroll_lines = 0;
static int scroll_off = 0;

static Cell *prev_buf = NULL;
static int force_full_redraw = 1;

static XftColor colors[258]; /* 0-255 ansi, 256=fg, 257=bg */
static int cur_fg = 256, cur_bg = 257, cur_attr = 0;

static char esc_buf[MAX_ESC];
static int esc_len = 0;
static int esc_state = 0; /* 0=normal, 1=ESC, 2=CSI, 3=OSC, 5=charset */

static volatile sig_atomic_t running = 1;
static int dirty = 1;

static void init_colors(void) {
    Theme t = load_theme();
    uint32_t p = t.border;
    uint32_t bk = t.bg;
    char hex[16];
    /* ANSI 0-7: dark variants */
    theme_color_to_hex(theme_blend(p, bk, 0.10f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[0]);
    theme_color_to_hex(theme_blend(0xf7768e, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[1]);
    theme_color_to_hex(theme_blend(0x9ece6a, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[2]);
    theme_color_to_hex(theme_blend(0xe0af68, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[3]);
    theme_color_to_hex(theme_blend(p, bk, 0.70f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[4]);
    theme_color_to_hex(theme_blend(0xbb9af7, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[5]);
    theme_color_to_hex(theme_blend(0x7dcfff, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[6]);
    theme_color_to_hex(theme_blend(0xc0caf5, bk, 0.85f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[7]);
    /* ANSI 8-15: bright */
    theme_color_to_hex(theme_blend(p, bk, 0.30f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[8]);
    theme_color_to_hex(theme_blend(0xf7768e, 0xffffff, 0.90f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[9]);
    theme_color_to_hex(theme_blend(0x9ece6a, 0xffffff, 0.90f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[10]);
    theme_color_to_hex(theme_blend(0xe0af68, 0xffffff, 0.90f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[11]);
    theme_color_to_hex(theme_blend(p, 0xffffff, 0.70f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[12]);
    theme_color_to_hex(theme_blend(0xbb9af7, 0xffffff, 0.90f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[13]);
    theme_color_to_hex(theme_blend(0x7dcfff, 0xffffff, 0.90f), hex); XftColorAllocName(dpy,vis,cmap,hex,&colors[14]);
    XftColorAllocName(dpy,vis,cmap,"#c0caf5",&colors[15]);

    /* 16-231: 6x6x6 color cube */
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int idx = 16 + r*36 + g*6 + b;
                snprintf(hex, sizeof(hex), "#%02x%02x%02x", 
                         r ? r*40 + 55 : 0, 
                         g ? g*40 + 55 : 0, 
                         b ? b*40 + 55 : 0);
                XftColorAllocName(dpy,vis,cmap,hex,&colors[idx]);
            }
        }
    }

    /* 232-255: grayscale ramp */
    for (int i = 0; i < 24; i++) {
        int idx = 232 + i;
        int val = 8 + i*10;
        snprintf(hex, sizeof(hex), "#%02x%02x%02x", val, val, val);
        XftColorAllocName(dpy,vis,cmap,hex,&colors[idx]);
    }

    XftColorAllocName(dpy,vis,cmap,t.fg_hex,&colors[256]);
    XftColorAllocName(dpy,vis,cmap,t.bg_hex,&colors[257]);
}

static int match_color(int r, int g, int b) {
    static struct { uint8_t r, g, b; int idx; } cache[256];
    static int cache_init = 0;
    if (!cache_init) {
        for (int i=0; i<256; i++) cache[i].idx = -1;
        cache_init = 1;
    }
    
    uint8_t hash = (r ^ g ^ b);
    if (cache[hash].idx != -1 && cache[hash].r == r && cache[hash].g == g && cache[hash].b == b)
        return cache[hash].idx;

    int best_idx = 0;
    int min_dist = 1000000;
    for (int i = 0; i < 256; i++) {
        int cr, cg, cb;
        if (i < 16) {
            static const struct { uint8_t r, g, b; } ansi_colors[16] = {
                {0,0,0}, {170,0,0}, {0,170,0}, {170,85,0}, {0,0,170}, {170,0,170}, {0,170,170}, {170,170,170},
                {85,85,85}, {255,85,85}, {85,255,85}, {255,255,85}, {85,85,255}, {255,85,255}, {85,255,255}, {255,255,255}
            };
            cr = ansi_colors[i].r;
            cg = ansi_colors[i].g;
            cb = ansi_colors[i].b;
        } else if (i < 232) {
            int idx = i - 16;
            int dr = idx / 36;
            int dg = (idx % 36) / 6;
            int db = idx % 6;
            cr = dr ? dr*40 + 55 : 0;
            cg = dg ? dg*40 + 55 : 0;
            cb = db ? db*40 + 55 : 0;
        } else {
            cr = cg = cb = 8 + (i - 232)*10;
        }
        int dr = cr - r;
        int dg = cg - g;
        int db = cb - b;
        int dist = dr*dr + dg*dg + db*db;
        if (dist < min_dist) {
            min_dist = dist;
            best_idx = i;
            if (dist == 0) break;
        }
    }
    
    cache[hash].r = r; cache[hash].g = g; cache[hash].b = b; cache[hash].idx = best_idx;
    return best_idx;
}

static int cells_equal(const Cell *c1, const Cell *c2) {
    if (c1->len != c2->len) return 0;
    if (c1->fg != c2->fg || c1->bg != c2->bg || c1->attr != c2->attr) return 0;
    return memcmp(c1->ch, c2->ch, c1->len) == 0;
}

static Cell* cell_at(int x, int y) { return &screen_buf[y*cols+x]; }

static Cell* get_cell_to_draw(int x, int y) {
    static Cell b_cell;
    b_cell.ch[0]=' '; b_cell.ch[1]=0; b_cell.ch[2]=0; b_cell.ch[3]=0;
    b_cell.len=1; b_cell.fg=256; b_cell.bg=257; b_cell.attr=0;

    if (scroll_off > 0) {
        long abs_y = (long)scroll_lines - scroll_off + y;
        if (abs_y < scroll_lines) {
            if (abs_y >= 0 && abs_y >= (long)scroll_lines - SCROLLBACK) {
                return &scroll_buf[(abs_y % SCROLLBACK)*cols + x];
            } else {
                return &b_cell;
            }
        } else {
            int sy = abs_y - scroll_lines;
            if (sy >= 0 && sy < rows) return cell_at(x, sy);
            else return &b_cell;
        }
    } else {
        return cell_at(x, y);
    }
}

static Cell blank_cell(void) {
    Cell c;
    c.ch[0]=' '; c.ch[1]=0; c.ch[2]=0; c.ch[3]=0;
    c.len=1; c.fg=cur_fg; c.bg=cur_bg; c.attr=0;
    return c;
}

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
    pending_wrap = 0;
    if (cy >= scroll_bot) scroll_up(scroll_top, scroll_bot);
    else cy++;
}

static void put_char(const char *utf8, int len, int width) {
    if (pending_wrap) {
        cx = 0;
        newline();
        pending_wrap = 0;
    }
    if (cx + width > cols) {
        cx = 0;
        newline();
    }
    
    Cell *c = cell_at(cx, cy);
    memcpy(c->ch, utf8, len);
    c->len = len;
    c->fg = cur_fg; c->bg = cur_bg; c->attr = cur_attr;
    
    if (width == 2 && cx + 1 < cols) {
        Cell *nc = cell_at(cx + 1, cy);
        nc->ch[0] = 0;
        nc->len = 0;
        nc->fg = cur_fg; nc->bg = cur_bg; nc->attr = cur_attr;
    }

    cx += width;
    if (cx >= cols) {
        cx = cols - 1;
        pending_wrap = 1;
    }
}

static void draw(void) {
    if (force_full_redraw) {
        XftDrawRect(xdraw, &colors[257], 0, 0, win_w, win_h);
        if (prev_buf) {
            for (int i = 0; i < rows*cols; i++) prev_buf[i].len = 0xff;
        }
        force_full_redraw = 0;
    }

    FcChar8 text_buf[8192];
    for (int y=0; y<rows; y++) {
        int x = 0;
        while (x < cols) {
            Cell *c = get_cell_to_draw(x, y);
            if (prev_buf && cells_equal(c, &prev_buf[y*cols + x])) {
                x++;
                continue;
            }

            int run_len = 1;
            int text_len = c->len;
            if (text_len > 0) memcpy(text_buf, c->ch, c->len);

            int fg_i = c->fg, bg_i = c->bg;
            if (c->attr & ATTR_REVERSE) { int t=fg_i; fg_i=bg_i; bg_i=t; }

            for (int nx = x + 1; nx < cols; nx++) {
                Cell *nc = get_cell_to_draw(nx, y);
                if (prev_buf && cells_equal(nc, &prev_buf[y*cols + nx])) break;

                int nfg = nc->fg, nbg = nc->bg;
                if (nc->attr & ATTR_REVERSE) { int t=nfg; nfg=nbg; nbg=t; }

                if (nfg == fg_i && nbg == bg_i && nc->attr == c->attr && text_len + nc->len < (int)sizeof(text_buf)) {
                    run_len++;
                    if (nc->len > 0) {
                        memcpy(text_buf + text_len, nc->ch, nc->len);
                        text_len += nc->len;
                    }
                } else {
                    break;
                }
            }

            XftDrawRect(xdraw, &colors[bg_i], x*cw, y*ch, run_len*cw, ch);
            if (text_len > 0) {
                XftColor *fc = &colors[fg_i];
                if (c->attr & ATTR_BOLD && fg_i < 8) fc = &colors[fg_i+8];
                int all_spaces = 1;
                for (int i=0; i<text_len; i++) {
                    if (text_buf[i] != ' ' && text_buf[i] != 0) { all_spaces = 0; break; }
                }
                if (!all_spaces) {
                    XftDrawStringUtf8(xdraw, fc, fnt, x*cw, y*ch+fnt->ascent, text_buf, text_len);
                }
            }
            if (c->attr & ATTR_UNDERLINE) {
                XftDrawRect(xdraw, &colors[fg_i], x*cw, y*ch+ch-1, run_len*cw, 1);
            }

            if (prev_buf) {
                for (int i = 0; i < run_len; i++) {
                    prev_buf[y*cols + x + i] = *get_cell_to_draw(x + i, y);
                }
            }

            x += run_len;
        }
    }

    /* cursor */
    int draw_cursor = (scroll_off == 0 && cursor_visible);
    int cxp = 0, cyp = 0;
    if (draw_cursor) {
        cxp = pending_wrap ? cols - 1 : (cx < cols ? cx : cols-1);
        cyp = cy;
        XftDrawRect(xdraw, &colors[256], cxp*cw, cyp*ch, cw, ch);
        Cell *cc = cell_at(cxp, cyp);
        if (cc->len > 0)
            XftDrawStringUtf8(xdraw, &colors[257], fnt, cxp*cw, cyp*ch+fnt->ascent,
                (FcChar8*)cc->ch, cc->len);
    }

    XCopyArea(dpy, buf, win, xgc, 0, 0, win_w, win_h, 0, 0);

    /* restore cell under cursor on pixmap buf */
    if (draw_cursor) {
        Cell *cc = cell_at(cxp, cyp);
        int fg_i = cc->fg, bg_i = cc->bg;
        if (cc->attr & ATTR_REVERSE) { int t=fg_i; fg_i=bg_i; bg_i=t; }
        XftDrawRect(xdraw, &colors[bg_i], cxp*cw, cyp*ch, cw, ch);
        if (cc->len > 0) {
            XftColor *fc = &colors[fg_i];
            if (cc->attr & ATTR_BOLD && fg_i < 8) fc = &colors[fg_i+8];
            int all_spaces = 1;
            for (int i=0; i<cc->len; i++) {
                if (cc->ch[i] != ' ' && cc->ch[i] != 0) { all_spaces = 0; break; }
            }
            if (!all_spaces) {
                XftDrawStringUtf8(xdraw, fc, fnt, cxp*cw, cyp*ch+fnt->ascent, (FcChar8*)cc->ch, cc->len);
            }
        }
        if (cc->attr & ATTR_UNDERLINE) {
            XftDrawRect(xdraw, &colors[fg_i], cxp*cw, cyp*ch+ch-1, cw, 1);
        }
    }
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
        if (p==0) { cur_fg=256; cur_bg=257; cur_attr=0; }
        else if (p==1) cur_attr|=ATTR_BOLD;
        else if (p==4) cur_attr|=ATTR_UNDERLINE;
        else if (p==7) cur_attr|=ATTR_REVERSE;
        else if (p==22) cur_attr&=~ATTR_BOLD;
        else if (p==24) cur_attr&=~ATTR_UNDERLINE;
        else if (p==27) cur_attr&=~ATTR_REVERSE;
        else if (p>=30&&p<=37) cur_fg=p-30;
        else if (p==39) cur_fg=256;
        else if (p>=40&&p<=47) cur_bg=p-40;
        else if (p==49) cur_bg=257;
        else if (p>=90&&p<=97) cur_fg=p-90+8;
        else if (p>=100&&p<=107) cur_bg=p-100+8;
        else if (p==38) {
            if (i+1 < np) {
                if (params[i+1] == 5 && i+2 < np) {
                    cur_fg = params[i+2];
                    i += 2;
                } else if (params[i+1] == 2 && i+4 < np) {
                    cur_fg = match_color(params[i+2], params[i+3], params[i+4]);
                    i += 4;
                }
            }
        }
        else if (p==48) {
            if (i+1 < np) {
                if (params[i+1] == 5 && i+2 < np) {
                    cur_bg = params[i+2];
                    i += 2;
                } else if (params[i+1] == 2 && i+4 < np) {
                    cur_bg = match_color(params[i+2], params[i+3], params[i+4]);
                    i += 4;
                }
            }
        }
    }
}

static void handle_csi(char cmd) {
    int is_priv = (esc_buf[0] == '?');
    const char *p_str = is_priv ? esc_buf + 1 : esc_buf;
    int params[16]={0}; 
    parse_params(p_str, params, 16);
    int a=params[0], b=params[1];
    switch(cmd) {
    case 'A': cy -= a?a:1; if(cy<scroll_top) cy=scroll_top; pending_wrap=0; break;
    case 'B': cy += a?a:1; if(cy>scroll_bot) cy=scroll_bot; pending_wrap=0; break;
    case 'C': cx += a?a:1; if(cx>=cols) cx=cols-1; pending_wrap=0; break;
    case 'D': cx -= a?a:1; if(cx<0) cx=0; pending_wrap=0; break;
    case 'E': cx=0; cy+=a?a:1; if(cy>scroll_bot) cy=scroll_bot; pending_wrap=0; break;
    case 'F': cx=0; cy-=a?a:1; if(cy<scroll_top) cy=scroll_top; pending_wrap=0; break;
    case 'G': cx=(a?a:1)-1; if(cx>=cols) cx=cols-1; pending_wrap=0; break;
    case 'H': case 'f':
        cy=(a?a:1)-1; cx=(b?b:1)-1;
        if(cy>=rows) cy=rows-1;
        if(cx>=cols) cx=cols-1;
        if(cy<0) cy=0;
        if(cx<0) cx=0;
        pending_wrap=0;
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
    case 'd': cy=(a?a:1)-1; if(cy>=rows) cy=rows-1; pending_wrap=0; break;
    case 'm': handle_sgr(); break;
    case 'r':
        scroll_top=(a?a:1)-1; scroll_bot=(b?b:rows)-1;
        if(scroll_top<0) scroll_top=0;
        if(scroll_bot>=rows) scroll_bot=rows-1;
        cx=0; cy=0; pending_wrap=0; break;
    case 's': save_cx=cx; save_cy=cy; break;
    case 'u': cx=save_cx; cy=save_cy; pending_wrap=0; break;
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
    case 'l': case 'h': 
        if (is_priv) {
            int mode = a;
            int set = (cmd == 'h');
            if (mode == 25) {
                cursor_visible = set;
            } else if (mode == 1049) {
                if (set && !use_alt_buf) {
                    pri_cx = cx; pri_cy = cy;
                    use_alt_buf = 1;
                    screen_buf = alt_buf;
                    clear_region(0,0,cols-1,rows-1);
                    cx = 0; cy = 0;
                    pending_wrap = 0;
                } else if (!set && use_alt_buf) {
                    use_alt_buf = 0;
                    screen_buf = pri_buf;
                    cx = pri_cx; cy = pri_cy;
                    pending_wrap = 0;
                }
            }
        }
        break;
    }
}

static mbstate_t ps;
static char utf8_buf[4];
static int utf8_len = 0;

static void process_byte(unsigned char c) {
    scroll_off = 0;
    
    if (c < 32) {
        if (c==7 && esc_state==3) { esc_state=0; esc_len=0; return; }
        if (c=='\r') { cx=0; pending_wrap=0; return; }
        if (c=='\n') { newline(); return; }
        if (c=='\b') { 
            if (pending_wrap) pending_wrap = 0; 
            else if (cx>0) cx--; 
            return; 
        }
        if (c=='\t') { 
            cx=((cx/TAB_WIDTH)+1)*TAB_WIDTH; 
            if(cx>=cols) { cx=cols-1; pending_wrap=1; }
            return; 
        }
        if (c==27) { 
            esc_state=1; esc_len=0; utf8_len=0; memset(&ps, 0, sizeof(ps)); return; 
        }
        if (c==7) return; /* bell */
        return; /* ignore other controls */
    }

    if (esc_state==5) { esc_state=0; esc_len=0; return; }
    if (esc_state==3) { /* OSC - eat until ST or BEL */
        if (c==0x9c) { esc_state=0; esc_len=0; }
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
        if (c=='[') { esc_state=2; memset(esc_buf,0,MAX_ESC); return; }
        else if (c==']') { esc_state=3; return; }
        else if (c=='(' || c==')' || c=='*' || c=='+' || c=='-' || c=='.' || c=='/' || c=='%') { esc_state=5; return; }
        esc_state=0; esc_len=0;
        if (c=='D') newline();
        else if (c=='M') { if(cy<=scroll_top) scroll_down(scroll_top,scroll_bot); else cy--; pending_wrap=0; }
        else if (c=='7') { save_cx=cx; save_cy=cy; }
        else if (c=='8') { cx=save_cx; cy=save_cy; pending_wrap=0; }
        else if (c=='c') { clear_region(0,0,cols-1,rows-1); cx=cy=0; cur_fg=256; cur_bg=257; cur_attr=0; pending_wrap=0; }
        return;
    }
    
    /* UTF-8 handling */
    utf8_buf[utf8_len++] = c;
    wchar_t wc;
    size_t res = mbrtowc(&wc, utf8_buf, utf8_len, &ps);
    if (res == (size_t)-1) {
        utf8_len = 0;
        memset(&ps, 0, sizeof(ps));
    } else if (res == (size_t)-2) {
        if (utf8_len >= 4) {
            utf8_len = 0;
            memset(&ps, 0, sizeof(ps));
        }
    } else {
        int width = wcwidth(wc);
        if (width < 0) width = 1;
        if (width > 0) {
            put_char(utf8_buf, utf8_len, width);
        }
        utf8_len = 0;
    }
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

    Cell *n_pri = calloc(new_rows*new_cols, sizeof(Cell));
    Cell *n_alt = calloc(new_rows*new_cols, sizeof(Cell));
    Cell b = blank_cell();
    b.fg = 256; b.bg = 257; b.attr = 0;
    for (int i=0;i<new_rows*new_cols;i++) {
        n_pri[i]=b; n_alt[i]=b;
    }

    int min_row = 0;
    int pull_lines = 0;

    if (!use_alt_buf) {
        if (new_rows < rows) {
            if (cy >= new_rows) {
                min_row = cy - new_rows + 1;
                for (int i=0; i<min_row; i++) {
                    int idx = scroll_lines % SCROLLBACK;
                    memcpy(&scroll_buf[idx*cols], &pri_buf[i*cols], cols*sizeof(Cell));
                    scroll_lines++;
                }
            }
        } else if (new_rows > rows) {
            pull_lines = new_rows - rows;
            if (pull_lines > scroll_lines) pull_lines = scroll_lines;
        }
    }

    int copy_r = rows - min_row;
    if (copy_r > new_rows) copy_r = new_rows;
    int copy_c = cols < new_cols ? cols : new_cols;

    if (pri_buf) {
        for (int y=0;y<copy_r;y++) {
            for (int x=0;x<copy_c;x++) {
                n_pri[(y + pull_lines)*new_cols + x] = pri_buf[(min_row+y)*cols+x];
            }
        }
        if (pull_lines > 0) {
            for (int i=0; i<pull_lines; i++) {
                int scroll_idx = (scroll_lines - pull_lines + i) % SCROLLBACK;
                for (int x=0; x<copy_c; x++) {
                    n_pri[i*new_cols + x] = scroll_buf[scroll_idx*cols + x];
                }
            }
            scroll_lines -= pull_lines;
        }
        free(pri_buf);
    }
    
    if (alt_buf) {
        int alt_copy_r = rows < new_rows ? rows : new_rows;
        for (int y=0;y<alt_copy_r;y++) {
            for (int x=0;x<copy_c;x++) {
                n_alt[y*new_cols + x] = alt_buf[y*cols+x];
            }
        }
        free(alt_buf);
    }

    pri_buf = n_pri;
    alt_buf = n_alt;
    screen_buf = use_alt_buf ? alt_buf : pri_buf;
    
    Cell *nsb = calloc(SCROLLBACK*new_cols, sizeof(Cell));
    for (int i=0;i<SCROLLBACK*new_cols;i++) nsb[i]=b;
    if (scroll_buf) {
        for (int i=0; i<SCROLLBACK; i++) {
            for (int x=0; x<copy_c; x++) {
                nsb[i*new_cols+x] = scroll_buf[i*cols+x];
            }
        }
        free(scroll_buf);
    }
    scroll_buf = nsb;

    cols=new_cols; rows=new_rows;
    scroll_top=0; scroll_bot=rows-1;
    
    if (!use_alt_buf) {
        cy = cy - min_row + pull_lines;
    } else {
        if (cy >= rows) cy = rows - 1;
    }
    
    if(cx>=cols) cx=cols-1;
    if(cy>=rows) cy=rows-1;
    if(cy<0) cy=0;
    
    if (pri_cx >= cols) pri_cx = cols-1;
    if (pri_cy >= rows) pri_cy = rows-1;

    if (buf) XFreePixmap(dpy, buf);
    buf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy,scr));
    if (xdraw) XftDrawDestroy(xdraw);
    xdraw = XftDrawCreate(dpy, buf, vis, cmap);

    if (prev_buf) free(prev_buf);
    prev_buf = calloc(new_rows*new_cols, sizeof(Cell));
    if (prev_buf) {
        for (int i = 0; i < new_rows*new_cols; i++) prev_buf[i].len = 0xff;
    }
    force_full_redraw = 1;

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

static volatile sig_atomic_t child_exited = 0;
static void sigchld(int s) { (void)s; child_exited = 1; running = 0; }

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
    swa.background_pixmap = None;
    swa.bit_gravity = NorthWestGravity;
    win = XCreateWindow(dpy, RootWindow(dpy,scr), 100,100, win_w,win_h, 0,
        CopyFromParent, InputOutput, vis, CWEventMask|CWBackPixmap|CWBitGravity, &swa);

    XClassHint cls = { .res_name="starlight", .res_class="Starlight" };
    XSetClassHint(dpy, win, &cls);
    XStoreName(dpy, win, "Starlight");

    Atom wm_name = XInternAtom(dpy,"_NET_WM_NAME",False);
    Atom utf8 = XInternAtom(dpy,"UTF8_STRING",False);
    XChangeProperty(dpy,win,wm_name,utf8,8,PropModeReplace,(unsigned char*)"Starlight",9);

    Theme t = load_theme();
    XftColor bc; XftColorAllocName(dpy,vis,cmap,t.border_hex,&bc);
    XSetWindowBorder(dpy, win, bc.pixel);
    XSetWindowBorderWidth(dpy, win, 2);

    buf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy,scr));
    xdraw = XftDrawCreate(dpy, buf, vis, cmap);
    xgc = XCreateGC(dpy, win, 0, NULL);

    pri_buf = calloc(rows*cols, sizeof(Cell));
    alt_buf = calloc(rows*cols, sizeof(Cell));
    screen_buf = pri_buf;
    scroll_buf = calloc(SCROLLBACK*cols, sizeof(Cell));
    prev_buf = calloc(rows*cols, sizeof(Cell));
    Cell b = blank_cell();
    for (int i=0;i<rows*cols;i++) { pri_buf[i]=b; alt_buf[i]=b; }
    if (prev_buf) {
        for (int i=0;i<rows*cols;i++) prev_buf[i].len = 0xff;
    }
    force_full_redraw = 1;
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
        if (child_exited) {
            int st;
            while (waitpid(-1, &st, WNOHANG) > 0) {
            }
            child_exited = 0;
        }

        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        if (master_fd >= 0) FD_SET(master_fd, &rfds);
        int maxfd = xfd > master_fd ? xfd : master_fd;
        struct timeval tv = {0, 16000};
        select(maxfd+1, &rfds, NULL, NULL, dirty ? &tv : NULL);

        /* Read PTY */
        if (master_fd >= 0 && FD_ISSET(master_fd, &rfds)) {
            unsigned char rbuf[4096];
            int nr = read(master_fd, rbuf, sizeof(rbuf));
            if (nr <= 0) { running = 0; break; }
            for (int i=0; i<nr; i++) process_byte(rbuf[i]);
            dirty = 1;
        }

        /* X11 events */
        int needs_immediate_draw = 0;
        while (XPending(dpy)) {
            XEvent ev; XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) { dirty = 1; needs_immediate_draw = 1; force_full_redraw = 1; }
            else if (ev.type == KeyPress) { handle_key(&ev.xkey); dirty = 1; }
            else if (ev.type == ConfigureNotify) { resize_term(); dirty = 1; needs_immediate_draw = 1; }
        }

        /* Redraw at ~60fps */
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec-last.tv_sec)*1000 + (now.tv_nsec-last.tv_nsec)/1000000;
        if (dirty && (ms >= 16 || needs_immediate_draw)) { draw(); clock_gettime(CLOCK_MONOTONIC, &last); dirty = 0; }
    }

    close(master_fd);
    XftDrawDestroy(xdraw);
    XFreePixmap(dpy, buf);
    XftFontClose(dpy, fnt);
    XFreeGC(dpy, xgc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    free(pri_buf);
    free(alt_buf);
    free(scroll_buf);
    if (prev_buf) free(prev_buf);
    return 0;
}
