; ay_sfx.asm -- FX-only AY output (channel C).
;
; When the title menu chooses SOUND=FX the PT3 player is never initialised, but
; IM2 still ticks _music_tick at 50 Hz. These routines write a minimal channel-C
; voice straight from the same _asfx_* state that pt3_glue.asm's sfx_merge
; overlays onto the player's registers in MUSIC+FX mode.
;
; Conventions: SECTION code_user; only AY ports are written (never 0xFF/0xFE).

        SECTION code_user

        PUBLIC  _ay_sfx_out             ; void ay_sfx_out(void): FX-only channel C
        PUBLIC  _ay_sfx_mute            ; void ay_sfx_mute(void): silence FX-only C

        EXTERN  sel_write               ; ay_ports.asm: select reg B, write C

        ; channel-C sound-effect state (music.c)
        EXTERN  _asfx_vol               ; u8  0=inactive, else amplitude 1..15
        EXTERN  _asfx_kind              ; u8  0=tone, 1=noise
        EXTERN  _asfx_tper              ; u16 tone period (R4/R5)
        EXTERN  _asfx_nper              ; u8  noise period (R6)

; ---------------------------------------------------------------------------
; FX-only AY output. When the title menu chooses SOUND=FX, the PT3 player is not
; initialised, but IM2 still ticks _music_tick at 50 Hz. These routines write a
; minimal channel-C voice directly from the same _asfx_* state that sfx_merge
; uses in MUSIC+FX mode.
; ---------------------------------------------------------------------------
_ay_sfx_out:
        ld      a,(_asfx_vol)
        or      a
        ret     z
        ld      d,a                     ; D = volume to write last

        ld      b,8                     ; amp A = 0
        ld      c,0
        call    sel_write
        ld      b,9                     ; amp B = 0
        ld      c,0
        call    sel_write

        ld      a,(_asfx_kind)
        or      a
        jr      nz,aso_noise
        ld      a,(_asfx_tper)          ; tone C fine (R4)
        ld      b,4
        ld      c,a
        call    sel_write
        ld      a,(_asfx_tper+1)        ; tone C coarse (R5)
        ld      b,5
        ld      c,a
        call    sel_write
        ld      b,7
        ld      c,0x3B                  ; A/B off, C tone on, C noise off
        call    sel_write
        jr      aso_amp
aso_noise:
        ld      a,(_asfx_nper)          ; noise period (R6)
        ld      b,6
        ld      c,a
        call    sel_write
        ld      b,7
        ld      c,0x1F                  ; A/B off, C tone off, C noise on
        call    sel_write
aso_amp:
        ld      b,10                    ; amp C = SFX volume
        ld      c,d
        call    sel_write
        ret

_ay_sfx_mute:
        ld      b,10                    ; amp C = 0
        ld      c,0
        call    sel_write
        ld      b,7
        ld      c,0x3F                  ; disable tone/noise on all channels
        call    sel_write
        ret
