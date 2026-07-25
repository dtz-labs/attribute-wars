; music_im2.asm -- the 50 Hz IM2 interrupt that drives the music.
;
; Ticking the player from the frame interrupt (not the main loop) keeps the tune
; at exact tempo no matter how long a frame's work takes -- the death explosion,
; a multi-frame screen clear, a busy wave. The interrupt fires during those
; blocking sections and plays a music frame right there, so the main loop never
; calls music_tick at all.
;
; Only set up when an AY is present (music_init); a beeper-only machine keeps
; the ROM's IM1 handler unchanged.
;
; Conventions: SECTION code_user; the ISR saves EVERYTHING, including IY (the
; sdcc_iy frame pointer) and the alternate bank (the PT3 player uses EXX).

        SECTION code_user

        PUBLIC  _music_im2_init         ; void music_im2_init(void): switch to IM2
        PUBLIC  _music_im1_init         ; void music_im1_init(void): switch to IM1

        EXTERN  _music_tick             ; C: play one frame + decay SFX (ISR calls it)

; ===========================================================================
; IM2 interrupt-driven music. Ticking the player from the 50 Hz frame interrupt
; (not the main loop) keeps the tune at exact tempo no matter how long a frame's
; work takes -- the death explosion, a multi-frame screen clear, a busy wave.
; The interrupt fires during those blocking sections and plays a music frame
; right there, so the main loop no longer needs to call music_tick at all.
;
; The 257-byte IM2 vector table + the jump-to-handler live in the UNUSED RAM hole
; AFTER screen B (its attributes end at 0x7AFF; the program starts at 0x8000), so
; they cost zero program/stack space and nothing else touches them. This hole is
; genuinely free -- unlike 0x5B00 (the ROM PRINTER BUFFER) and 0x5C00+ (the ROM
; SYSTEM VARIABLES), which an earlier version wrongly used and which a Spectrum
; ROM (TC2048 / ZX 128) actively overwrites -> a crash.
;   table : 257 bytes of 0x7C at 0x7B00            -> I = 0x7B
;   vector: the CPU reads I*256 + (floating bus 0xFF) = the word at 0x7BFF = 0x7C7C
;   0x7C7C: JP isr_main
; Only set up when an AY is present (music_init); a beeper-only machine keeps the
; ROM's IM1 handler unchanged.
; ===========================================================================
; IM2 table location -- depends on the resident memory layout, which differs
; between targets:
;   Timex / 48K: program is ORG'd at 0x8000, so the free RAM hole at 0x7B00
;       (after screen B's attributes at 0x7AFF) is the home, vector 0x7C7C.
;   ZX128 page-flip: the resident program is ORG'd LOWER (0x6000) so the AY/FX
;       code fits below 0xC000, which puts 0x7B00 INSIDE the program. Park the
;       table in page 7's free RAM instead: the shadow screen uses 0xC000-0xDAFF
;       and main.c's preshift tables live at 0xDB00..0xDF7F (9*128 B), so 0xF000
;       sits ~4 KB above both. Page 7 is permanently mapped at 0xC000 in this
;       build, and the table + vector are built at runtime (music_im2_init) --
;       nothing is tape-loaded there -- so the address is always reachable when
;       an interrupt fires.
        IFDEF   ZX128_PAGE_FLIP
IM2_TABLE equ 0xF000            ; page 7 free RAM, well above shadow screen+preshift
IM2_FILL  equ 0xF1              ; table fill byte -> vector IM2_FILL*256+IM2_FILL
IM2_VEC   equ 0xF1F1            ; = IM2_FILL*256 + IM2_FILL
        ELSE
IM2_TABLE equ 0x7B00            ; 256-aligned, in the free hole after screen B
IM2_FILL  equ 0x7C              ; table fill byte -> vector IM2_FILL*256+IM2_FILL
IM2_VEC   equ 0x7C7C            ; = IM2_FILL*256 + IM2_FILL
        ENDIF

; void music_im2_init(void) -- build the IM2 table + jump, switch IM1 -> IM2.
_music_im2_init:
        di
        IFDEF   ZX128_PAGE_FLIP
        ; EXPERIMENTAL (DivMMC/DivIDE/PicoDIV reset on AY): page the storage
        ; interface OUT before we take over IM2. esxdos auto-maps itself into
        ; $0000-$3FFF on ROM fetches ($0038 etc.); in IM1 its handler runs every
        ; frame and keeps that state consistent, but in IM2 we never hit $0038,
        ; so esxdos is left half-mapped -> reset on some interfaces (PicoDIV).
        ; Writing $00 to port $E3 clears CONMEM/MAPRAM and restores the normal
        ; ROM; with no automap address fetched under IM2 it stays paged out.
        ; Harmless on machines without the interface (port $E3 undecoded).
        ld      bc,0x00E3
        xor     a
        out     (c),a
        ENDIF
        ld      hl,IM2_TABLE            ; fill 257 bytes with IM2_FILL
        ld      de,IM2_TABLE+1
        ld      bc,256
        ld      (hl),IM2_FILL
        ldir
        ld      a,0xC3                  ; JP isr_main at the vector address
        ld      (IM2_VEC),a
        ld      hl,isr_main
        ld      (IM2_VEC+1),hl
        ld      a,IM2_TABLE >> 8        ; I = high byte of the table (0x7B)
        ld      i,a
        im      2
        ei
        ret

; void music_im1_init(void) -- return to the ROM's normal interrupt mode.
_music_im1_init:
        di
        im      1
        ei
        ret

; isr_main -- the 50 Hz handler. Saves EVERYTHING the interrupted code or the
; player could rely on: the main set, IX, IY (sdcc_iy frame pointer), and the
; ALTERNATE bank (the PT3 player uses EXX). Then ticks the music and RETIs.
isr_main:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ex      af,af'
        push    af
        exx
        push    bc
        push    de
        push    hl
        call    _music_tick             ; play one frame + decay SFX (no-op if !on)
        pop     hl
        pop     de
        pop     bc
        exx
        pop     af
        ex      af,af'
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti
