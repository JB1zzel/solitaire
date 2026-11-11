/* sol_z88.c - Cambridge Z88 front end for Klondike solitaire (TEXT MODE).
 *
 * Display: OZ's ordinary text screen, 80 x 8 characters used.  No graphics
 * map.  Cards are rank + a two-character suit drawn with user-defined
 * characters; black suits are in reverse video.  A graphics-map version is
 * kept as sol_z88_graphics.c.
 *
 * Layout (character columns):
 *   0-3   stock, waste, foundations stacked down the left edge
 *   6-45  seven tableau columns, 6 cells apart, 8 cards high; a longer
 *         column scrolls (latest 8 shown; push up at the top to scroll)
 *   50-71 score, seed and messages
 *
 * Build:
 *   zcc +z88 -subtype=app -O2 -create-app sol_core.c sol_z88.c -osolitaire
 *
 * Keys: Q A O P = up/down/left/right   SPACE = pick up / put down
 *       F = auto to foundations   Z = undo   N = new game
 *       ENTER = redraw the screen   ESC = quit
 */

#if defined(__Z88__)
#include <arch/z88/dor.h>

#define HELP1 "Klondike solitaire"
#define HELP2 "QAOP move, SPACE pick/drop"
#define HELP3 "F auto, Z undo"
#define HELP4 "N new game, ESC quit"
#define HELP5 ""
#define HELP6 ""
#define APP_INFO "Solitaire"
#define APP_KEY 'S'
#define APP_NAME "Solitaire"
/* APP_TYPE is deliberately NOT set: z88dk then uses its default (a bad
 * application with OZ preserving the screen), which is what the working
 * coswave.c graphics example uses.  An explicit AT_Bad without AT_DRAW
 * leaves the graphics map unprotected when OZ switches applications. */

#include <arch/z88/application.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sol_core.h"
#ifdef WITH_PICTURE
#include <graphics.h>
#include <arch/z88/z88.h>
#include "jester.h"
#endif

/* ---------------- text screen ----------------
 * The display is OZ's ordinary text screen: no graphics map.  Cards are
 * drawn with user-defined characters (1,138,'=',code,rows) and printed with
 * 1,'2','?',code.  Each suit is two characters wide (left + right half).
 * Black suits use reverse video.
 *
 * bk[] holds what we want on screen, sh[] what we last sent.  Codes in a
 * cell:  0x20..0x7E  plain ASCII          0x80..0x8F  user glyph k = c-0x80
 *        0xA0..0xFE  ASCII, reversed      0x90..0x9F  user glyph, reversed */
#define GRID_W   80
#define GRID_H    8
#define CELLS   (GRID_W * GRID_H)

#define CARD_W     3                /* rank + two suit halves        */
#define PANEL_X    0                /* stock / waste / foundations   */
#define COL_X0     6                /* first tableau column          */
#define COL_PITCH  6                /* cells between columns         */
#define VIEW_ROWS  8                /* rows of cards visible         */
#ifdef WITH_PICTURE
#define INFO_X    48                /* right-hand info panel         */
#else
#define INFO_X    50
#endif
#define INFO_W    22

static const unsigned char panel_row[6] = { 0, 1, 3, 4, 5, 6 };

#define G_HEART   0x80              /* + 1 = right half */
#define G_SPADE   0x82
#define G_CLUB    0x84
#define G_DIAMOND 0x86
#define G_TEN     0x88
#define G_CURSOR  0x89              /* filled triangle pointing left */
#define G_CHOSEN  0x8A              /* small ring: the picked-up card */
#define G_BOTH    0x8B              /* solid disc: cursor on picked-up card */
#define G_BLOCK   0x8C              /* progress bar */
#define G_BACK    0x8D              /* face-down card */
#define UDG_COUNT 14                /* <= 16: safe on an unexpanded Z88 */
#define UDG_BASE  64                /* first character code we redefine */

/* 6x8 cells; bit 5 = left pixel.  Row 0 is left blank so stacked cards
 * stay visibly separate. */
static const unsigned char udg[UDG_COUNT][8] = {
    { 0x00,0x1E,0x3F,0x3F,0x1F,0x0F,0x07,0x03 },   /* heart L   */
    { 0x00,0x1E,0x3F,0x3F,0x3E,0x3C,0x38,0x30 },   /* heart R   */
    { 0x00,0x01,0x07,0x1F,0x3F,0x3F,0x1D,0x03 },   /* spade L   */
    { 0x00,0x20,0x38,0x3E,0x3F,0x3F,0x2E,0x30 },   /* spade R   */
    { 0x00,0x03,0x07,0x03,0x1D,0x3F,0x1D,0x03 },   /* club L    */
    { 0x00,0x30,0x38,0x30,0x2E,0x3F,0x2E,0x30 },   /* club R    */
    { 0x00,0x01,0x07,0x0F,0x1F,0x0F,0x07,0x01 },   /* diamond L */
    { 0x00,0x20,0x38,0x3C,0x3E,0x3C,0x38,0x20 },   /* diamond R */
    { 0x00,0x17,0x35,0x15,0x15,0x15,0x37,0x00 },   /* "10"      */
    { 0x00,0x02,0x06,0x0E,0x1E,0x0E,0x06,0x02 },   /* cursor    */
    { 0x00,0x0C,0x12,0x12,0x12,0x12,0x0C,0x00 },   /* ring      */
    { 0x00,0x0C,0x1E,0x1E,0x1E,0x1E,0x0C,0x00 },   /* disc      */
    { 0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F },   /* block     */
    { 0x15,0x2A,0x15,0x2A,0x15,0x2A,0x15,0x2A }    /* card back */
};

/* black suits are shown in reverse video */
static unsigned char inverse(unsigned char c)
{
    if (c >= 32 && c < 127)          return (unsigned char)(c | 0x80);
    if (c >= G_HEART && c <= G_BACK) return (unsigned char)(c + 0x10);
    return c;
}

static unsigned char bk[CELLS];
static unsigned char sh[CELLS];

static void clear_bk(void) { memset(bk, ' ', CELLS); }

static void put_ch(int x, int y, unsigned char c)
{
    if (x >= 0 && x < GRID_W && y >= 0 && y < GRID_H)
        bk[y * GRID_W + x] = c;
}
static void put_str(int x, int y, const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        put_ch(x++, y, c);
    }
}
static void utoa10(unsigned v, char *out)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}
/* grey digits: cell codes 0x10..0x19 are '0'..'9' sent with the grey attribute */
static void put_gnum(int x, int y, unsigned v)
{
    char num[12];
    int i;
    utoa10(v, num);
    for (i = 0; num[i]; i++)
        put_ch(x + i, y, (unsigned char)(0x10 + (num[i] - '0')));
}

static void put_num(int x, int y, unsigned v)
{
    char num[12];
    utoa10(v, num);
    put_str(x, y, num);
}

/* ---- OZ screen driver sequences ---- */
static void at(int x, int y)
{
    putchar(1); putchar('3'); putchar('@'); putchar(32 + x); putchar(32 + y);
}
static void attr(int on, int which)             /* 1,'2','+'/'-',attr */
{
    putchar(1); putchar('2'); putchar(on ? '+' : '-'); putchar(which);
}

static void define_udgs(void)
{
    int i, r;
    for (i = 0; i < UDG_COUNT; i++) {
        putchar(1); putchar(128 + 10); putchar('='); putchar(UDG_BASE + i);
        for (r = 0; r < 8; r++) putchar(udg[i][r] | 0x80);   /* top bit set */
    }
}

static int grey_on;                 /* grey attribute state while sending a row */

static void emit(unsigned char c, int *inv)
{
    int want = 0, grey = 0;
    if (c >= 0xA0)      { want = 1; c = (unsigned char)(c & 0x7F); }
    else if (c >= 0x90) { want = 1; c = (unsigned char)(c - 0x10); }
    else if (c >= 0x10 && c <= 0x19) { grey = 1; c = (unsigned char)('0' + (c - 0x10)); }
    if (want != *inv) { attr(want, 'R'); *inv = want; }
    if (grey != grey_on) { attr(grey, 'G'); grey_on = grey; }
    if (c >= 0x80) {
        putchar(1); putchar('2'); putchar('?');
        putchar(UDG_BASE + (c - 0x80));
    } else {
        putchar(c);
    }
}

/* send only what changed: per row, from the first to the last difference */
static void flush(void)
{
    int x, y, a, b, inv;
    for (y = 0; y < GRID_H; y++) {
        unsigned char *p = bk + y * GRID_W, *q = sh + y * GRID_W;
        a = -1; b = -1;
        for (x = 0; x < GRID_W; x++)
            if (p[x] != q[x]) { if (a < 0) a = x; b = x; }
        if (a < 0) continue;
        at(a, y);
        inv = 0; grey_on = 0;
        for (x = a; x <= b; x++) emit(p[x], &inv);
        if (inv) attr(0, 'R');
        if (grey_on) attr(0, 'G');
        memcpy(q + a, p + a, (size_t)(b - a + 1));
    }
}

static void cls_screen(void)
{
    at(0, 0);
    putchar(1); putchar('2'); putchar('C'); putchar(254);   /* to end of window */
    memset(sh, ' ', CELLS);
}

/* ---------------- picture window + text window ----------------
 * The 64x64 jester lives in a graphics map window of its own at the left.
 * The text window (window 1, where the whole game is drawn) is moved to the
 * right of it with a raw OZ sequence, as the SimCity port for the Z88 does.
 *
 * Build options (defaults are what worked in z88_pic_probe.c):
 *   -DTXT_X=22 -DTXT_W=70   text window start column and width
 *   -DWIN_W=64 -DWIN_D=8    picture window size
 *   -DTEXT_FIRST            define the text window before opening the map
 *   -DNO_CLOSE              do not closegfx() on exit
 *   -DWITH_PICTURE          enable the jester map window (off by default) */
#ifndef TXT_X
#define TXT_X 22
#endif
#ifndef TXT_W
#define TXT_W 70
#endif
#ifndef WIN_X
#define WIN_X 80
#endif
#ifndef WIN_W
#define WIN_W 64
#endif
#ifndef WIN_D
#define WIN_D 8
#endif
#ifndef WIN_TYPE
#define WIN_TYPE 128
#endif

#ifdef WITH_PICTURE
static struct window gfx;

static void select_text_window(void)        /* 1,'2','H','1' */
{
    putchar(1); putchar('2'); putchar('H'); putchar('1');
}

static void define_text_window(void)        /* 1,'7','#',1,x,y,w,d,type */
{
    putchar(1); putchar('7'); putchar('#'); putchar('1');
    putchar(32 + TXT_X); putchar(32 + 0);
    putchar(32 + TXT_W); putchar(32 + 8);
    putchar(128);
}

static void plot_picture(void)
{
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if (jester[y][x >> 3] & (0x80 >> (x & 7)))
                plot(x, y);
}

/* open the map ONCE; later repaints only clear and replot it */
static void open_picture(void)
{
    memset(&gfx, 0, sizeof gfx);
    gfx.number = '2';
    gfx.x      = WIN_X;
    gfx.y      = 0;
    gfx.width  = WIN_W;
    gfx.depth  = WIN_D;
    gfx.type   = WIN_TYPE;
    gfx.graph  = 1;
    window(&gfx);
    clg();
    plot_picture();
}

static void draw_picture(void)
{
    clga(0, 0, 63, 63);
    plot_picture();
}
#else
static void select_text_window(void) {}
static void define_text_window(void) {}
static void open_picture(void) {}
static void draw_picture(void) {}
#endif

static void screen_modes(void)
{
    define_udgs();
    attr(0, 'C');                   /* text cursor off */
    attr(0, 'S');                   /* no scrolling when writing the last cell */
}

static void screen_init(void)
{
#ifdef TEXT_FIRST
    define_text_window();
    select_text_window();
    open_picture();
#else
    open_picture();
    define_text_window();
    select_text_window();
#endif
    screen_modes();
    cls_screen();
    clear_bk();
}

/* ---------------- game state owned by the front end ---------------- */
static int sel_row, sel_col, sel_depth;   /* cursor: row 0 = panel, 1 = column */
static int selected, selected_index;      /* picked-up source, -1 = none        */
static int last_panel = 1;                /* panel slot to return to            */
static char msg[64];
static const char win_msg[] = "YOU WIN! N=new";
static const char shuf_msg[] = "SHUFFLING";
static unsigned chosen_seed = 1;
static unsigned idle_count;

/* the pile behind a slot number (slots 0..5 match the pile enum) */
static pile *slot_pile(int slot)
{
    return (slot >= SLOT_COL1) ? piles[P_COL1 + (slot - SLOT_COL1)]
                               : piles[slot];
}

/* ---------------- cards ---------------- */
static unsigned char rank_code(int rank)
{
    switch (rank) {
    case RANK_A:  return 'A';
    case RANK_10: return G_TEN;
    case RANK_J:  return 'J';
    case RANK_Q:  return 'Q';
    case RANK_K:  return 'K';
    default:      return (unsigned char)('2' + (rank - RANK_2));
    }
}
static unsigned char suit_code(int suit)
{
    switch (suit) {
    case SUIT_HEART:  return G_HEART;
    case SUIT_SPADE:  return G_SPADE;
    case SUIT_CLUB:   return G_CLUB;
    default:          return G_DIAMOND;
    }
}

static void put_suit(int x, int y, unsigned char g, int black)
{
    put_ch(x, y, black ? inverse(g) : g);
    put_ch(x + 1, y, black ? inverse((unsigned char)(g + 1)) : (unsigned char)(g + 1));
}

static void put_card(int x, int y, card *c)
{
    if (!c) {
        put_ch(x, y, '[');
        put_ch(x + 1, y, ' ');
        put_ch(x + 2, y, ']');
    } else if (!c->revealed) {
        put_ch(x, y, G_BACK);
        put_ch(x + 1, y, G_BACK);
        put_ch(x + 2, y, G_BACK);
    } else {
        int black = is_black_card(c);
        unsigned char r = rank_code(c->rank);
        put_ch(x, y, black ? inverse(r) : r);
        put_suit(x + 1, y, suit_code(c->suit), black);
    }
}

/* number of face-down cards at the bottom of a column */
static int facedown_count(pile *p)
{
    int n = 0;
    while (n < p->num_cards) {
        card *c = peek_at(p, n);
        if (c && c->revealed) break;
        n++;
    }
    return n;
}

/* Each tableau pile is one column of up to 8 visible cards.  A longer pile
 * scrolls: by default the LATEST 8 cards are shown; moving the cursor up
 * past the top visible card scrolls the view up, and down scrolls it back. */
static int view_col = -1;           /* column whose view the cursor controls */
static int view_top;                /* first visible card in that column     */

static int max_top(pile *p)
{
    return p->num_cards > VIEW_ROWS ? p->num_cards - VIEW_ROWS : 0;
}

/* first visible card of a column */
static int col_top(int col)
{
    if (sel_row == 1 && col == sel_col) return view_top;
    return max_top(piles[P_COL1 + col]);
}

/* keep the cursor card in view */
static void adjust_view(void)
{
    int mt;
    if (sel_row != 1) { view_col = -1; return; }
    mt = max_top(piles[P_COL1 + sel_col]);
    if (view_col != sel_col) { view_col = sel_col; view_top = mt; }
    if (view_top > mt) view_top = mt;
    if (view_top < 0)  view_top = 0;
    if (sel_depth >= 0) {
        if (sel_depth < view_top) view_top = sel_depth;
        if (sel_depth > view_top + VIEW_ROWS - 1)
            view_top = sel_depth - VIEW_ROWS + 1;
    }
}

static void draw_column(int col)
{
    pile *p = piles[P_COL1 + col];
    int n = p->num_cards, top, k, x = COL_X0 + col * COL_PITCH;

    if (n == 0) { put_card(x, 0, NULL); return; }
    top = col_top(col);
    for (k = top; k < n && k - top < VIEW_ROWS; k++)
        put_card(x, k - top, peek_at(p, k));
    /* how many cards are hidden: above on the top row, below on the bottom row */
    if (top > 0)             put_gnum(x + CARD_W + 1, 0, (unsigned)top);
    if (top < max_top(p))    put_gnum(x + CARD_W + 1, VIEW_ROWS - 1,
                                     (unsigned)(max_top(p) - top));
}

/* ---------------- cursor positions ---------------- */
/* where the marker for a slot/depth goes; returns 0 if that card is
 * scrolled out of view */
static int slot_cell(int slot, int depth, int *mx, int *my)
{
    if (slot >= SLOT_COL1) {
        int col = slot - SLOT_COL1, top;
        *mx = COL_X0 + col * COL_PITCH + CARD_W;
        if (depth < 0) { *my = 0; return 1; }
        top = col_top(col);
        if (depth < top || depth >= top + VIEW_ROWS) return 0;
        *my = depth - top;
        return 1;
    }
    *mx = PANEL_X + CARD_W;
    *my = panel_row[slot];
    return 1;
}

static void draw_markers(void)
{
    int cslot = (sel_row == 0) ? sel_col : (SLOT_COL1 + sel_col);
    int cx, cy, sx, sy, cvis, svis;
    cvis = slot_cell(cslot, sel_depth, &cx, &cy);

    if (selected >= 0) {
        svis = slot_cell(selected, selected_index, &sx, &sy);
        if (svis) {
            if (cvis && sx == cx && sy == cy) {
                put_ch(cx, cy, G_BOTH);
                return;
            }
            put_ch(sx, sy, G_CHOSEN);
        }
    }
    if (cvis) put_ch(cx, cy, G_CURSOR);
}

/* ---------------- info panel ---------------- */
static void draw_info(void)
{
    int line, pos, len = (int)strlen(msg);

    put_str(INFO_X, 0, "SCORE");
    put_num(INFO_X, 1, (unsigned)(score < 0 ? 0 : score));
    put_str(INFO_X, 2, "SEED");
    put_ch(INFO_X, 3, '#');
    put_num(INFO_X + 1, 3, current_seed);

    for (line = 0, pos = 0; line < 4 && pos < len; line++, pos += INFO_W) {
        int i;
        for (i = 0; i < INFO_W && pos + i < len; i++) {
            unsigned char c = (unsigned char)msg[pos + i];
            if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
            put_ch(INFO_X + i, 4 + line, c);
        }
    }
}

/* ---------------- full repaint ---------------- */
static void render(void)
{
    int i;
    clear_bk();
    adjust_view();

    if (piles[P_DECK]->num_cards > 0) {
        put_ch(PANEL_X, panel_row[0], G_BACK);
        put_ch(PANEL_X + 1, panel_row[0], G_BACK);
        put_ch(PANEL_X + 2, panel_row[0], G_BACK);
    } else {
        put_card(PANEL_X, panel_row[0], NULL);
    }
    put_card(PANEL_X, panel_row[1], peek_last(piles[P_WASTE]));
    for (i = 0; i < FDN_COUNT; i++)
        put_card(PANEL_X, panel_row[2 + i], peek_last(piles[P_FDN1 + i]));

    for (i = 0; i < COL_COUNT; i++)
        draw_column(i);

    draw_markers();
    draw_info();
    flush();
}


/* ---- coming back from another application ------------------------------
 * While another application (PipeDream, say) runs, OZ takes the display and
 * the graphics map away.  Our map pointer is then stale, and plotting to it
 * corrupts memory (soft reset) or scrambles the screen.  We poll the keyboard
 * constantly, so a gap of more than a second between two polls means we were
 * swapped out: reopen the map and repaint before anything else is drawn. */
static void refresh_screen(void);
static int  game_active;            /* 1 while the board is on screen */
static unsigned char last_sec;      /* Blink seconds counter, 0..59 */
static unsigned char have_sec;

static unsigned char read_sec(void)
{
#asm
    in   a,($D1)
    ld   l,a
    ld   h,0
#endasm
}

/* OZ tells the application, through these two hooks, when something happened
 * while it was inside getk(): redrawscreen() when the screen was overwritten
 * (another application used it) and handlecmds() for menu commands.  The
 * SimCity port for the Z88 uses the same hooks.  The seconds-counter check
 * below stays as a backstop in case a redraw request is ever missed. */
static volatile unsigned char need_redraw;

void redrawscreen(void) { need_redraw = 1; }
void handlecmds(int cmd) { (void)cmd; }

static int poll_key(void)
{
    /* Blink TIM1 (port $D1) counts seconds 0..59.  It is not latched, so a
     * read that straddles a tick can be wrong: take two reads and ignore the
     * sample unless they agree and are in range. */
    unsigned char t = read_sec(), t2 = read_sec();
    if (t == t2 && t < 60) {
        if (have_sec && (unsigned char)((t + 60 - last_sec) % 60) > 2) {
            if (game_active) refresh_screen();
            else { select_text_window(); cls_screen(); screen_modes(); draw_picture(); }
        }
        last_sec = t; have_sec = 1;
    }
    {
        int k = getk();
        if (need_redraw) {
            need_redraw = 0;
            if (game_active) refresh_screen();
            else { select_text_window(); cls_screen(); screen_modes(); draw_picture(); }
        }
        return k;
    }
}

/* ---------------- input ---------------- */
enum { KEY_NONE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
       KEY_SPACE, KEY_NEW, KEY_UNDO, KEY_AUTO, KEY_REDRAW, KEY_QUIT };

static void wait_release(void) { while (poll_key() != 0) ; }

static int read_key(void)
{
    int k;
    wait_release();                 /* previous key must be up first */
    for (;;) {
        k = poll_key();
        if (k != 0) break;
        idle_count++;
    }
    switch (k) {
    case 'q': case 'Q': return KEY_UP;
    case 'a': case 'A': return KEY_DOWN;
    case 'o': case 'O': return KEY_LEFT;
    case 'p': case 'P': return KEY_RIGHT;
    case ' ':           return KEY_SPACE;
    case 'n': case 'N': return KEY_NEW;
    case 'z': case 'Z': return KEY_UNDO;
    case 'f': case 'F': return KEY_AUTO;
    case 13: case 10:   return KEY_REDRAW;
    case 27:            return KEY_QUIT;
    }
    return KEY_NONE;
}

static unsigned random_seed(unsigned idle)
{
    unsigned v = idle * 25173u + 13849u;
    return v ? v : 1;
}

/* ---------------- cursor movement ---------------- */
/* put the cursor on a legal card of its column (face-up cards only) */
static void fix_cursor(void)
{
    pile *p;
    int n, f;
    if (sel_row != 1) { sel_depth = -1; return; }
    p = piles[P_COL1 + sel_col];
    n = p->num_cards;
    if (n == 0) { sel_depth = -1; return; }
    f = facedown_count(p);
    if (f >= n) f = n - 1;
    if (sel_depth < f) sel_depth = f;
    if (sel_depth > n - 1) sel_depth = n - 1;
}

static void enter_column(int col)
{
    pile *p = piles[P_COL1 + col];
    sel_row = 1;
    sel_col = col;
    sel_depth = p->num_cards - 1;      /* last card, or -1 if empty */
    fix_cursor();
}

static void enter_panel(void)
{
    sel_row = 0;
    sel_col = last_panel;
    sel_depth = -1;
}

static void move_cursor(int key)
{
    if (sel_row == 0) {                         /* stock/waste/foundations */
        if (key == KEY_UP) {
            if (sel_col > 0) sel_col--;
        } else if (key == KEY_DOWN) {
            if (sel_col < 5) sel_col++;
        } else if (key == KEY_RIGHT) {
            last_panel = sel_col;
            enter_column(0);
        } else if (key == KEY_LEFT) {
            last_panel = sel_col;
            enter_column(COL_COUNT - 1);
        }
        return;
    }
    if (key == KEY_UP) {                        /* inside a column */
        pile *p = piles[P_COL1 + sel_col];
        int f = facedown_count(p);
        if (f >= p->num_cards) f = p->num_cards - 1;
        if (sel_depth > f) sel_depth--;
    } else if (key == KEY_DOWN) {
        pile *p = piles[P_COL1 + sel_col];
        if (sel_depth >= 0 && sel_depth < p->num_cards - 1) sel_depth++;
    } else if (key == KEY_LEFT) {
        if (sel_col > 0) enter_column(sel_col - 1);
        else             enter_panel();
    } else if (key == KEY_RIGHT) {
        if (sel_col < COL_COUNT - 1) enter_column(sel_col + 1);
        else                         enter_panel();
    }
}

/* ---------------- shuffle progress bar ---------------- */
static int bar_game = 0;            /* 0 = title screen, 1 = in game */

static void shuffle_progress(int done, int total)
{
    int i, w, filled, x, y;
    if (bar_game) { x = INFO_X; y = 7; w = INFO_W;
                    put_str(INFO_X, 6, shuf_msg); }
    else          { x = 30; y = 6; w = 20;
                    put_str(18, 6, shuf_msg); }
    filled = (total > 0) ? (int)(((unsigned)done * w) / total) : 0;
    for (i = 0; i < w; i++)
        put_ch(x + i, y, i < filled ? G_BLOCK : '.');
    flush();
}

/* ---------------- title screen ---------------- */
static void show_title(void)
{
    clear_bk();
    put_suit(27, 0, G_HEART, 0);  put_suit(30, 0, G_SPADE, 1);
    put_str(34, 0, "SOLITAIRE");
    put_suit(44, 0, G_CLUB, 1);   put_suit(47, 0, G_DIAMOND, 0);

    put_str(6, 2, "QA OP MOVE  SPACE PICK/DROP");
    put_str(6, 3, "F AUTO  Z UNDO  N NEW  ESC QUIT");
    put_str(6, 4, "ENTER REDRAW");
    put_ch(6, 5, G_CURSOR);  put_ch(8, 5, G_CHOSEN);

    put_str(6, 7, "ANY KEY TO DEAL");
    flush();
}

/* ---------------- new-game prompt ----------------
 * Type up to 5 digits (0..65535) then ENTER to deal that seed;
 * ENTER alone deals a random game.  R = replay this deal,
 * ESC = cancel.  Returns 0 cancel, 1 new (chosen_seed), 2 replay. */
static int prompt_new_game(void)
{
    char buf[8];
    int len = 0;
    buf[0] = 0;

    for (;;) {
        int k;
        strcpy(msg, "SEED/ENTER R=REDO ESC: ");
        strcat(msg, buf);
        render();
        wait_release();
        for (;;) { k = poll_key(); if (k) break; idle_count++; }

        if (k == 13 || k == 10) {
            if (len == 0) {
                chosen_seed = random_seed(idle_count);
            } else {
                unsigned v = 0;
                int i;
                for (i = 0; i < len; i++)
                    v = v * 10 + (unsigned)(buf[i] - '0');
                chosen_seed = v;
            }
            return 1;
        }
        if (k == 'r' || k == 'R') return 2;
        if (k == 27) return 0;
        if ((k == 8 || k == 127) && len > 0) {
            buf[--len] = 0;
        } else if (k >= '0' && k <= '9' && len < 5) {
            unsigned v = 0;
            int i;
            for (i = 0; i < len; i++)
                v = v * 10 + (unsigned)(buf[i] - '0');
            if (len < 4 || v < 6553 || (v == 6553 && k <= '5')) {
                buf[len] = (char)k; buf[len + 1] = 0;
                len++;
            }
        }
    }
}

/* ---------------- main ---------------- */
/* Repaint everything from scratch.  OZ can wipe the display when another
 * application runs (the map must be opened again after RC_DRAW), so ENTER
 * gives the player a way to recover it. */
static void refresh_screen(void)
{
    select_text_window();
    cls_screen();                   /* clear first, then redefine: no half-defined glyphs on show */
    screen_modes();
    draw_picture();
    render();
}

static void reset_cursor(void)
{
    last_panel = 1;
    enter_panel();
    selected = -1; selected_index = -1;
}

void main()
{
    /* Nothing below may rely on RAM contents or on initialised statics:
     * a Z88 application is re-entered without being reloaded. */
    core_init();
    view_col = -1; view_top = 0;
    game_active = 0; have_sec = 0;
    bar_game = 0;
    last_panel = 1;
    idle_count = 0;
    chosen_seed = 1;
    selected = -1; selected_index = -1;
    sel_row = 0; sel_col = 0; sel_depth = -1;
    memset(msg, 0, sizeof msg);
    memset(bk, ' ', CELLS);
    memset(sh, ' ', CELLS);
    screen_init();
    shuffle_hook = shuffle_progress;

    show_title();
    idle_count = 0;
    while (poll_key() == 0) idle_count++;
    bar_game = 0;
#if defined(STAGE) && STAGE == 1
    return;                         /* stage 1: title screen only */
#endif
    prepare_game(random_seed(idle_count));
    have_sec = 0;                   /* the shuffle took a while: not a swap-out */

    reset_cursor();
    msg[0] = 0;
    render();
    game_active = 1;
#if defined(STAGE) && STAGE == 2
    while (poll_key() == 0) ;           /* stage 2: dealt board, no game loop */
    return;
#endif

    for (;;) {
        int key = read_key();

        if (key == KEY_QUIT) break;

        if (key == KEY_REDRAW) {
            refresh_screen();
            continue;
        }

        if (key == KEY_LEFT || key == KEY_RIGHT ||
            key == KEY_UP   || key == KEY_DOWN) {
            move_cursor(key);
            render();
            continue;
        }

        if (key == KEY_UNDO) {
            if (!undo_pop(&sel_row, &sel_col, &sel_depth,
                          &selected, &selected_index))
                strcpy(msg, "No undo.");
            else {
                fix_cursor();
                strcpy(msg, "Undone.");
            }
            render();
            continue;
        }

        if (key == KEY_NEW) {
            int answer = prompt_new_game();
            if (answer) {
                free_game();
                bar_game = 1;
                if (answer == 1) prepare_game(chosen_seed);
                else             replay_game();
                have_sec = 0;               /* shuffle delay is not a swap-out */
                reset_cursor();
                strcpy(msg, answer == 1 ? "New game." : "Replay.");
            } else {
                msg[0] = 0;
            }
            render();
            continue;
        }

        if (key == KEY_AUTO) {
            int n;
            undo_push(sel_row, sel_col, sel_depth, selected, selected_index);
            n = auto_foundation();
            selected = -1; selected_index = -1;
            if (n == 0) {
                undo_drop();
                strcpy(msg, "No move.");
            } else {
                strcpy(msg, "Moved.");
                fix_cursor();
            }
            if (game_won()) strcpy(msg, win_msg);
            render();
            continue;
        }

        if (key == KEY_SPACE) {
            int slot = (sel_row == 0) ? sel_col : (SLOT_COL1 + sel_col);
            int here = (sel_row == 0) ? -1 : sel_depth;

            if (selected < 0) {
                if (slot == SLOT_STOCK) {
                    const char *r0;
                    undo_push(sel_row, sel_col, sel_depth,
                              selected, selected_index);
                    r0 = try_move(SLOT_STOCK, SLOT_STOCK, -1);
                    strcpy(msg, r0);
                    if (r0[0] != 'R' && r0[0] != 'D')
                        undo_drop();
                } else if (slot_pile(slot)->num_cards == 0) {
                    strcpy(msg, "Empty.");
                } else {
                    selected = slot;
                    selected_index = here;
                    strcpy(msg, "Picked.");
                }
            } else if (selected == slot && selected_index == here) {
                selected = -1; selected_index = -1;
                strcpy(msg, "Cancelled.");
            } else {
                const char *r;
                undo_push(sel_row, sel_col, sel_depth,
                          selected, selected_index);
                r = try_move(selected, slot, selected_index);
                strcpy(msg, r);
                if (r[0] == 'O' || r[0] == 'R' || r[0] == 'D') {
                    if (slot >= SLOT_COL1) enter_column(slot - SLOT_COL1);
                    else { sel_row = 0; sel_col = slot; sel_depth = -1; }
                    selected = -1; selected_index = -1;
                } else {
                    undo_drop();
                }
            }
            if (game_won()) strcpy(msg, win_msg);
            render();
            continue;
        }
    }

    game_active = 0;
    free_game();                    /* hand every card and pile back */
    shuffle_hook = NULL;
    clear_bk();
    flush();
#if defined(WITH_PICTURE) && !defined(NO_CLOSE)
    closegfx(&gfx);
#endif
}
