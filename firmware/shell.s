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
ZP_CHAN  = $EB
ZP_ARG2 = $EC              ; second arg pointer (mv), 2 bytes
ZP_DCNT = $EE              ; disk byte counter
ZP_DFLG = $EF              ; disk flags
ZP_CHAN2 = $F0             ; second disk channel (cp)

; --- Line buffer in unallocated RAM ---
LINE_BUF  = $0200
LINE_MAX  = 79
DISK_BUF  = $0300          ; 256-byte disk I/O buffer

.segment "RODATA"

banner:     .byte "N8 Shell v0.1.0",0
prompt:     .byte "N8>",0
err_prefix: .byte "unknown command: ",0

; Command table: (name_ptr, handler_ptr) pairs, null-terminated
cmd_table:
        .word cmd_ls_name, cmd_ls
        .word cmd_cd_name, cmd_cd
        .word cmd_pwd_name, cmd_pwd
        .word cmd_cat_name, cmd_cat
        .word cmd_mkdir_name, cmd_mkdir
        .word cmd_rmdir_name, cmd_rmdir
        .word cmd_rm_name, cmd_rm
        .word cmd_mv_name, cmd_mv
        .word cmd_cp_name, cmd_cp
        .word $0000

cmd_ls_name:    .byte "ls",0
cmd_cd_name:    .byte "cd",0
cmd_pwd_name:   .byte "pwd",0
cmd_cat_name:   .byte "cat",0
cmd_mkdir_name: .byte "mkdir",0
cmd_rmdir_name: .byte "rmdir",0
cmd_rm_name:    .byte "rm",0
cmd_mv_name:    .byte "mv",0
cmd_cp_name:    .byte "cp",0

str_ls_bare:     .byte "LS",0
msg_pwd_err:     .byte "pwd: error",0
msg_missing_arg: .byte "missing argument",0
msg_mv_usage:    .byte "mv: usage: mv <src> <dst>",0
msg_cp_usage:    .byte "cp: usage: cp <src> <dst>",0
msg_generic_err: .byte "error",0

; Command error string table ($01-$0E)
err_tbl:
        .word err_01, err_02, err_03, err_04, err_05, err_06, err_07
        .word err_08, err_09, err_0a, err_0b, err_0c, err_0d, err_0e

err_01: .byte "not found",0
err_02: .byte "already exists",0
err_03: .byte "dir not found",0
err_04: .byte "not open",0
err_05: .byte "no free channel",0
err_06: .byte "disk full",0
err_07: .byte "is a directory",0
err_08: .byte "permission denied",0
err_09: .byte "bad syntax",0
err_0a: .byte "bad argument",0
err_0b: .byte "name too long",0
err_0c: .byte "not a directory",0
err_0d: .byte "not empty",0
err_0e: .byte "not ready",0

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
; =================================================================
; cmd_ls -- List directory (buffer + parse approach)
;
; Pattern: disk_read_to_buf reads entire response into DISK_BUF,
;   then a state machine walks the buffer to reformat entries.
;   Needed when raw device format differs from display format.
;
; Raw LS format per entry: D|F \t <name> \t <size_lo> <size_hi> \n
; Compact display: <name>/ (for dirs) or <name> (for files), one per line
; =================================================================
cmd_ls:
        ; Build "LS" or "LS,<arg>" depending on whether arg exists
        LDY #0
        LDA (ZP_ARG),Y
        BEQ @ls_noarg

        ; Has arg: build "LS,<arg>"
        LDA #'L'
        LDX #'S'
        JSR build_cmd
        JMP @ls_send

@ls_noarg:
        ; No arg: just send "LS"
        LDA #<str_ls_bare
        STA ZP_PTR
        LDA #>str_ls_bare
        STA ZP_PTR+1

@ls_send:
        JSR disk_send_cmd
        JSR disk_check_error
        BCS @ls_ret

        ; Read channel ID
        LDA N8_DISK_DATA
        STA ZP_CHAN
        STA N8_DISK_CHAN

        ; Read response into buffer
        JSR disk_read_to_buf

        ; Parse buffer: walk entries
        ; Format: type(1) \t name... \t size_lo size_hi \n
        LDX #0                  ; buffer index
@entry:
        CPX ZP_DCNT
        BCS @ls_done            ; past end of data

        ; Read type byte (D or F)
        LDA DISK_BUF,X
        CMP #'D'
        BEQ @is_dir
        LDA #0
        STA ZP_DFLG             ; not a directory
        JMP @skip_tab1
@is_dir:
        LDA #1
        STA ZP_DFLG             ; is a directory

@skip_tab1:
        INX                     ; skip type byte
        CPX ZP_DCNT
        BCS @ls_done
        INX                     ; skip \t
        CPX ZP_DCNT
        BCS @ls_done

        ; Print name bytes until \t
@name:
        CPX ZP_DCNT
        BCS @ls_done
        LDA DISK_BUF,X
        CMP #$09                ; tab = end of name
        BEQ @name_done
        JSR K_CON_PUTCHAR
        INX
        JMP @name

@name_done:
        ; If directory, print '/'
        LDA ZP_DFLG
        BEQ @skip_slash
        LDA #'/'
        JSR K_CON_PUTCHAR
@skip_slash:

        ; Skip \t + size_lo + size_hi + \n (4 bytes)
        INX                     ; \t
        INX                     ; size_lo
        INX                     ; size_hi
        INX                     ; \n

        JSR K_CON_NEWLINE
        JMP @entry

@ls_done:
@ls_ret:
        JMP prompt_loop

; =================================================================
; cmd_cd -- Change directory (buffer-based command construction)
;
; Pattern: build_cmd constructs "XX,<arg>" in DISK_BUF, then
;   disk_send_cmd sends it. More flexible than inline sends —
;   handles variable-length arguments naturally.
; Pattern: disk_check_error reads DISK_ERROR, looks up error
;   string from table, prints "<cmd>: <message>". Returns C=1
;   on error so caller can BCS to prompt_loop.
; =================================================================
cmd_cd:
        JSR check_arg
        BCS @cd_ret
        LDA #'C'
        LDX #'D'
        JSR build_cmd
        JSR disk_send_cmd
        JSR disk_check_error
@cd_ret:
        JMP prompt_loop

; =================================================================
; cmd_mkdir -- Create directory
; =================================================================
cmd_mkdir:
        JSR check_arg
        BCS @mk_ret
        LDA #'M'
        LDX #'D'
        JSR build_cmd
        JSR disk_send_cmd
        JSR disk_check_error
@mk_ret:
        JMP prompt_loop

; =================================================================
; cmd_rmdir -- Remove empty directory
; =================================================================
cmd_rmdir:
        JSR check_arg
        BCS @rd_ret
        LDA #'R'
        LDX #'D'
        JSR build_cmd
        JSR disk_send_cmd
        JSR disk_check_error
@rd_ret:
        JMP prompt_loop

; =================================================================
; cmd_rm -- Remove file
; =================================================================
cmd_rm:
        JSR check_arg
        BCS @rm_ret
        LDA #'R'
        LDX #'M'
        JSR build_cmd
        JSR disk_send_cmd
        JSR disk_check_error
@rm_ret:
        JMP prompt_loop

; =================================================================
; cmd_mv -- Move/rename (uses split_args + build_two_arg_cmd)
; =================================================================
cmd_mv:
        JSR split_args
        BCS @mv_usage
        LDA #'M'
        LDX #'V'
        JSR build_two_arg_cmd
        JSR disk_send_cmd
        JSR disk_check_error
        JMP prompt_loop
@mv_usage:
        LDA #<msg_mv_usage
        LDX #>msg_mv_usage
        JSR print_str
        JSR K_CON_NEWLINE
        JMP prompt_loop

; =================================================================
; cmd_cp -- Copy file (open src read, open dst write, copy, close)
;
; Pattern: two file channels open simultaneously. ZP_CHAN = src
;   (read), ZP_CHAN2 = dst (write). Stream bytes from src data
;   channel to dst data channel. Close both when done.
; =================================================================
cmd_cp:
        JSR split_args
        BCC @cp_args_ok
        JMP @cp_usage
@cp_args_ok:

        ; Open src for read: "OP,R,<src>"
        LDX #0
        LDA #'O'
        STA DISK_BUF,X
        INX
        LDA #'P'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        LDA #'R'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        LDY #0
@cps:   LDA (ZP_ARG),Y
        STA DISK_BUF,X
        BEQ @src_built
        INX
        INY
        BNE @cps
@src_built:
        LDA #<DISK_BUF
        STA ZP_PTR
        LDA #>DISK_BUF
        STA ZP_PTR+1
        JSR disk_send_cmd
        JSR disk_check_error
        BCC @cp_src_ok
        JMP @cp_ret
@cp_src_ok:
        ; Save src channel
        LDA N8_DISK_DATA
        STA ZP_CHAN

        ; Open dst for write: "OP,W,<dst>"
        LDX #0
        LDA #'O'
        STA DISK_BUF,X
        INX
        LDA #'P'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        LDA #'W'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        LDY #0
@cpd:   LDA (ZP_ARG2),Y
        STA DISK_BUF,X
        BEQ @dst_built
        INX
        INY
        BNE @cpd
@dst_built:
        LDA #<DISK_BUF
        STA ZP_PTR
        LDA #>DISK_BUF
        STA ZP_PTR+1
        JSR disk_send_cmd
        JSR disk_check_error
        BCS @cp_close_src       ; close src if dst open failed

        ; Save dst channel
        LDA N8_DISK_DATA
        STA ZP_CHAN2

        ; Copy loop: read from src, write to dst
        LDA ZP_CHAN
        STA N8_DISK_CHAN         ; select src channel
@cpoll:
        LDA N8_DISK_STATUS
        TAX
        AND #N8_DISK_STAT_AVAIL
        BNE @cread
        TXA
        AND #N8_DISK_STAT_EOF
        BNE @cp_done
        JMP @cpoll
@cread:
        TAY                     ; Y = avail count
@cbyte:
        ; Read from src
        LDA ZP_CHAN
        STA N8_DISK_CHAN
        LDA N8_DISK_DATA
        PHA
        ; Write to dst
        LDA ZP_CHAN2
        STA N8_DISK_CHAN
        PLA
        STA N8_DISK_DATA
        ; Switch back to src for next read
        LDA ZP_CHAN
        STA N8_DISK_CHAN
        DEY
        BNE @cbyte
        JMP @cpoll

@cp_done:
        ; Close dst channel
        LDA ZP_CHAN
        PHA                     ; save src channel ID
        LDA ZP_CHAN2
        STA ZP_CHAN
        JSR disk_close_chan
        PLA
        STA ZP_CHAN             ; restore src channel ID
@cp_close_src:
        ; Close src channel
        JSR disk_close_chan
        JMP prompt_loop

@cp_usage:
        LDA #<msg_cp_usage
        LDX #>msg_cp_usage
        JSR print_str
        JSR K_CON_NEWLINE
@cp_ret:
        JMP prompt_loop

; =================================================================
; cmd_cat -- Display file contents (open, stream, close)
;
; Pattern: file channel lifecycle — OPEN returns a channel ID,
;   stream data to console, then CLOSE via binary CL command.
;   Unlike response channels (pwd, ls), file channels do NOT
;   auto-close at EOF — explicit close is required.
; =================================================================
cmd_cat:
        JSR check_arg
        BCS @cat_ret
        ; Build "OP,R,<filename>" in DISK_BUF
        ; Can't use build_cmd (2-char opcode), so build inline
        LDX #0
        LDA #'O'
        STA DISK_BUF,X
        INX
        LDA #'P'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        LDA #'R'
        STA DISK_BUF,X
        INX
        LDA #','
        STA DISK_BUF,X
        INX
        ; Copy filename from ZP_ARG
        LDY #0
@cpy:   LDA (ZP_ARG),Y
        STA DISK_BUF,X
        BEQ @cpy_done
        INX
        INY
        BNE @cpy
@cpy_done:
        LDA #<DISK_BUF
        STA ZP_PTR
        LDA #>DISK_BUF
        STA ZP_PTR+1

        JSR disk_send_cmd
        JSR disk_check_error
        BCS @cat_ret

        ; Read channel ID, select data channel
        LDA N8_DISK_DATA
        STA ZP_CHAN
        STA N8_DISK_CHAN

        ; Stream file contents to console
        JSR disk_stream_to_con

        ; Close file channel (required — not auto-close)
        JSR disk_close_chan

@cat_ret:
        JMP prompt_loop

; =================================================================
; cmd_pwd -- Print working directory (inline send, stream response)
;
; Pattern: inline sends — write command bytes directly to DISK_DATA.
;   Tightest code for fixed (no-argument) commands.
; Pattern: stream-to-console — read DISK_DATA → K_CON_PUTCHAR in
;   a poll loop driven by DISK_STATUS. Best for commands where
;   response goes straight to screen with no reformatting.
; =================================================================
cmd_pwd:
        ; Select control channel (clears any prior error)
        LDA #N8_DISK_CONTROL_CHAN
        STA N8_DISK_CHAN

        ; Send "PD" + null terminator inline
        LDA #'P'
        STA N8_DISK_DATA
        LDA #'D'
        STA N8_DISK_DATA
        LDA #$00
        STA N8_DISK_DATA        ; null = execute command

        ; Check for error
        LDA N8_DISK_ERROR
        AND #N8_DISK_ERR_CMD
        BNE @pwd_err

        ; Read channel ID from response
        LDA N8_DISK_DATA
        STA ZP_CHAN
        STA N8_DISK_CHAN         ; select response channel

        ; Stream response to console
        JSR disk_stream_to_con
        JSR K_CON_NEWLINE
        JMP prompt_loop

@pwd_err:
        ; For now, generic error (Phase 2 adds proper error table)
        LDA #<msg_pwd_err
        LDX #>msg_pwd_err
        JSR print_str
        JSR K_CON_NEWLINE
        JMP prompt_loop

; =================================================================
; disk_stream_to_con -- Stream data channel to console
;
;   Prereq: data channel already selected via DISK_CHAN
;   Reads DISK_DATA bytes, sends each to K_CON_PUTCHAR.
;   Converts $0A (LF) to K_CON_NEWLINE.
;   Polls DISK_STATUS for avail/EOF/busy.
;   Returns when EOF reached and buffer drained.
;   Clobbers: A, X, Y
; =================================================================
disk_stream_to_con:
@poll:
        LDA N8_DISK_STATUS
        TAX                     ; save full status
        AND #N8_DISK_STAT_AVAIL
        BNE @read               ; bytes available
        TXA
        AND #N8_DISK_STAT_EOF
        BNE @done               ; EOF, no more data
        JMP @poll               ; busy — wait

@read:
        TAY                     ; Y = bytes available count
@byte:
        LDA N8_DISK_DATA
        CMP #$0A
        BEQ @newline
        JSR K_CON_PUTCHAR
        JMP @next
@newline:
        JSR K_CON_NEWLINE
@next:
        DEY
        BNE @byte
        JMP @poll               ; check for more

@done:
        RTS

; =================================================================
; check_arg -- Verify ZP_ARG is not empty
;   Out: C=0 if arg present, C=1 if missing (prints message)
;   Prints "<cmd>: missing argument" on missing.
;   Clobbers: A, Y, ZP_PTR
; =================================================================
check_arg:
        LDY #0
        LDA (ZP_ARG),Y
        BNE @has_arg
        ; Print "<cmd>: missing argument"
        LDA ZP_CMD
        STA ZP_PTR
        LDA ZP_CMD+1
        STA ZP_PTR+1
        LDY #0
@pn:    LDA (ZP_PTR),Y
        BEQ @sep
        JSR K_CON_PUTCHAR
        INY
        BNE @pn
@sep:   LDA #':'
        JSR K_CON_PUTCHAR
        LDA #' '
        JSR K_CON_PUTCHAR
        LDA #<msg_missing_arg
        LDX #>msg_missing_arg
        JSR print_str
        JSR K_CON_NEWLINE
        SEC
        RTS
@has_arg:
        CLC
        RTS

; =================================================================
; split_args -- Split ZP_ARG into two args at first space
;   Out: C=0 if both args present (ZP_ARG=first, ZP_ARG2=second)
;        C=1 if missing (prints nothing — caller handles message)
;   Modifies LINE_BUF in place (null-terminates first arg).
;   Clobbers: A, Y
; =================================================================
split_args:
        LDY #0
        LDA (ZP_ARG),Y
        BEQ @fail               ; empty = no first arg
        ; Find space
@find:  LDA (ZP_ARG),Y
        BEQ @fail               ; null before space = no second arg
        CMP #' '
        BEQ @split
        INY
        BNE @find
@split:
        LDA #0
        STA (ZP_ARG),Y          ; null-terminate first arg
        INY
@skip:  LDA (ZP_ARG),Y
        CMP #' '
        BNE @got
        INY
        BNE @skip
@got:   LDA (ZP_ARG),Y
        BEQ @fail               ; empty second arg
        TYA
        CLC
        ADC ZP_ARG
        STA ZP_ARG2
        LDA ZP_ARG+1
        ADC #0
        STA ZP_ARG2+1
        CLC
        RTS
@fail:  SEC
        RTS

; =================================================================
; build_cmd -- Build "XX,<arg>" in DISK_BUF
;   In: A = first command char, X = second command char
;       ZP_ARG = pointer to null-terminated argument
;   Out: ZP_PTR = DISK_BUF (ready for disk_send_cmd)
;   Clobbers: Y, A
; =================================================================
build_cmd:
        STA DISK_BUF+0
        STX DISK_BUF+1
        LDA #','
        STA DISK_BUF+2
        ; Copy arg bytes
        LDY #0
@copy:  LDA (ZP_ARG),Y
        STA DISK_BUF+3,Y
        BEQ @done               ; copied the null terminator
        INY
        BNE @copy
@done:
        LDA #<DISK_BUF
        STA ZP_PTR
        LDA #>DISK_BUF
        STA ZP_PTR+1
        RTS

; =================================================================
; build_two_arg_cmd -- Build "XX,<arg1>,<arg2>" in DISK_BUF
;   In: A = first cmd char, X = second cmd char
;       ZP_ARG = first arg, ZP_ARG2 = second arg
;   Out: ZP_PTR = DISK_BUF
;   Clobbers: X, Y, A
; =================================================================
build_two_arg_cmd:
        STA DISK_BUF+0
        STX DISK_BUF+1
        LDA #','
        STA DISK_BUF+2
        LDX #3
        ; Copy first arg
        LDY #0
@cp1:   LDA (ZP_ARG),Y
        BEQ @sep
        STA DISK_BUF,X
        INX
        INY
        BNE @cp1
@sep:   LDA #','
        STA DISK_BUF,X
        INX
        ; Copy second arg
        LDY #0
@cp2:   LDA (ZP_ARG2),Y
        STA DISK_BUF,X
        BEQ @done
        INX
        INY
        BNE @cp2
@done:
        LDA #<DISK_BUF
        STA ZP_PTR
        LDA #>DISK_BUF
        STA ZP_PTR+1
        RTS

; =================================================================
; disk_send_cmd -- Send command at (ZP_PTR) to control channel
;   Selects control channel first (clears prior error).
;   Sends bytes including null terminator (which executes cmd).
;   Clobbers: A, Y
; =================================================================
disk_send_cmd:
        LDA #N8_DISK_CONTROL_CHAN
        STA N8_DISK_CHAN
        LDY #0
@loop:  LDA (ZP_PTR),Y
        STA N8_DISK_DATA
        BEQ @done
        INY
        BNE @loop
@done:  RTS

; =================================================================
; disk_check_error -- Check DISK_ERROR after command execution
;   If bit 7 set: prints "<cmd>: <message>" and sets C=1
;   If no error: clears C
;   Prereq: ZP_CMD points to command name (set by process_line)
;   Clobbers: A, X, Y, ZP_PTR
; =================================================================
disk_check_error:
        LDA N8_DISK_ERROR
        AND #N8_DISK_ERR_CMD
        BEQ @no_err

        ; Read the command error code
        LDA N8_DISK_DATA
        TAX                     ; X = error code

        ; Print command name
        LDA ZP_CMD
        STA ZP_PTR
        LDA ZP_CMD+1
        STA ZP_PTR+1
        LDY #0
@pname: LDA (ZP_PTR),Y
        BEQ @sep
        JSR K_CON_PUTCHAR
        INY
        BNE @pname

@sep:   ; Print ": "
        LDA #':'
        JSR K_CON_PUTCHAR
        LDA #' '
        JSR K_CON_PUTCHAR

        ; Look up error message (code $01-$0E → table index 0-13)
        DEX                     ; code-1 = index
        CPX #14
        BCS @generic            ; out of range
        TXA
        ASL A                   ; *2 for word offset
        TAX
        LDA err_tbl,X
        STA ZP_PTR
        LDA err_tbl+1,X
        STA ZP_PTR+1
        LDY #0
@pmsg:  LDA (ZP_PTR),Y
        BEQ @eol
        JSR K_CON_PUTCHAR
        INY
        BNE @pmsg
@eol:
        JSR K_CON_NEWLINE
        SEC
        RTS

@generic:
        LDA #<msg_generic_err
        LDX #>msg_generic_err
        JSR print_str
        JSR K_CON_NEWLINE
        SEC
        RTS

@no_err:
        CLC
        RTS

; =================================================================
; disk_close_chan -- Close channel in ZP_CHAN via binary CL command
;   Sends CL,<chan_id> with binary payload.
;   Clobbers: A
; =================================================================
disk_close_chan:
        LDA #N8_DISK_CONTROL_CHAN
        STA N8_DISK_CHAN
        LDA #'C'
        STA N8_DISK_DATA
        LDA #'L'
        STA N8_DISK_DATA
        LDA #','
        STA N8_DISK_DATA
        LDA ZP_CHAN
        STA N8_DISK_DATA
        LDA #$00
        STA N8_DISK_DATA
        RTS

; =================================================================
; disk_read_to_buf -- Read data channel into DISK_BUF
;   Prereq: data channel already selected via DISK_CHAN
;   Out: ZP_DCNT = total bytes read (max 256, stored as 0 if 256)
;   Clobbers: A, X, Y
; =================================================================
disk_read_to_buf:
        LDX #0                  ; buffer index
@poll:
        LDA N8_DISK_STATUS
        TAY                     ; save full status
        AND #N8_DISK_STAT_AVAIL
        BNE @read
        TYA
        AND #N8_DISK_STAT_EOF
        BNE @done
        JMP @poll               ; busy — wait

@read:
        TAY                     ; Y = avail count
@byte:
        LDA N8_DISK_DATA
        STA DISK_BUF,X
        INX
        BEQ @done               ; wrapped to 0 = buffer full (256)
        DEY
        BNE @byte
        JMP @poll

@done:
        STX ZP_DCNT
        RTS

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
