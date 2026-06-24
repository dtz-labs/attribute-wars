; music_ay.asm -- AY-3-8910 detection + dual-scheme hardware output for z88dk's
; VortexTracker2 PT3 player.
;
; WHY this file exists:
;   z88dk ships the VT2 player (ay_vt2_init/play/start/stop/mute + PT3PROM), but
;   its +zx hardware-out (libsrc/target/zx/psg/asm_vt2_hardware_out.asm) hardcodes
;   the 128K ports 0xFFFD/0xBFFD via an OUTI trick that needs the shared 0xFD low
;   byte -- which the TS2068 scheme (0xF5/0xF6) does NOT share. The player calls
;   asm_vt_hardware_out / _A0 (both EXTERN in PT3PROM.asm) to emit the 14 AY
;   registers from asm_VT_AYREGS. By DEFINING those symbols here, the linker takes
;   our version and never pulls the library module -- so we route the writes
;   through ports detected at runtime, supporting 128K/+2/+3 AND TS2068/TC2068.
;
; Also here:
;   _ay_detect  -- detect a 2068 and latch its AY ports. C calls it once.
;   _pt3_play_safe -- IY-preserving shim around ay_vt2_play (which trashes IY).
;
; Conventions (mirror blit.asm/sfx.asm): SECTION code_user; never touch IY except
; to save/restore it; parameterless; only AY ports are written (never 0xFF/0xFE).

        SECTION code_user

        PUBLIC  _ay_detect              ; u8  ay_detect(void) -> 1 if an AY answered
        PUBLIC  _music_im2_init          ; void music_im2_init(void): switch to IM2
        PUBLIC  _music_im1_init          ; void music_im1_init(void): switch to IM1
        PUBLIC  _ay_default_sound        ; u8  ay_default_sound(void): menu default
        PUBLIC  _ay_machine_status       ; u8  ay_machine_status(void): machine|AY
        PUBLIC  _ay_sfx_out              ; void ay_sfx_out(void): FX-only channel C
        PUBLIC  _ay_sfx_mute             ; void ay_sfx_mute(void): silence FX-only C

; PT3 music player symbols -- only when the full player is linked
; (omitted in ZX128_NO_MUSIC builds to stay under $C000)
        IFNDEF  ZX128_NO_MUSIC
        PUBLIC  _pt3_init               ; void pt3_init(void): load+init the tune
        PUBLIC  _pt3_play_safe          ; void pt3_play_safe(void): IY-safe play
        PUBLIC  _pt3_mute               ; void pt3_mute(void): silence the AY
        PUBLIC  asm_vt_hardware_out     ; override of z88dk's 128K-only output
        PUBLIC  asm_vt_hardware_out_A0
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
        ENDIF

        EXTERN  _music_tick             ; C: play one frame + decay SFX (ISR calls it)

        ; channel-C sound-effect state (music.c); overlaid by sfx_merge below
        EXTERN  _asfx_vol               ; u8  0=inactive, else amplitude 1..15
        EXTERN  _asfx_kind              ; u8  0=tone, 1=noise
        EXTERN  _asfx_tper              ; u16 tone period (R4/R5)
        EXTERN  _asfx_nper              ; u8  noise period (R6)

; ---------------------------------------------------------------------------
; Latched by _ay_detect; read by the output path. Local (not PUBLIC).
; ---------------------------------------------------------------------------
ay_sel: defw    0
ay_dat: defw    0
ay_rd:  defw    0

; "Timex" exactly as it sits at HOME-ROM offset 0x113D on a TS2068/TC2068 (the
; "(c) 1983 Timex Computer Corp" line). A TC2048 (modified-Spectrum ROM) has Z80
; code at that offset, never this string -- so it identifies a 2068 with a pure
; ROM read, touching no port. Verified against ZEsarUX's ts2068.rom/tc2048.rom.
sig_timex:
        defb    'T','i','m','e','x'
SIG_ADDR equ 0x113D

; ---------------------------------------------------------------------------
; u8 ay_detect(void) -- identify the machine RELIABLY, latch the AY ports, and
; return 1 if music can play (else 0 = beeper). Order matters:
;   1. ROM "Timex" at 0x113D -> TS2068/TC2068 -> AY at 0xF5/0xF6.
;   2. Else, is this a Timex (SCLD)? IN 0xFF echoes the last OUT on a TC2048/2068
;      but floats on a 48K/128K. A Timex that is NOT a 2068 is a TC2048 -> BEEPER.
;      We must NOT probe 0xFFFD on a TC2048: emulators answer that probe with no
;      real AY behind it, which would wrongly silence the beeper.
;   3. Else (48K / ZX 128), the 0xFFFD/0xBFFD probe IS reliable: a 128K answers
;      (real AY) -> use it; a bare 48K does not -> beeper.
; All steps are side-effect-safe (ROM reads; 0xFF only ever 0x00/0x01; 0xFFFD/
; 0xBFFD are odd, never the ULA).
; ---------------------------------------------------------------------------
_ay_detect:
        ; --- step 1: ROM signature -> TC2068 ---
        ld      hl,SIG_ADDR
        ld      de,sig_timex
        ld      b,5
sig_cmp:
        ld      a,(de)
        cp      (hl)
        jr      nz,not_2068             ; mismatch -> not a 2068
        inc     hl
        inc     de
        djnz    sig_cmp
        ld      de,0x00F5               ; confirmed 2068 -> 0xF5 (sel) / 0xF6 (dat+rd)
        ld      (ay_sel),de
        ld      bc,0x00F6
        ld      (ay_dat),bc
        ld      (ay_rd),bc
        ld      a,1
        ld      l,a                     ; sdcc returns 8-bit values in L
        ret
not_2068:
        ; Not a 2068 -> default to BEEPER. We deliberately do NOT auto-probe
        ; 0xFFFD: emulators answer that probe with no real AY behind it, which
        ; wrongly silences the beeper and (worse) pushes a TC2048 into the AY/IM2
        ; path -> crash. AY on a ZX 128 is enabled by the title-screen SOUND menu
        ; (the human knows their machine); see _ay_set_ports_std.
det_beeper:
        xor     a
        ld      l,a                     ; sdcc returns 8-bit values in L
        ret

; void ay_set_ports_std(void) -- latch the standard 0xFFFD/0xBFFD AY (for the
; SOUND menu: a ZX 128 the user explicitly switched to music/fx). Odd ports, so
; safe on every machine even if no AY is actually there (just silent).
        PUBLIC  _ay_set_ports_std
_ay_set_ports_std:
        ld      de,0xFFFD
        ld      (ay_sel),de
        ld      bc,0xBFFD
        ld      (ay_dat),bc
        ld      bc,0xFFFD
        ld      (ay_rd),bc
        ret

; u8 ay_default_sound(void) -- return the title-screen default SOUND choice:
;   SOUND_MUSIC_FX (1) on a ROM-confirmed 2068 or standard AY machine,
;   SOUND_BEEPER   (0) on a TC2048 or anything else.
; This never enables IM2 and never touches 0xF5/0xF6 unless the ROM signature
; already proved a 2068. The standard AY probe is gated behind "not Timex SCLD",
; so ZEsarUX's TC2048 false-positive cannot select MUSIC+FX by default.
_ay_default_sound:
        call    _ay_detect              ; 2068? latches 0xF5/0xF6, returns A/L=1
        or      a
        jr      nz,ads_music
        call    scld_present_p          ; TC2048/2068 SCLD? 2068 was ruled out
        or      a
        jr      nz,ads_beeper           ; TC2048 -> BEEPER
        call    _ay_set_ports_std       ; ZX 128 / 48K+AY: odd ports, ULA-safe
        call    ay_probe
        or      a
        jr      z,ads_beeper
ads_music:
        ld      a,1                     ; SOUND_MUSIC_FX
        ld      l,a
        ret
ads_beeper:
        xor     a                       ; SOUND_BEEPER
        ld      l,a
        ret

; u8 ay_machine_status(void) -- packed title-screen hardware status:
;   low nibble  = machine: 0 ZX48, 1 ZX128, 2 TC2048, 3 TC2068/TS2068
;   high nibble = AY:      0 none, 1 standard ZX128/Melodik, 2 Timex 2068
; This follows the same conservative rules as _ay_default_sound: never probe
; 0xFFFD on a TC2048 because ZEsarUX can false-positive there.
_ay_machine_status:
        call    _ay_detect              ; ROM-confirmed 2068?
        or      a
        jr      z,ams_not_2068
        ld      a,0x23                  ; machine=3, AY=2
        ld      l,a
        ret
ams_not_2068:
        call    scld_present_p
        or      a
        jr      z,ams_sinclair
        ld      a,0x02                  ; machine=2, AY=0
        ld      l,a
        ret
ams_sinclair:
        call    _ay_set_ports_std
        call    ay_probe
        or      a
        jr      z,ams_zx48
        ld      a,0x11                  ; machine=1, AY=1
        ld      l,a
        ret
ams_zx48:
        xor     a                       ; machine=0, AY=0
        ld      l,a
        ret

; scld_present_p -- A=1 if a Timex SCLD answers port 0xFF (it returns the last
; byte written; a 48K/128K floats). Writes only 0x01/0x00 (bits 6-7 stay 0) and
; leaves it at 0x00 (= show screen A, matching scld_init). Clobbers A,BC.
scld_present_p:
        ld      bc,0x00FF
        ld      a,0x01
        out     (c),a                   ; OUT 0xFF, 0x01
        in      a,(c)                   ; IN  0xFF -> 0x01 on a Timex
        cp      0x01
        jr      nz,scld_no
        ld      bc,0x00FF
        xor     a
        out     (c),a                   ; OUT 0xFF, 0x00 (back to screen A)
        in      a,(c)
        or      a                       ; echoes 0x00 ?
        jr      nz,scld_no
        ld      a,1
        ret
scld_no:
        xor     a
        ret

; ay_probe -- ROBUST presence test on the latched ports. A=1 only if a real AY
; is there, else 0. Two independent checks:
;   1. R1 (coarse tone A) is a 4-BIT register: writing 0xFF reads back 0x0F on a
;      real AY, but 0xFF (or garbage) on a floating bus / ULA echo. This is the
;      key discriminator -- it rejects the false positive that the old two-byte
;      round-trip suffered on an emulator's floating bus (and that, on the TS2068
;      scheme, drove the player's writes into port 0xF6 == the ULA on a TC2048).
;   2. R0 (8-bit) must still round-trip 0x55 and 0xAA.
; Restores R0=R1=0 on success. Clobbers A,BC.
ay_probe:
        ld      b,1                     ; R1 = coarse tone A (4-bit)
        ld      c,0xFF
        call    sel_write
        ld      b,1
        call    sel_read
        cp      0x0F                    ; real AY masks 0xFF -> 0x0F
        jr      nz,probe_no
        ld      b,0                     ; R0 = fine tone A (8-bit)
        ld      c,0x55
        call    sel_write
        ld      b,0
        call    sel_read
        cp      0x55
        jr      nz,probe_no
        ld      b,0
        ld      c,0xAA
        call    sel_write
        ld      b,0
        call    sel_read
        cp      0xAA
        jr      nz,probe_no
        ld      b,0                     ; tidy: R0=0
        ld      c,0
        call    sel_write
        ld      b,1                     ; tidy: R1=0
        ld      c,0
        call    sel_write
        ld      a,1
        ret
probe_no:
        xor     a
        ret

; sel_write -- select reg B, write value C (via ay_sel/ay_dat). Clobbers A,BC.
sel_write:
        ld      a,b
        push    bc
        ld      bc,(ay_sel)
        out     (c),a                   ; latch register address
        pop     bc
        ld      a,c
        ld      bc,(ay_dat)
        out     (c),a                   ; write data
        ret

; sel_read -- select reg B, A := its value (via ay_sel/ay_rd). Clobbers A,BC.
sel_read:
        ld      a,b
        ld      bc,(ay_sel)
        out     (c),a                   ; latch register address
        ld      bc,(ay_rd)
        in      a,(c)                   ; read data
        ret

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

; ---------------------------------------------------------------------------
; PT3 player wrappers and AY register output -- only when the full player is
; linked (omitted in ZX128_NO_MUSIC builds where pt3prom.asm/tune.asm are not
; included). In FX-only (ZX128) mode, ay_sfx_out writes channel C directly
; without needing asm_VT_AYREGS or the sfx_merge overlay.
; ---------------------------------------------------------------------------
        IFNDEF  ZX128_NO_MUSIC

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

; C-callable wrappers around the vendored player. All save/restore IX and IY:
; the player trashes both (it is built for an ISR that saves the world), and IY
; is the sdcc_iy frame pointer. Parameterless (project convention); the tune
; address is wired in directly from tune.asm.

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

        ENDIF   ; !ZX128_NO_MUSIC

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
