; -------------------------------------------------------
; shell.s -- Interactive shell with command parsing
; -------------------------------------------------------
; Collects input into LINE_BUF ($0200), parses the first
; word as a command (case-insensitive), and dispatches it
; via a table of known commands.
;
; ZP usage ($E0-$EA):
;   $E0-$E1  ZP_PTR    general string pointer (print_str, etc.)
;   $E2-$E3  ZP_PTR2   second string pointer (str_equal)
;   $E4-$E5  ZP_CMD    pointer to command word (preserved)
;   $E6-$E7  ZP_ARG    pointer to args after command
;   $E8      ZP_LEN    line buffer length
;   $E9      ZP_POS    cursor position within line
;   $EA      ZP_TMP    temp byte

.include "kentry.inc"
.include "n8_memory_map.inc"

; --- Zero page ---
ZP_PTR   = $E0
ZP_PTR2  = $E2
ZP_CMD   = $E4
ZP_ARG   = $E6
ZP_LEN   = $E8
ZP_POS   = $E9
ZP_TMP   = $EA

; --- Line buffer in unallocated RAM ---
LINE_BUF = $0200
LINE_MAX = 79

.segment "RODATA"

banner:     .byte "N8 Shell v0.1.0",0
prompt:     .byte "N8>",0
err_prefix: .byte "unknown command: ",0

; Command table: (name_ptr, handler_ptr) pairs, null-terminated
cmd_table:
        .word cmd_ls_name, cmd_ls
        .word cmd_cd_name, cmd_cd
        .word $0000

cmd_ls_name: .byte "ls",0
cmd_cd_name: .byte "cd",0

msg_ls:  .byte "ls: no filesystem",0
msg_cd:  .byte "cd: no filesystem",0

.segment "CODE"

.export _shell

; =================================================================
; Shell entry point
; =================================================================
_shell:
        JSR K_CON_CLEAR
        LDA #(N8_VID_CURSOR_FLASH | N8_VID_CURSOR_UNDERLINE | $20)
        STA N8_VID_CURSOR

        LDA #<banner
        LDX #>banner
        JSR print_str
        JSR K_CON_NEWLINE

; =================================================================
; Main prompt loop
; =================================================================
prompt_loop:
        LDA #<prompt
        LDX #>prompt
        JSR print_str

        LDA #0
        STA ZP_LEN
        STA ZP_POS

; -----------------------------------------------------------------
; Read a line of input into LINE_BUF
; -----------------------------------------------------------------
read_line:
        JSR K_CON_GETKEY
        CMP #$00
        BEQ read_line

        ; --- Enter ---
        CMP #N8_KEY_ENTER
        BNE @not_enter
        JSR K_CON_NEWLINE
        JMP process_line
@not_enter:

        ; --- Left arrow ---
        CMP #N8_KEY_LEFT
        BNE @not_left
        LDA ZP_POS
        BEQ read_line           ; already at start
        DEC ZP_POS
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER
        JMP read_line
@not_left:

        ; --- Right arrow ---
        CMP #N8_KEY_RIGHT
        BNE @not_right
        LDA ZP_POS
        CMP ZP_LEN
        BCS read_line           ; at or past end
        INC ZP_POS
        LDA #N8_VIDOP_CURSOR_RIGHT
        STA N8_VID_OPER
        JMP read_line
@not_right:

        ; --- Backspace ---
        CMP #N8_KEY_BACKSPACE
        BNE @not_bs
        LDA ZP_POS
        BEQ read_line           ; nothing to the left

        ; Shift LINE_BUF[POS..LEN-1] left by one
        LDX ZP_POS
@bs_shift:
        LDA LINE_BUF,X
        STA LINE_BUF-1,X
        INX
        CPX ZP_LEN
        BCC @bs_shift

        DEC ZP_LEN
        DEC ZP_POS

        ; Move video cursor left
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER

        ; Redraw from cursor to end, clear trailing char
        JSR redraw_tail
        JMP read_line
@not_bs:

        ; --- Delete ---
        CMP #N8_KEY_DELETE
        BNE @not_del
        LDA ZP_POS
        CMP ZP_LEN
        BCS @to_read_line       ; at end, nothing to delete

        ; Shift LINE_BUF[POS+1..LEN-1] left by one
        LDX ZP_POS
        INX
@del_shift:
        LDA LINE_BUF,X
        STA LINE_BUF-1,X
        INX
        CPX ZP_LEN
        BCC @del_shift

        DEC ZP_LEN
        JSR redraw_tail
@to_read_line:
        JMP read_line
@not_del:

        ; --- Ignore non-printable ---
        CMP #$20
        BCC @to_read_line2
        CMP #$80
        BCS @to_read_line2

        ; --- Buffer full? ---
        LDX ZP_LEN
        CPX #LINE_MAX
        BCC @not_full
@to_read_line2:
        JMP read_line
@not_full:

        ; Save the character
        STA ZP_TMP

        ; Shift LINE_BUF[POS..LEN-1] right by one to make room
        LDX ZP_LEN
@ins_shift:
        CPX ZP_POS
        BEQ @ins_done
        LDA LINE_BUF-1,X
        STA LINE_BUF,X
        DEX
        JMP @ins_shift
@ins_done:
        ; Insert character at POS
        LDA ZP_TMP
        LDX ZP_POS
        STA LINE_BUF,X

        INC ZP_LEN
        INC ZP_POS

        ; If inserting at end, just echo the char
        LDA ZP_POS
        CMP ZP_LEN
        BNE @mid_insert
        LDA ZP_TMP
        JSR K_CON_PUTCHAR
        JMP read_line

@mid_insert:
        ; Print inserted char, then redraw rest of line
        LDA ZP_TMP
        JSR K_CON_PUTCHAR
        JSR redraw_tail
        JMP read_line

; -----------------------------------------------------------------
; redraw_tail -- Redraw LINE_BUF[POS..LEN-1] and clear old last
;   char, then move cursor back to POS.
;   Clobbers: A, X, Y
; -----------------------------------------------------------------
redraw_tail:
        ; Print chars from POS to LEN-1
        LDX ZP_POS
@rt_print:
        CPX ZP_LEN
        BCS @rt_clear
        LDA LINE_BUF,X
        JSR K_CON_PUTCHAR
        INX
        JMP @rt_print
@rt_clear:
        ; Print a space to erase the old trailing character
        LDA #' '
        JSR K_CON_PUTCHAR

        ; Move cursor back: we need to go back (LEN - POS + 1) positions
        ; (the chars we printed + the space)
        LDA ZP_LEN
        SEC
        SBC ZP_POS
        CLC
        ADC #1                  ; +1 for the trailing space
        TAX
@rt_back:
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER
        DEX
        BNE @rt_back
        RTS

; =================================================================
; Process the line: parse command and dispatch
; =================================================================
process_line:
        ; Null-terminate
        LDX ZP_LEN
        LDA #0
        STA LINE_BUF,X

        ; Skip leading spaces
        LDX #0
@skip_lead:
        LDA LINE_BUF,X
        CMP #' '
        BNE @got_start
        INX
        BNE @skip_lead
@got_start:
        ; Empty line?
        CMP #0
        BNE @not_empty
        JMP prompt_loop
@not_empty:

        ; Save command start index
        STX ZP_TMP

        ; Find end of command word
@find_end:
        LDA LINE_BUF,X
        BEQ @word_end
        CMP #' '
        BEQ @word_end
        INX
        JMP @find_end
@word_end:

        ; Null-terminate command word if stopped on space
        LDA LINE_BUF,X
        BEQ @no_split
        LDA #0
        STA LINE_BUF,X
        INX
@no_split:

        ; Skip spaces to find args
@skip_arg_sp:
        LDA LINE_BUF,X
        CMP #' '
        BNE @args_set
        INX
        JMP @skip_arg_sp
@args_set:
        ; ZP_ARG = &LINE_BUF[X]
        TXA
        CLC
        ADC #<LINE_BUF
        STA ZP_ARG
        LDA #>LINE_BUF
        ADC #0
        STA ZP_ARG+1

        ; ZP_CMD = &LINE_BUF[ZP_TMP] (command word)
        LDA ZP_TMP
        CLC
        ADC #<LINE_BUF
        STA ZP_CMD
        LDA #>LINE_BUF
        ADC #0
        STA ZP_CMD+1

        ; Lowercase the command word in-place
        LDY #0
@to_lower:
        LDA (ZP_CMD),Y
        BEQ @lower_done
        CMP #'A'
        BCC @lnext
        CMP #'Z'+1
        BCS @lnext
        ORA #$20
        STA (ZP_CMD),Y
@lnext: INY
        BNE @to_lower
@lower_done:

        ; --- Walk command table ---
        LDX #0                  ; byte offset into cmd_table
@cmd_loop:
        ; Check for end sentinel (high byte of name ptr == 0)
        LDA cmd_table+1,X
        BEQ @no_match

        ; ZP_PTR = table entry name pointer
        LDA cmd_table,X
        STA ZP_PTR
        LDA cmd_table+1,X
        STA ZP_PTR+1

        ; ZP_PTR2 = ZP_CMD (command word)
        LDA ZP_CMD
        STA ZP_PTR2
        LDA ZP_CMD+1
        STA ZP_PTR2+1

        JSR str_equal
        BEQ @found_cmd

        ; Next entry (4 bytes)
        INX
        INX
        INX
        INX
        JMP @cmd_loop

@found_cmd:
        ; Load handler address into ZP_PTR, indirect jump
        LDA cmd_table+2,X
        STA ZP_PTR
        LDA cmd_table+3,X
        STA ZP_PTR+1
        JMP (ZP_PTR)

@no_match:
        LDA #<err_prefix
        LDX #>err_prefix
        JSR print_str
        LDA ZP_CMD
        LDX ZP_CMD+1
        JSR print_str
        JSR K_CON_NEWLINE
        JMP prompt_loop

; =================================================================
; Command handlers (stubs)
; =================================================================
cmd_ls:
        LDA #<msg_ls
        LDX #>msg_ls
        JSR print_str
        JSR K_CON_NEWLINE
        JMP prompt_loop

cmd_cd:
        LDA #<msg_cd
        LDX #>msg_cd
        JSR print_str
        JSR K_CON_NEWLINE
        JMP prompt_loop

; =================================================================
; print_str -- Print null-terminated string
;   In: A/X = low/high of string pointer
;   Clobbers: ZP_PTR, Y, A
; =================================================================
print_str:
        STA ZP_PTR
        STX ZP_PTR+1
        LDY #0
@loop:  LDA (ZP_PTR),Y
        BEQ @done
        JSR K_CON_PUTCHAR
        INY
        BNE @loop
@done:  RTS

; =================================================================
; str_equal -- Compare two null-terminated strings
;   In:  ZP_PTR = string 1, ZP_PTR2 = string 2
;   Out: Z=1 if equal, Z=0 if not
;   Clobbers: Y, A
; =================================================================
str_equal:
        LDY #0
@loop:  LDA (ZP_PTR),Y
        CMP (ZP_PTR2),Y
        BNE @ne
        CMP #0
        BEQ @eq
        INY
        BNE @loop
@ne:    LDA #1                  ; clear Z
        RTS
@eq:    LDA #0                  ; set Z
        RTS
