# N8 Machine I/O Hardware Reference

Two approaches for text I/O on the N8: TTY device or direct frame buffer + video registers.

## Approach 1: TTY Device ($D820-$D823)

Simple serial-style I/O. The emulator renders TTY output in an ImGui window.

```asm
; Output: poll busy flag, write data
putc:   LDA $D820         ; TTY_OUT_CTRL — bit 0 = busy
        AND #$01
        BNE putc
        STA $D821         ; TTY_OUT_DATA
        RTS

; Input: poll ready flag, read data
getc:   LDA $D822         ; TTY_IN_CTRL — bit 0 = available
        AND #$01
        BEQ getc
        LDA $D823         ; TTY_IN_DATA
        RTS
```

Pro: Simple. Con: No cursor, no scrolling control, no screen positioning.

## Approach 2: Frame Buffer + Video Registers (this game)

Direct writes to the 80x25 character grid at $C000. Video hardware handles font rendering, cursor, and scroll.

### Frame Buffer ($C000-$CFFF)

4KB memory-mapped character grid. Row-major, 80 bytes per row, 25 rows = 2000 bytes. Each byte is an ASCII character code.

```
Address = $C000 + (row * 80) + col
Row 0:  $C000-$C04F
Row 1:  $C050-$C09F
...
Row 24: $C780-$C7CF
```

Write a character directly:
```asm
; Write 'A' at row 3, col 10
LDA #<($C000 + 3*80 + 10)  ; = $C0FA
STA zp_fb
LDA #>($C000 + 3*80 + 10)
STA zp_fb+1
LDA #'A'
LDY #$00
STA (zp_fb),Y
```

### Row * 80 Calculation

No MUL instruction on the 6502. Use: `row*80 = row*64 + row*16`.

```asm
calc_fb_addr:
        ; row * 16
        LDA #$00
        STA zp_fb+1
        LDA zp_row
        ASL A : ROL zp_fb+1    ; *2
        ASL A : ROL zp_fb+1    ; *4
        ASL A : ROL zp_fb+1    ; *8
        ASL A : ROL zp_fb+1    ; *16
        STA zp_fb

        ; Save row*16
        LDA zp_fb+1 : PHA
        LDA zp_fb   : PHA

        ; row*64 = row*16 << 2
        ASL zp_fb : ROL zp_fb+1
        ASL zp_fb : ROL zp_fb+1

        ; row*80 = row*64 + row*16
        PLA : CLC : ADC zp_fb   : STA zp_fb
        PLA :       ADC zp_fb+1 : STA zp_fb+1

        ; + col + $C000
        CLC
        LDA zp_fb   : ADC zp_col : STA zp_fb
        LDA zp_fb+1 : ADC #$00   : STA zp_fb+1
        CLC
        LDA zp_fb+1 : ADC #>$C000 : STA zp_fb+1
        RTS
```

### Video Registers ($D840-$D848)

| Register | Addr  | R/W | Description |
|----------|-------|-----|-------------|
| VID_MODE    | $D840 | RW | Mode: $00=text default, $01=text custom |
| VID_WIDTH   | $D841 | R  | Columns (80) |
| VID_HEIGHT  | $D842 | R  | Rows (25) |
| VID_STRIDE  | $D843 | R  | Bytes per row (80) |
| VID_OPER    | $D844 | W  | Operation trigger (write-once, does not latch) |
| VID_CURSOR  | $D845 | RW | Cursor style |
| VID_CURCOL  | $D846 | RW | Cursor column (0-79) |
| VID_CURROW  | $D847 | RW | Cursor row (0-24) |
| VID_VSYNC   | $D848 | R  | VSync counter |

### VID_OPER ($D844) — Hardware Operations

Write a value to trigger an operation. Value does not latch (reads back $00).

| Value | Operation |
|-------|-----------|
| $01   | Scroll up (move rows 1-24 → 0-23, clear row 24) |
| $02   | Scroll down |
| $03   | Scroll left |
| $04   | Scroll right |

```asm
scroll_up:
        LDA #$01
        STA $D844
        RTS
```

### VID_CURSOR ($D845) — Cursor Control

Bit field:
- Bits 0-1: mode (0=off, 1=steady, 2=flash)
- Bits 2-3: shape (0=underline, 1=block)
- Bits 4-7: flash rate (frames per toggle; 0=not displayed)

```asm
CURSOR_STYLE = $F6    ; flash + block + rate 15

cursor_on:
        LDA zp_col
        STA $D846         ; VID_CURCOL
        LDA zp_row
        STA $D847         ; VID_CURROW
        LDA #CURSOR_STYLE
        STA $D845         ; VID_CURSOR
        RTS

cursor_off:
        LDA #$00
        STA $D845
        RTS
```

### Keyboard Registers ($D860-$D862)

| Register | Addr  | R/W | Description |
|----------|-------|-----|-------------|
| KBD_DATA   | $D860 | R  | Front keycode from FIFO |
| KBD_STATUS | $D861 | R  | bit0=available, bit1=overflow, bits2-5=modifiers |
| KBD_ACK    | $D861 | W  | Write $01 to pop front entry |
| KBD_CTRL   | $D862 | -- | Reserved |

Polling loop:
```asm
kbd_wait:
        LDA $D861         ; KBD_STATUS
        AND #$01          ; bit 0 = key available
        BEQ kbd_wait
        RTS

kbd_read:
        LDA $D860         ; KBD_DATA = keycode
        PHA
        LDA #$01
        STA $D861         ; KBD_ACK = pop
        PLA               ; A = keycode
        RTS
```

### Clear Screen

Fill 2000 bytes at $C000 with spaces ($20):

```asm
clear_screen:
        LDA #$00
        STA zp_col
        STA zp_row
        LDA #<$C000
        STA zp_fb
        LDA #>$C000
        STA zp_fb+1
        LDA #$20
        LDX #$08          ; 8 pages = 2048 bytes (covers 80*25=2000)
        LDY #$00
@page:  STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        DEX
        BNE @page
        LDA #$00
        STA $D846          ; cursor col = 0
        STA $D847          ; cursor row = 0
        RTS
```

### Readline with Frame Buffer

Adapted from mon1.s readline but using keyboard polling + frame buffer echo:

```asm
readline_fb:
        LDA #$00
        STA line_len
@loop:  JSR kbd_wait
        JSR kbd_read       ; A = keycode
        CMP #$0D           ; Enter
        BEQ @done
        CMP #$08           ; Backspace
        BEQ @bs
        CMP #$20           ; < $20 = control char
        BCC @loop
        CMP #$7F           ; > $7E = not printable
        BCS @loop
        LDX line_len
        CPX #BUF_SIZE
        BCS @loop          ; buffer full
        STA LINE_BUF,X
        INC line_len
        PHA
        JSR cursor_off
        PLA
        JSR put_char
        JSR cursor_on
        JMP @loop
@bs:    LDX line_len
        BEQ @loop
        DEC line_len
        JSR cursor_off
        DEC zp_col
        LDA #$20
        JSR put_char_at_cur
        DEC zp_col
        JSR cursor_on
        JMP @loop
@done:  LDX line_len
        LDA #$00
        STA LINE_BUF,X    ; null-terminate
        JSR cursor_off
        JSR new_line
        JSR cursor_on
        RTS
```
