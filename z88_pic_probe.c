/* z88_pic_probe.c - text screen plus a 64x64 picture drawn once in the
 * graphics map at the right-hand side.
 *
 *   zcc +z88 -subtype=app -O2 -create-app z88_pic_probe.c -opicprobe
 *
 * FIRST RUN: a 512-px map opened with an all-zero struct covers the text from
 * about column 11 rightwards and hides it.  z88.h shows the struct has a
 * window number, position and size, so this version opens the map as its
 * own small window (number '2', 11x8 characters at the right) and leaves the
 * default text window alone.  If the picture lands in the wrong place, try
 *   -DWIN_X=70   (other column)       -DWIN_TYPE=129 / 131 (other window type)
 *
 * Questions this answers:
 *   1. Does a map opened ONCE after the text is up coexist with text?
 *   2. Is x=436..499 to the right of the text, or does it land on it?
 * The map is opened exactly once (that is the pattern that was stable
 * before).  Any key repaints the text only; ESC quits.
 */
#if defined(__Z88__)
#include <arch/z88/dor.h>
#define HELP1 "Picture probe"
#define HELP2 ""
#define HELP3 ""
#define HELP4 ""
#define HELP5 ""
#define HELP6 ""
#define APP_INFO "PicProbe"
#define APP_KEY 'I'
#define APP_NAME "PicProbe"
#include <arch/z88/application.h>
#endif

#include <graphics.h>
#include <arch/z88/z88.h>
#include <stdio.h>
#include <string.h>
#include "jester.h"

static void at(int x, int y)
{
    putchar(1); putchar('3'); putchar('@'); putchar(32 + x); putchar(32 + y);
}

static void text(void)
{
    int y;
    for (y = 0; y < 8; y++) {
        at(0, y);
        printf("ROW %d  0123456789 0123456789 0123456789 0123456789 0123456789 0123456789", y);
    }
}

/* Picture window: z88.h says  struct window { number, x, y, width, depth,
 * type, graph }  with graph 1 = 256 px map, 2 = 512 px map.  Positions are
 * in characters from the top-left of the application area.  Window 1 is the
 * default text window (the game's text); the picture gets window 2, 11
 * characters (66 px) wide and the full 8 rows (64 px) high, at the far right. */
#ifndef WIN_X
#define WIN_X 80
#endif
#ifndef WIN_TYPE
#define WIN_TYPE 128
#endif

static void select_text_window(void)       /* 1,'2','H','1' */
{
    putchar(1); putchar('2'); putchar('H'); putchar('1');
}

void main()
{
    static struct window gfx;
    int x, y, k;

    text();
    memset(&gfx, 0, sizeof gfx);
    gfx.number = '2';
    gfx.x      = WIN_X;
    gfx.y      = 0;
    gfx.width  = 11;
    gfx.depth  = 8;
    gfx.type   = WIN_TYPE;
    gfx.graph  = 1;                      /* 256-pixel map; the window shows its left part */
    window(&gfx);
    clg();                               /* the map is NOT cleared on open */

    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if (jester[y][x >> 3] & (0x80 >> (x & 7)))
                plot(x, y);

    select_text_window();                /* text goes to window 1 again */
    at(0, 0);
    printf("PICTURE WINDOW OPENED - ESC QUITS");

    for (;;) {
        k = getk();
        if (k == 27) break;
    }
    closegfx(&gfx);
}
