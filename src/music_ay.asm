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
; Candidate port schemes, tried in order. Triple = (select, data, read).
;   128K/+2/+3 (and 48K+AY): select 0xFFFD, write 0xBFFD, read 0xFFFD
;   TS2068 / TC2068:         select 0x00F5, write 0x00F6, read 0x00F6
; A zero select terminates the table.
; ---------------------------------------------------------------------------
ay_pairs:
        defw    0xFFFD, 0xBFFD, 0xFFFD
        defw    0x00F5, 0x00F6, 0x00F6
        defw    0x0000, 0x0000, 0x0000

; Latched once by _ay_detect; read by the output path. Local (not PUBLIC).
ay_sel: defw    0
ay_dat: defw    0
ay_rd:  defw    0

; ---------------------------------------------------------------------------
; u8 ay_detect(void) -- probe each scheme; on the first that echoes a written
; pattern back from R0 (twice, to defeat the floating bus), latch its ports and
; return 1. Return 0 if none answers (beeper-only machine -> silent music).
; ---------------------------------------------------------------------------
_ay_detect:
        ld      hl,ay_pairs
det_next:
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      a,e
        or      d
        jr      z,det_fail              ; select==0 -> end of table
        ld      (ay_sel),de
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        inc     hl
        ld      (ay_dat),bc
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        inc     hl
        ld      (ay_rd),bc
        push    hl                      ; save table cursor
        call    ay_probe                ; A=1 if both patterns echo
        pop     hl
        or      a
        jr      nz,det_ok
        jr      det_next
det_ok:
        ld      a,1
        ret
det_fail:
        xor     a
        ret

; ay_probe -- with the latched ports: R0:=0x55 read-back, then R0:=0xAA
; read-back. A=1 if both echoed, else 0. Restores R0=0 on success.
ay_probe:
        ld      b,0
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
        ld      b,0
        ld      c,0
        call    sel_write               ; tidy: R0=0
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
