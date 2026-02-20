; test_zeropage.s - Zero page and indirect addressing test
;
; Uses ZP pointers with indirect indexed addressing. Modify pointers via
; n8gdb write to redirect where data goes.
; Good for testing: read on ZP ($00-$FF), write to ZP pointers, step
;
; Expected workflow with n8gdb:
;   sym test_zeropage.sym
;   bp indirect_store      ; or bp copy_next
;   run
;   read 00E0 8           ; read ZP pointers: E0/E1=ptr_src, E2/E3=ptr_dst
;   step                  ; execute STA (ptr_dst),Y
;   read 0400 8           ; verify data written at $0400
;   write 00E2 0005       ; change ptr_dst to $0500 (little-endian)
;   step                  ; next store goes to $0500 instead
;   read 0500 8           ; verify redirected write

.export   _main
.export   setup_src, setup_dst, copy_loop, copy_next, indirect_store
.export   zp_sum, restart, table_data

; Zero page pointers (from zp.inc allocation)
ptr_src = $E0                ; source pointer (E0/E1)
ptr_dst = $E2                ; destination pointer (E2/E3)
zp_tmp  = $F0                ; scratch byte

.segment  "CODE"

_main:

; --- Set up source pointer to table_data ---
setup_src:
        LDA #<table_data
        STA ptr_src
        LDA #>table_data
        STA ptr_src+1

; --- Set up destination pointer to $0400 ---
setup_dst:
        LDA #$00
        STA ptr_dst
        LDA #$04
        STA ptr_dst+1

; --- Copy 8 bytes via indirect indexed ---
copy_loop:
        LDY #$00
copy_next:
        LDA (ptr_src),Y      ; load from source table
indirect_store:
        STA (ptr_dst),Y      ; store to destination
        INY
        CPY #$08
        BNE copy_next

; --- ZP arithmetic: sum first 4 bytes of table into zp_tmp ---
zp_sum:
        LDA #$00
        STA zp_tmp
        LDY #$00
sum_next:
        LDA (ptr_src),Y
        CLC
        ADC zp_tmp
        STA zp_tmp
        INY
        CPY #$04
        BNE sum_next
        NOP                  ; zp_tmp now holds sum of first 4 bytes

; --- ZP indirect JMP (self-modifying pointer) ---
; Store address of _main into ptr_src, then use it to restart
restart:
        LDA #<_main
        STA ptr_src
        LDA #>_main
        STA ptr_src+1
        JMP (_main_vec)

_main_vec:
        .addr _main          ; indirect jump target

; --- Source data table (in ROM) ---
table_data:
        .byte $10, $20, $30, $40, $50, $60, $70, $80
