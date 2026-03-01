# Firmware Stdlib Analysis

Extracted from playtesting the Sprawl adventure (`firmware/playground/adventure/`).
Goal: identify routines worth sharing across playground programs.

## Current State

### Kernel ($F000-$FFFF)
- 3 entry points via jump table at $FE00: `K_TTY_PUTC`, `K_TTY_PUTC_B`, `K_TTY_PUTS`
- TTY-model I/O only (serial character output via $D820)
- Uses ZP $E0-$FF
- cc65 runtime linked (zerobss, copydata, initlib)

### Playground Programs
- Fully self-contained — no kernel dependency, no shared library
- Each reimplements what it needs (mon1/mon2/mon3 each have TTY routines)
- `common_init.s` + `common_vectors.s` duplicated in `gdb_playground/` and `playground/`
- Adventure defines 52 routines; 14 are general-purpose

### Two I/O Models
| Model | Path | Used by |
|-------|------|---------|
| TTY | Write byte to $D820, kernel handles cursor/scroll | Kernel monitor, mon1/mon2/mon3, test_tty |
| Frame Buffer | Direct writes to $C000, hardware cursor/scroll via $D840 | Adventure |

These are fundamentally different display paths. A shared library needs to decide which to support (or both).

## General-Purpose Routines (extraction candidates)

### Tier 1: Screen I/O (frame buffer model)

| Routine | Bytes | In | Out | Clobbers | Calls/turn | Cycles | Notes |
|---------|-------|----|-----|----------|-----------|--------|-------|
| `calc_fb_addr` | ~45 | zp_row, zp_col (implicit) | zp_fb | A | ~280 | 120 | row*80+col+$C000 via shift-add. Uses stack for intermediate. Hot path. |
| `put_char` | ~25 | A=char | — | A, Y | ~280 | 170 | JSR calc_fb_addr + STA (zp_fb),Y + advance col. Wraps row at col 80. Updates VID_CURCOL/VID_CURROW. |
| `put_char_at_cur` | ~15 | A=char | — | A, Y | ~5 | 140 | Like put_char but no row wrap. Used by backspace erase. |
| `new_line` | ~12 | — | — | A | 5-8 | 55 | Sets col=0, JSR advance_row, updates VID hw cursor. |
| `advance_row` | ~15 | — | — | A | 5-8 | 20/22k | row++. No-scroll: 20 cyc. Scroll: +scroll_up +update_status_bar. |
| `scroll_up` | ~8 | — | — | A | 0-5 | 12+ | STA VID_OPER. 12 cyc CPU-side; hw does the actual blit. |
| `clear_screen` | ~30 | — | — | A, X, Y | 0 | 22,600 | Fill 8 pages ($C000-$C7FF) with $20. Startup only. |
| `cursor_on` | ~12 | zp_col, zp_row (implicit) | — | A | ~4 | 26 | Writes VID_CURCOL, VID_CURROW, VID_CURSOR. |
| `cursor_off` | ~6 | — | — | A | ~4 | 12 | Writes VID_CURSOR=0. |

**Total: ~168 bytes, 9 routines**

Cycles = typical single invocation. "Calls/turn" = estimated runtime calls for a typical game turn (player types short command, sees room description + exits + items ≈ 280 output chars).

`calc_fb_addr` and `put_char` dominate runtime — called once per output character. `calc_fb_addr` alone is ~33,600 cycles/turn (120 × 280).

`advance_row` has a dependency on `update_status_bar` (called on scroll). This couples it to the game. A shared version would need a callback hook or drop the status bar call.

### Tier 2: String Output

| Routine | Bytes | In | Out | Clobbers | Calls/turn | Cycles | Notes |
|---------|-------|----|-----|----------|-----------|--------|-------|
| `print_str` | ~30 | zp_str=ptr | — | A, Y | 4-6 | ~190/char | Null-terminated, $0D=newline. 20-char string ≈ 3,800 cyc. Handles page crossing (INY wrap → INC zp_str+1). |
| `print_wrap` | ~90 | zp_str=ptr | — | A, Y | 1-2 | ~220/char | Word-wrap at col 80, lookahead from col 60. 200-char desc ≈ 45k cyc. Strips leading spaces after wrap. |
| `word_len_ahead` | ~35 | zp_tmp=Y index | A=length | A, Y | 5-10 | ~175 | Private helper for print_wrap. ~25 cyc/char in word, typical word 6 chars. Saves/restores zp_str+1. |

**Total: ~155 bytes, 3 routines**

`print_wrap` dominates a turn's cycle budget — a single 200-char room description costs ~45,000 cycles. It's also the highest-value extraction target: complex enough that nobody wants to rewrite it.

### Tier 3: Keyboard Input

| Routine | Bytes | In | Out | Clobbers | Calls/turn | Cycles | Notes |
|---------|-------|----|-----|----------|-----------|--------|-------|
| `kbd_wait` | ~8 | — | — | A | ~5 | 9/poll | Busy-loop on KBD_STATUS bit 0. Total cycles dominated by human latency — millions of polls per keypress. |
| `kbd_read` | ~10 | — | A=keycode | A | ~5 | 23 | Reads KBD_DATA, writes KBD_ACK=1 to pop ring buffer. |
| `readline_fb` | ~80 | — | LINE_BUF filled, line_len set | A, X | 1 | ~270/key | Full line editor: echo, backspace, 79-char max, null-terminate. Toggles cursor per keypress. Blocks on kbd_wait. |

**Total: ~98 bytes, 3 routines**

`kbd_wait`/`kbd_read` are trivial but every program needs them. `readline_fb` is a full line editor — highest reuse value in this tier. Hardcoded refs to LINE_BUF and line_len BSS symbols need decoupling for stdlib.

### Tier 4: String Comparison

| Routine | Bytes | In | Out | Clobbers | Calls/turn | Cycles | Notes |
|---------|-------|----|-----|----------|-----------|--------|-------|
| `strcmp` | ~20 | zp_ptr2=target ptr | A=0 match, A=1 mismatch | A, Y | ~8 | ~120 | Source hardcoded to VERB_BUF. ~20 cyc/char, typical verb 4-6 chars. Called once per verb_table entry until match. |
| `strcmp_noun` | ~20 | zp_ptr2=target ptr | A=0 match, A=1 mismatch | A, Y | 3-10 | ~120 | Source hardcoded to NOUN_BUF. Same logic, different source buffer. Called by do_go, find_noun_item, do_drop. |

Near-duplicates. A generic `strcmp(zp_str, zp_ptr2)` taking two ZP pointers would replace both — saves 20 bytes and removes BSS coupling.

### Estimated Totals

| Tier | Routines | Bytes | Hot path? | Cycles/turn |
|------|----------|-------|-----------|-------------|
| Screen I/O | 9 | ~168 | `calc_fb_addr` + `put_char` (280×) | ~48k |
| String Output | 3 | ~155 | `print_wrap` (200+ chars) | ~45k |
| Keyboard Input | 3 | ~98 | `kbd_wait` (busy-loop) | blocked on human |
| String Compare | 1 (generic) | ~25 | `strcmp` (8× table scan) | ~1k |
| **Total** | **16** | **~446** | | **~94k** + I/O wait |

~446 bytes of reusable code. Fits easily in a static .lib or ~5% of a 8KB ROM.

At 1 MHz, a typical game turn burns ~94,000 cycles on output (~94ms) plus human input wait. The hot loop is `put_char` → `calc_fb_addr` — called per character, 170 cycles each, 280 times per turn. If perf ever matters, `calc_fb_addr` is the optimization target (cache row base, skip recalc when col advances within same row).

## Zero-Page Layout

### Current Adventure Layout ($00-$0C)
```
$00     zp_col      cursor column (0-79)
$01     zp_row      cursor row (0-24)
$02-03  zp_fb       frame buffer pointer
$04-05  zp_str      string pointer (print_str, print_wrap)
$06     zp_tmp      Y save during string printing
$07     line_len    input line length
$08     cur_room    current room ID (game-specific)
$09-0A  zp_ptr2     second string pointer (strcmp)
$0B     zp_tmp2     second temporary
$0C     zp_item     resolved item ID (game-specific)
```

### Kernel Layout ($E0-$FF)
```
$E0-E1  tty_ptr     TTY string pointer
$E2     tty_tmp     TTY temporary
$E3-E7  (unused)
$F0-F1  irq_save    IRQ A/X save
$F2-F3  (unused)
```

### Observations
- Shared FB routines need: col, row, fb(2), str(2), tmp, tmp2, line_len = 9 bytes ZP minimum
- Game-specific: cur_room, zp_item, zp_ptr2 = 4 bytes (program's own allocation)
- **Options for stdlib ZP:**
  - **Low ZP ($00-$0B):** Fastest. But claims prime real estate programs might want.
  - **Kernel range ($E0+):** Already reserved. Room for 9 bytes at $E3-$EB (currently unused). Consistent convention.
  - **Configurable .inc:** Define `STDLIB_ZP_BASE = $E3` in a .inc, all routines reference `STDLIB_ZP_BASE+0` etc. Programs override if needed. Most flexible, slight complexity.
- The kernel's TTY routines already use $E0-$E2. FB routines at $E3+ would be consistent.

## Coupling Issues

### `advance_row` → `update_status_bar`
Currently: when `advance_row` hits row 25, it calls `scroll_up` AND `update_status_bar`. The status bar call is game-specific.

Options:
1. **Callback vector:** Store a scroll-hook function pointer in ZP or a fixed address. Default to no-op.
2. **Remove from advance_row:** Let the application handle post-scroll actions. Simpler but means apps must wrap `advance_row`.
3. **Separate scroll_with_status:** Keep `advance_row` clean, adventure provides its own wrapper.

Option 3 is cleanest — stdlib `advance_row` just does row++/scroll, adventure wraps it.

### `strcmp` hardcoded buffer
Currently hardcoded to read from `VERB_BUF` / `NOUN_BUF`. A generic version would take two ZP pointers (source string, target string).

### `readline_fb` → BSS buffer
Writes to `LINE_BUF` at a hardcoded BSS address. A generic version could take a buffer pointer parameter, or programs define `LINE_BUF` at a standard location.

## Placement Options

### Option A: Static Library (`n8_fb.lib`)
- Build `.o` files from shared source, archive into `.lib`
- Each program links what it imports — cc65 linker pulls only referenced modules
- Programs are independent, no kernel coupling
- ~446 bytes duplicated per program that uses it (acceptable for 8KB ROM)

### Option B: Kernel Extension
- Add FB routines to kernel, extend $FE00 jump table
- Programs call via `JSR K_FB_PUTCHAR` etc.
- Saves ROM space in programs but grows kernel
- Programs become coupled to kernel version
- Risk: kernel already at 4KB ($F000-$FFFF), adding ~450 bytes is fine but sets precedent

### Option C: Hybrid
- Kernel provides routines + jump table entries
- `.lib` or `.inc` provides import stubs so programs use clean labels
- Best of both but most complexity

### Recommendation
**Option A (static .lib)** is the natural first step:
- No kernel changes required
- Playground programs remain self-contained
- Easy to extract — routines are already isolated in adventure.s
- Later, if kernel expansion makes sense, the same source files can be linked into both

## common_init.s / common_vectors.s Consolidation

### Current State
- Identical files exist in both `firmware/gdb_playground/` and `firmware/playground/`
- Both are ~20 lines each
- Already duplicated in adventure's build (adventure uses playground's copies)

### Consolidation Plan
Move to `firmware/shared/`:
```
firmware/shared/common_init.s
firmware/shared/common_vectors.s
```
Update Makefiles in `gdb_playground/` and `playground/` to reference `../shared/`.
Trivial change, removes duplication, no behavior change.

## What Would Trigger Implementation

- A second frame-buffer program in playground (most likely trigger)
- Kernel v2 work from the spec's open items
- Desire to port mon1/mon2/mon3 from TTY to FB model
