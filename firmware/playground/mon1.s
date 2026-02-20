; mon1.s - Line parser
;
; Reads a line, splits into command + remainder, echoes the parse.
; getc/putc/puts - TTY character I/O
; readline - 80-byte buffer, echo as typed, backspace erases, CR terminates
; parse - find first space to split command/args
; Output: "CMD: <word1>  REM: <rest>"
; Prompt: "> "
; Loop forever

.export   _main
.export   getc, putc, puts, newline
.export   readline, parse
.export   main_loop

; --- TTY device registers ---
TTY_OUT_CTRL = $C100
TTY_OUT_DATA = $C101
TTY_IN_CTRL  = $C102
TTY_IN_DATA  = $C103

; --- Zero page ---
zp_ptr    = $E0             ; +$E1 string pointer for puts
zp_ptr2   = $E2             ; +$E3 args pointer (set by parse)
line_len  = $F3             ; current line length

; --- RAM buffers ---
LINE_BUF  = $0300           ; 80-byte input buffer
CMD_BUF   = $0350           ; 16-byte command word

; --- Constants ---
CR        = $0D
LF        = $0A
BS        = $08
DEL       = $7F
SPACE     = $20
BUF_SIZE  = 79              ; max chars (leave room for null)

; =====================================================================
.segment  "CODE"

_main:
        ; Print banner
        LDA #<str_banner
        LDX #>str_banner
        JSR puts

main_loop:
        ; Print prompt
        LDA #<str_prompt
        LDX #>str_prompt
        JSR puts

        ; Read a line
        JSR readline

        ; Empty line -> re-prompt
        LDA line_len
        BEQ main_loop

        ; Parse command
        JSR parse

        ; Print "CMD: "
        LDA #<str_cmd
        LDX #>str_cmd
        JSR puts

        ; Print command word
        LDA #<CMD_BUF
        LDX #>CMD_BUF
        JSR puts

        ; Print "  REM: "
        LDA #<str_rem
        LDX #>str_rem
        JSR puts

        ; Print remainder (args)
        LDA zp_ptr2
        LDX zp_ptr2+1
        JSR puts

        JSR newline

        JMP main_loop

; =====================================================================
; TTY I/O routines
; =====================================================================

; putc - send character in A to TTY
putc:
        PHA
@wait:  LDA TTY_OUT_CTRL
        AND #$01
        BNE @wait
        PLA
        STA TTY_OUT_DATA
        RTS

; getc - read character from TTY into A (blocking poll)
getc:
        LDA TTY_IN_CTRL
        AND #$01
        BEQ getc
        LDA TTY_IN_DATA
        RTS

; puts - print null-terminated string; A=lo, X=hi
puts:
        STA zp_ptr
        STX zp_ptr+1
        LDY #$00
        LDA (zp_ptr),Y
        BEQ @done
@loop:  JSR putc
        INY
        LDA (zp_ptr),Y
        BNE @loop
@done:  RTS

; newline - print CR+LF
newline:
        LDA #CR
        JSR putc
        LDA #LF
        JMP putc

; =====================================================================
; readline - read line into LINE_BUF with echo and backspace
; Sets line_len. Null-terminates.
; =====================================================================
readline:
        LDA #$00
        STA line_len
@loop:
        JSR getc

        ; CR or LF -> done
        CMP #CR
        BEQ @done
        CMP #LF
        BEQ @done

        ; Backspace or DEL -> erase
        CMP #BS
        BEQ @bs
        CMP #DEL
        BEQ @bs

        ; Buffer full -> ignore
        LDX line_len
        CPX #BUF_SIZE
        BCS @loop

        ; Store and echo
        STA LINE_BUF,X
        INC line_len
        JSR putc
        JMP @loop

@bs:
        LDX line_len
        BEQ @loop               ; nothing to erase
        DEC line_len
        ; Visual erase: BS SPACE BS
        LDA #BS
        JSR putc
        LDA #SPACE
        JSR putc
        LDA #BS
        JSR putc
        JMP @loop

@done:
        ; Null-terminate
        LDX line_len
        LDA #$00
        STA LINE_BUF,X
        ; Echo newline
        JSR newline
        RTS

; =====================================================================
; parse - split LINE_BUF into CMD_BUF + remainder
; Sets zp_ptr2 to point at args (or empty string if none).
; =====================================================================
parse:
        LDX #$00               ; index into LINE_BUF
        LDY #$00               ; index into CMD_BUF
        ; Copy command word (up to space or end)
@cmd:   LDA LINE_BUF,X
        BEQ @end_cmd
        CMP #SPACE
        BEQ @end_cmd
        CPY #15                 ; CMD_BUF max 15 chars + null
        BCS @skip_cmd
        STA CMD_BUF,Y
        INY
@skip_cmd:
        INX
        JMP @cmd

@end_cmd:
        ; Null-terminate command
        LDA #$00
        STA CMD_BUF,Y

        ; If at end of string, no args
        LDA LINE_BUF,X
        BEQ @no_args

        ; Skip spaces before args
@skip_sp:
        LDA LINE_BUF,X
        CMP #SPACE
        BNE @has_args
        INX
        JMP @skip_sp

@has_args:
        ; Point zp_ptr2 to remainder in LINE_BUF
        CLC
        TXA
        ADC #<LINE_BUF
        STA zp_ptr2
        LDA #>LINE_BUF
        ADC #$00
        STA zp_ptr2+1
        RTS

@no_args:
        LDA #<str_empty
        STA zp_ptr2
        LDA #>str_empty
        STA zp_ptr2+1
        RTS

; =====================================================================
; String data
; =====================================================================

str_banner:
        .byte "N8 Mon1 - Line Parser", CR, LF, 0
str_prompt:
        .byte "> ", 0
str_cmd:
        .byte "CMD: ", 0
str_rem:
        .byte "  REM: ", 0
str_empty:
        .byte 0
