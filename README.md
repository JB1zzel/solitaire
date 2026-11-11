# Solitaire for the Cambridge Z88

Klondike solitaire for the Cambridge Z88, written in C with [z88dk](https://z88dk.org).
It runs as a normal Z88 application (menu entry, help page) in OZ's ordinary
text screen, with the card suits drawn as user-defined characters.

## Features

- Standard Klondike: stock, waste, four foundations, seven tableau columns
- Pick up and put down single cards or runs, with move checking and short messages
- Auto-move to the foundations (`F`)
- Undo (`Z`) of the last 56 logged moves
- Numbered deals: type a seed (0-65535) to replay a specific deal, `R` to replay the current one, or `ENTER` for a random game
- Score, seed and message panel; win detection
- Columns taller than 8 cards show the latest 8 and scroll when you push the cursor past the top, with faint (grey) counts of the hidden cards above and below
- Black suits in reverse video; card backs, cursor and "picked up" markers
- Only the changed parts of the screen are redrawn
- Survives a trip to another application (PipeDream, etc.): it detects the swap and repaints, and `ENTER` forces a redraw
- Frees its memory on exit and can be started again without a hard reset

## Keys

| Key | Action |
|-----|--------|
| `Q` / `A` | cursor up / down |
| `O` / `P` | cursor left / right |
| `SPACE` | pick up / put down (on the stock: draw a card) |
| `F` | auto-move to the foundations |
| `Z` | undo |
| `N` | new game / replay / enter a seed |
| `ENTER` | redraw the screen |
| `ESC` | quit |

## Building

You need z88dk. From this folder:

    zcc +z88 -subtype=app -O2 -create-app sol_core.c sol_z88.c -osolitaire

This writes the application bank files (`solitaire.63`, plus an `.epr` card image).

For installing into RAM with the Installer (OZ 4.x) or OZ 5:

    zcc +z88 -subtype=installer -O2 -create-app sol_core.c sol_z88.c -osolitaire

This writes `.app` and `.ap0` files for OZ 4.x and an `.a5p` file for OZ 5.

### Size

A Z88 application has to fit in one 16 KB bank, and this game is close to the
limit. If a change makes the build fail with "Binary file too large", the fixes
that have worked so far are `-O2` (or `-O3`), keeping the seed 16-bit (which
avoids the long-division library routines) and trimming text.

## Source files

| File | What it is |
|------|------------|
| `sol_core.c`, `sol_core.h` | Game logic, independent of the display (shared with the ZX Spectrum builds) |
| `sol_z88.c` | Z88 front end: text-mode renderer, input, application header |
| `jester.h` | 64x64 bitmap not used |

## How the display works

- OZ text screen, 80 x 8 cells. A backing buffer holds what should be on screen, a shadow buffer holds what was last sent, and each row is updated from its first to its last changed cell.
- Each suit is two characters wide (left and right half), 14 user-defined characters in all, so it stays within the 16 that are safe on an unexpanded Z88.
- Coming back from another application is detected by reading the Blink seconds counter (port `$D1`) and noticing a gap of more than two seconds between key polls.

## Status

Tested in the OZvm emulator. Not yet tested on real hardware.

Not working or not done:
- The jester picture (see above)

