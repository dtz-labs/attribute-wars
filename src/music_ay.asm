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
;   _ay_detect  -- probe both schemes, latch the answering ports. C calls it once.
;   _pt3_play_safe -- IY-preserving shim around ay_vt2_play (which trashes IY).
;
; Conventions (mirror blit.asm/sfx.asm): SECTION code_user; never touch IY except
; to save/restore it; parameterless; only AY ports are written (never 0xFF/0xFE).

        SECTION code_user

        PUBLIC  _ay_detect              ; u8  ay_detect(void) -> 1 if an AY answered
        PUBLIC  _pt3_init               ; void pt3_init(void): load+init the tune
        PUBLIC  _pt3_play_safe          ; void pt3_play_safe(void): IY-safe play
        PUBLIC  _pt3_mute               ; void pt3_mute(void): silence the AY
        PUBLIC  asm_vt_hardware_out     ; override of z88dk's 128K-only output
        PUBLIC  asm_vt_hardware_out_A0

        EXTERN  asm_VT_AYREGS           ; 14-byte computed AY register file (player)
        EXTERN  asm_VT_INIT             ; vendored player: init, module addr in HL
        EXTERN  asm_VT_PLAY             ; vendored player: play one frame
        EXTERN  asm_VT_MUTE             ; vendored player: silence all channels
        EXTERN  _spectrumizer_pt3       ; tune.asm: the PT3 module (label = addr)

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
; u8 ay_detect(void) -- return 1 (and latch the AY ports) if music can play.
; Two SAFE steps, in order:
;   1. Probe the standard 0xFFFD/0xBFFD AY. Both are ODD ports, so they can
;      never be the ULA (which decodes EVEN ports) -- safe on every machine.
;      Covers a ZX 128/+2/+3 and any TC2048/48K with a standard AY interface.
;   2. If absent, ROM-signature the machine: a TS2068/TC2068 has ASCII "Timex"
;      at 0x113D; a TC2048 does not. ROM reads have no side effects, so a TC2048
;      is never disturbed. ONLY on a confirmed 2068 do we enable the AY at
;      0xF5/0xF6 -- there 0xF6 is the AY; on a TC2048 it would be the ULA, so we
;      must be certain before touching it.
; Returns 0 (stay silent) on a beeper-only machine.
; ---------------------------------------------------------------------------
_ay_detect:
        ; --- step 1: standard location (odd ports, ULA-safe) ---
        ld      de,0xFFFD
        ld      (ay_sel),de
        ld      bc,0xBFFD
        ld      (ay_dat),bc
        ld      bc,0xFFFD
        ld      (ay_rd),bc
        call    ay_probe
        or      a
        ret     nz                      ; standard AY found -> use it (A=1)

        ; --- step 2: ROM signature for a TS2068/TC2068 ---
        ld      hl,SIG_ADDR
        ld      de,sig_timex
        ld      b,5
sig_cmp:
        ld      a,(de)
        cp      (hl)
        jr      nz,det_fail             ; mismatch -> not a 2068
        inc     hl
        inc     de
        djnz    sig_cmp
        ; confirmed TS2068/TC2068 -> latch its AY at 0xF5 (select) / 0xF6 (data+read)
        ld      de,0x00F5
        ld      (ay_sel),de
        ld      bc,0x00F6
        ld      (ay_dat),bc
        ld      (ay_rd),bc
        ld      a,1
        ret
det_fail:
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
; asm_vt_hardware_out [_A0] -- OVERRIDE of z88dk's output stage. Emit AY
; registers from asm_VT_AYREGS via the latched ports. Faithful to the library
; contract: the plain entry starts at register 0; the _A0 entry starts at the
; register index already in A; register 13 (envelope) is skipped when its byte
; has bit 7 set (the PT3 "don't retrigger the envelope" sentinel).
; ---------------------------------------------------------------------------
asm_vt_hardware_out:
        xor     a                       ; start at register 0
asm_vt_hardware_out_A0:
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

; ---------------------------------------------------------------------------
; C-callable wrappers around the vendored player. All save/restore IX and IY:
; the player trashes both (it is built for an ISR that saves the world), and IY
; is the sdcc_iy frame pointer. Parameterless (project convention); the tune
; address is wired in directly from tune.asm.
; ---------------------------------------------------------------------------
; void pt3_init(void) -- load the module + reset the player to the start.
_pt3_init:
        push    ix
        push    iy
        ld      hl,_spectrumizer_pt3    ; HL = module address (asm_VT_INIT arg)
        call    asm_VT_INIT
        pop     iy
        pop     ix
        ret

; void pt3_play_safe(void) -- advance the player one 50 Hz frame.
_pt3_play_safe:
        push    ix
        push    iy
        call    asm_VT_PLAY
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
