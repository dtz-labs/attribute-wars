; pt3_glue.asm -- glue between the vendored PT3 player and this project.
;
; WHY this file exists:
;   z88dk ships the VT2 player (PT3PROM), but its +zx hardware-out
;   (libsrc/target/zx/psg/asm_vt2_hardware_out.asm) hardcodes the 128K ports
;   0xFFFD/0xBFFD via an OUTI trick that needs the shared 0xFD low byte -- which
;   the TS2068 scheme (0xF5/0xF6) does NOT share. The player calls
;   asm_vt_hardware_out / _A0 (both EXTERN in PT3PROM.asm) to emit the 14 AY
;   registers from asm_VT_AYREGS. By DEFINING those symbols here, the linker
;   takes our version and never pulls the library module -- so register writes
;   go through ay_ports.asm's runtime-latched ports and both 128K and TS2068
;   play.
;
; Also here: the C-callable wrappers around the player. All save/restore IX and
; IY, because the player trashes both (it is built for an ISR that saves the
; world) and IY is the sdcc_iy frame pointer. Parameterless (project
; convention); the tune address is wired in directly from tune.asm.
;
; Conventions: SECTION code_user; only AY ports are written (never 0xFF/0xFE).

        SECTION code_user

        PUBLIC  _pt3_init               ; void pt3_init(void): load+init the tune
        PUBLIC  _pt3_play_safe          ; void pt3_play_safe(void): IY-safe play
        PUBLIC  _pt3_mute               ; void pt3_mute(void): silence the AY
        PUBLIC  asm_vt_hardware_out     ; override of z88dk's 128K-only output
        PUBLIC  asm_vt_hardware_out_A0

        EXTERN  ay_sel                  ; ay_ports.asm: latched select port
        EXTERN  ay_dat                  ; ay_ports.asm: latched data port

        EXTERN  asm_VT_AYREGS           ; 14-byte computed AY register file (player)
        EXTERN  asm_VT_SETUP            ; player setup/status flags (bit7=loop)
        EXTERN  asm_VT_INIT             ; vendored player: init, module addr in HL
        EXTERN  asm_VT_PLAY             ; vendored player: play one frame
        EXTERN  asm_VT_MUTE             ; vendored player: silence all channels
        IFNDEF  ZX128_PAGE_FLIP
        EXTERN  _spectrumizer_pt3       ; tune.asm: resident module (Timex/48K)
        ELSE
        EXTERN  zx128_tune_in           ; zx128: tune banked at $C000, page it in
        EXTERN  zx128_tune_out
        ENDIF

        ; channel-C sound-effect state (music.c); overlaid by sfx_merge below
        EXTERN  _asfx_vol               ; u8  0=inactive, else amplitude 1..15
        EXTERN  _asfx_kind              ; u8  0=tone, 1=noise
        EXTERN  _asfx_tper              ; u16 tone period (R4/R5)
        EXTERN  _asfx_nper              ; u8  noise period (R6)

; asm_vt_hardware_out [_A0] -- OVERRIDE of z88dk's output stage. Emit AY
; registers from asm_VT_AYREGS via the latched ports. Faithful to the library
; contract: the plain entry starts at register 0; the _A0 entry starts at the
; register index already in A; register 13 (envelope) is skipped when its byte
; has bit 7 set (the PT3 "don't retrigger the envelope" sentinel).
asm_vt_hardware_out:
        xor     a                       ; start at register 0
asm_vt_hardware_out_A0:
        ; If a channel-C sound effect is live, overlay it onto the player's
        ; freshly-computed AYREGS before they go out (the player keeps A+B).
        push    af
        ld      a,(_asfx_vol)
        or      a
        call    nz,sfx_merge
        pop     af
        ld      hl,asm_VT_AYREGS
        ld      e,a
        ld      d,0
        add     hl,de                   ; HL -> asm_VT_AYREGS[A]
vho_loop:
        cp      13
        jr      z,vho_env
        call    vho_out                 ; AY[A] := (HL); preserves A,HL
        inc     hl
        inc     a
        jr      vho_loop
vho_env:
        ld      a,(hl)                  ; AYREGS[13]
        and     a
        ret     m                       ; bit7 set -> no envelope retrigger
        ld      a,13
        call    vho_out
        ret

; vho_out -- AY[A] := (HL), via the latched ports. Preserves A (reg index), HL.
vho_out:
        push    hl
        ld      d,a                     ; save register index
        ld      bc,(ay_sel)
        out     (c),a                   ; latch register address (= reg index)
        ld      a,(hl)                  ; value
        ld      bc,(ay_dat)
        out     (c),a                   ; write data
        ld      a,d                     ; restore register index
        pop     hl
        ret

; sfx_merge -- overlay the live channel-C sound effect onto asm_VT_AYREGS (the
; player keeps channels A+B). Sets amp C = _asfx_vol, and either tone C (R4/R5 +
; mixer tone-C enable) or noise (R6 + mixer noise-C enable) per _asfx_kind. The
; player recomputes AYREGS every frame, so this overlay is naturally transient:
; when the effect ends (_asfx_vol==0) channel C returns to the music. The AY
; mixer is active-LOW (a 0 bit enables that source). Clobbers A,B.
sfx_merge:
        ld      a,(_asfx_vol)
        ld      (asm_VT_AYREGS+10),a    ; amp C = SFX volume (fixed, no envelope)
        ld      a,(asm_VT_AYREGS+7)
        ld      b,a                     ; B = player's mixer byte
        ld      a,(_asfx_kind)
        or      a
        jr      nz,sm_noise
        ; tone: enable tone C (bit2=0), disable noise C (bit5=1)
        ld      a,b
        and     0xFB
        or      0x20
        ld      (asm_VT_AYREGS+7),a
        ld      a,(_asfx_tper)          ; tone C fine   (R4)
        ld      (asm_VT_AYREGS+4),a
        ld      a,(_asfx_tper+1)        ; tone C coarse (R5)
        ld      (asm_VT_AYREGS+5),a
        ret
sm_noise:
        ; noise: disable tone C (bit2=1), enable noise C (bit5=0)
        ld      a,b
        or      0x04
        and     0xDF
        ld      (asm_VT_AYREGS+7),a
        ld      a,(_asfx_nper)          ; noise period (shared R6)
        ld      (asm_VT_AYREGS+6),a
        ret

; void pt3_init(void) -- load the module + reset the player to the start.
_pt3_init:
        push    ix
        push    iy
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_in           ; map bank 4 ($C000) while the player reads
        ld      hl,$C000                ; HL = banked module address
        ELSE
        ld      hl,_spectrumizer_pt3    ; HL = resident module address
        ENDIF
        call    asm_VT_INIT
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_out          ; restore page 7 (shadow screen)
        ENDIF
        pop     iy
        pop     ix
        ret

; void pt3_play_safe(void) -- advance the player one 50 Hz frame.
_pt3_play_safe:
        push    ix
        push    iy
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_in           ; bank 4 in for the whole tick
        ENDIF
        call    asm_VT_PLAY
        ld      a,(asm_VT_SETUP)
        bit     7,a                     ; player passed the PT3 loop/end point
        jr      z,pt3_play_done
        IFDEF   ZX128_PAGE_FLIP
        ld      hl,$C000                ; restart from the banked module base
        ELSE
        ld      hl,_spectrumizer_pt3    ; restart from the beginning next tick
        ENDIF
        call    asm_VT_INIT
pt3_play_done:
        IFDEF   ZX128_PAGE_FLIP
        call    zx128_tune_out          ; restore page 7
        ENDIF
        pop     iy
        pop     ix
        ret

; void pt3_mute(void) -- silence all AY channels.
_pt3_mute:
        push    ix
        push    iy
        call    asm_VT_MUTE
        pop     iy
        pop     ix
        ret
