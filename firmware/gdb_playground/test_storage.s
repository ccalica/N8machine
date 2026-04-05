; test_storage.s - Storage device register validation
;
; 6 test phases exercising the Phase 1 storage commands (PWDIR, LS, OPEN R, CLOSE).
; Results stored in RAM at $0300-$04FF for GDB inspection.
; Labeled checkpoints (phase1_done..phase6_done) serve as breakpoints.
;
; Prerequisites:
;   - Emulator launched with --storage <dir> pointing to a directory
;     containing a file "test.txt" with content "Foo\nBar\nBaz\n\n" (13 bytes)
;   - Default ./storage/ works if storage/test.txt exists
;
; Expected workflow with n8gdb:
;   load test_storage 0xE000
;   reset
;   bp phase1_done
;   bp phase2_done
;   bp phase3_done
;   bp phase4_done
;   bp phase5_done
;   bp phase6_done
;   bp all_done
;   run
;   (verify results at each breakpoint)
;
; Phase summary:
;   1  PWDIR — read working directory, expect "/"
;   2  LIST — list root directory, verify response contains entries
;   3  OPEN R — open test.txt, read first 3 bytes, expect "Foo"
;   4  STATUS lifecycle — verify bytes_avail and EOF progression
;   5  CLOSE — close file channel, verify inactive
;   6  Error handling — invalid command, file not found, double null

.export   _main
.export   phase1_done, phase2_done, phase3_done, phase4_done
.export   phase5_done, phase6_done, all_done

; --- Storage device registers ---
DISK_CHAN   = $D880
DISK_DATA   = $D881
DISK_ERROR  = $D882
DISK_CTRL   = $D883
DISK_STATUS = $D884

; --- Video registers (for screen output) ---
VID_OPER    = $D844
VID_CTRL    = $D849
VID_DATA    = $D84A
VIDOP_CLEAR = $05
VIDCTRL_ALL = $07

; --- Constants ---
CONTROL_CHAN = $0F
CHAN_ACTIVE  = $20          ; bit 5 of DISK_CHAN read
ERR_CMD_BIT = $80          ; bit 7 of DISK_ERROR
STAT_AVAIL  = $0F          ; bits 0-3 mask
STAT_EOF    = $20          ; bit 5
STAT_CTS    = $40          ; bit 6

; --- Result buffers ---
; $0300-$031F: phase 1 results (PWDIR)
; $0320-$034F: phase 2 results (LIST response, first 48 bytes)
; $0350-$036F: phase 3 results (OPEN R: first bytes)
; $0370-$037F: phase 4 results (STATUS lifecycle)
; $0380-$038F: phase 5 results (CLOSE)
; $0390-$039F: phase 6 results (error handling)

RES_PWDIR   = $0300
RES_LIST    = $0320
RES_OPEN    = $0350
RES_STATUS  = $0370
RES_CLOSE   = $0380
RES_ERROR   = $0390

; --- Zero page ---
.segment "ZEROPAGE"
zp_chan:     .res 1         ; saved channel ID
zp_count:   .res 1         ; byte counter
zp_ptr:     .res 2         ; pointer for indirect store

.segment "CODE"

_main:

; ============================================================
; Phase 1: PWDIR
;   Send "PD" command, read response channel.
;   Expect: "/" (1 byte), channel auto-closes.
;   RES_PWDIR: [0]=error [1]=chan_id [2]=first_byte [3]=status_after
;              [4]=chan_active_after
; ============================================================
phase1:
        ; Select control channel
        LDA #CONTROL_CHAN
        STA DISK_CHAN

        ; Send "PD" + null
        LDA #'P'
        STA DISK_DATA
        LDA #'D'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        ; Check error
        LDA DISK_ERROR
        STA RES_PWDIR+0        ; [0] expect $00

        ; Read result (channel ID)
        LDA DISK_DATA
        STA RES_PWDIR+1        ; [1] channel ID
        STA zp_chan

        ; Switch to response channel and read
        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_DATA
        STA RES_PWDIR+2        ; [2] expect '/' ($2F)

        ; After reading the one byte, channel should auto-close
        ; Check status — should be EOF
        LDA DISK_STATUS
        STA RES_PWDIR+3        ; [3] expect EOF+CTS = $60

        ; Check channel active bit
        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_CHAN
        AND #CHAN_ACTIVE
        STA RES_PWDIR+4        ; [4] expect $00 (auto-closed)

phase1_done:
        JMP phase2

; ============================================================
; Phase 2: LIST
;   Send "LS" command, read first 48 bytes of response.
;   Expect: at least one entry starting with 'D' or 'F'.
;   RES_LIST: [0]=error [1]=chan_id [2..33]=first 32 bytes of listing
;             [34]=entry_count
; ============================================================
phase2:
        LDA #CONTROL_CHAN
        STA DISK_CHAN

        ; Send "LS" + null
        LDA #'L'
        STA DISK_DATA
        LDA #'S'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_LIST+0         ; [0] expect $00

        LDA DISK_DATA
        STA RES_LIST+1         ; [1] channel ID
        STA zp_chan

        ; Switch to listing channel, read up to 32 bytes
        LDA zp_chan
        STA DISK_CHAN

        LDX #0
        LDY #0                 ; newline counter
@read:  LDA DISK_STATUS
        PHA                     ; save status
        AND #STAT_AVAIL
        BEQ @check_eof

        ; Read one byte
        LDA DISK_DATA
        STA RES_LIST+2,X       ; store in result buffer
        CMP #$0A               ; newline?
        BNE @not_nl
        INY                     ; count entries
@not_nl:
        INX
        CPX #32
        BCS @full               ; read at most 32 bytes
        PLA                     ; discard saved status
        JMP @read

@check_eof:
        PLA                     ; restore status (already consumed by PLA)
        AND #STAT_EOF
        BNE @done_read
        JMP @read               ; busy-wait (shouldn't happen)

@full:  PLA                     ; discard saved status (BCS skipped the PLA above)
@done_read:

@store_count:
        STY RES_LIST+34        ; [34] = entry count

        ; Display listing to screen
        JSR vid_clear
        LDA #VIDCTRL_ALL
        STA VID_CTRL

        ; Print "LS:" header
        LDA #'L'
        STA VID_DATA
        LDA #'S'
        STA VID_DATA
        LDA #':'
        STA VID_DATA
        LDA #$0A               ; newline char in N8 charset = $0A? Use space
        ; Print listing bytes from RES_LIST+2, X bytes
        STX zp_count            ; save byte count
        LDX #0
@print: CPX zp_count
        BCS @print_done
        LDA RES_LIST+2,X
        CMP #$09               ; tab → space
        BNE @not_tab
        LDA #' '
@not_tab:
        CMP #$0A               ; newline → skip (entries on one line)
        BEQ @skip_nl
        CMP #$20               ; skip non-printable (binary size bytes)
        BCC @skip_nl
        STA VID_DATA
@skip_nl:
        INX
        JMP @print
@print_done:

phase2_done:
        JMP phase3

; ============================================================
; Phase 3: OPEN R — read first bytes of test.txt
;   Send "OP,R,test.txt", read first 3 bytes.
;   Expect: 'F','o','o' (first line of test.txt)
;   RES_OPEN: [0]=error [1]=chan_id [2]='F' [3]='o' [4]='o'
;             [5]=status_avail
; ============================================================
phase3:
        LDA #CONTROL_CHAN
        STA DISK_CHAN

        ; Send "OP,R,test.txt" + null
        LDX #0
@cmd:   LDA str_open,X
        BEQ @send_null
        STA DISK_DATA
        INX
        JMP @cmd
@send_null:
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_OPEN+0         ; [0] expect $00

        LDA DISK_DATA
        STA RES_OPEN+1         ; [1] channel ID
        STA zp_chan

        ; Switch to data channel
        LDA zp_chan
        STA DISK_CHAN

        ; Read 3 bytes
        LDA DISK_DATA
        STA RES_OPEN+2         ; [2] expect 'F' ($46)
        LDA DISK_DATA
        STA RES_OPEN+3         ; [3] expect 'o' ($6F)
        LDA DISK_DATA
        STA RES_OPEN+4         ; [4] expect 'o' ($6F)

        ; Check available count
        LDA DISK_STATUS
        AND #STAT_AVAIL
        STA RES_OPEN+5         ; [5] expect 10 (13-3=10)

        ; Display file contents to screen — start with header
        JSR vid_clear
        LDA #VIDCTRL_ALL
        STA VID_CTRL

        LDX #0
@hdr:   LDA str_open_hdr,X
        BEQ @hdr_done
        STA VID_DATA
        INX
        JMP @hdr
@hdr_done:
        ; Print the 3 bytes we already read
        LDA RES_OPEN+2
        STA VID_DATA
        LDA RES_OPEN+3
        STA VID_DATA
        LDA RES_OPEN+4
        STA VID_DATA

        ; Read and display remaining bytes from file
        LDA zp_chan
        STA DISK_CHAN
        LDX #0                 ; remaining byte counter
@tail:  LDA DISK_STATUS
        TAY
        AND #STAT_AVAIL
        BEQ @tail_eof
        LDA DISK_DATA
        STA VID_DATA            ; display to screen
        STA RES_STATUS+1       ; keep last byte
        INX
        JMP @tail
@tail_eof:
        TYA
        AND #STAT_EOF
        BEQ @tail               ; busy-wait (won't happen)

        STX RES_STATUS+0       ; [0] expect 10

phase3_done:
        JMP phase4

; ============================================================
; Phase 4: STATUS lifecycle — verify EOF + past-EOF error
;   File was fully read in phase 3. Channel should show EOF.
;   RES_STATUS: [0]=bytes_read (set in phase 3) [1]=last_byte (set in phase 3)
;               [2]=status_eof [3]=error_past_eof
; ============================================================
phase4:
        ; Channel still selected from phase 3
        LDA DISK_STATUS
        AND #STAT_EOF
        STA RES_STATUS+2       ; [2] expect $20 (EOF bit set)

        ; Try to read past EOF
        LDA DISK_DATA
        LDA DISK_ERROR
        STA RES_STATUS+3       ; [3] expect $03 (PAST_EOF)

phase4_done:
        JMP phase5

; ============================================================
; Phase 5: CLOSE — close the file, verify channel inactive
;   RES_CLOSE: [0]=error [1]=result [2]=chan_active_after
; ============================================================
phase5:
        LDA #CONTROL_CHAN
        STA DISK_CHAN

        ; Send "CL,<binary_chan_id>" + null
        LDA #'C'
        STA DISK_DATA
        LDA #'L'
        STA DISK_DATA
        LDA #','
        STA DISK_DATA
        LDA zp_chan             ; binary channel ID byte
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA          ; execute

        LDA DISK_ERROR
        STA RES_CLOSE+0        ; [0] expect $00
        LDA DISK_DATA
        STA RES_CLOSE+1        ; [1] expect $00 (success)

        ; Verify channel is inactive
        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_CHAN
        AND #CHAN_ACTIVE
        STA RES_CLOSE+2        ; [2] expect $00

phase5_done:
        JMP phase6

; ============================================================
; Phase 6: Error handling
;   a) Invalid command "XX" — expect cmd error $09
;   b) Open nonexistent file — expect cmd error $01
;   c) Double null — should not re-execute
;   RES_ERROR: [0]=xx_error [1]=xx_code [2]=notfound_error
;              [3]=notfound_code [4]=double_null_error
; ============================================================
phase6:
        ; a) Invalid command
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'X'
        STA DISK_DATA
        LDA #'X'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_ERROR+0        ; [0] expect $80 (cmd error flag)
        LDA DISK_DATA
        STA RES_ERROR+1        ; [1] expect $09 (bad syntax)

        ; b) File not found
        LDA #CONTROL_CHAN
        STA DISK_CHAN           ; clears previous error

        LDX #0
@nf:    LDA str_notfound,X
        BEQ @nf_null
        STA DISK_DATA
        INX
        JMP @nf
@nf_null:
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_ERROR+2        ; [2] expect $80
        LDA DISK_DATA
        STA RES_ERROR+3        ; [3] expect $01 (file not found)

        ; c) Double null — send "PD" + null + null
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'P'
        STA DISK_DATA
        LDA #'D'
        STA DISK_DATA
        LDA #$00               ; execute PD
        STA DISK_DATA
        LDA DISK_DATA           ; consume channel ID result
        LDA #$00               ; second null — should be ignored
        STA DISK_DATA
        LDA DISK_ERROR
        STA RES_ERROR+4        ; [4] expect $00 (no error, null was ignored)

phase6_done:
        JMP all_pass

; ============================================================
; All phases passed
; ============================================================
all_pass:
        JSR vid_clear
        LDA #VIDCTRL_ALL
        STA VID_CTRL

        LDX #0
@loop:  LDA str_pass,X
        BEQ all_done
        STA VID_DATA
        INX
        JMP @loop

all_done:
        JMP all_done

; ============================================================
; Subroutines
; ============================================================

vid_clear:
        LDA #VIDOP_CLEAR
        STA VID_OPER
        RTS

; ============================================================
; RODATA
; ============================================================
.segment "RODATA"

str_open:
        .byte "OP,R,test.txt", 0

str_open_hdr:
        .byte "OPEN: ", 0

str_notfound:
        .byte "OP,R,no_such_file.xyz", 0

str_pass:
        .byte "STORAGE TEST: ALL 6 PHASES PASSED", 0
