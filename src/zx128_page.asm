; zx128_page.asm -- ZX Spectrum 128K RAM page 7 screen flipping + banked-tune
; paging. Page 7 is kept mapped at $C000 (shadow screen 0xC000..0xDAFF); bit 3
; of $7FFD selects which display file the ULA shows. The PT3 tune lives in bank
; 4 and is paged into $C000 only around each player tick. $7FFD is write-only,
; so a software shadow (zx128_7ffd) tracks the last value written; ALL $7FFD
; writes are confined to this file.
;
;   0: normal screen in RAM page 5 at $4000
;   1: shadow screen in RAM page 7 at $C000

        SECTION code_user

        PUBLIC  _zx128_page_show_a
        PUBLIC  _zx128_page_show_b
        PUBLIC  zx128_tune_in
        PUBLIC  zx128_tune_out
        PUBLIC  _zx128_load_tune

ZX128_PORT_7FFD EQU     $7FFD
ZX128_PAGE7     EQU     $07
ZX128_TUNEBANK  EQU     $04        ; spare, non-contended bank for the PT3 tune
ZX128_SCREEN7   EQU     $08
ZX128_ROM1      EQU     $10
ROM_LD_BYTES    EQU     $0556      ; 48K-ROM tape loader (needs ROM1 paged)
TUNE_ADDR       EQU     $C000      ; tune base in the banked window

; void zx128_page_show_a(void)
; Keep RAM page 7 mapped at $C000 and show the normal page-5 screen.
_zx128_page_show_a:
        ld      a,ZX128_ROM1 | ZX128_PAGE7
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; void zx128_page_show_b(void)
; Keep RAM page 7 mapped at $C000 and show its shadow screen.
_zx128_page_show_b:
        ld      a,ZX128_ROM1 | ZX128_PAGE7 | ZX128_SCREEN7
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; zx128_tune_in -- map bank 4 into $C000, preserving the ROM-select and
; displayed-screen bits from the shadow. Clobbers A,BC.
zx128_tune_in:
        ld      a,(zx128_7ffd)
        and     $F8                ; clear page bits 0..2
        or      ZX128_TUNEBANK     ; select bank 4
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; zx128_tune_out -- restore the normal mapping from the shadow (page 7 + the
; current displayed screen). Clobbers A,BC.
zx128_tune_out:
        ld      a,(zx128_7ffd)
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ret

; void zx128_load_tune(void) -- pull the trailing tape block (the PT3 tune) into
; bank 4 at $C000 via the ROM loader. Runs once at startup, before music_init.
; IX/IY saved (IY is the sdcc_iy frame pointer; the ROM routine trashes plenty).
_zx128_load_tune:
        push    ix
        push    iy
        di
        ld      a,ZX128_ROM1 | ZX128_TUNEBANK   ; bank 4 at $C000, keep ROM1
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ld      ix,TUNE_ADDR
        ld      de,ZX128_TUNE_LEN               ; -Ca-D from the Makefile
        ld      a,$FF                           ; expect a data block
        scf                                     ; carry set = LOAD
        call    ROM_LD_BYTES
        ld      a,ZX128_ROM1 | ZX128_PAGE7      ; restore page 7 (screen A shown)
        ld      (zx128_7ffd),a
        ld      bc,ZX128_PORT_7FFD
        out     (c),a
        ei
        pop     iy
        pop     ix
        ret

        SECTION data_user
zx128_7ffd:  defb  ZX128_ROM1 | ZX128_PAGE7     ; software shadow of port $7FFD
