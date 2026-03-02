; -------------------------------------------------------
; console.s -- Console kernel routines (keyboard + video)
; -------------------------------------------------------

.export   _con_getkey, _con_setmode, _con_getstatus
.export   _con_putchar, _con_newline, _con_clear
.export   _con_scroll, _con_movcursor, _con_setcursor

.include  "n8_memory_map.inc"

.segment "CODE"

; -----------------------------------------------------------------
; con_getkey -- Non-blocking key read
;   Out: A = keycode ($00 if none), X = modifier bits
; -----------------------------------------------------------------
_con_getkey:
        LDA N8_KBD_STATUS
        AND #N8_KBD_STAT_AVAIL
        BNE @have_key
        TAX                     ; X = $00
        RTS
@have_key:
        LDA N8_KBD_DATA         ; A = keycode
        PHA
        LDA N8_KBD_STATUS
        AND #$3C                ; SHIFT|CTRL|ALT|CAPS only
        TAX                     ; X = modifier bits
        LDA #$01
        STA N8_KBD_ACK          ; pop key from buffer
        PLA                     ; A = keycode
        RTS

; -----------------------------------------------------------------
; con_setmode -- Set video mode and control flags
;   In: A = VID_MODE, X = VID_CTRL
; -----------------------------------------------------------------
_con_setmode:
        STA N8_VID_MODE
        STX N8_VID_CTRL
        RTS

; -----------------------------------------------------------------
; con_getstatus -- Read video status and cursor position
;   Out: A = VID_STATUS, X = CURCOL, Y = CURROW
; -----------------------------------------------------------------
_con_getstatus:
        LDA N8_VID_STATUS
        LDX N8_VID_CURCOL
        LDY N8_VID_CURROW
        RTS

; -----------------------------------------------------------------
; con_putchar -- Write character at cursor via VID_DATA
;   In: A = character
; -----------------------------------------------------------------
_con_putchar:
        STA N8_VID_DATA
        RTS

; -----------------------------------------------------------------
; con_newline -- Move to col 0, advance row or scroll at bottom
; -----------------------------------------------------------------
_con_newline:
        LDA #0
        STA N8_VID_CURCOL
        LDA N8_VID_CURROW
        CLC
        ADC #1
        CMP N8_VID_HEIGHT       ; compare with hardware height register
        BCC @set_row            ; row+1 < height: just set it
        ; at bottom — scroll up, keep row unchanged
        LDA #N8_VIDOP_SCROLL_UP
        STA N8_VID_OPER
        RTS
@set_row:
        STA N8_VID_CURROW
        RTS

; -----------------------------------------------------------------
; con_clear -- Clear screen (VIDOP_CLEAR)
; -----------------------------------------------------------------
_con_clear:
        LDA #N8_VIDOP_CLEAR
        STA N8_VID_OPER
        RTS

; -----------------------------------------------------------------
; con_scroll -- Scroll screen by signed deltas
;   In: X = horizontal (positive=right, negative=left)
;       Y = vertical   (positive=down,  negative=up)
; -----------------------------------------------------------------
_con_scroll:
        ; Save Y on stack for vertical pass
        TYA
        PHA

        ; --- Horizontal pass (X) ---
        CPX #0
        BEQ @horiz_done
        BPL @scroll_right
        ; Negative X: negate and loop left
        TXA
        EOR #$FF
        CLC
        ADC #1
        TAX
@scroll_left:
        LDA #N8_VIDOP_SCROLL_LEFT
        STA N8_VID_OPER
        DEX
        BNE @scroll_left
        JMP @horiz_done
@scroll_right:
        LDA #N8_VIDOP_SCROLL_RIGHT
        STA N8_VID_OPER
        DEX
        BNE @scroll_right
@horiz_done:

        ; --- Vertical pass (Y from stack) ---
        PLA
        TAY
        CPY #0
        BEQ @done
        BPL @scroll_down
        ; Negative Y: negate and loop up
        TYA
        EOR #$FF
        CLC
        ADC #1
        TAY
@scroll_up:
        LDA #N8_VIDOP_SCROLL_UP
        STA N8_VID_OPER
        DEY
        BNE @scroll_up
        RTS
@scroll_down:
        LDA #N8_VIDOP_SCROLL_DOWN
        STA N8_VID_OPER
        DEY
        BNE @scroll_down
@done:  RTS

; -----------------------------------------------------------------
; con_movcursor -- Move cursor by signed deltas
;   In: X = horizontal (positive=right, negative=left)
;       Y = vertical   (positive=down,  negative=up)
; -----------------------------------------------------------------
_con_movcursor:
        ; Save Y on stack for vertical pass
        TYA
        PHA

        ; --- Horizontal pass (X) ---
        CPX #0
        BEQ @horiz_done
        BPL @move_right
        ; Negative X: negate and loop left
        TXA
        EOR #$FF
        CLC
        ADC #1
        TAX
@move_left:
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER
        DEX
        BNE @move_left
        JMP @horiz_done
@move_right:
        LDA #N8_VIDOP_CURSOR_RIGHT
        STA N8_VID_OPER
        DEX
        BNE @move_right
@horiz_done:

        ; --- Vertical pass (Y from stack) ---
        PLA
        TAY
        CPY #0
        BEQ @done
        BPL @move_down
        ; Negative Y: negate and loop up
        TYA
        EOR #$FF
        CLC
        ADC #1
        TAY
@move_up:
        LDA #N8_VIDOP_CURSOR_UP
        STA N8_VID_OPER
        DEY
        BNE @move_up
        RTS
@move_down:
        LDA #N8_VIDOP_CURSOR_DOWN
        STA N8_VID_OPER
        DEY
        BNE @move_down
@done:  RTS

; -----------------------------------------------------------------
; con_setcursor -- Set cursor position directly
;   In: X = column, Y = row
; -----------------------------------------------------------------
_con_setcursor:
        STX N8_VID_CURCOL
        STY N8_VID_CURROW
        RTS
