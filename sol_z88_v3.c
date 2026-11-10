/* sol_z88.c - Cambridge Z88 front end for Klondike solitaire.
 *
 * Display: the 512x64 graphics map (window graph=2), treated as a
 * 64 x 8 grid of 8x8 cells.  All text and cards are drawn by this
 * file's own 8x8 glyph table, so no console/font setup is needed.
 *
 * Layout (cell columns):
 *   0-1   stock, waste, foundations stacked down the left edge
 *   4-50  seven tableau columns, each split into two sub-columns of 8
 *   53-63 score, seed and messages
 *
 * Build (OZvm / real Z88 application):
 *   zcc +z88 -subtype=app -create-app sol_z88.c sol_core.c -osolitaire
 *
 * Keys: Q A O P = up/down/left/right   SPACE = pick up / put down
 *       F = auto to foundations   Z = undo   N = new game
 *       ENTER = redraw the screen   ESC = quit
 */

#if defined(__Z88__)
#include <arch/z88/dor.h>

#define HELP1 "Klondike solitaire"
#define HELP2 "Q A O P move, SPACE pick up/put down"
#define HELP3 "F auto to foundations, Z undo"
#define HELP4 "N new game/replay/seed, ESC quit"
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

#include <graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sol_core.h"

/* ---------------- grid ---------------- */
#define GRID_W   64
#define GRID_H    8
#define CELLS   (GRID_W * GRID_H)

#define PANEL_X   0                 /* stock / waste / foundations */
#define COL_X0    4                 /* first tableau pair          */
#define PAIR_PITCH 7                /* cells between column pairs  */
#define SUB_PITCH  3                /* cells between sub-columns   */
#define CAP       16                /* cards shown per column      */
#define INFO_X   53                 /* right-hand info panel       */
#define INFO_W   11

static const unsigned char panel_row[6] = { 0, 1, 3, 4, 5, 6 };

/* ---------------- glyphs ---------------- */
#define G_HEART   0x90
#define G_SPADE   0x91
#define G_CLUB    0x92
#define G_DIAMOND 0x93
#define G_TEN     0x94
#define G_CURSOR  0x95    /* filled triangle pointing left */
#define G_CHOSEN  0x96    /* small ring: the picked-up card */
#define G_BOTH    0x97    /* solid disc: cursor on picked-up card */
#define G_BLOCK   0x98    /* progress bar */
#define G_BACK    0x99    /* face-down card */

static const unsigned char udg[10][8] = {
    { 0x00,0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18 },   /* heart   */
    { 0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x18,0x3C },   /* spade   */
    { 0x18,0x3C,0x3C,0xDB,0xFF,0xDB,0x18,0x3C },   /* club    */
    { 0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18 },   /* diamond */
    { 0x46,0xC9,0x49,0x49,0x49,0x49,0xE6,0x00 },   /* "10"    */
    { 0x00,0x06,0x1E,0x7E,0x7E,0x1E,0x06,0x00 },   /* cursor  */
    { 0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00 },   /* ring    */
    { 0x00,0x3C,0x7E,0x7E,0x7E,0x7E,0x3C,0x00 },   /* disc    */
    { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF },   /* block   */
    { 0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA }    /* card back */
};

/* ASCII 32..95 (letters are upper case; lower case is folded) */
static const unsigned char font[64][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00}, /* ! */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0x00}, /* # */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* $ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* % */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* & */
    {0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x10,0x20,0x40,0x40,0x40,0x20,0x10,0x00}, /* ( */
    {0x10,0x08,0x04,0x04,0x04,0x08,0x10,0x00}, /* ) */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* * */
    {0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20}, /* , */
    {0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00}, /* . */
    {0x04,0x04,0x08,0x10,0x20,0x40,0x40,0x00}, /* / */
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0x00}, /* 0 */
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0x00}, /* 1 */
    {0x38,0x44,0x04,0x08,0x10,0x20,0x7C,0x00}, /* 2 */
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0x00}, /* 3 */
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x00}, /* 4 */
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0x00}, /* 5 */
    {0x38,0x40,0x40,0x78,0x44,0x44,0x38,0x00}, /* 6 */
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0x00}, /* 7 */
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00}, /* 8 */
    {0x38,0x44,0x44,0x3C,0x04,0x04,0x38,0x00}, /* 9 */
    {0x00,0x30,0x30,0x00,0x30,0x30,0x00,0x00}, /* : */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ; */
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00}, /* < */
    {0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00}, /* = */
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00}, /* > */
    {0x38,0x44,0x04,0x08,0x10,0x00,0x10,0x00}, /* ? */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* @ */
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0x00}, /* A */
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00}, /* B */
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0x00}, /* C */
    {0x78,0x44,0x44,0x44,0x44,0x44,0x78,0x00}, /* D */
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0x00}, /* E */
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x00}, /* F */
    {0x38,0x44,0x40,0x5C,0x44,0x44,0x38,0x00}, /* G */
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0x00}, /* H */
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00}, /* I */
    {0x1C,0x08,0x08,0x08,0x08,0x48,0x30,0x00}, /* J */
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0x00}, /* K */
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00}, /* L */
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0x00}, /* M */
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0x00}, /* N */
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0x00}, /* O */
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00}, /* P */
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0x00}, /* Q */
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x00}, /* R */
    {0x3C,0x40,0x40,0x38,0x04,0x04,0x78,0x00}, /* S */
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00}, /* T */
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00}, /* U */
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0x00}, /* V */
    {0x44,0x44,0x44,0x54,0x54,0x6C,0x44,0x00}, /* W */
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0x00}, /* X */
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x00}, /* Y */
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0x00}, /* Z */
    {0x70,0x40,0x40,0x40,0x40,0x40,0x70,0x00}, /* [ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* bslash */
    {0x1C,0x04,0x04,0x04,0x04,0x04,0x1C,0x00}, /* ] */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7C}, /* _ */
};

/* Inverse video: black-suit cards (spades, clubs) are drawn as white on
 * black so they differ from the red suits (hearts, diamonds).  Inverse
 * versions of a glyph use their own codes: ASCII 32..95 -> 0xA0..0xDF,
 * the custom glyphs 0x90..0x99 -> 0xE0..0xE9.  Row 0 of an inverse cell
 * is left clear so cards stacked in a column stay visibly separate. */
static unsigned char inverse(unsigned char c)
{
    if (c >= 32 && c < 96)           return (unsigned char)(c | 0x80);
    if (c >= G_HEART && c <= G_BACK) return (unsigned char)(c + 0x50);
    return c;
}

static unsigned char glyph_row(unsigned char c, unsigned char r)
{
    unsigned char b;
    if (c >= 0xE0 && c <= 0xE9)      b = udg[c - 0xE0][r];
    else if (c >= 0xA0 && c < 0xE0)  b = font[c - 0xA0][r];
    else if (c >= 32 && c < 96)      return font[c - 32][r];
    else if (c >= G_HEART && c <= G_BACK) return udg[c - G_HEART][r];
    else                             return 0;
    return r == 0 ? 0 : (unsigned char)~b;      /* inverse cell */
}

/* ---------------- screen: back buffer + diff flush ----------------
 * Drawing calls fill the back buffer; flush() compares it with what
 * is on the LCD and plots only the pixels that differ. */
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
static void utoa10(unsigned long v, char *out)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}
static void put_num(int x, int y, unsigned long v)
{
    char num[12];
    utoa10(v, num);
    put_str(x, y, num);
}

static void draw_cell(int cx, int cy, unsigned char oldc, unsigned char newc)
{
    unsigned char r, b, o, n, d, m;
    int px = cx * 8, py = cy * 8;
    for (r = 0; r < 8; r++) {
        o = glyph_row(oldc, r);
        n = glyph_row(newc, r);
        d = (unsigned char)(o ^ n);
        if (!d) continue;
        for (b = 0, m = 0x80; b < 8; b++, m >>= 1)
            if (d & m) {
                if (n & m) plot(px + b, py + r);
                else       unplot(px + b, py + r);
            }
    }
}

static void flush(void)
{
    int x, y, i = 0;
    for (y = 0; y < GRID_H; y++)
        for (x = 0; x < GRID_W; x++, i++)
            if (bk[i] != sh[i]) {
                draw_cell(x, y, sh[i], bk[i]);
                sh[i] = bk[i];
            }
}

static struct window gfx;           /* static: the library may keep the pointer */

static void open_screen(void)
{
    /* Zero the whole struct first: window() may read fields we do not
     * set, and on a Z88 leftover RAM from an earlier run is not zero. */
    memset(&gfx, 0, sizeof gfx);
    gfx.graph = 2;                  /* 512 pixel wide graphics display */
    window(&gfx);
}

static void screen_init(void)
{
    open_screen();
    clg();
    memset(sh, ' ', CELLS);
    clear_bk();
}

/* ---------------- game state owned by the front end ---------------- */
static int sel_row, sel_col, sel_depth;   /* cursor: row 0 = panel, 1 = column */
static int selected, selected_index;      /* picked-up source, -1 = none        */
static int last_panel = 1;                /* panel slot to return to            */
static char msg[64];
static unsigned long chosen_seed = 1;
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

static void put_card(int x, int y, card *c)
{
    if (!c) {
        put_ch(x, y, '[');
        put_ch(x + 1, y, ']');
    } else if (!c->revealed) {
        put_ch(x, y, G_BACK);
        put_ch(x + 1, y, G_BACK);
    } else if (is_black_card(c)) {
        put_ch(x, y, inverse(rank_code(c->rank)));
        put_ch(x + 1, y, inverse(suit_code(c->suit)));
    } else {
        put_ch(x, y, rank_code(c->rank));
        put_ch(x + 1, y, suit_code(c->suit));
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

/* How a column is drawn.  Up to CAP cards are all shown.  If there are
 * more, face-down cards are squeezed (the last shown face-down cell
 * becomes a "+N" count) so every face-up card stays visible.
 * f  = face-down cards in the pile
 * fd = face-down cells drawn */
static void col_layout(pile *p, int *f, int *fd)
{
    int n = p->num_cards;
    int dn = facedown_count(p);
    int up = n - dn;
    int cells = dn;
    if (n > CAP) {
        cells = CAP - up;
        if (cells < 1) cells = 1;
        if (cells > dn) cells = dn;
    }
    *f = dn;
    *fd = cells;
}

static int col_cell_x(int col, int k)
{
    return COL_X0 + col * PAIR_PITCH + (k >> 3) * SUB_PITCH;
}
static int col_cell_y(int k) { return k & 7; }

/* display cell index of logical card `depth` in a column */
static int depth_to_cell(pile *p, int depth)
{
    int f, fd;
    if (depth < 0) return 0;
    col_layout(p, &f, &fd);
    if (depth < f) return depth < fd ? depth : fd - 1;
    return fd + (depth - f);
}

static void draw_column(int col)
{
    pile *p = piles[P_COL1 + col];
    int n = p->num_cards, f, fd, k, j;

    if (n == 0) { put_card(col_cell_x(col, 0), 0, NULL); return; }
    col_layout(p, &f, &fd);
    for (k = 0; k < fd; k++) {
        int x = col_cell_x(col, k), y = col_cell_y(k);
        if (f > fd && k == fd - 1) {          /* squeezed: show a count */
            put_ch(x, y, '+');
            put_ch(x + 1, y, (unsigned char)('0' + (f - (fd - 1))));
        } else {
            put_card(x, y, peek_at(p, k));
        }
    }
    for (j = f; j < n && fd + (j - f) < CAP; j++) {
        k = fd + (j - f);
        put_card(col_cell_x(col, k), col_cell_y(k), peek_at(p, j));
    }
}

/* ---------------- cursor positions ---------------- */
static void slot_cell(int slot, int depth, int *mx, int *my)
{
    if (slot >= SLOT_COL1) {
        int col = slot - SLOT_COL1;
        int k = depth_to_cell(piles[P_COL1 + col], depth);
        *mx = col_cell_x(col, k) + 2;
        *my = col_cell_y(k);
    } else {
        *mx = PANEL_X + 2;
        *my = panel_row[slot];
    }
}

static void draw_markers(void)
{
    int cslot = (sel_row == 0) ? sel_col : (SLOT_COL1 + sel_col);
    int cx, cy, sx, sy;
    slot_cell(cslot, sel_depth, &cx, &cy);

    if (selected >= 0) {
        slot_cell(selected, selected_index, &sx, &sy);
        if (sx == cx && sy == cy) {
            put_ch(cx, cy, G_BOTH);
            return;
        }
        put_ch(sx, sy, G_CHOSEN);
    }
    put_ch(cx, cy, G_CURSOR);
}

/* ---------------- info panel ---------------- */
static void draw_info(void)
{
    int line, pos, len = (int)strlen(msg);

    put_str(INFO_X, 0, "SCORE");
    put_num(INFO_X, 1, (unsigned long)(score < 0 ? 0 : score));
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

    if (piles[P_DECK]->num_cards > 0) {
        put_ch(PANEL_X, panel_row[0], G_BACK);
        put_ch(PANEL_X + 1, panel_row[0], G_BACK);
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

/* ---------------- input ---------------- */
enum { KEY_NONE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
       KEY_SPACE, KEY_NEW, KEY_UNDO, KEY_AUTO, KEY_REDRAW, KEY_QUIT };

static void wait_release(void) { while (getk() != 0) ; }

static int read_key(void)
{
    int k;
    wait_release();                 /* previous key must be up first */
    for (;;) {
        k = getk();
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

static unsigned long random_seed(unsigned idle)
{
    unsigned v = idle * 25173u + 13849u;
    return v ? (unsigned long)v : 1UL;
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
                    put_str(INFO_X, 6, "SHUFFLING"); }
    else          { x = 22; y = 6; w = 20;
                    put_str(10, 6, "SHUFFLING"); }
    filled = (total > 0) ? (int)(((long)done * w) / total) : 0;
    for (i = 0; i < w; i++)
        put_ch(x + i, y, i < filled ? G_BLOCK : '.');
    flush();
}

/* ---------------- title screen ---------------- */
static void centre(int y, const char *s)
{
    put_str((GRID_W - (int)strlen(s)) / 2, y, s);
}

static void show_title(void)
{
    clear_bk();
    put_ch(23, 0, G_HEART);  put_ch(24, 0, G_SPADE);
    put_str(27, 0, "SOLITAIRE");
    put_ch(38, 0, G_CLUB);   put_ch(39, 0, G_DIAMOND);

    put_str(2, 1,  "Q / A   CURSOR UP / DOWN");
    put_str(2, 2,  "O / P   CURSOR LEFT / RIGHT");
    put_str(2, 3,  "SPACE   PICK UP, THEN PUT DOWN");
    put_str(2, 4,  "        ON THE STOCK: DRAW A CARD");
    put_str(36, 1, "F   AUTO-MOVE TO FOUNDATIONS");
    put_str(36, 2, "Z   UNDO");
    put_str(36, 3, "N   NEW GAME / REPLAY / SEED");
    put_str(36, 4, "ESC QUIT   ENTER REDRAW");

    put_ch(2, 5, G_CURSOR);  put_str(4, 5, "CURSOR");
    put_ch(13, 5, G_CHOSEN); put_str(15, 5, "PICKED UP");
    put_str(36, 5, "BUILD A TO K BY SUIT");

    centre(7, "PRESS ANY KEY TO DEAL");
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
        strcpy(msg, "SEED, ENTER=NEW R=REDO ESC=BACK: ");
        strcat(msg, buf);
        render();
        wait_release();
        for (;;) { k = getk(); if (k) break; idle_count++; }

        if (k == 13 || k == 10) {
            if (len == 0) {
                chosen_seed = random_seed(idle_count);
            } else {
                unsigned long v = 0;
                int i;
                for (i = 0; i < len; i++)
                    v = v * 10 + (unsigned long)(buf[i] - '0');
                chosen_seed = v;
            }
            return 1;
        }
        if (k == 'r' || k == 'R') return 2;
        if (k == 27) return 0;
        if ((k == 8 || k == 127) && len > 0) {
            buf[--len] = 0;
        } else if (k >= '0' && k <= '9' && len < 5) {
            unsigned long v = 0;
            int i;
            buf[len] = (char)k; buf[len + 1] = 0;
            for (i = 0; i <= len; i++)
                v = v * 10 + (unsigned long)(buf[i] - '0');
            if (v > 65535UL) buf[len] = 0;
            else len++;
        }
    }
}

/* ---------------- main ---------------- */
/* Repaint everything from scratch.  OZ can wipe the display when another
 * application runs (the map must be opened again after RC_DRAW), so ENTER
 * gives the player a way to recover it. */
static void refresh_screen(void)
{
    open_screen();
    clg();
    memset(sh, ' ', CELLS);
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
    while (getk() == 0) idle_count++;
    bar_game = 0;
    prepare_game(random_seed(idle_count));

    reset_cursor();
    strcpy(msg, "");
    render();

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
                strcpy(msg, "Nothing to undo.");
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
                reset_cursor();
                strcpy(msg, answer == 1 ? "New game." : "Replay.");
            } else {
                strcpy(msg, "");
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
                strcpy(msg, "Nothing to move.");
            } else {
                strcpy(msg, "Auto-moved.");
                fix_cursor();
            }
            if (game_won()) strcpy(msg, "YOU WIN! N = new game");
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
                    strcpy(msg, "Empty pile.");
                } else {
                    selected = slot;
                    selected_index = here;
                    strcpy(msg, "Picked up.");
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
            if (game_won()) strcpy(msg, "YOU WIN! N = new game");
            render();
            continue;
        }
    }

    free_game();                    /* hand every card and pile back */
    shuffle_hook = NULL;
    clear_bk();
    flush();
}
