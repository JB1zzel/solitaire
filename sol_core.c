/* sol_core.c - Klondike solitaire game logic.
 * No platform includes, no drawing, no input.  The front end owns the
 * screen and the keys; this file owns the rules and the state.
 *
 * Memory model (about 300 bytes of RAM in total, nothing allocated):
 *   - a card is one byte, its id:  (suit << 4) | rank
 *   - every pile is a linked list threaded through one shared nxt[] array
 *   - face-up flags are one bit per card id
 *   - undo is a log of moves, 3 bytes each
 */

#include "sol_core.h"

#define NIL 255

pile *piles[PILE_COUNT];
int   score;
unsigned long current_seed = 0;
void (*shuffle_hook)(int done, int total) = NULL;

static pile          pile_store[PILE_COUNT];
static unsigned char nxt[64];               /* next card in the same pile */
static unsigned char rev[8];                /* bit per card id: face up   */
static card          tmp[4];                /* results of peek_at/last    */
static unsigned char tmp_i;

static const char pile_types[] = "dwffffccccccc";
static const unsigned char bitmask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

#define SUIT_OF(id)  ((id) >> 4)
#define RANK_OF(id)  ((id) & 15)
#define MAKE_ID(s, r) ((unsigned char)(((s) << 4) | (r)))
#define IS_UP(id)    (rev[(id) >> 3] & bitmask[(id) & 7])

static void set_up(unsigned char id, int up)
{
    if (up) rev[id >> 3] |= bitmask[id & 7];
    else    rev[id >> 3] &= (unsigned char)~bitmask[id & 7];
}

/* ---------------- suit / rank ---------------- */
const char *suit_ch(int suit)
{
    switch (suit) {
    case SUIT_HEART:   return "H";
    case SUIT_SPADE:   return "S";
    case SUIT_CLUB:    return "C";
    case SUIT_DIAMOND: return "D";
    default:           return "?";
    }
}

const char *rank_ch(int rank)
{
    static const char *r[] = { "A","2","3","4","5","6","7","8","9","T",
                               "J","Q","K" };
    if (rank < 0 || rank >= RANK_COUNT) return "?";
    return r[rank];
}

int is_red_card(const card *c)
{
    return c->suit == SUIT_HEART || c->suit == SUIT_DIAMOND;
}
int is_black_card(const card *c)
{
    return c->suit == SUIT_CLUB || c->suit == SUIT_SPADE;
}

/* ---------------- rules on card ids ---------------- */
static int is_black(unsigned char id)
{
    int s = SUIT_OF(id);
    return s == SUIT_CLUB || s == SUIT_SPADE;
}
static int is_alt(unsigned char a, unsigned char b)
{
    return is_black(a) != is_black(b);
}
/* child may sit on parent in a column: alternate colour, one rank lower */
static int can_col(unsigned char parent, unsigned char child)
{
    return is_alt(parent, child) && RANK_OF(parent) == RANK_OF(child) + 1;
}
/* child may go on a foundation: same suit, one rank higher */
static int can_fdn(unsigned char parent, unsigned char child)
{
    return SUIT_OF(parent) == SUIT_OF(child)
        && RANK_OF(child) == RANK_OF(parent) + 1;
}

/* ---------------- slots ---------------- */
int slot_to_pile(int slot)
{
    if (slot == SLOT_STOCK) return P_DECK;
    if (slot == SLOT_WASTE) return P_WASTE;
    if (slot >= SLOT_FDN1 && slot < SLOT_FDN1 + FDN_COUNT)
        return P_FDN1 + (slot - SLOT_FDN1);
    if (slot >= SLOT_COL1 && slot < SLOT_COL1 + COL_COUNT)
        return P_COL1 + (slot - SLOT_COL1);
    return -1;
}

int slot_x(int slot)
{
    if (slot == SLOT_STOCK) return 0;
    if (slot == SLOT_WASTE) return COL_SP;
    if (slot >= SLOT_FDN1 && slot < SLOT_FDN1 + FDN_COUNT)
        return (3 + (slot - SLOT_FDN1)) * COL_SP;
    if (slot >= SLOT_COL1 && slot < SLOT_COL1 + COL_COUNT)
        return (slot - SLOT_COL1) * COL_SP;
    return 0;
}

/* ---------------- pile primitives ---------------- */
static void p_push(pile *p, unsigned char id)       /* add on top (tail) */
{
    nxt[id] = NIL;
    if (p->num_cards == 0) p->head = id;
    else                   nxt[p->tail] = id;
    p->tail = id;
    p->num_cards++;
}

static unsigned char p_shift(pile *p)               /* remove from head */
{
    unsigned char id = p->head;
    p->head = nxt[id];
    p->num_cards--;
    if (p->num_cards == 0) p->tail = NIL;
    return id;
}

static void p_unshift(pile *p, unsigned char id)    /* add at head */
{
    nxt[id] = (p->num_cards == 0) ? NIL : p->head;
    if (p->num_cards == 0) p->tail = id;
    p->head = id;
    p->num_cards++;
}

static unsigned char p_at(pile *p, int idx)
{
    unsigned char id = p->head;
    while (idx-- > 0 && id != NIL) id = nxt[id];
    return id;
}

static unsigned char p_pop_tail(pile *p)
{
    unsigned char id = p->tail;
    if (p->num_cards == 1) {
        p->head = p->tail = NIL;
    } else {
        p->tail = p_at(p, p->num_cards - 2);
        nxt[p->tail] = NIL;
    }
    p->num_cards--;
    return id;
}

/* move the last n cards of sp on top of dp, keeping their order */
static void splice_tail(pile *sp, pile *dp, int n)
{
    int keep = sp->num_cards - n;
    unsigned char first, last = sp->tail;
    if (n <= 0) return;
    if (keep == 0) {
        first = sp->head;
        sp->head = sp->tail = NIL;
    } else {
        unsigned char prev = p_at(sp, keep - 1);
        first = nxt[prev];
        nxt[prev] = NIL;
        sp->tail = prev;
    }
    if (dp->num_cards == 0) dp->head = first;
    else                    nxt[dp->tail] = first;
    dp->tail = last;
    sp->num_cards = keep;
    dp->num_cards += n;
}

/* list a pile's card ids into buf (at most 24 cards in any pile) */
static int gather(pile *p, unsigned char *buf)
{
    int n = 0;
    unsigned char id = p->head;
    while (id != NIL && n < 24) { buf[n++] = id; id = nxt[id]; }
    return n;
}

/* ---------------- peek (compatibility with the front ends) ---------------- */
static card *mk(unsigned char id)
{
    card *c;
    if (id == NIL) return NULL;
    c = &tmp[tmp_i];
    tmp_i = (unsigned char)((tmp_i + 1) & 3);
    c->suit = (unsigned char)SUIT_OF(id);
    c->rank = (unsigned char)RANK_OF(id);
    c->revealed = IS_UP(id) ? 1 : 0;
    return c;
}

card *peek_at(pile *p, int idx)
{
    if (!p || idx < 0 || idx >= p->num_cards) return NULL;
    return mk(p_at(p, idx));
}

card *peek_last(pile *p)
{
    if (!p || p->num_cards == 0) return NULL;
    return mk(p->tail);
}

/* ---------------- undo log ----------------
 * entry[0] = (src pile << 4) | dst pile
 * entry[1] = count | flipped << 5 | kind << 6
 * entry[2] = (score change + 100) | first-move-of-an-action << 7
 */
enum { KIND_MOVE, KIND_DRAW, KIND_REDEAL };

static unsigned char lg[UNDO_MOVES][3];
static int           lg_n;
static unsigned char pending;       /* next logged move starts an action */

static void log_drop_oldest(void)
{
    int k;
    for (k = 1; k < lg_n; k++)
        if (lg[k][2] & 0x80) break;
    if (k >= lg_n) { lg_n = 0; return; }
    memmove(&lg[0][0], &lg[k][0], (unsigned)(lg_n - k) * 3);
    lg_n -= k;
}

static void log_add(int src, int dst, int n, int flip, int kind, int delta)
{
    if (lg_n >= UNDO_MOVES) log_drop_oldest();
    lg[lg_n][0] = (unsigned char)((src << 4) | dst);
    lg[lg_n][1] = (unsigned char)(n | (flip << 5) | (kind << 6));
    lg[lg_n][2] = (unsigned char)((delta + 100) | (pending ? 0x80 : 0));
    lg_n++;
    pending = 0;
}

void undo_push(int sel_row, int sel_col, int sel_depth,
               int selected, int selected_index)
{
    (void)sel_row; (void)sel_col; (void)sel_depth;
    (void)selected; (void)selected_index;
    pending = 1;                    /* the next move opens a new action */
}

void undo_drop(void)
{
    pending = 0;                    /* the action did nothing */
}

static void reverse_entry(const unsigned char *e)
{
    pile *src = piles[e[0] >> 4];
    pile *dst = piles[e[0] & 15];
    int n = e[1] & 31, flip = (e[1] >> 5) & 1, kind = e[1] >> 6;
    int delta = (int)(e[2] & 0x7F) - 100;

    if (kind == KIND_DRAW) {                    /* waste top -> deck head */
        unsigned char id = p_pop_tail(dst);
        set_up(id, 0);
        p_unshift(src, id);
    } else if (kind == KIND_REDEAL) {           /* deck -> waste, face up */
        int i;
        for (i = 0; i < n; i++) {
            unsigned char id = p_shift(dst);
            set_up(id, 1);
            p_push(src, id);
        }
    } else {
        if (flip) set_up(src->tail, 0);         /* turn the card back over */
        splice_tail(dst, src, n);
    }
    score -= delta;
}

int undo_pop(int *sel_row, int *sel_col, int *sel_depth,
             int *selected, int *selected_index)
{
    int slot = 0;
    if (lg_n == 0) return 0;
    while (lg_n > 0) {
        const unsigned char *e = lg[--lg_n];
        reverse_entry(e);
        if (e[2] & 0x80) {
            /* draws and redeals start at the stock, moves at their source */
            slot = ((e[1] >> 6) == KIND_MOVE) ? (e[0] >> 4) : SLOT_STOCK;
            break;
        }
    }
    /* put the cursor where the action started; nothing is picked up */
    if (slot < SLOT_COL1) {
        *sel_row = 0; *sel_col = slot; *sel_depth = -1;
    } else {
        *sel_row = 1; *sel_col = slot - SLOT_COL1;
        *sel_depth = piles[slot]->num_cards - 1;
    }
    *selected = -1;
    *selected_index = -1;
    pending = 0;
    return 1;
}

/* ---------------- scoring ---------------- */
static void add_score(int delta)
{
    score += delta;
    if (score < 0) score = 0;
}

/* ---------------- deck / deal / turn ---------------- */
static void turn(void)
{
    unsigned char id = p_shift(piles[P_DECK]);
    set_up(id, 1);
    p_push(piles[P_WASTE], id);
    log_add(P_DECK, P_WASTE, 1, 0, KIND_DRAW, 0);
}

static void redeal(void)
{
    int n = piles[P_WASTE]->num_cards, s0 = score;
    while (piles[P_WASTE]->num_cards > 0) {
        unsigned char id = p_shift(piles[P_WASTE]);
        set_up(id, 0);
        p_push(piles[P_DECK], id);
    }
    add_score(-100);
    log_add(P_WASTE, P_DECK, n, 0, KIND_REDEAL, score - s0);
}

static void deal(void)
{
    int i, k;
    for (i = 0; i < COL_COUNT; i++) {
        pile *col = piles[P_COL1 + i];
        for (k = 0; k <= i; k++) {
            unsigned char id = p_shift(piles[P_DECK]);
            p_push(col, id);
            if (k == i) set_up(id, 1);
        }
    }
    {
        unsigned char id = p_shift(piles[P_DECK]);
        set_up(id, 1);
        p_push(piles[P_WASTE], id);
    }
}

/* ---------------- move logic ---------------- */
static int column_run_start(const unsigned char *buf, int n)
{
    int i = n - 1;
    while (i > 0) {
        unsigned char lower = buf[i], higher = buf[i - 1];
        if (!IS_UP(lower) || !IS_UP(higher)) break;
        if (!is_alt(lower, higher)) break;
        if (RANK_OF(higher) != RANK_OF(lower) + 1) break;
        i--;
    }
    return i;
}

static int pick_amount(int spi, int dpi, int dst_is_fdn, int forced_index)
{
    pile *sp = piles[spi], *dp = piles[dpi];
    unsigned char dtop = (dp->num_cards > 0) ? dp->tail : NIL;

    if (sp->num_cards == 0) return 0;

    if (forced_index >= 0) {
        unsigned char bottom;
        if (forced_index >= sp->num_cards) return 0;
        bottom = p_at(sp, forced_index);
        if (!IS_UP(bottom)) return 0;
        if (dst_is_fdn) {
            if (forced_index != sp->num_cards - 1) return 0;
            if (dtop == NIL) return (RANK_OF(bottom) == RANK_A) ? 1 : 0;
            return can_fdn(dtop, bottom) ? 1 : 0;
        }
        if (dtop == NIL)
            return (RANK_OF(bottom) == RANK_K)
                 ? sp->num_cards - forced_index : 0;
        return can_col(dtop, bottom) ? sp->num_cards - forced_index : 0;
    }

    if (dst_is_fdn) {
        unsigned char sc = sp->tail;
        if (!IS_UP(sc)) return 0;
        if (dtop == NIL) return (RANK_OF(sc) == RANK_A) ? 1 : 0;
        return can_fdn(dtop, sc) ? 1 : 0;
    }
    {
        unsigned char buf[24], run_head, top;
        int n = gather(sp, buf);
        /* multi-card runs only exist in tableau columns; waste and
           foundation piles always move one card */
        int start = (sp->type == 'c') ? column_run_start(buf, n) : n - 1;
        int run_len = n - start;

        run_head = buf[start];
        if (run_len > 0 && IS_UP(run_head)) {
            if (dtop == NIL) {
                if (RANK_OF(run_head) == RANK_K) return run_len;
            } else if (can_col(dtop, run_head)) {
                return run_len;
            }
        }
        top = sp->tail;
        if (IS_UP(top)) {
            if (dtop == NIL) {
                if (RANK_OF(top) == RANK_K) return 1;
            } else if (can_col(dtop, top)) {
                return 1;
            }
        }
        return 0;
    }
}

static void move_cards(int spi, int dpi, int amount)
{
    pile *sp = piles[spi], *dp = piles[dpi];
    int s0 = score, flip = 0;

    splice_tail(sp, dp, amount);
    if (sp->num_cards > 0 && !IS_UP(sp->tail)) {
        set_up(sp->tail, 1);
        add_score(5);
        flip = 1;
    }
    if (sp->type == 'f' && dp->type == 'c') add_score(-15);  /* pull back */
    if (dp->type == 'f') add_score(10);
    if (sp->type == 'w' && dp->type == 'c') add_score(5);
    log_add(spi, dpi, amount, flip, KIND_MOVE, score - s0);
}

const char *try_move(int src_slot, int dst_slot, int src_index)
{
    int spi = slot_to_pile(src_slot);
    int dpi = slot_to_pile(dst_slot);
    pile *sp, *dp;
    int amount, dst_is_fdn;

    if (spi < 0 || dpi < 0) return "Invalid slot";

    if (dpi == P_DECK && spi != P_DECK) return "Cannot move to stock";

    sp = piles[spi];
    dp = piles[dpi];

    if (spi == P_DECK) {
        if (piles[P_DECK]->num_cards == 0) {
            if (piles[P_WASTE]->num_cards == 0) return "Cannot redeal";
            redeal();
            return "Redealt";
        }
        turn();
        return "Drew card";
    }

    if (sp->num_cards == 0) return "Source empty";
    dst_is_fdn = (dp->type == 'f');

    /* the waste pile is never a destination, and shuffling cards
       between foundations is pointless (and would farm score) */
    if (dp->type == 'w') return "Invalid move";
    if (dst_is_fdn && sp->type == 'f') return "Invalid move";

    amount = pick_amount(spi, dpi, dst_is_fdn, src_index);
    if (amount == 0) return "Invalid move";

    move_cards(spi, dpi, amount);
    return "OK";
}

int auto_foundation(void)
{
    int moved = 0;
    int progress = 1;

    while (progress) {
        int i;
        progress = 0;

        if (piles[P_WASTE]->num_cards > 0) {
            unsigned char c = piles[P_WASTE]->tail;
            if (IS_UP(c)) {
                for (i = 0; i < FDN_COUNT; i++) {
                    pile *f = piles[P_FDN1 + i];
                    int ok = (f->num_cards > 0) ? can_fdn(f->tail, c)
                                                : (RANK_OF(c) == RANK_A);
                    if (ok) {
                        move_cards(P_WASTE, P_FDN1 + i, 1);
                        moved++;
                        progress = 1;
                        break;
                    }
                }
            }
        }
        if (progress) continue;

        for (i = 0; i < COL_COUNT; i++) {
            pile *col = piles[P_COL1 + i];
            int j;
            unsigned char c;
            if (col->num_cards == 0) continue;
            c = col->tail;
            if (!IS_UP(c)) continue;
            for (j = 0; j < FDN_COUNT; j++) {
                pile *f = piles[P_FDN1 + j];
                int ok = (f->num_cards > 0) ? can_fdn(f->tail, c)
                                            : (RANK_OF(c) == RANK_A);
                if (ok) {
                    move_cards(P_COL1 + i, P_FDN1 + j, 1);
                    moved++;
                    progress = 1;
                    break;
                }
            }
            if (progress) break;
        }
    }

    return moved;
}

int game_won(void)
{
    int i;
    for (i = 0; i < FDN_COUNT; i++)
        if (piles[P_FDN1 + i]->num_cards != 13) return 0;
    return 1;
}

/* ---------------- game setup ---------------- */
static void reset_state(void)
{
    int i;
    for (i = 0; i < PILE_COUNT; i++) {
        piles[i] = &pile_store[i];
        pile_store[i].head = pile_store[i].tail = NIL;
        pile_store[i].num_cards = 0;
        pile_store[i].type = pile_types[i];
    }
    memset(nxt, NIL, sizeof nxt);
    memset(rev, 0, sizeof rev);
    memset(tmp, 0, sizeof tmp);
    tmp_i = 0;
    lg_n = 0;
    pending = 0;
}

/* Call once at program start.  On platforms where the program can be
 * re-entered without being reloaded (e.g. a Z88 application), RAM is not
 * guaranteed to be zero, so nothing here may rely on it. */
void core_init(void)
{
    reset_state();
    score = 0;
    current_seed = 0;
    shuffle_hook = NULL;
}

void prepare_game(unsigned long seed)
{
    unsigned char order[52];
    int i, j, s, r;

    current_seed = seed;
    srand((unsigned)seed);
    reset_state();

    i = 0;
    for (s = 0; s < SUIT_COUNT; s++)
        for (r = 0; r < RANK_COUNT; r++)
            order[i++] = MAKE_ID(s, r);

    /* Fisher-Yates shuffle */
    for (i = 51; i > 0; i--) {
        unsigned char t;
        if (shuffle_hook && (i % 3) == 0) shuffle_hook(51 - i, 51);
        j = rand() % (i + 1);
        t = order[i]; order[i] = order[j]; order[j] = t;
    }
    if (shuffle_hook) shuffle_hook(51, 51);         /* finish at 100% */

    for (i = 0; i < 52; i++)
        p_push(piles[P_DECK], order[i]);
    deal();
    score = 0;
}

/* The same seed always gives the same deal, so a replay is just a
 * fresh deal of the current seed (quietly: no progress bar). */
void replay_game(void)
{
    void (*hook)(int, int) = shuffle_hook;
    shuffle_hook = NULL;
    prepare_game(current_seed);
    shuffle_hook = hook;
}

void free_game(void)
{
    reset_state();                  /* nothing is allocated; just empty it */
}
