# N8 Storage Device — Firmware Best Practices

Patterns discovered while building the shell's storage commands. All code
references are to `firmware/shell.s` unless noted. Device spec is in
`docs/memory_map/storage.md`.

## Command Construction

Two approaches were tested:

### Inline sends (best for fixed, no-argument commands)

Write command bytes directly to DISK_DATA. Tightest code — 3 bytes per
character (LDA imm + STA abs). Used by `cmd_pwd`:

```asm
    LDA #N8_DISK_CONTROL_CHAN
    STA N8_DISK_CHAN        ; select control channel, clears errors
    LDA #'P'
    STA N8_DISK_DATA
    LDA #'D'
    STA N8_DISK_DATA
    LDA #$00
    STA N8_DISK_DATA        ; null = execute
```

Cost: 9 bytes for "PD" + 6 bytes for channel select. Total 15 bytes.

### Buffer construction (best for variable-argument commands)

Build command string in RAM, then send via generic `disk_send_cmd`. Used
by `cd`, `mkdir`, `rmdir`, `rm` through the shared `build_cmd` helper:

```asm
    LDA #'C'
    LDX #'D'
    JSR build_cmd           ; builds "CD,<arg>" in DISK_BUF
    JSR disk_send_cmd       ; sends bytes + null to control channel
```

Cost: 6 bytes at call site. `build_cmd` is 25 bytes, `disk_send_cmd` is
18 bytes — shared across all callers.

**Verdict**: Use inline for 0-1 callers with fixed commands. Use buffer
for 2+ callers with variable arguments. The crossover is ~3 callers.

For commands with multi-part prefixes (e.g., `"OP,R,<file>"`), inline
buffer construction is clearer than trying to generalize `build_cmd`:

```asm
    LDX #0
    LDA #'O'  / STA DISK_BUF,X / INX
    LDA #'P'  / STA DISK_BUF,X / INX
    LDA #','  / STA DISK_BUF,X / INX
    LDA #'R'  / STA DISK_BUF,X / INX
    LDA #','  / STA DISK_BUF,X / INX
    ; copy arg bytes...
```

## Response Reading

### Stream-to-console (best for text output)

Read DISK_DATA one byte at a time, send to K_CON_PUTCHAR. Poll
DISK_STATUS for available count, then read exactly that many bytes
before re-polling. Convert $0A to K_CON_NEWLINE.

```asm
disk_stream_to_con:
@poll:  LDA N8_DISK_STATUS
        TAX
        AND #N8_DISK_STAT_AVAIL
        BNE @read
        TXA
        AND #N8_DISK_STAT_EOF
        BNE @done
        JMP @poll
@read:  TAY                     ; Y = avail count
@byte:  LDA N8_DISK_DATA
        CMP #$0A
        BEQ @newline
        JSR K_CON_PUTCHAR
        JMP @next
@newline: JSR K_CON_NEWLINE
@next:  DEY
        BNE @byte
        JMP @poll
@done:  RTS
```

Key pattern: the inner loop reads exactly `avail` bytes before going
back to poll. This avoids reading DISK_STATUS on every byte — saves
cycles in the common case where multiple bytes are available.

Used by: `pwd`, `cat`. 30 bytes, highly reusable.

### Buffer-then-parse (best for structured data)

Read entire response into RAM buffer, then walk with an index register.
Used by `ls` where raw format must be reformatted.

```asm
disk_read_to_buf:
        LDX #0
@poll:  LDA N8_DISK_STATUS
        ; ... same poll pattern ...
@byte:  LDA N8_DISK_DATA
        STA DISK_BUF,X
        INX
        BEQ @done               ; X wraps to 0 = 256 bytes = full
        DEY / BNE @byte
        JMP @poll
@done:  STX ZP_DCNT
        RTS
```

The `INX / BEQ @done` trick gives a free 256-byte overflow guard
using the 6502's zero flag on register wrap.

Buffer costs 256 bytes of RAM ($0300-$03FF) but makes parsing trivial —
just walk with X as index. For LS format `D|F\t<name>\t<lo><hi>\n`,
the parser skips fixed fields and loops only on the variable-length
name. ~80 bytes of parser code.

**Verdict**: Stream for pass-through text. Buffer for structured data
that needs reformatting. Don't try to stream-parse LS format — the
state machine complexity isn't worth the RAM savings.

## Error Handling

### Error string table

14 error codes ($01-$0E) mapped to short human-readable strings via a
pointer table. `disk_check_error` reads DISK_ERROR, indexes the table,
and prints `"<cmd>: <message>"`:

```asm
disk_check_error:
        LDA N8_DISK_ERROR
        AND #N8_DISK_ERR_CMD
        BEQ @no_err
        LDA N8_DISK_DATA        ; error code
        TAX
        ; print command name from ZP_CMD...
        DEX                     ; code-1 = table index
        TXA / ASL A / TAX       ; *2 for word offset
        LDA err_tbl,X / STA ZP_PTR
        LDA err_tbl+1,X / STA ZP_PTR+1
        ; print message...
        SEC / RTS               ; C=1 = error
@no_err: CLC / RTS              ; C=0 = ok
```

Cost: ~70 bytes code + ~190 bytes string data = 260 bytes total. Shared
by all 8 commands. Worth it — hex error codes are hostile for
interactive use.

The carry flag convention (C=1 error, C=0 ok) is idiomatic 6502. All
callers just `BCS` to their return path.

## Channel Lifecycle

### Response channels (LS, PWD)

Auto-close when the last byte is consumed (EOF + buffer drained).
After auto-close, DISK_CHAN bit 5 (Active) reads 0. No explicit
CLOSE needed.

Safe to skip close. Tested: `pwd` and `ls` work without close, and
the channel is reclaimed for the next command.

### File channels (OPEN for read/write)

Must close explicitly via binary CL command. Forgetting to close
leaks a channel slot (max 15). A second OPEN of the same file will
fail with "already exists" ($02) if the first is still open.

```asm
disk_close_chan:
        LDA #N8_DISK_CONTROL_CHAN
        STA N8_DISK_CHAN
        LDA #'C' / STA N8_DISK_DATA
        LDA #'L' / STA N8_DISK_DATA
        LDA #',' / STA N8_DISK_DATA
        LDA ZP_CHAN / STA N8_DISK_DATA    ; binary channel ID byte
        LDA #$00 / STA N8_DISK_DATA       ; null = execute
        RTS
```

Note: CL uses binary payload — the parser knows to expect exactly
1 byte after the comma. The channel ID byte can be $00 (channel 0)
without triggering command execution.

### Rule of thumb

- Response channels: don't close, let auto-close handle it
- File channels: always close in the same handler that opened them

## Argument Parsing

### No args (pwd)

Trivial — just send command.

### Single arg (cd, cat, mkdir, rmdir, rm)

`ZP_ARG` is set by the shell's `process_line` to point past the
command word and any separating spaces. Just check it's not empty
(`LDA (ZP_ARG),Y / BEQ missing`).

The `check_arg` helper (35 bytes) validates and prints
`"<cmd>: missing argument"` on failure. Returns C=1 so caller can
`BCS` to prompt. Used by 5 commands — saves ~25 bytes per caller
vs inline checks.

### Optional arg (ls)

Check if arg is empty. If so, send bare command ("LS"). If not,
use `build_cmd` to construct "LS,<arg>".

### Two args (mv, cp)

The `split_args` helper (40 bytes) walks ZP_ARG forward looking for
a space. Null-terminates first arg in place, advances past spaces,
points ZP_ARG2 at second arg. Returns C=1 if either arg is missing.
Modifies LINE_BUF in place — safe because the buffer is disposable
after dispatch.

Similarly, `build_two_arg_cmd` (35 bytes) constructs `"XX,<src>,<dst>"`
in DISK_BUF from ZP_ARG and ZP_ARG2. Used by `mv`; `cp` builds its
OPEN commands inline since the prefix is `"OP,R,"` / `"OP,W,"` (not
a 2-char opcode).

## ZP Conventions

| Range | Purpose |
|-------|---------|
| $E0-$E1 | ZP_PTR — general pointer (print_str, etc.) |
| $E2-$E3 | ZP_PTR2 — second pointer (str_equal) |
| $E4-$E5 | ZP_CMD — command word pointer (set by parser) |
| $E6-$E7 | ZP_ARG — argument pointer (set by parser) |
| $E8 | ZP_LEN — line buffer length |
| $E9 | ZP_POS — cursor position |
| $EA | ZP_TMP — temp byte |
| $EB | ZP_CHAN — current disk channel ID |
| $EC-$ED | ZP_ARG2 — second arg pointer (mv) |
| $EE | ZP_DCNT — disk byte counter |
| $EF | ZP_DFLG — disk flags |
| $F0 | ZP_CHAN2 — second disk channel (cp) |

Rule: use the `$E0-$FF` range for shell state. The `$00-$DF` range
belongs to cc65 runtime and user programs.

## ROM Budget

All 9 commands fit in 1894 bytes (46% of 4KB shell ROM).

| Phase | Binary | Delta | What was added |
|-------|--------|-------|----------------|
| Baseline | 597 | — | Shell with stub ls/cd |
| pwd | 713 | +116 | pwd, disk_stream_to_con |
| cd | 1120 | +407 | cd, build_cmd, disk_send_cmd, disk_check_error, error table |
| ls | 1246 | +126 | ls parser, disk_read_to_buf |
| cat | 1351 | +105 | cat, disk_close_chan |
| mkdir,rmdir,rm,mv | 1625 | +274 | 4 handlers, check_arg, split_args |
| cp | 1894 | +269 | cp, split_args refactor, build_two_arg_cmd, ZP_CHAN2 |

Shared subroutines amortize well. The biggest one-time cost is the
error string table (~260 bytes). After that, each new simple command
adds only ~12-15 bytes at the call site.

## Multi-Channel Operations (cp)

`cp` holds two file channels open simultaneously:
- ZP_CHAN = source (read)
- ZP_CHAN2 = destination (write)

The copy loop switches DISK_CHAN between src and dst on every byte.
This works because DISK_CHAN is just a register select — switching
channels doesn't disturb either channel's state.

Close order matters: close dst first (flush writes), then close src.
On error opening dst, still close src before returning.

```asm
@cbyte:
    LDA ZP_CHAN             ; select src
    STA N8_DISK_CHAN
    LDA N8_DISK_DATA        ; read byte
    PHA
    LDA ZP_CHAN2            ; select dst
    STA N8_DISK_CHAN
    PLA
    STA N8_DISK_DATA         ; write byte
    LDA ZP_CHAN             ; back to src for status poll
    STA N8_DISK_CHAN
```

6 register writes per byte copied. Could be optimized with a
buffered approach (read N bytes into RAM, switch to dst, write N
bytes), but the simple version is clear and correct.

## 6502 Gotchas Encountered

### Branch range limits

Conditional branches (BCS, BCC, BEQ, BNE) have a signed 8-bit
offset: -128 to +127 bytes. The `cp` handler is large enough that
forward branches to error handlers at the bottom exceed this range.

Fix: invert the condition and use JMP:
```asm
    ; Instead of: BCS @far_label
    BCC @nearby_ok
    JMP @far_label
@nearby_ok:
```

Costs 2 extra bytes per occurrence. Watch for this whenever a
handler exceeds ~100 bytes of code.

### INX overflow as bounds check

`INX / BEQ @done` gives a free 256-byte buffer overflow guard:
when X wraps from $FF to $00, the zero flag fires. Used in
`disk_read_to_buf`. Only works for exactly 256-byte buffers.
