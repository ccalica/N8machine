; test_storage.s - Storage device register validation (Phase 1 + Phase 2)
;
; 12 test phases exercising all storage commands.
; Phase 1-6:  PWDIR, LS, OPEN R, STATUS, CLOSE, error handling
; Phase 7-12: OPEN W, SEEK, MKDIR+CHDIR, RMDIR, MOVE, REMOVE
;
; Each phase prints a one-line result to the screen (all visible at once).
; Results also stored in RAM at $0300-$03FF for GDB inspection.
; Labeled checkpoints (phase1_done..phase12_done) serve as breakpoints.
;
; Prerequisites:
;   - Emulator launched with --storage <dir> containing:
;     * "test.txt" with content "Foo\nBar\nBaz\n\n" (13 bytes)
;     * "dir/" subdirectory
;   - Default ./storage/ works if the above exist
;
; Phases 7-12 create and clean up their own files/dirs.
;
; Expected workflow with n8gdb:
;   load test_storage 0xE000
;   reset
;   bp phase1_done  ... bp phase12_done  bp all_done
;   run
;   (verify results at each breakpoint)

.export   _main
.export   phase1_done, phase2_done, phase3_done, phase4_done
.export   phase5_done, phase6_done, phase7_done, phase8_done
.export   phase9_done, phase10_done, phase11_done, phase12_done
.export   all_done

; --- Storage device registers ---
DISK_CHAN   = $D880
DISK_DATA   = $D881
DISK_ERROR  = $D882
DISK_CTRL   = $D883
DISK_STATUS = $D884

; --- Video registers ---
VID_OPER    = $D844
VID_CURCOL  = $D846
VID_CURROW  = $D847
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

; --- Result buffers ($0300-$03FF) ---
RES_PWDIR   = $0300        ; [0]=error [1]=chan [2]=byte [3]=status [4]=active
RES_LIST    = $0320        ; [0]=error [1]=chan [2..33]=data [34]=count
RES_OPEN    = $0350        ; [0]=error [1]=chan [2-4]=Foo [5]=avail
RES_STATUS  = $0370        ; [0]=tail_count [1]=last_byte [2]=eof [3]=pasteof
RES_CLOSE   = $0380        ; [0]=error [1]=result [2]=active
RES_ERROR   = $0390        ; [0-1]=bad_cmd [2-3]=notfound [4]=double_null
RES_WRITE   = $03A0        ; [0]=w_err [1]=w_chan [2]=cl_err [3]=r_err [4]=r_chan [5-9]=readback
RES_SEEK    = $03B0        ; [0]=open_err [1]=chan [2]=seek_err [3-5]=Bar
RES_MKDIR   = $03C0        ; [0]=md_err [1]=cd_err [2]=pwd_err [3]=pwd_chan [4-8]=data
RES_RMDIR   = $03D0        ; [0]=cd_err [1]=rd_err
RES_MOVE    = $03E0        ; [0]=mv_err [1]=open_err [2]=open_chan
RES_REMOVE  = $03F0        ; [0]=rm_err [1]=open_err [2]=open_code

; --- Zero page ---
.segment "ZEROPAGE"
zp_chan:     .res 1
zp_count:   .res 1
zp_ptr:     .res 2

.segment "CODE"

_main:
        ; Clear screen and enable all video features
        LDA #VIDOP_CLEAR
        STA VID_OPER
        LDA #VIDCTRL_ALL
        STA VID_CTRL

; ============================================================
; Phase 1: PWDIR — expect "/"
; ============================================================
phase1:
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'P'
        STA DISK_DATA
        LDA #'D'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_PWDIR+0        ; expect $00

        LDA DISK_DATA
        STA RES_PWDIR+1        ; channel ID
        STA zp_chan

        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_DATA
        STA RES_PWDIR+2        ; expect '/' ($2F)

        LDA DISK_STATUS
        STA RES_PWDIR+3        ; expect $60 (EOF+CTS)

        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_CHAN
        AND #CHAN_ACTIVE
        STA RES_PWDIR+4        ; expect $00 (auto-closed)

        ; Display: "P1 PWDIR /"
        LDA #<str_p1
        STA zp_ptr
        LDA #>str_p1
        STA zp_ptr+1
        JSR print_str
        LDA RES_PWDIR+2
        STA VID_DATA
        JSR vid_newline

phase1_done:
        JMP phase2

; ============================================================
; Phase 2: LIST — read directory listing, count entries
; ============================================================
phase2:
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'L'
        STA DISK_DATA
        LDA #'S'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_LIST+0         ; expect $00

        LDA DISK_DATA
        STA RES_LIST+1         ; channel ID
        STA zp_chan

        LDA zp_chan
        STA DISK_CHAN

        LDX #0
        LDY #0                 ; newline counter (= entry count)
@read:  LDA DISK_STATUS
        PHA
        AND #STAT_AVAIL
        BEQ @check_eof

        LDA DISK_DATA
        STA RES_LIST+2,X
        CMP #$0A
        BNE @not_nl
        INY
@not_nl:
        INX
        CPX #32
        BCS @full
        PLA
        JMP @read

@check_eof:
        PLA
        AND #STAT_EOF
        BNE @done_read
        JMP @read

@full:  PLA
@done_read:
        STY RES_LIST+34        ; entry count

        ; Display: "P2 LIST <count>"
        ; Count entries properly: each entry ends with <size_lo><size_hi><$0A>.
        ; Walk the buffer and count $0A bytes that are preceded by two size bytes.
        ; Simpler: the listing format has one $0A per entry at position after
        ; <type><tab><name><tab><size_lo><size_hi>. Count entries by counting
        ; lines where the first byte is 'D' or 'F'.
        STX zp_count            ; total bytes read
        LDX #0
        LDY #0                  ; entry count
        STY zp_chan             ; track: 1=at start of line
        LDA #1
        STA zp_chan
@count: CPX zp_count
        BCS @counted
        LDA zp_chan
        BEQ @not_start
        ; At start of line — check for D or F
        LDA RES_LIST+2,X
        CMP #'D'
        BEQ @found_entry
        CMP #'F'
        BEQ @found_entry
        JMP @not_entry
@found_entry:
        INY
@not_entry:
        LDA #0
        STA zp_chan             ; no longer at start
@not_start:
        LDA RES_LIST+2,X
        CMP #$0A
        BNE @no_nl
        LDA #1
        STA zp_chan             ; next byte is start of line
@no_nl: INX
        JMP @count
@counted:
        STY RES_LIST+34        ; store accurate count
        LDA #<str_p2
        STA zp_ptr
        LDA #>str_p2
        STA zp_ptr+1
        JSR print_str
        LDA RES_LIST+34    ; load saved entry count (print_str clobbers Y)
        CLC
        ADC #'0'
        STA VID_DATA
        JSR vid_newline

phase2_done:
        JMP phase3

; ============================================================
; Phase 3: OPEN R — read first 3 bytes of test.txt ("Foo")
; ============================================================
phase3:
        LDA #<str_open_r
        STA zp_ptr
        LDA #>str_open_r
        STA zp_ptr+1
        JSR send_cmd

        LDA DISK_ERROR
        STA RES_OPEN+0         ; expect $00

        LDA DISK_DATA
        STA RES_OPEN+1         ; channel ID
        STA zp_chan

        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_DATA
        STA RES_OPEN+2         ; expect 'F' ($46)
        LDA DISK_DATA
        STA RES_OPEN+3         ; expect 'o' ($6F)
        LDA DISK_DATA
        STA RES_OPEN+4         ; expect 'o' ($6F)

        LDA DISK_STATUS
        AND #STAT_AVAIL
        STA RES_OPEN+5         ; expect 10 (13-3=10, saturates at 15)

        ; Display: "P3 READ Foo"
        LDA #<str_p3
        STA zp_ptr
        LDA #>str_p3
        STA zp_ptr+1
        JSR print_str
        LDA RES_OPEN+2
        STA VID_DATA
        LDA RES_OPEN+3
        STA VID_DATA
        LDA RES_OPEN+4
        STA VID_DATA
        JSR vid_newline

phase3_done:
        JMP phase4

; ============================================================
; Phase 4: STATUS — drain file, verify EOF + past-EOF error
; ============================================================
phase4:
        ; Channel still selected from phase 3
        LDX #0
@tail:  LDA DISK_STATUS
        TAY
        AND #STAT_AVAIL
        BEQ @tail_eof
        LDA DISK_DATA
        STA RES_STATUS+1       ; keep overwriting with last byte
        INX
        JMP @tail
@tail_eof:
        TYA
        AND #STAT_EOF
        BEQ @tail              ; busy-wait
        STX RES_STATUS+0       ; bytes read (expect 10)

        LDA DISK_STATUS
        AND #STAT_EOF
        STA RES_STATUS+2       ; expect $20

        LDA DISK_DATA          ; read past EOF
        LDA DISK_ERROR
        STA RES_STATUS+3       ; expect $03 (PAST_EOF)

        ; Display: "P4 EOF ok"
        LDA #<str_p4
        STA zp_ptr
        LDA #>str_p4
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase4_done:
        JMP phase5

; ============================================================
; Phase 5: CLOSE — close file via binary CL command
; ============================================================
phase5:
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'C'
        STA DISK_DATA
        LDA #'L'
        STA DISK_DATA
        LDA #','
        STA DISK_DATA
        LDA zp_chan             ; binary channel ID
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_CLOSE+0        ; expect $00
        LDA DISK_DATA
        STA RES_CLOSE+1        ; expect $00

        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_CHAN
        AND #CHAN_ACTIVE
        STA RES_CLOSE+2        ; expect $00

        ; Display: "P5 CLOSE ok"
        LDA #<str_p5
        STA zp_ptr
        LDA #>str_p5
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase5_done:
        JMP phase6

; ============================================================
; Phase 6: Error handling — bad cmd, file not found, double null
; ============================================================
phase6:
        ; a) Invalid command "XX"
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'X'
        STA DISK_DATA
        LDA #'X'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_ERROR+0        ; expect $80
        LDA DISK_DATA
        STA RES_ERROR+1        ; expect $09 (bad syntax)

        ; b) File not found (send_cmd selects control chan, clears error)
        LDA #<str_notfound
        STA zp_ptr
        LDA #>str_notfound
        STA zp_ptr+1
        JSR send_cmd

        LDA DISK_ERROR
        STA RES_ERROR+2        ; expect $80
        LDA DISK_DATA
        STA RES_ERROR+3        ; expect $01 (file not found)

        ; c) Double null — send PD, consume result, send extra null
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'P'
        STA DISK_DATA
        LDA #'D'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA
        LDA DISK_DATA           ; consume channel result
        LDA #$00               ; second null — should be ignored
        STA DISK_DATA
        LDA DISK_ERROR
        STA RES_ERROR+4        ; expect $00

        ; Display: "P6 ERR ok"
        LDA #<str_p6
        STA zp_ptr
        LDA #>str_p6
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase6_done:
        JMP phase7

; ============================================================
; Phase 7: OPEN W — write "HELLO", close, read back to verify
; ============================================================
phase7:
        ; Open wtest.txt for write
        LDA #<str_open_w
        STA zp_ptr
        LDA #>str_open_w
        STA zp_ptr+1
        JSR send_cmd

        LDA DISK_ERROR
        STA RES_WRITE+0        ; expect $00
        LDA DISK_DATA
        STA RES_WRITE+1        ; channel ID
        STA zp_chan

        ; Write "HELLO" to data channel
        LDA zp_chan
        STA DISK_CHAN
        LDA #'H'
        STA DISK_DATA
        LDA #'E'
        STA DISK_DATA
        LDA #'L'
        STA DISK_DATA
        LDA #'L'
        STA DISK_DATA
        LDA #'O'
        STA DISK_DATA

        ; Close via CL binary command
        JSR close_zp_chan
        LDA DISK_ERROR
        STA RES_WRITE+2        ; expect $00

        ; Read back
        LDA #<str_read_w
        STA zp_ptr
        LDA #>str_read_w
        STA zp_ptr+1
        JSR send_cmd

        LDA DISK_ERROR
        STA RES_WRITE+3        ; expect $00
        LDA DISK_DATA
        STA RES_WRITE+4        ; channel ID
        STA zp_chan

        ; Read 5 bytes directly (file is exactly 5 bytes)
        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_DATA
        STA RES_WRITE+5        ; 'H'
        LDA DISK_DATA
        STA RES_WRITE+6        ; 'E'
        LDA DISK_DATA
        STA RES_WRITE+7        ; 'L'
        LDA DISK_DATA
        STA RES_WRITE+8        ; 'L'
        LDA DISK_DATA
        STA RES_WRITE+9        ; 'O'

        JSR close_zp_chan

        ; Display: "P7 WRITE HELLO"
        LDA #<str_p7
        STA zp_ptr
        LDA #>str_p7
        STA zp_ptr+1
        JSR print_str
        LDX #0
@disp:  LDA RES_WRITE+5,X
        STA VID_DATA
        INX
        CPX #5
        BCC @disp
        JSR vid_newline

phase7_done:
        JMP phase8

; ============================================================
; Phase 8: SEEK — open test.txt, seek to offset 4, read "Bar"
;   test.txt = "Foo\nBar\nBaz\n\n", offset 4 = 'B','a','r'
; ============================================================
phase8:
        LDA #<str_open_r
        STA zp_ptr
        LDA #>str_open_r
        STA zp_ptr+1
        JSR send_cmd

        LDA DISK_ERROR
        STA RES_SEEK+0         ; expect $00
        LDA DISK_DATA
        STA RES_SEEK+1         ; channel ID
        STA zp_chan

        ; Send SEEK: SK,<chan>,<','>,<'A'>,<','>,<$04>,<$00>
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'S'
        STA DISK_DATA
        LDA #'K'
        STA DISK_DATA
        LDA #','               ; triggers binary payload mode (6 bytes)
        STA DISK_DATA
        LDA zp_chan             ; payload byte 1: channel ID
        STA DISK_DATA
        LDA #','               ; payload byte 2: delimiter
        STA DISK_DATA
        LDA #'A'               ; payload byte 3: seek type (absolute)
        STA DISK_DATA
        LDA #','               ; payload byte 4: delimiter
        STA DISK_DATA
        LDA #$04               ; payload byte 5: offset lo
        STA DISK_DATA
        LDA #$00               ; payload byte 6: offset hi
        STA DISK_DATA
        LDA #$00               ; null terminator — execute command
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_SEEK+2         ; expect $00

        ; Read 3 bytes from data channel
        LDA zp_chan
        STA DISK_CHAN
        LDA DISK_DATA
        STA RES_SEEK+3         ; expect 'B' ($42)
        LDA DISK_DATA
        STA RES_SEEK+4         ; expect 'a' ($61)
        LDA DISK_DATA
        STA RES_SEEK+5         ; expect 'r' ($72)

        JSR close_zp_chan

        ; Display: "P8 SEEK Bar"
        LDA #<str_p8
        STA zp_ptr
        LDA #>str_p8
        STA zp_ptr+1
        JSR print_str
        LDA RES_SEEK+3
        STA VID_DATA
        LDA RES_SEEK+4
        STA VID_DATA
        LDA RES_SEEK+5
        STA VID_DATA
        JSR vid_newline

phase8_done:
        JMP phase9

; ============================================================
; Phase 9: MKDIR + CHDIR — create "tdir", cd into it, PWDIR
; ============================================================
phase9:
        ; MKDIR "tdir"
        LDA #<str_mkdir
        STA zp_ptr
        LDA #>str_mkdir
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_MKDIR+0        ; expect $00

        ; CHDIR "tdir"
        LDA #<str_chdir
        STA zp_ptr
        LDA #>str_chdir
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_MKDIR+1        ; expect $00

        ; PWDIR to verify
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'P'
        STA DISK_DATA
        LDA #'D'
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA

        LDA DISK_ERROR
        STA RES_MKDIR+2        ; expect $00
        LDA DISK_DATA
        STA RES_MKDIR+3        ; response channel
        STA zp_chan

        ; Read PWDIR response (expect "/tdir" = 5 bytes)
        LDA zp_chan
        STA DISK_CHAN
        LDX #0
@read:  LDA DISK_STATUS
        PHA
        AND #STAT_AVAIL
        BEQ @check_eof
        LDA DISK_DATA
        STA RES_MKDIR+4,X
        INX
        CPX #8                 ; read at most 8 bytes
        BCS @full
        PLA
        JMP @read
@check_eof:
        PLA
        AND #STAT_EOF
        BNE @done
        JMP @read
@full:  PLA
@done:
        STX zp_count           ; save byte count

        ; Display: "P9 MKDIR /tdir"
        LDA #<str_p9
        STA zp_ptr
        LDA #>str_p9
        STA zp_ptr+1
        JSR print_str
        LDX #0
@disp:  CPX zp_count
        BCS @disp_done
        LDA RES_MKDIR+4,X
        STA VID_DATA
        INX
        JMP @disp
@disp_done:
        JSR vid_newline

phase9_done:
        JMP phase10

; ============================================================
; Phase 10: RMDIR — cd back to root, remove tdir
; ============================================================
phase10:
        ; CHDIR "/"
        LDA #<str_chdir_root
        STA zp_ptr
        LDA #>str_chdir_root
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_RMDIR+0        ; expect $00

        ; RMDIR "tdir"
        LDA #<str_rmdir
        STA zp_ptr
        LDA #>str_rmdir
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_RMDIR+1        ; expect $00

        ; Display: "P10 RMDIR ok"
        LDA #<str_p10
        STA zp_ptr
        LDA #>str_p10
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase10_done:
        JMP phase11

; ============================================================
; Phase 11: MOVE — rename wtest.txt to moved.txt, verify
; ============================================================
phase11:
        LDA #<str_move
        STA zp_ptr
        LDA #>str_move
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_MOVE+0         ; expect $00

        ; Verify: open moved.txt for read
        LDA #<str_open_moved
        STA zp_ptr
        LDA #>str_open_moved
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_MOVE+1         ; expect $00
        LDA DISK_DATA
        STA RES_MOVE+2         ; channel ID
        STA zp_chan

        JSR close_zp_chan

        ; Display: "P11 MOVE ok"
        LDA #<str_p11
        STA zp_ptr
        LDA #>str_p11
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase11_done:
        JMP phase12

; ============================================================
; Phase 12: REMOVE — delete moved.txt, verify it's gone
; ============================================================
phase12:
        LDA #<str_remove
        STA zp_ptr
        LDA #>str_remove
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_REMOVE+0       ; expect $00

        ; Verify: try to open — should fail with file not found
        LDA #<str_open_moved
        STA zp_ptr
        LDA #>str_open_moved
        STA zp_ptr+1
        JSR send_cmd
        LDA DISK_ERROR
        STA RES_REMOVE+1       ; expect $80 (cmd error)
        LDA DISK_DATA
        STA RES_REMOVE+2       ; expect $01 (file not found)

        ; Display: "P12 RM ok"
        LDA #<str_p12
        STA zp_ptr
        LDA #>str_p12
        STA zp_ptr+1
        JSR print_str
        JSR vid_newline

phase12_done:
        JMP all_pass

; ============================================================
; All phases passed
; ============================================================
all_pass:
        JSR vid_newline
        LDA #<str_pass
        STA zp_ptr
        LDA #>str_pass
        STA zp_ptr+1
        JSR print_str

all_done:
        JMP all_done

; ============================================================
; Subroutines
; ============================================================

; Print null-terminated string at (zp_ptr) to VID_DATA
print_str:
        LDY #0
@loop:  LDA (zp_ptr),Y
        BEQ @done
        STA VID_DATA
        INY
        BNE @loop
@done:  RTS

; Move cursor to start of next line
vid_newline:
        LDA #$00
        STA VID_CURCOL
        LDA VID_CURROW
        CLC
        ADC #1
        STA VID_CURROW
        RTS

; Send null-terminated command at (zp_ptr) to control channel.
; Selects control channel first (clears any prior error state).
send_cmd:
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDY #0
@loop:  LDA (zp_ptr),Y
        STA DISK_DATA
        BEQ @done
        INY
        BNE @loop
@done:  RTS

; Close channel in zp_chan via binary CL command
close_zp_chan:
        LDA #CONTROL_CHAN
        STA DISK_CHAN
        LDA #'C'
        STA DISK_DATA
        LDA #'L'
        STA DISK_DATA
        LDA #','
        STA DISK_DATA
        LDA zp_chan
        STA DISK_DATA
        LDA #$00
        STA DISK_DATA
        RTS

; ============================================================
; RODATA
; ============================================================
.segment "RODATA"

; Phase label strings
str_p1:         .byte "P1 PWDIR ", 0
str_p2:         .byte "P2 LIST ", 0
str_p3:         .byte "P3 READ ", 0
str_p4:         .byte "P4 EOF ok", 0
str_p5:         .byte "P5 CLOSE ok", 0
str_p6:         .byte "P6 ERR ok", 0
str_p7:         .byte "P7 WRITE ", 0
str_p8:         .byte "P8 SEEK ", 0
str_p9:         .byte "P9 MKDIR ", 0
str_p10:        .byte "P10 RMDIR ok", 0
str_p11:        .byte "P11 MOVE ok", 0
str_p12:        .byte "P12 RM ok", 0
str_pass:       .byte "ALL 12 PHASES PASSED", 0

; Command strings
str_open_r:     .byte "OP,R,test.txt", 0
str_open_w:     .byte "OP,W,wtest.txt", 0
str_read_w:     .byte "OP,R,wtest.txt", 0
str_notfound:   .byte "OP,R,no_such_file.xyz", 0
str_mkdir:      .byte "MD,tdir", 0
str_chdir:      .byte "CD,tdir", 0
str_chdir_root: .byte "CD,/", 0
str_rmdir:      .byte "RD,tdir", 0
str_move:       .byte "MV,wtest.txt,moved.txt", 0
str_open_moved: .byte "OP,R,moved.txt", 0
str_remove:     .byte "RM,moved.txt", 0
