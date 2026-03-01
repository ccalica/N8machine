; -------------------------------------------------------
; kentry.s -- Kernel entry jump table at $FE00
; -------------------------------------------------------

.import _tty_putc, _tty_getc, _tty_peekc

.segment "KENTRY"

    JMP _tty_putc       ; $FE00
    JMP _tty_getc       ; $FE03
    JMP _tty_peekc      ; $FE06
