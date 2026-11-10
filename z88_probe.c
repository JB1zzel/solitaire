/* z88_probe.c - tiny test app to find out WHY entering from the Index fails.
 *
 * It does only: open the 512x64 map, clear it, draw a border and a few
 * words, wait for a key, quit on ESC.  No game, no core, no buffers.
 *
 * Build:
 *   zcc +z88 -subtype=app -create-app z88_probe.c -oprobe
 *
 * Load it in OZvm exactly like the game and enter it from the Index five
 * or six times in a row (leave with ESC each time), and write down which
 * entries soft-reset or give "memory corruption".
 *   - If the probe fails too, the problem is the app header / graphics
 *     setup, not the solitaire code.
 *   - If the probe is solid, the problem is in the game.
 *
 * To test the explicit AT_Bad header that earlier game builds used, add
 * -DUSE_AT_BAD to the zcc command.
 */

#if defined(__Z88__)
#include <arch/z88/dor.h>

#define HELP1 "Probe"
#define HELP2 ""
#define HELP3 ""
#define HELP4 ""
#define HELP5 ""
#define HELP6 ""
#define APP_INFO "Probe"
#define APP_KEY 'P'
#define APP_NAME "Probe"
#ifdef USE_AT_BAD
#define APP_TYPE AT_Bad
#endif

#include <arch/z88/application.h>
#endif

#include <graphics.h>
#include <stdio.h>
#include <string.h>

void main()
{
    static struct window gfx;
    int x, y, n = 0, k;

    memset(&gfx, 0, sizeof gfx);
    gfx.graph = 2;
    window(&gfx);
    clg();

    for (x = 0; x < 512; x++) { plot(x, 0); plot(x, 63); }
    for (y = 0; y < 64; y++)  { plot(0, y); plot(511, y); }
    for (x = 8; x < 16 + 8; x++) plot(x, 8);          /* a little mark */

    for (;;) {
        k = getk();
        if (k == 27) break;
        if (k != 0) {                                  /* each key plots a dot */
            plot(20 + n, 32); plot(20 + n, 33);
            n += 3; if (n > 400) n = 0;
            while (getk() != 0) ;
        }
    }
}
