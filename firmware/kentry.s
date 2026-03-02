; -------------------------------------------------------
; kentry.s -- Kernel entry jump table at $FE00
; -------------------------------------------------------

.import _tty_putc, _tty_getc, _tty_peekc
.import _con_getkey, _con_setmode, _con_getstatus
.import _con_putchar, _con_newline, _con_clear
.import _con_scroll, _con_movcursor, _con_setcursor

.segment "KENTRY"

    JMP _tty_putc       ; $FE00
    JMP _tty_getc       ; $FE03
    JMP _tty_peekc      ; $FE06
    JMP _con_getkey     ; $FE09
    JMP _con_setmode    ; $FE0C
    JMP _con_getstatus  ; $FE0F
    JMP _con_putchar    ; $FE12
    JMP _con_newline    ; $FE15
    JMP _con_clear      ; $FE18
    JMP _con_scroll     ; $FE1B
    JMP _con_movcursor  ; $FE1E
    JMP _con_setcursor  ; $FE21
