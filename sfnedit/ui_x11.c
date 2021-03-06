/*
 * sfnedit/ui_x11.c
 *
 * Copyright (C) 2019 bzt (bztsrc@gitlab)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @brief User interface fallback driver for X11
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <Xlib.h>
#include <Xutil.h>
#include <Xos.h>
#include <Xatom.h>
#ifdef HAS_XMU
#include <Xmu/Atoms.h>
#endif
#include <keysymdef.h>
#include <cursorfont.h>
#include "lang.h"
#include "util.h"
#include "ui.h"

Display *display;
Visual *visual;
Atom wmDel, wmTargets;
Cursor cursors[5];
extern uint8_t *tools, *icon32, *icon64;
#define ICON_LENGTH (2 + 16 * 16 + 2 + 32 * 32 + 2 + 64 * 64)
long icons[ICON_LENGTH];
int screen_num = 0, red_shift,green_shift,blue_shift, btnflags = 0, keyflags = 0, keypressed = 0;
#ifdef HAS_XMU
unsigned char selection[8] = { 0 };
#endif

/**
 * Copy text to clipboard
 */
void ui_copy(char *s)
{
#ifdef HAS_XMU
    memcpy(selection, s, sizeof(selection));
    XSetSelectionOwner(display, XA_CLIPBOARD(display), (Window)wins[0].winid, CurrentTime);
#else
    /* in lack of Xmu and XA_CLIPBOARD, we just print it to console */
    printf("UTF8: %s\n", s);
#endif
}

/**
 * Create a window
 */
void *ui_createwin(int w, int h)
{
    Window window;
    Atom net_wm_icon, cardinal;

    if(!(window = XCreateSimpleWindow(display, RootWindow(display, screen_num), 0, 0, w, h, 0, theme[THEME_FG], theme[THEME_BG])))
        return 0;
    XSelectInput(display, window, KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|
        ButtonMotionMask|PointerMotionMask|ExposureMask|StructureNotifyMask);

    net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
    cardinal = XInternAtom(display, "CARDINAL", False);
    XChangeProperty(display, window, net_wm_icon, cardinal, 32, PropModeReplace, (unsigned char *)icons, ICON_LENGTH);
    XSetWMProtocols(display, window, &wmDel, 1);
    return (void*)window;
}

/**
 * Set window title
 */
void ui_titlewin(ui_win_t *win, char *title)
{
    XTextProperty title_property;
    int i;
    i = XStringListToTextProperty(&title, 1, &title_property);
    if(i) {
        XSetWMName(display, (Window)win->winid, &title_property);
        XSetWMIconName(display, (Window)win->winid, &title_property);
    }
}

/**
 * Resize a window
 */
void ui_resizewin(ui_win_t *win, int w, int h)
{
    int i;
    XImage *xi;

    if(win->data) free(win->data);
    if(win->surface) {
        ((XImage*)win->surface)->data = NULL;
        XDestroyImage((XImage*)win->surface);
    }
    xi = XCreateImage(display, visual, DefaultDepth(display, screen_num), ZPixmap, 0, NULL, w, h, 8, w * 4);
    win->data = (uint32_t*)malloc(w * h * sizeof(uint32_t));
    if(!xi || !win->data) ui_error("x11", ERR_MEM);
    xi->data = (char*)win->data;
    xi->byte_order= LSBFirst;
    xi->bits_per_pixel = 32;
    win->surface = (void*)xi;
    win->p = w;
    win->w = w;
    win->h = h;
    for(i = 0; i < w * h; i++) win->data[i] = theme[THEME_BG];
}

/**
 * Display modified window
 */
void ui_flushwin(ui_win_t *win, int x, int y, int w, int h)
{
    GC gc = XCreateGC(display, (Window)win->winid, 0, 0);
    XPutImage(display, (Window)win->winid, gc, (XImage*)win->surface, x, y, x, y, w, h);
    XFlush(display);
    XFreeGC(display, gc);
}

/**
 * Destroy a window
 */
void ui_destroywin(ui_win_t *win)
{
    free(win->data);
    win->data = NULL;
    ((XImage*)win->surface)->data = NULL;
    XDestroyImage((XImage*)win->surface);
    XUnmapWindow(display, (Window)win->winid);
    XDestroyWindow(display, (Window)win->winid);
    XFlush(display);
}

/**
 * Focus a window
 */
void ui_focuswin(ui_win_t *win)
{
    XMapWindow(display, (Window)win->winid);
    XRaiseWindow(display, (Window)win->winid);
    XFlush(display);
}

/**
 * Change cursor of window
 */
void ui_cursorwin(ui_win_t *win, int cursor)
{
    XDefineCursor(display, (Window)win->winid, cursors[cursor]);
}

/**
 * Initialize X11 driver
 */
void ui_init()
{
    int i;
    long *ptr = icons;

    display = XOpenDisplay(getenv("DISPLAY"));
    if (!display) ui_error("x11", ERR_DISPLAY);

    screen_num = DefaultScreen(display);
    visual = DefaultVisual(display, screen_num);
    red_shift = green_shift = blue_shift = 24;
    if(!DisplayWidth(display, screen_num)) ui_error("x11", ERR_DISPLAY);
    for (i = visual->red_mask; !(i&0x80000000); i <<= 1) red_shift--;
    for (i = visual->green_mask; !(i&0x80000000); i <<= 1) green_shift--;
    for (i = visual->blue_mask; !(i&0x80000000); i <<= 1) blue_shift--;

    *ptr++ = 16;
    *ptr++ = 16;
    for(i=0; i < 16 * 16 * 4; i += 4)
        *ptr++ = tools[i+2] | (tools[i+1] << 8) | (tools[i] << 16) | (tools[i+3] << 24);
    *ptr++ = 32;
    *ptr++ = 32;
    for(i=0; i < 32 * 32 * 4; i += 4)
        *ptr++ = icon32[i+2] | (icon32[i+1] << 8) | (icon32[i] << 16) | (icon32[i+3] << 24);
    *ptr++ = 64;
    *ptr++ = 64;
    for(i=0; i < 64 * 64 * 4; i += 4)
        *ptr++ = icon64[i+2] | (icon64[i+1] << 8) | (icon64[i] << 16) | (icon64[i+3] << 24);

    cursors[CURSOR_LOADING] = XCreateFontCursor(display, XC_watch);
    cursors[CURSOR_PTR] = XCreateFontCursor(display, XC_left_ptr);
    cursors[CURSOR_CROSS] = XCreateFontCursor(display, XC_crosshair);
    cursors[CURSOR_MOVE] = XCreateFontCursor(display, XC_fleur);
    cursors[CURSOR_GRAB] = XCreateFontCursor(display, XC_hand2);

    wmDel = XInternAtom(display, "WM_DELETE_WINDOW", False);
    wmTargets = XInternAtom(display, "TARGETS", False);
}

/**
 * Finish X11 driver
 */
void ui_fini()
{
    int i;

    for(i=0;i<5;i++) XFreeCursor(display, cursors[i]);
    XCloseDisplay(display);
}

/**
 * Convert an X11 event
 */
void ui_getevent()
{
    XEvent e;
#ifdef HAS_XMU
    XEvent r;
#endif
    KeySym k;

    e.type = None;
    event.type = E_NONE;
    XNextEvent(display, &e);
    event.win = ui_getwin((void*)e.xany.window);
    switch(e.type) {
        case Expose:
            event.type = E_REFRESH;
            event.x = e.xexpose.x;
            event.y = e.xexpose.y;
            event.w = e.xexpose.width;
            event.h = e.xexpose.height;
        break;
        case ConfigureNotify:
            event.type = E_RESIZE;
            event.x = event.y = 0;
            event.w = e.xconfigure.width;
            event.h = e.xconfigure.height;
        break;
        case ClientMessage:
            if((Atom)e.xclient.data.l[0] == wmDel) event.type = E_CLOSE;
        break;
        case KeyRelease:
            if(!keypressed) break; else keypressed--;
            k = XLookupKeysym(&e.xkey, 0);
            event.x = XLookupKeysym(&e.xkey, keyflags & 1);
            switch(k) {
                case XK_F1: event.x = K_F1; break;
                case XK_Escape: event.x = K_ESC; break;
                case XK_Delete: event.x = K_DEL; break;
                case XK_BackSpace: event.x = K_BACKSPC; break;
                case XK_Tab: event.x = K_TAB; break;
                case XK_Up: event.x = K_UP; break;
                case XK_Down: event.x = K_DOWN; break;
                case XK_Left: event.x = K_LEFT; break;
                case XK_Right: event.x = K_RIGHT; break;
                case XK_Home: event.x = K_HOME; break;
                case XK_End: event.x = K_END; break;
                case XK_Page_Up: event.x = K_PGUP; break;
                case XK_Page_Down: event.x = K_PGDN; break;
                case XK_Return: event.x = K_ENTER; break;
                case XK_Shift_L:
                case XK_Shift_R: event.x = 0; keyflags &= ~1; break;
                case XK_Control_L:
                case XK_Control_R: event.x = 0; keyflags &= ~2; break;
                case XK_Super_L:
                case XK_Super_R:
                case XK_Alt_L:
                case XK_Alt_R: event.x = 0; keyflags &= ~4;break;
            }
            event.h = keyflags;
            if(event.x) event.type = E_KEY;
        break;
        case KeyPress:
            k = XLookupKeysym(&e.xkey, 0);
            keypressed++;
            switch(k) {
                case XK_Shift_L:
                case XK_Shift_R: keyflags |= 1; break;
                case XK_Control_L:
                case XK_Control_R: keyflags |= 2; break;
                case XK_Super_L:
                case XK_Super_R:
                case XK_Alt_L:
                case XK_Alt_R: keyflags |= 4; break;
            }
        break;
        case MotionNotify:
            event.type = E_MOUSEMOVE;
            event.x = e.xmotion.x;
            event.y = e.xmotion.y;
            event.w = btnflags;
            event.h = keyflags;
        break;
        case ButtonPress:
            event.type = E_BTNPRESS;
            event.x = e.xbutton.x;
            event.y = e.xbutton.y;
            btnflags |= (1<<(e.xbutton.button-1));
            event.w = btnflags;
            event.h = keyflags;
        break;
        case ButtonRelease:
            event.type = E_BTNRELEASE;
            event.x = e.xbutton.x;
            event.y = e.xbutton.y;
            btnflags &= ~(1<<(e.xbutton.button-1));
            event.w = btnflags;
            event.h = keyflags;
        break;
#ifdef HAS_XMU
        case SelectionRequest:
            if(selection[0]) {
                XChangeProperty(display, e.xselectionrequest.requestor, e.xselectionrequest.property,
                    XA_UTF8_STRING(display), 8, PropModeReplace, selection, strlen((char*)selection));
                r.xselection.type = SelectionNotify;
                r.xselection.requestor = e.xselectionrequest.requestor;
                r.xselection.property = e.xselectionrequest.property;
                r.xselection.display = display;
                r.xselection.selection = e.xselectionrequest.selection;
                r.xselection.target = e.xselectionrequest.target;
                r.xselection.time = e.xselectionrequest.time;
                XSendEvent(display, e.xselectionrequest.requestor, 0, 0, &r);
                XFlush(display);
            }
        break;
#endif
    }
}
