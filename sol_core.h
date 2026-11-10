/* solitaire_core.h - Klondike solitaire game logic, no platform deps. */
#ifndef SOLITAIRE_CORE_H
#define SOLITAIRE_CORE_H

#include <stdlib.h>
#include <string.h>

/* ---------------- geometry ---------------- */
#define COL_SP     4        /* 7 cols * 4 = 28, fits 32-char screen */
#define COL_COUNT  7
#define FDN_COUNT  4
#define MAX_STACK 19        /* fits 24-row screen */

/* ---------------- cards ---------------- */
enum { SUIT_HEART, SUIT_SPADE, SUIT_CLUB, SUIT_DIAMOND, SUIT_COUNT };
enum { RANK_A, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7,
       RANK_8, RANK_9, RANK_10, RANK_J, RANK_Q, RANK_K, RANK_COUNT };

typedef struct card {
    unsigned char suit, rank, revealed;
} card;

enum {
    P_DECK, P_WASTE,
    P_FDN1, P_FDN2, P_FDN3, P_FDN4,
    P_COL1, P_COL2, P_COL3, P_COL4, P_COL5, P_COL6, P_COL7,
    PILE_COUNT
};

/* A pile is a linked list threaded through one shared next[] array:
 * head/tail are card ids (255 = none).  Card id = (suit << 4) | rank. */
typedef struct pile {
    unsigned char head, tail;
    char type;                  /* 'd'eck 'w'aste 'f'oundation 'c'olumn */
    int num_cards;
} pile;

/* ---------------- slots ---------------- */
#define SLOT_STOCK   0
#define SLOT_WASTE   1
#define SLOT_FDN1    2
#define SLOT_COL1    6
#define SLOT_COUNT   13

/* ---------------- undo ----------------
 * Undo is a log of moves (3 bytes each), not copies of the table.
 * UNDO_MOVES is the number of logged moves kept.  One player action
 * (e.g. an auto-move) can log many moves, but undo reverses a whole
 * action at a time.  The old UNDO_DEPTH option is ignored. */
#ifndef UNDO_MOVES
#define UNDO_MOVES 56
#endif

/* ---------------- shared state ---------------- */
extern pile *piles[PILE_COUNT];
extern int   score;
extern unsigned long current_seed;
/* optional: called now and then while shuffling (done, total) */
extern void (*shuffle_hook)(int done, int total);

/* ---------------- prototypes ---------------- */
int  slot_to_pile(int slot);
int  slot_x(int slot);
const char *suit_ch(int suit);
const char *rank_ch(int rank);
int  is_red_card(const card *c);
int  is_black_card(const card *c);

card *peek_at(pile *p, int idx);
card *peek_last(pile *p);

void core_init(void);   /* call once at start; makes re-entry safe */
void prepare_game(unsigned long seed);
void replay_game(void);
void free_game(void);

const char *try_move(int src_slot, int dst_slot, int src_index);
int  auto_foundation(void);
int  game_won(void);

void undo_push(int sel_row, int sel_col, int sel_depth,
               int selected, int selected_index);
int  undo_pop(int *sel_row, int *sel_col, int *sel_depth,
              int *selected, int *selected_index);
void undo_drop(void);   /* discard newest snapshot without restoring it */

#endif