; zx128_page.asm -- ZX Spectrum 128K RAM page 7 screen flipping.
;
; The resident program must stay below $C000. RAM page 7 is kept mapped into
; $C000..$FFFF, where its display file lives at $C000 and its attributes at
; $D800. Bit 3 of port $7FFD selects which display file the ULA shows:
;   0: normal screen in RAM page 5 at $4000
;   1: shadow screen in RAM page 7 at $C000

        SECTION code_user

        PUBLIC  _zx128_page_show_a
        PUBLIC  _zx128_page_show_b

ZX128_PORT_7FFD EQU     $7FFD
ZX128_PAGE7     EQU     $07
ZX128_SCREEN7   EQU     $08
ZX128_ROM1      EQU     $10

; void zx128_page_show_a(void)
; Keep RAM page 7 mapped at $C000 and show the normal page-5 screen.
_zx128_page_show_a:
        ld      bc,ZX128_PORT_7FFD
        ld      a,ZX128_ROM1 | ZX128_PAGE7
        out     (c),a
        ret

; void zx128_page_show_b(void)
; Keep RAM page 7 mapped at $C000 and show its shadow screen.
_zx128_page_show_b:
        ld      bc,ZX128_PORT_7FFD
        ld      a,ZX128_ROM1 | ZX128_PAGE7 | ZX128_SCREEN7
        out     (c),a
        ret
