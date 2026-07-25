; ay_ports.asm -- AY-3-8910 machine detection and low-level register access.
;
; Owns the LATCHED PORT PAIR the whole AY stack writes through. Two schemes
; exist and they do not share a low byte, so the choice has to be made at
; runtime: 128K/+2/+3 use 0xFFFD (select) / 0xBFFD (data), while TS2068/TC2068
; use 0xF5 / 0xF6. Everything else here exists to pick between them safely.
;
; ay_sel/ay_dat/ay_rd and sel_write/sel_read are PUBLIC because ay_sfx.asm and
; pt3_glue.asm write registers through the same latched ports.
;
; Conventions (mirror blit.asm/sfx.asm): SECTION code_user; never touch IY except
; to save/restore it; parameterless; only AY ports are written (never 0xFF/0xFE).

        SECTION code_user

        PUBLIC  _ay_detect              ; u8  ay_detect(void) -> 1 if an AY answered
        PUBLIC  _ay_set_ports_std       ; void ay_set_ports_std(void)
        PUBLIC  _ay_default_sound       ; u8  ay_default_sound(void): menu default
        PUBLIC  _ay_machine_status      ; u8  ay_machine_status(void): machine|AY
        PUBLIC  ay_sel                  ; latched select port (ay_sfx/pt3_glue)
        PUBLIC  ay_dat                  ; latched data port
        PUBLIC  ay_rd                   ; latched read-back port
        PUBLIC  sel_write               ; select reg B, write value C
        PUBLIC  sel_read                ; select reg B, A := its value

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
