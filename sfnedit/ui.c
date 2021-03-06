/*
 * sfnedit/ui.c
 *
 * Copyright (C) 2020 bzt (bztsrc@gitlab)
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
 * @brief Common user interface routines
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "stb_png.h"
#include "lang.h"
#include "ui.h"
#include "icon.h"
#include "util.h"
#define SSFN_CONSOLEBITMAP_TRUECOLOR
#define SSFN_COMMON
#include "libsfn.h"

char verstr[8];
extern char filename[FILENAME_MAX+64];

uint8_t *icon32, *icon64, *tools, *bga;

uint32_t theme[] = { 0xFF454545, 0xFFBEBEBE, 0xFF5C5C5C, 0xFF343434, 0xFF606060, 0xFF303030, 0xFF3C3C3C,
    0xFF101010, 0xFF686868, 0xFF515151, 0xFF484848, 0xFF404040,
                0xFF744C4C, 0xFF5D3535, 0xFF542C2C, 0xFF4C2424,
    0xFF606060, 0xFFF0F0F0, 0xFF909090, 0xFF4E4E4E,
    0xFF101010, 0xFF343434, 0xFFB0B0B0,
    0xFF800000, 0xFF004040, 0xFF005050, 0xFFFF0000, 0xFF007F7F, 0xFF0000B0, 0xFF00B000, 0xFF007F00, 0xFF005050 };

int gw = 36+16+512, gh = 24+8+512, gotevt = 0, quiet = 0, lastpercent = 100, mainloop = 1, modified = 0, posx = 0, posy = 0;
int scrolly  = 0;
char ws[0x110000], *status, *errstatus = NULL;

int numwin = 0;
ui_win_t *wins = NULL;
ui_event_t event;

void *winid = NULL;
int winidx = 0;

ssfn_t logofnt;
int cursor = CURSOR_PTR, lastcursor = -1, seltool = -1;
int zip = 1, ascii = 0, selfield = -1;

extern int lastsave, input_maxlen, input_callback, selstart, selend, selfiles, clkfiles, selranges, clkranges, selkern;
extern char *input_str, *input_cur, *input_scr, *input_end, gstat[];

/**
 * Read in a theme from a GIMP Palette file
 */
void ui_gettheme(char *fn)
{
    FILE *f;
    char line[256], *s;
    int i = 0, r,g,b;
    f = fopen(fn, "r");
    if(f) {
        while(i < THEME_UNDEF && !feof(f)) {
            line[0] = 0;
            if(!fgets(line, 256, f) || !line[0] || line[0] == '\r' || line[0] == '\n' || line[0] == '#' ||
                !memcmp(line, "GIMP", 4) || !memcmp(line, "Name", 4)) continue;
            line[255] = 0; s = line;
            for(; *s && *s == ' '; s++);
            r = atoi(s); for(; *s && *s != ' '; s++);
            for(; *s && *s == ' '; s++);
            g = atoi(s); for(; *s && *s != ' '; s++);
            for(; *s && *s == ' '; s++);
            b = atoi(s); for(; *s && *s != ' '; s++);
            theme[i] = 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
        }
        fclose(f);
    }
}

/**
 * Compare two strings
 */
int ui_casecmp(char *a, char *b, int l)
{
    for(;*a && *b && l--;a++,b++) {
        if(tolowercase(*a) != tolowercase(*b)) return 1;
    }
    return 0;
}

/**
 * Report an error and exit
 */
void ui_error(char *subsystem, int fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "sfnedit: %s: ", subsystem);
    vfprintf(stderr, lang[fmt], args);
    fprintf(stderr, "\n");
    exit(fmt + 1);
}

/**
 * Update window titles
 */
void ui_updatetitle(int idx)
{
    char title[278], *fn = NULL;
    int i;
    if(ctx.filename)
        fn = strrchr(ctx.filename, DIRSEP);
    if(!fn) fn = ctx.filename; else fn++;
    if(!idx) {
        memcpy(title, "sfnedit", 8);
        if(fn) { memcpy(title + 7, " - ", 3); strncpy(title + 10, fn, 256); }
        ui_titlewin(&wins[0], title);
    }
    for(i = idx ? idx : 1; i < (idx ? idx + 1 : numwin); i++) {
        sprintf(title, "sfnedit - U+%06X - ", wins[i].unicode);
        if(fn) strncpy(title + 21, fn, 256);
        ui_titlewin(&wins[i], title);
    }
}

/**
 * Get number of input fields on a window
 */
int ui_maxfield(int idx)
{
    if(!idx) {
        switch(wins[idx].tool) {
            case MAIN_TOOL_ABOUT: return 6;
            case MAIN_TOOL_LOAD: return 12;
            case MAIN_TOOL_SAVE: return 12;
            case MAIN_TOOL_PROPS: return 16;
            case MAIN_TOOL_RANGES: return 8;
            case MAIN_TOOL_GLYPHS: return 15;
            case MAIN_TOOL_DOSAVE: return 7;
            case MAIN_TOOL_NEW: return 7;
        }
    } else {
        switch(wins[idx].tool) {
            case GLYPH_TOOL_COORD: return 23;
            case GLYPH_TOOL_LAYER: return 15;
            case GLYPH_TOOL_KERN: return 8;
            case GLYPH_TOOL_COLOR: return 7;
        }
    }
    return 3;
}

/**
 * Return text width in pixels
 */
int ui_textwidth(char *str)
{
    int ret = 0, u;
    if(!str || !*str) return 0;
    while((u = ssfn_utf8(&str)))
        ret += (int)ws[u];
    return ret;
}

/**
 * Open a window
 */
void ui_openwin(uint32_t unicode)
{
    int w = gw, h = gh, i, j = 0;

    for(i=0; i < numwin; i++) {
        if(wins[i].winid) {
            if(wins[i].unicode == unicode) { ui_focuswin(&wins[i]); return; }
        } else {
            if(!j) j = i;
        }
    }
    if(!j) {
        if(!numwin) {
            w = MAIN_W; h = MAIN_H; unicode = WINTYPE_MAIN;
        } else {
            w = gw; h = gh;
        }
        j = numwin++;
        wins = (ui_win_t*)realloc(wins, numwin*sizeof(ui_win_t));
        if(!wins) ui_error("openwin", ERR_MEM);
    }
    memset(&wins[j], 0, sizeof(ui_win_t));
    wins[j].unicode = unicode;
    wins[j].uniname = uninames[uniname(unicode)].name;
    wins[j].winid = ui_createwin(w, h);
    wins[j].field = -1;
    wins[j].rc = 1;
    if(unicode == WINTYPE_MAIN) { wins[j].zoom = 4; wins[j].tool = -1; }
    else if(!ctx.glyphs[unicode].numlayer) {
        ctx.glyphs[unicode].width = ctx.width;
        ctx.glyphs[unicode].height = ctx.height;
        if(!iswhitespace(unicode))
            ctx.glyphs[unicode].adv_x = ctx.glyphs[unicode].adv_y = ctx.glyphs[unicode].ovl_x = 0;
    }
    input_maxlen = 0;
    input_str = input_cur = NULL;
    ui_updatetitle(j);
    ui_resizewin(&wins[j], w, h);
    ui_refreshwin(j, 0, 0, w, h);
    ui_focuswin(&wins[j]);
}

/**
 * Close a window
 */
void ui_closewin(int idx)
{
    int i;

    ui_cursorwin(&wins[idx], CURSOR_PTR);
    ui_cursorwin(&wins[0], CURSOR_PTR);
    ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);
    if(idx < 0 || idx >= numwin || !wins[idx].winid) return;
    hist_free(&wins[idx]);
    if(!idx) {
        for(i=1; i < numwin; i++)
            if(wins[i].winid)
                ui_closewin(i);
        numwin = 1;
        if(modified && wins[0].tool != MAIN_TOOL_DOSAVE) {
            wins[0].tool = MAIN_TOOL_DOSAVE;
            ui_resizewin(&wins[0], wins[0].w, wins[0].h);
            ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);
        } else
            mainloop = 0;
        return;
    }
    ui_destroywin(&wins[idx]);
    wins[idx].winid = NULL;
    wins[idx].surface = NULL;
    wins[idx].data = NULL;
    wins[idx].unicode = -1;
    wins[idx].histmin = wins[idx].histmax = 0;
    while(numwin && !wins[numwin-1].winid) numwin--;
}

/**
 * Get common window id (idx) for driver specific window id
 */
int ui_getwin(void *wid)
{
    int i;

    if(wid == winid) return winidx;
    for(i=0; i < numwin; i++)
        if(wins[i].winid == wid) {
            winid = wid;
            winidx = i;
            return i;
        }
    return -1;
}

/**
 * Load resources
 */
static void ui_loadrc()
{
    unsigned int w, h, f;
    uint8_t c, *ptr;
    stbi__context s;
    stbi__result_info ri;

    sprintf(verstr, "v%d.%d.%d", SSFN_VERSION >> 8, (SSFN_VERSION >> 4) & 0xF, SSFN_VERSION & 0xF);

    /* load icons */
    s.read_from_callbacks = 0;

    s.img_buffer = s.img_buffer_original = icon32_png;
    s.img_buffer_end = s.img_buffer_original_end = icon32_png + sizeof(icon32_png);
    ri.bits_per_channel = 8;
    icon32 = (uint8_t*)stbi__png_load(&s, (int*)&w, (int*)&h, (int*)&f, 0, &ri);
    if(!icon32) ui_error("ui_loadrc", ERR_MEM);

    s.img_buffer = s.img_buffer_original = icon64_png;
    s.img_buffer_end = s.img_buffer_original_end = icon64_png + sizeof(icon64_png);
    ri.bits_per_channel = 8;
    icon64 = (uint8_t*)stbi__png_load(&s, (int*)&w, (int*)&h, (int*)&f, 0, &ri);
    if(!icon64) ui_error("ui_loadrc", ERR_MEM);

    s.img_buffer = s.img_buffer_original = icons_png;
    s.img_buffer_end = s.img_buffer_original_end = icons_png + sizeof(icons_png);
    ri.bits_per_channel = 8;
    tools = (uint8_t*)stbi__png_load(&s, (int*)&w, (int*)&h, (int*)&f, 0, &ri);
    if(!tools) ui_error("ui_loadrc", ERR_MEM);

    s.img_buffer = s.img_buffer_original = bga_png;
    s.img_buffer_end = s.img_buffer_original_end = bga_png + sizeof(bga_png);
    ri.bits_per_channel = 8;
    bga = (uint8_t*)stbi__png_load(&s, (int*)&w, (int*)&h, (int*)&f, 0, &ri);
    /* not an issue if we can't load this */

    /* uncompress ui font */
    ptr = unifont_gz + 3;
    c = *ptr++; ptr += 6;
    if(c & 4) { w = *ptr++; w += (*ptr++ << 8); ptr += w; }
    if(c & 8) { while(*ptr++ != 0); }
    if(c & 16) { while(*ptr++ != 0); }
    f = sizeof(unifont_gz) - (size_t)(ptr - unifont_gz);
    w = 0;
    ssfn_src = (ssfn_font_t*)stbi_zlib_decode_malloc_guesssize_headerflag((const char*)ptr, f, 4096, (int*)&w, 0);
    if(!ssfn_src) ui_error("ui_loadrc", ERR_MEM);
    memset(&ws, 0, sizeof(ws));
    ptr = (uint8_t*)ssfn_src + ssfn_src->characters_offs;
    for(ptr = (uint8_t*)ssfn_src + ssfn_src->characters_offs, w = 0; w < 0x110000; w++) {
        if(ptr[0] == 0xFF) { w += 65535; ptr++; }
        else if((ptr[0] & 0xC0) == 0xC0) { h = (((ptr[0] & 0x3F) << 8) | ptr[1]); w += h; ptr += 2; }
        else if((ptr[0] & 0xC0) == 0x80) { h = (ptr[0] & 0x3F); w += h; ptr++; }
        else {
            ws[w] = ptr[2];
            ptr += 6 + ptr[1] * (ptr[0] & 0x40 ? 6 : 5);
        }
    }

    memset(&logofnt, 0, sizeof(ssfn_t));
    ssfn_load(&logofnt, &logo_sfn);
    ssfn_select(&logofnt, SSFN_FAMILY_SERIF, NULL, SSFN_STYLE_REGULAR, 56);
}

/**
 * Quit handler (via Ctrl-C signal or normally)
 */
void ui_quit(int sig __attribute__((unused)))
{
    copypaste_fini();
    ui_fini();
    ssfn_free(&logofnt);
    sfn_free();
    uniname_free();
    free(icon64);
    free(icon32);
    free(tools);
    free(bga);
    free(ssfn_src);
    if(sig) exit(1);
}

/**
 * Progress bar hook
 */
void ui_pb(int step, int numstep, int curr, int total, int msg)
{
    int i, n;
    char s[64];

    n = (long int)(curr + 1) * 100L / (long int)(total + 1);
    if(n == lastpercent) return;
    lastpercent = n;
    ui_box(&wins[0], 0, wins[0].h - 18, wins[0].w, 18, theme[THEME_DARK], theme[THEME_DARK], theme[THEME_DARK]);
    i = 18;
    if(numstep > 0) {
        ui_box(&wins[0], 0, wins[0].h - 18, wins[0].w * step / numstep, 6, theme[THEME_LIGHTER], theme[THEME_LIGHT],
            theme[THEME_DARKER]);
        i = 12;
        sprintf(s, "[ %d / %d ] %3d%%", step, numstep, n);
    } else
        sprintf(s, "%3d%%", n);
    ui_box(&wins[0], 0, wins[0].h - i, (int)(long)wins[0].w * (long)(curr + 1) / (long)(total + 1), i,
        theme[THEME_LIGHTER], theme[THEME_LIGHT], theme[THEME_LIGHTER]);
    ssfn_dst.fg = theme[THEME_FG];
    ssfn_dst.bg = 0;
    ui_text(&wins[0], 0, wins[0].h - 18, s);
    ui_text(&wins[0], ssfn_dst.x + 8, wins[0].h - 18, !msg ? "" : lang[msg - 1 + STAT_MEASURE]);
    ui_flushwin(&wins[0], 0, wins[0].h - 18, wins[0].w, 18);
}

/**
 * Get character info to status bar
 */
void ui_chrinfo(int unicode)
{
    char *u, *s;
    u = utf8(unicode);
    if(!u[1]) u[2] = u[3] = 0; else if(!u[2]) u[3] = 0;
    if(unicode >= SSFN_LIG_FIRST && unicode <= SSFN_LIG_LAST) {
        sprintf(gstat, "U+%06X  %02x %02x %02x %02x  %d  %s  %s #%d", unicode,
            (unsigned char)u[0], (unsigned char)u[1], (unsigned char)u[2], (unsigned char)u[3], unicode,
            ctx.ligatures[unicode - SSFN_LIG_FIRST] ? ctx.ligatures[unicode - SSFN_LIG_FIRST] : "",
            lang[GLYPHS_LIGATURE], unicode - SSFN_LIG_FIRST);
    } else {
        s = uninames[uniname(unicode)].name;
        sprintf(gstat, "U+%06X  %02x %02x %02x %02x  %d  %s  %s", unicode,
            (unsigned char)u[0], (unsigned char)u[1], (unsigned char)u[2], (unsigned char)u[3], unicode,
            u, s && *s ? s : lang[GLYPHS_UNDEF]);
    }
    status = gstat;
}

/**
 * Redraw part of a window
 */
void ui_refreshwin(int idx, int wx, int wy, int ww, int wh)
{
    ui_win_t *win = &wins[idx];
    int i, j, k, m, p, q;
    uint32_t lc = theme[THEME_BG] + 0x060606;
    uint8_t *b = (uint8_t*)&theme[THEME_BG], *c = (uint8_t*)&lc;
    char st[32];

    if(idx < 0 || idx >= numwin || win->w < 8 || win->h < 8 || wx+ww < 32 || wy+wh < 32) return;
    ssfn_dst.w = win->w;
    ssfn_dst.h = win->h;
    ssfn_dst.fg = theme[THEME_FG];
    ssfn_dst.bg = 0;
    input_maxlen = 0;
    if(bga && (win->help || (!idx && wins[idx].tool != MAIN_TOOL_GLYPHS))) {
        p = win->w > 256 ? 256 : win->w;
        q = win->h > 256 ? 256 : win->h;
        for(k = (win->h-q+1)*win->p - p, m = ((256 - q) << 8) + (256 - p), j = 0; j < q; j++, k += win->p, m += 256)
            for(i = 0; i < p; i++)
                if(bga[m+i]) {
                    ((uint8_t*)&win->data[k+i])[0] = (c[0]*bga[m+i] + (256 - bga[m+i])*b[0])>>8;
                    ((uint8_t*)&win->data[k+i])[1] = (c[1]*bga[m+i] + (256 - bga[m+i])*b[1])>>8;
                    ((uint8_t*)&win->data[k+i])[2] = (c[2]*bga[m+i] + (256 - bga[m+i])*b[2])>>8;
                }
    }
    if(win->help) {
        view_help(idx);
    } else {
        ui_toolbox(idx);
        ssfn_dst.bg = theme[THEME_BG];
        if(!idx) {
            switch(wins[idx].tool) {
                case -1:
                case MAIN_TOOL_ABOUT: view_about(); break;
                case MAIN_TOOL_LOAD: view_fileops(0); break;
                case MAIN_TOOL_SAVE: view_fileops(1); break;
                case MAIN_TOOL_PROPS: view_props(); break;
                case MAIN_TOOL_RANGES: view_ranges(); break;
                case MAIN_TOOL_GLYPHS: view_glyphs(); break;
                case MAIN_TOOL_DOSAVE: view_dosave(); break;
                case MAIN_TOOL_NEW: view_new(); break;
            }
        } else {
            switch(wins[idx].tool) {
                case GLYPH_TOOL_COORD: view_coords(idx); break;
                case GLYPH_TOOL_LAYER: view_layers(idx); break;
                case GLYPH_TOOL_KERN: view_kern(idx); break;
                case GLYPH_TOOL_COLOR: view_color(idx); break;
            }
        }
        ssfn_dst.w = win->w;
        ssfn_dst.h = win->h;
        ssfn_dst.fg = theme[THEME_FG];
        ssfn_dst.bg = 0;
        if(errstatus) {
            ui_box(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18,
                theme[THEME_BTN1D], theme[THEME_BTN1D], theme[THEME_BTN1D]);
            ssfn_dst.bg = theme[THEME_BTN1D];
            ui_text(&wins[event.win], 0, wins[event.win].h - 18, errstatus);
        } else {
            ui_box(win, 0, win->h - 18, win->w, 18, theme[THEME_DARK], theme[THEME_DARK], theme[THEME_DARK]);
            if(event.win && posx != -1 && posy != -1) {
                sprintf(st, "X: %3d Y: %3d", posx, posy);
                ui_text(win, 0, win->h - 18, st);
            }
        }
    }
    ui_flushwin(win, wx, wy, ww, wh);
}

/**
 * Refresh all windows
 */
void ui_refreshall()
{
    int i;

    for(i=0; i < numwin; i++)
        if(wins[i].winid) {
            if(wins[i].unicode < 0x110000 && !ctx.glyphs[wins[i].unicode].numlayer &&
                !ctx.glyphs[wins[i].unicode].adv_x && !ctx.glyphs[wins[i].unicode].adv_y)
                    ui_closewin(i);
            else {
                hist_free(&wins[i]);
                if(i) { wins[i].zoom = 0; wins[i].rc = 1; }
                ui_resizewin(&wins[i], wins[i].w, wins[i].h);
                ui_refreshwin(i, 0, 0, wins[i].w, wins[i].h);
            }
        }
}

/**
 * Finish user input
 */
void ui_inputfinish()
{
    char *s = input_str;
    if(input_maxlen && input_str && input_callback)
        switch(input_callback) {
            case 1:
                if((input_str[0] == 'U' || input_str[0] == 'u') && input_str[1] == '+') rs = (int)gethex(input_str + 2, 6);
                else rs = (int)ssfn_utf8(&s);
                if(rs < 0) rs = 0;
                if(rs > 0x10FFFF) rs = 0x10FFFF;
                sprintf(input_str, "U+%X", rs);
            break;
            case 2:
                if((input_str[0] == 'U' || input_str[0] == 'u') && input_str[1] == '+') re = (int)gethex(input_str + 2, 6);
                else re = (int)ssfn_utf8(&s);
                if(re < 0) re = 0;
                if(re > 0x10FFFF) re = 0x10FFFF;
                sprintf(input_str, "U+%X", re);
            break;
            case 3: sfn_setstr(&ctx.name, input_str, 0); break;
            case 4: sfn_setstr(&ctx.familyname, input_str, 0); break;
            case 5: sfn_setstr(&ctx.subname, input_str, 0); break;
            case 6: sfn_setstr(&ctx.revision, input_str, 0); break;
            case 7: sfn_setstr(&ctx.manufacturer, input_str, 0); break;
            case 8: sfn_setstr(&ctx.license, input_str, 0); break;
            default:
                if(input_callback >= 1024 && input_callback <= 1024 + SSFN_LIG_LAST - SSFN_LIG_FIRST)
                    sfn_setstr(&ctx.ligatures[input_callback - 1024], input_str, 0);
            break;
        }
    input_str = input_cur = NULL; input_maxlen = 0;
}

/**
 * Main user interface event handler
 */
void ui_main(char *fn)
{
    int i, j;
    char *s, st[32];

    /* load resources */
    ui_loadrc();
    /* driver specific init */
    ui_init();
    /* if specified a file on command line, load it */
    ui_openwin(WINTYPE_MAIN);
    ui_cursorwin(&wins[0], CURSOR_LOADING);
    sfn_init(ui_pb);
    if(fn && sfn_load(fn, 0)) {
        s = strrchr(fn, DIRSEP);
        if(s) s++; else s = fn;
        strcpy(filename, s);
        sfn_sanitize(-1);
        wins[0].tool = MAIN_TOOL_GLYPHS;
        ui_updatetitle(0);
        ui_resizewin(&wins[0], wins[0].w, wins[0].h);
        ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);
    } else
        wins[0].tool = MAIN_TOOL_ABOUT;
    ui_cursorwin(&wins[0], CURSOR_PTR);
    ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);

    /* main event loop */
    while(mainloop) {
        ui_getevent();
        if(event.win < 0) continue;
        if(event.type != E_REFRESH && event.type != E_MOUSEMOVE && event.type != E_BTNRELEASE) errstatus = NULL;
        if(wins[event.win].help) {
            switch(event.type) {
                case E_CLOSE: ui_closewin(event.win); break;
                case E_RESIZE:
                    ui_resizewin(&wins[event.win], event.w, event.h);
                    ui_refreshwin(event.win, 0, 0, event.w, event.h);
                    if(event.win) { gw = event.w; gh = event.h; }
                break;
                case E_REFRESH: ui_refreshwin(event.win, event.x, event.y, event.w, event.h); break;
                case E_BTNRELEASE: case E_KEY:
                    wins[event.win].help = 0;
                    ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                    ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                break;
            }
            continue;
        }
        if(event.type == E_KEY && (event.h & (3 << 1)) && (event.x == 's' || event.x == 'S')) {
            if(modified || 1) {
                wins[0].field = -1;
                view_fileops(1);
                if((event.h & 1) || !filename[0]) {
                    wins[0].field = 11;
                    wins[0].tool = MAIN_TOOL_SAVE;
                    ui_resizewin(&wins[0], wins[0].w, wins[0].h);
                    ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);
                    ui_focuswin(&wins[0]);
                    ctrl_dosave_onenter();
                    wins[0].field = -1;
                } else {
                    wins[0].field = 12;
                    ctrl_fileops_onenter(1);
                    wins[0].field = -1;
                    if(wins[0].tool == MAIN_TOOL_SAVE) ui_refreshwin(0, 0, 0, wins[0].w, wins[0].h);
                    if(errstatus) {
                        ui_box(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18,
                        theme[THEME_BTN1D], theme[THEME_BTN1D], theme[THEME_BTN1D]);
                        ssfn_dst.bg = theme[THEME_BTN1D];
                        ui_text(&wins[event.win], 0, wins[event.win].h - 18, errstatus);
                        ui_flushwin(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18);
                    }
                    modified = 0;
                }
            }
            continue;
        }
        switch(event.type) {
            case E_CLOSE: ui_closewin(event.win); break;
            case E_RESIZE:
                ui_resizewin(&wins[event.win], event.w, event.h);
                ui_refreshwin(event.win, 0, 0, event.w, event.h);
                if(event.win) { gw = event.w; gh = event.h; }
            break;
            case E_REFRESH: ui_refreshwin(event.win, event.x, event.y, event.w, event.h); break;
            case E_MOUSEMOVE:
                status = NULL; cursor = CURSOR_PTR;
                if(event.win && wins[event.win].tool == GLYPH_TOOL_COORD && event.y < 24 && event.x >= 144)
                    cursor = CURSOR_GRAB;
                if(event.y < 23 && event.x < 144) {
                    i = (event.x - 1) / 24;
                    if(i < (!event.win ? 6 : 3))
                        status = lang[(!event.win ? MTOOL_ABOUT : GTOOL_MEASURES) + i];
                    if(event.win && event.x >= 6 + 3 * 24)
                        ui_chrinfo(wins[event.win].unicode);
                } else {
                    if(!event.win)
                        switch(wins[0].tool) {
                            case MAIN_TOOL_ABOUT: ctrl_about_onmove(); break;
                            case MAIN_TOOL_LOAD: case MAIN_TOOL_SAVE : ctrl_fileops_onmove(); break;
                            case MAIN_TOOL_RANGES: ctrl_ranges_onmove(); break;
                            case MAIN_TOOL_GLYPHS: ctrl_glyphs_onmove(); break;
                        }
                    else
                        switch(wins[event.win].tool) {
                            case GLYPH_TOOL_COORD: ctrl_coords_onmove(event.win); break;
                            case GLYPH_TOOL_LAYER: ctrl_layers_onmove(event.win); break;
                            case GLYPH_TOOL_KERN: ctrl_kern_onmove(event.win); break;
                            case GLYPH_TOOL_COLOR: ctrl_colors_onmove(event.win); break;
                        }
                }
                ssfn_dst.w = wins[event.win].w;
                ssfn_dst.h = wins[event.win].h;
                ssfn_dst.fg = theme[THEME_FG];
                if(errstatus) {
                    ui_box(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18,
                    theme[THEME_BTN1D], theme[THEME_BTN1D], theme[THEME_BTN1D]);
                    ssfn_dst.bg = theme[THEME_BTN1D];
                    ui_text(&wins[event.win], 0, wins[event.win].h - 18, errstatus);
                } else {
                    ui_box(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18,
                        theme[THEME_DARK], theme[THEME_DARK], theme[THEME_DARK]);
                    ssfn_dst.bg = theme[THEME_DARK];
                    if(status) {
                        ui_text(&wins[event.win], 0, wins[event.win].h - 18, status);
                        posx = posy = -1;
                    } else if(event.win && posx != -1 && posy != -1) {
                        sprintf(st, "X: %3d Y: %3d", posx, posy);
                        ui_text(&wins[event.win], 0, wins[event.win].h - 18, st);
                    }
                }
                if(cursor != lastcursor) {
                    ui_cursorwin(&wins[event.win], cursor);
                    lastcursor = cursor;
                }
                ui_flushwin(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18);
            break;
            case E_BTNPRESS:
                ui_inputfinish();
                if(event.win && wins[event.win].tool == GLYPH_TOOL_COORD && event.y < 24 && event.x >= 144 &&
                    wins[event.win].unicode > 0) {
                    s = utf8(wins[event.win].unicode);
                    if(s && *s) ui_copy(s);
                    ssfn_dst.w = wins[event.win].w;
                    ssfn_dst.h = wins[event.win].h;
                    ssfn_dst.fg = theme[THEME_FG];
                    ssfn_dst.bg = theme[THEME_DARK];
                    ui_box(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18,
                        theme[THEME_DARK], theme[THEME_DARK], theme[THEME_DARK]);
                    ui_text(&wins[event.win], 0, wins[event.win].h - 18, lang[COORDS_CLIPBRD]);
                    ui_flushwin(&wins[event.win], 0, wins[event.win].h - 18, wins[event.win].w, 18);
                } else
                if(event.y < 23 && event.x < 144) {
                    selstart = selend = selfiles = clkfiles = selranges = clkranges = -1;
                    i = (event.x - 1) / 24;
                    if(i < (!event.win ? 6 : 3)) {
                        seltool = i;
                        ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                    }
                    if(wins[event.win].unicode >= SSFN_LIG_FIRST && wins[event.win].unicode <= SSFN_LIG_LAST &&
                        event.x >= 6 + 3 * 24 && event.x <= 6 + 3 * 24 + 54) {
                            wins[event.win].field = 3;
                        }
                } else {
                    if(!event.win)
                        switch(wins[0].tool) {
                            case MAIN_TOOL_LOAD: ctrl_fileops_onbtnpress(0); break;
                            case MAIN_TOOL_SAVE: ctrl_fileops_onbtnpress(1); break;
                            case MAIN_TOOL_PROPS: ctrl_props_onbtnpress(); break;
                            case MAIN_TOOL_RANGES: ctrl_ranges_onbtnpress(); break;
                            case MAIN_TOOL_GLYPHS: ctrl_glyphs_onbtnpress(); break;
                            case MAIN_TOOL_DOSAVE: ctrl_dosave_onbtnpress(); break;
                            case MAIN_TOOL_NEW: ctrl_new_onbtnpress(); break;
                        }
                    else
                        switch(wins[event.win].tool) {
                            case GLYPH_TOOL_COORD: ctrl_coords_onbtnpress(event.win); break;
                            case GLYPH_TOOL_LAYER: ctrl_layers_onbtnpress(event.win); break;
                            case GLYPH_TOOL_KERN: ctrl_kern_onbtnpress(event.win); break;
                            case GLYPH_TOOL_COLOR: ctrl_colors_onbtnpress(event.win); break;
                        }
                    ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                }
            break;
            case E_BTNRELEASE:
                if(event.y < 23 && event.x < 144) {
                    i = (event.x - 1) / 24;
                    if(i < (!event.win ? 6 : 3) && i == seltool) {
                        wins[event.win].tool = i;
                        wins[event.win].field = selfield = selkern = -1;
                        ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                    }
                } else {
                    if(!event.win)
                        switch(wins[0].tool) {
                            case MAIN_TOOL_ABOUT: ctrl_about_onclick(); break;
                            case MAIN_TOOL_LOAD: ctrl_fileops_onclick(0); break;
                            case MAIN_TOOL_SAVE: ctrl_fileops_onclick(1); break;
                            case MAIN_TOOL_PROPS: ctrl_props_onclick(); break;
                            case MAIN_TOOL_GLYPHS: ctrl_glyphs_onclick(); break;
                            case MAIN_TOOL_DOSAVE: ctrl_dosave_onclick(); break;
                            case MAIN_TOOL_NEW: ctrl_new_onclick(); break;
                        }
                    else
                        switch(wins[event.win].tool) {
                            case GLYPH_TOOL_COORD: ctrl_coords_onclick(event.win); break;
                            case GLYPH_TOOL_LAYER: ctrl_layers_onclick(event.win); break;
                            case GLYPH_TOOL_KERN: ctrl_kern_onclick(event.win); break;
                            case GLYPH_TOOL_COLOR: ctrl_colors_onclick(event.win); break;
                        }
                }
                if(cursor != lastcursor) {
                    ui_cursorwin(&wins[event.win], cursor);
                    lastcursor = cursor;
                }
                seltool = selfield = -1;
                ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
            break;
            case E_KEY:
                seltool = -1;
                switch(event.x) {
                    case K_ESC: ui_closewin(event.win); break;
                    case K_F1:
                        if(event.win || wins[0].tool <= MAIN_TOOL_GLYPHS) {
                            wins[event.win].help = 1;
                            ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                            ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                        }
                    break;
                    case K_TAB:
                        ui_inputfinish();
                        if(event.h & 1) {
                            if(wins[event.win].field == -1)
                                wins[event.win].field = ui_maxfield(event.win);
                            else {
                                wins[event.win].field--;
                                if(event.win && wins[event.win].field == 3 && (wins[event.win].unicode < SSFN_LIG_FIRST ||
                                    wins[event.win].unicode > SSFN_LIG_LAST)) wins[event.win].field--;
                            }
                        } else {
                            if(wins[event.win].field == ui_maxfield(event.win))
                                wins[event.win].field = -1;
                            else {
                                wins[event.win].field++;
                                if(event.win && wins[event.win].field == 3 && (wins[event.win].unicode < SSFN_LIG_FIRST ||
                                    wins[event.win].unicode > SSFN_LIG_LAST)) wins[event.win].field++;
                            }
                        }
                        ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                        ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                    break;
                    case K_ENTER:
                        if(input_maxlen) {
                            ui_inputfinish();
                            if(wins[event.win].field == ui_maxfield(event.win))
                                wins[event.win].field = -1;
                            else
                                wins[event.win].field++;
                            ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                        } else {
                            if(wins[event.win].field > -1 && wins[event.win].field < (!event.win ? 6 : 3)) {
                                wins[event.win].tool = wins[event.win].field;
                                wins[event.win].field = seltool = selfield = selkern = -1;
                                ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                                ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                            } else {
                                if(!event.win)
                                    switch(wins[0].tool) {
                                        case MAIN_TOOL_ABOUT: ctrl_about_onenter(); break;
                                        case MAIN_TOOL_LOAD: ctrl_fileops_onenter(0); break;
                                        case MAIN_TOOL_SAVE: ctrl_fileops_onenter(1); break;
                                        case MAIN_TOOL_PROPS: ctrl_props_onenter(); break;
                                        case MAIN_TOOL_RANGES: ctrl_ranges_onenter(); break;
                                        case MAIN_TOOL_GLYPHS: ctrl_glyphs_onenter(); break;
                                        case MAIN_TOOL_DOSAVE: ctrl_dosave_onenter(); break;
                                        case MAIN_TOOL_NEW: ctrl_new_onenter(); break;
                                        break;
                                    }
                                else
                                    switch(wins[event.win].tool) {
                                        case GLYPH_TOOL_COORD: ctrl_coords_onenter(event.win); break;
                                        case GLYPH_TOOL_LAYER: ctrl_layers_onenter(event.win); break;
                                        case GLYPH_TOOL_KERN: ctrl_kern_onenter(event.win); break;
                                        case GLYPH_TOOL_COLOR: ctrl_colors_onenter(event.win); break;
                                    }
                                ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                                ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                            }
                        }
                    break;
                    default:
                        if(input_maxlen) {
                            switch(event.x) {
                                case K_UP: case K_HOME: input_cur = input_str; break;
                                case K_DOWN: case K_END: input_cur = input_end; break;
                                case K_LEFT:
                                    if(input_cur > input_str) {
                                        do { input_cur--; } while(input_cur > input_str && (*input_cur & 0xC0) == 0x80);
                                    }
                                break;
                                case K_RIGHT:
                                    if(input_cur < input_end) {
                                        do { input_cur++; } while(input_cur < input_end && (*input_cur & 0xC0) == 0x80);
                                    }
                                break;
                                case K_BACKSPC:
                                    if(input_cur > input_str) {
                                        s = input_cur;
                                        do { input_cur--; } while(input_cur > input_str && (*input_cur & 0xC0) == 0x80);
                                        for(i = 0; s + i <= input_end; i++) input_cur[i] = s[i];
                                        input_end -= s - input_cur;
                                        input_refresh = 1;
                                    }
                                break;
                                case K_DEL:
                                    if(input_cur < input_end) {
                                        s = input_cur;
                                        do { s++; } while(s < input_end && (*s & 0xC0) == 0x80);
                                        for(i = 0; s + i <= input_end; i++) input_cur[i] = s[i];
                                        input_end -= s - input_cur;
                                        input_refresh = 1;
                                    }
                                break;
                                default:
                                    j = strlen((char*)&event.x);
                                    if(event.x >= ' ' && input_end - input_str + j < input_maxlen) {
                                        for(s = input_end + j; s >= input_cur; s--) s[1] = s[0];
                                        memcpy(input_cur, &event.x, j);
                                        input_cur += j;
                                        input_end += j;
                                        input_refresh = 1;
                                    }
                                break;
                            }
                            *input_end = 0;
                            ui_resizewin(&wins[event.win], wins[event.win].w, wins[event.win].h);
                            ui_refreshwin(event.win, 0, 0, wins[event.win].w, wins[event.win].h);
                            input_refresh = 0;
                        } else {
                            if(!event.win)
                                switch(wins[0].tool) {
                                    case MAIN_TOOL_LOAD:
                                    case MAIN_TOOL_SAVE: ctrl_fileops_onkey(); break;
                                    case MAIN_TOOL_PROPS: ctrl_props_onkey(); break;
                                    case MAIN_TOOL_RANGES: ctrl_ranges_onkey(); break;
                                    case MAIN_TOOL_GLYPHS: ctrl_glyphs_onkey(); break;
                                    case MAIN_TOOL_NEW: ctrl_new_onkey(); break;
                                }
                            else
                                switch(wins[event.win].tool) {
                                    case GLYPH_TOOL_COORD: ctrl_coords_onkey(event.win); break;
                                    case GLYPH_TOOL_LAYER: ctrl_layers_onkey(event.win); break;
                                    case GLYPH_TOOL_KERN: ctrl_kern_onkey(event.win); break;
                                }
                        }
                    break;
                }
            break;
        }
    } /* while mainloop */
}
