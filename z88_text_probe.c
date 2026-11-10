/* z88_text_probe.c - does a plain TEXT-mode Z88 app with user-defined
 * characters work and survive Index / other-app round trips?
 *
 *   zcc +z88 -subtype=app -O2 -create-app z88_text_probe.c -otprobe
 *
 * No graphics map at all.  It redefines 8 characters (the four suits as a
 * left and right half each) with the OZ sequence
 *     1, 138, '=', code, r0..r7   (rows need their top bit set!)
 * and prints a user character with  1, '2', '?', code .
 * and prints them two ways, because the manual says codes 64..127 but I do
 * not know whether you print them as-is or with 128 added:
 *     line 1: the codes as defined      line 2: the codes + 128
 * Tell me which line shows suit shapes.  Any key redraws, ESC quits.
 */
#if defined(__Z88__)
#include <arch/z88/dor.h>
#define HELP1 "Text probe"
#define HELP2 ""
#define HELP3 ""
#define HELP4 ""
#define HELP5 ""
#define HELP6 ""
#define APP_INFO "TextProbe"
#define APP_KEY 'T'
#define APP_NAME "TextProbe"
#include <arch/z88/application.h>
#endif

#include <stdio.h>

#define BASE 64                     /* '@'.. : first 16 are safe on an unexpanded Z88 */

static const unsigned char gl[8][8] = {
    /* heart  L / R */
    {0x00,0x0A,0x1F,0x1F,0x0F,0x07,0x03,0x01}, {0x00,0x14,0x3E,0x3E,0x3C,0x38,0x30,0x20},
    /* diamond */
    {0x01,0x03,0x07,0x0F,0x0F,0x07,0x03,0x01}, {0x20,0x30,0x38,0x3C,0x3C,0x38,0x30,0x20},
    /* spade */
    {0x01,0x03,0x07,0x0F,0x1F,0x1F,0x03,0x07}, {0x20,0x30,0x38,0x3C,0x3E,0x3E,0x30,0x38},
    /* club */
    {0x03,0x03,0x0D,0x1F,0x1F,0x0D,0x03,0x07}, {0x30,0x30,0x2C,0x3E,0x3E,0x2C,0x30,0x38}
};

static void at(int x, int y) { putchar(1); putchar('3'); putchar('@'); putchar(32 + x); putchar(32 + y); }

static void define_all(void)
{
    int i, r;
    for (i = 0; i < 8; i++) {
        putchar(1); putchar(128 + 10); putchar('='); putchar(BASE + i);
        for (r = 0; r < 8; r++) putchar(gl[i][r] | 0x80);   /* top bit must be set */
    }
}

static void draw(void)
{
    int i;
    define_all();
    at(0, 0); printf("TEXT PROBE - ANY KEY REDRAWS, ESC QUITS");
    at(0, 2); printf("LINE 1: ");
    for (i = 0; i < 8; i++) { putchar(1); putchar('2'); putchar('?'); putchar(BASE + i); }
    at(0, 4); printf("LINE 2 (plain codes): ");
    for (i = 0; i < 8; i++) putchar(BASE + i);
    at(0, 6); printf("SOME LETTERS: A K Q J 10 9 8");
}

void main()
{
    int k;
    draw();
    for (;;) {
        k = getk();
        if (k == 27) break;
        if (k) { draw(); while (getk()) ; }
    }
}
