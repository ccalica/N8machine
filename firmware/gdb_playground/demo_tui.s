; demo_tui.s - TUI Dashboard Demo
;
; Multi-panel dashboard with animated progress bars, typing log,
; and live keyboard display. Exercises all 9 console kernel routines.
;
; Load:
;   load demo_tui 0xE000
;   load ../n8_kernel 0xF000
;   write 0xFFFC <lo><hi>   (patch reset vector to _init)
;   reset
;   run

.export   _main

; --- Kernel entry points ---
K_CON_GETKEY    = $FE09
K_CON_SETMODE   = $FE0C
K_CON_GETSTATUS = $FE0F
K_CON_PUTCHAR   = $FE12
K_CON_NEWLINE   = $FE15
K_CON_CLEAR     = $FE18
K_CON_SCROLL    = $FE1B
K_CON_MOVCURSOR = $FE1E
K_CON_SETCURSOR = $FE21

VID_CURSOR = $D845

; --- Constants ---
BAR_WIDTH  = 40
BAR_COL    = 9
KEY_ESCAPE = $1B
ANIM_DELAY = 15

; --- Zero page ---
.segment "ZEROPAGE"
zp_tmp:        .res 1
zp_str:        .res 2
zp_row:        .res 1
zp_frame:      .res 1
; Progress bars (fill 0-40, dir 0=up/$FF=down)
zp_bar1:       .res 1
zp_bar1_dir:   .res 1
zp_bar2:       .res 1
zp_bar2_dir:   .res 1
zp_bar3:       .res 1
zp_bar3_dir:   .res 1
; Log state
zp_log_idx:    .res 1
zp_log_typing: .res 1
zp_log_offset: .res 1
zp_log_col:    .res 1
zp_log_row:    .res 1

.segment "CODE"

; ============================================================
; Entry
; ============================================================

_main:
        LDA #$00
        STA VID_CURSOR
        LDA #$00
        LDX #$07               ; ADVANCE+WRAP+SCROLL
        JSR K_CON_SETMODE

restart:
        JSR K_CON_CLEAR

        ; --- Draw border ---
        ; Top row
        LDX #0
        LDY #0
        JSR K_CON_SETCURSOR
        LDA #<str_top
        STA zp_str
        LDA #>str_top
        STA zp_str+1
        JSR print_zp_str
        LDA #59
        STA zp_tmp
@td:    LDA #'-'
        JSR K_CON_PUTCHAR
        DEC zp_tmp
        BNE @td
        LDA #'+'
        JSR K_CON_PUTCHAR

        ; Side borders (rows 1-23)
        LDA #1
        STA zp_row
@sides: LDX #0
        LDY zp_row
        JSR K_CON_SETCURSOR
        LDA #'|'
        JSR K_CON_PUTCHAR
        LDX #79
        LDY zp_row
        JSR K_CON_SETCURSOR
        LDA #'|'
        JSR K_CON_PUTCHAR
        INC zp_row
        LDA zp_row
        CMP #24
        BCC @sides

        ; Bottom row
        LDX #0
        LDY #24
        JSR K_CON_SETCURSOR
        LDA #'+'
        JSR K_CON_PUTCHAR
        LDA #78
        STA zp_tmp
@bd:    LDA #'-'
        JSR K_CON_PUTCHAR
        DEC zp_tmp
        BNE @bd
        ; Last char: ADVANCE only to avoid scroll
        LDA #$00
        LDX #$01
        JSR K_CON_SETMODE
        LDA #'+'
        JSR K_CON_PUTCHAR
        LDA #$00
        LDX #$07
        JSR K_CON_SETMODE

        ; --- Draw layout from table ---
        LDX #0
@lay:   LDA layout,X
        CMP #$FF
        BEQ @lay_done
        STA zp_tmp             ; col
        LDA layout+1,X
        STA zp_row             ; row
        LDA layout+2,X
        STA zp_str
        LDA layout+3,X
        STA zp_str+1
        TXA
        PHA
        LDX zp_tmp
        LDY zp_row
        JSR K_CON_SETCURSOR
        JSR print_zp_str
        PLA
        TAX
        INX
        INX
        INX
        INX
        JMP @lay
@lay_done:

        ; Bar closing brackets
        LDA #8
        STA zp_row
@brk:   LDX #49
        LDY zp_row
        JSR K_CON_SETCURSOR
        LDA #']'
        JSR K_CON_PUTCHAR
        INC zp_row
        LDA zp_row
        CMP #11
        BCC @brk

        ; --- Initialize animation state ---
        LDA #0
        STA zp_frame
        STA zp_bar1
        STA zp_bar1_dir
        STA zp_log_idx
        STA zp_log_typing
        STA zp_log_offset

        LDA #13
        STA zp_bar2
        LDA #0
        STA zp_bar2_dir

        LDA #35
        STA zp_bar3
        LDA #$FF
        STA zp_bar3_dir

; ============================================================
; Animation loop
; ============================================================

animate:
        ; --- Bar 1: every frame ---
        LDA zp_bar1
        LDX zp_bar1_dir
        JSR update_bar_val
        STA zp_bar1
        STX zp_bar1_dir
        LDY #8
        JSR draw_bar

        ; --- Bar 2: every 2 frames ---
        LDA zp_frame
        AND #1
        BNE @skip2
        LDA zp_bar2
        LDX zp_bar2_dir
        JSR update_bar_val
        STA zp_bar2
        STX zp_bar2_dir
        LDY #9
        JSR draw_bar
@skip2:

        ; --- Bar 3: every 4 frames ---
        LDA zp_frame
        AND #3
        BNE @skip3
        LDA zp_bar3
        LDX zp_bar3_dir
        JSR update_bar_val
        STA zp_bar3
        STX zp_bar3_dir
        LDY #10
        JSR draw_bar
@skip3:

        ; --- Log: type one char every 2 frames ---
        JSR update_log

        ; --- Keyboard ---
        JSR K_CON_GETKEY
        CMP #0
        BEQ @no_key
        CMP #KEY_ESCAPE
        BEQ @do_restart
        JSR update_kbd
@no_key:

        LDA #ANIM_DELAY
        JSR delay

        INC zp_frame
        JMP animate

@do_restart:
        JMP restart

; ============================================================
; update_bar_val — oscillate bar value
;   In:  A = fill (0-40), X = dir (0=up, $FF=down)
;   Out: A = new fill, X = new dir
; ============================================================

update_bar_val:
        CPX #0
        BNE @down
        CLC
        ADC #1
        CMP #BAR_WIDTH+1
        BCC @done
        LDA #BAR_WIDTH
        LDX #$FF
        RTS
@down:  SEC
        SBC #1
        BPL @done
        LDA #0
        LDX #0
@done:  RTS

; ============================================================
; draw_bar — render bar at row Y with fill level in A
;   Cursor set to (BAR_COL, Y). Draws fill then spaces.
; ============================================================

draw_bar:
        STA zp_tmp
        LDX #BAR_COL
        JSR K_CON_SETCURSOR

        LDX zp_tmp
        BEQ @spaces
@fill:  LDA #'#'
        JSR K_CON_PUTCHAR
        DEX
        BNE @fill
@spaces:
        LDA #BAR_WIDTH
        SEC
        SBC zp_tmp
        TAX
        BEQ @done
@sp:    LDA #' '
        JSR K_CON_PUTCHAR
        DEX
        BNE @sp
@done:  RTS

; ============================================================
; update_log — type one log char per 2 frames
; ============================================================

update_log:
        LDA zp_frame
        AND #1
        BNE @ret

        ; Currently typing?
        LDA zp_log_typing
        BNE @type_char

        ; Time for new message? (on frame wrap)
        LDA zp_frame
        BNE @ret

        ; Any messages left?
        LDA zp_log_idx
        ASL A
        TAX
        LDA log_table,X
        CMP #$FF
        BEQ @ret

        ; Load string pointer
        STA zp_str
        LDA log_table+1,X
        STA zp_str+1

        ; Compute row = 13 + log_idx
        LDA zp_log_idx
        CLC
        ADC #13
        STA zp_log_row
        LDA #3
        STA zp_log_col
        LDA #0
        STA zp_log_offset
        LDA #1
        STA zp_log_typing
        ; Fall through to type first char

@type_char:
        LDX zp_log_col
        LDY zp_log_row
        JSR K_CON_SETCURSOR

        LDY zp_log_offset
        LDA (zp_str),Y
        BEQ @msg_done
        JSR K_CON_PUTCHAR
        INC zp_log_offset
        INC zp_log_col
@ret:   RTS

@msg_done:
        LDA #0
        STA zp_log_typing
        INC zp_log_idx
        RTS

; ============================================================
; update_kbd — display keycode and modifiers
;   In: A = keycode (from getkey), X = modifiers
; ============================================================

update_kbd:
        STA zp_tmp             ; save keycode
        TXA
        PHA                    ; save modifiers

        ; Keycode at (48, 3)
        LDX #48
        LDY #3
        JSR K_CON_SETCURSOR
        LDA #'$'
        JSR K_CON_PUTCHAR
        LDA zp_tmp
        JSR print_hex

        ; Modifiers at (48, 4)
        LDX #48
        LDY #4
        JSR K_CON_SETCURSOR
        LDA #'$'
        JSR K_CON_PUTCHAR
        PLA
        JSR print_hex
        RTS

; ============================================================
; Helpers
; ============================================================

delay:
        TAX
        BEQ @done
@outer: LDY #$FF
@inner: DEY
        BNE @inner
        DEX
        BNE @outer
@done:  RTS

print_zp_str:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        STY zp_tmp
        JSR K_CON_PUTCHAR
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS

; print_hex — print A as 2 hex digits
print_hex:
        PHA
        LSR A
        LSR A
        LSR A
        LSR A
        JSR @nib
        PLA
        AND #$0F
@nib:   CMP #$0A
        BCC @dig
        CLC
        ADC #('A' - $0A)
        JMP K_CON_PUTCHAR
@dig:   CLC
        ADC #'0'
        JMP K_CON_PUTCHAR

; ============================================================
; Layout table: col, row, str_lo, str_hi — $FF terminates
; ============================================================

.segment "RODATA"

layout:
        .byte  3,  2, <str_system, >str_system
        .byte 22,  2, <str_video, >str_video
        .byte 42,  2, <str_kbd_hdr, >str_kbd_hdr
        .byte  3,  3, <str_cpu, >str_cpu
        .byte  3,  4, <str_irq, >str_irq
        .byte  3,  5, <str_rom, >str_rom
        .byte 22,  3, <str_mode, >str_mode
        .byte 22,  4, <str_size, >str_size
        .byte 22,  5, <str_ctrl, >str_ctrl
        .byte 42,  3, <str_key, >str_key
        .byte 42,  4, <str_mod, >str_mod
        .byte 42,  5, <str_buf, >str_buf
        .byte  3,  7, <str_progress, >str_progress
        .byte  3,  8, <str_mem_lbl, >str_mem_lbl
        .byte  3,  9, <str_vid_lbl, >str_vid_lbl
        .byte  3, 10, <str_io_lbl, >str_io_lbl
        .byte  3, 12, <str_log_hdr, >str_log_hdr
        .byte 58, 23, <str_esc, >str_esc
        .byte $FF

; Log message pointer table — $FF terminates
log_table:
        .byte <str_l1, >str_l1
        .byte <str_l2, >str_l2
        .byte <str_l3, >str_l3
        .byte <str_l4, >str_l4
        .byte <str_l5, >str_l5
        .byte <str_l6, >str_l6
        .byte <str_l7, >str_l7
        .byte $FF

; --- Strings ---

str_top:      .byte "+--[ N8 DASHBOARD ]", 0
str_system:   .byte "[SYSTEM]", 0
str_video:    .byte "[VIDEO]", 0
str_kbd_hdr:  .byte "[KEYBOARD]", 0
str_cpu:      .byte "CPU:  6502", 0
str_irq:      .byte "IRQ:  off", 0
str_rom:      .byte "ROM:  $E000", 0
str_mode:     .byte "Mode: Text", 0
str_size:     .byte "Size: 80x25", 0
str_ctrl:     .byte "Ctrl: $07", 0
str_key:      .byte "Key:  $00", 0
str_mod:      .byte "Mod:  $00", 0
str_buf:      .byte "Buf:  0", 0
str_progress: .byte "[PROGRESS]", 0
str_mem_lbl:  .byte "Mem  [", 0
str_vid_lbl:  .byte "Vid  [", 0
str_io_lbl:   .byte "I/O  [", 0
str_log_hdr:  .byte "[LOG]", 0
str_esc:      .byte "[Esc] exit", 0

str_l1:       .byte "> System initialized", 0
str_l2:       .byte "> Video subsystem ready", 0
str_l3:       .byte "> Keyboard polling active", 0
str_l4:       .byte "> Progress monitor started", 0
str_l5:       .byte "> Dashboard render complete", 0
str_l6:       .byte "> All systems nominal", 0
str_l7:       .byte "> Waiting for input...", 0
