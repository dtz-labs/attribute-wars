;============================================================
; ZX Spectrum / Timex machine and AY detector
;
; Reference detector supplied by @mpasternak79, 2026-06-23.
; Not linked by build.sh yet. Keep this file as the canonical reference for a
; future machine/AY status API and any later rewrite of the SOUND menu probing.
;
; Call:
;       CALL detect_machine_ay
;
; Returns:
;       A = machine:
;           0 = ZX Spectrum 48K / Spectrum+ 48K
;           1 = ZX Spectrum 128K
;           2 = Timex TC2048
;           3 = Timex TS2068 / TC2068
;
;       B = AY configuration:
;           0 = no AY detected
;           1 = AY at ZX128/Melodik ports:
;                   select/read = $FFFD
;                   write       = $BFFD
;           2 = AY at Timex ports:
;                   select      = $F5
;                   read/write  = $F6
;
; Destroys:
;       AF, BC, DE
;
; Preserves:
;       HL, IX, IY
;
; Important:
;   Run before starting an AY player and preferably with interrupts disabled.
;   The AY register contents are restored, but register 11 remains selected
;   because the currently selected AY register cannot be read back.
;
; Detection assumptions:
;   * factory-standard machines;
;   * TC2048 AY expansions use ZX128-compatible ports;
;   * a ZX48 with a Melodik-style AY will look like a ZX128;
;   * a TC2048 modified with AY at $F5/$F6 will look like a 2068.
;============================================================

M_ZX48          equ 0
M_ZX128         equ 1
M_TC2048        equ 2
M_TIMEX2068     equ 3

AY_NONE         equ 0
AY_ZX_PORTS     equ 1
AY_TIMEX_PORTS  equ 2


;------------------------------------------------------------
; Main detector
;
; Output:
;       A = M_...
;       B = AY_...
;------------------------------------------------------------

detect_machine_ay:
        call    probe_timex_scld
        or      a
        jr      z,dma_sinclair

        ; It is a Timex-family machine.
        ; Factory TS/TC2068 has AY at F5/F6.
        call    probe_ay_timex
        or      a
        jr      z,dma_tc2048

        ld      a,M_TIMEX2068
        ld      b,AY_TIMEX_PORTS
        ret

dma_tc2048:
        ; A factory TC2048 has no AY, but common expansions
        ; use the ZX128/Melodik port mapping.
        call    probe_ay_tc2048
        or      a
        jr      z,dma_tc2048_no_ay

        ld      a,M_TC2048
        ld      b,AY_ZX_PORTS
        ret

dma_tc2048_no_ay:
        ld      a,M_TC2048
        ld      b,AY_NONE
        ret

dma_sinclair:
        ; On factory Sinclair machines an AY at FFFD/BFFD
        ; indicates a 128K machine.
        call    probe_ay_zx
        or      a
        jr      z,dma_zx48

        ld      a,M_ZX128
        ld      b,AY_ZX_PORTS
        ret

dma_zx48:
        ld      a,M_ZX48
        ld      b,AY_NONE
        ret


;------------------------------------------------------------
; Detect Timex SCLD
;
; Timex port FF is a readable/writable control register.
; Spectrum port FF behaves as an unattached/floating-bus port.
;
; Only bits 3..5 are changed, so the screen mode, interrupt control and
; EXROM/DOCK selection are preserved.
;
; Returns:
;       A = 1  Timex SCLD detected
;       A = 0  no Timex SCLD
;
; Destroys:
;       AF, BC, DE
;------------------------------------------------------------

probe_timex_scld:
        ld      bc,$00ff
        in      a,(c)
        ld      d,a                 ; original port FF value

        ; Pattern 1: toggle bit 3
        xor     $08
        ld      e,a
        out     (c),a
        in      a,(c)
        cp      e
        jr      nz,pts_no

        ; Pattern 2: toggle bit 4
        ld      a,d
        xor     $10
        ld      e,a
        out     (c),a
        in      a,(c)
        cp      e
        jr      nz,pts_no

        ; Pattern 3: toggle bit 5
        ld      a,d
        xor     $20
        ld      e,a
        out     (c),a
        in      a,(c)
        cp      e
        jr      nz,pts_no

        ; Restore original SCLD state
        ld      a,d
        out     (c),a

        ld      a,1
        ret

pts_no:
        ; Harmless on a Spectrum; restores state on a Timex.
        ld      a,d
        out     (c),a

        xor     a
        ret


;------------------------------------------------------------
; Detect AY at Timex TS/TC2068 ports
;
;       OUT $F5 = select AY register
;       IN  $F6 = read AY register
;       OUT $F6 = write AY register
;
; Register 11 is fully writable. Its original value is restored.
;
; Returns:
;       A = 1  AY detected
;       A = 0  no AY detected
;
; Leaves AY register 11 selected.
;
; Destroys:
;       AF, BC, D
;------------------------------------------------------------

probe_ay_timex:
        ld      bc,$00f5
        ld      a,11
        out     (c),a               ; select AY register 11

        inc     c                   ; BC = $00F6
        in      a,(c)
        ld      d,a                 ; save original R11

        ld      a,$55
        out     (c),a
        in      a,(c)
        cp      $55
        jr      nz,pat_no

        ld      a,$aa
        out     (c),a
        in      a,(c)
        cp      $aa
        jr      nz,pat_no

        ld      a,$3c
        out     (c),a
        in      a,(c)
        cp      $3c
        jr      nz,pat_no

        ; Restore original register value
        ld      a,d
        out     (c),a

        ld      a,1
        ret

pat_no:
        ld      a,d
        out     (c),a

        xor     a
        ret


;------------------------------------------------------------
; Detect AY at ZX Spectrum 128 / Melodik ports
;
;       OUT $FFFD = select AY register
;       IN  $FFFD = read AY register
;       OUT $BFFD = write AY register
;
; Register 11 is fully writable. Its original value is restored.
;
; Returns:
;       A = 1  AY detected
;       A = 0  no AY detected
;
; Leaves AY register 11 selected.
;
; Destroys:
;       AF, BC, D
;------------------------------------------------------------

probe_ay_zx:
        ld      bc,$fffd
        ld      a,11
        out     (c),a               ; select AY register 11

        in      a,(c)
        ld      d,a                 ; save original R11

        ; Test pattern $55
        ld      b,$bf               ; BC = $BFFD
        ld      a,$55
        out     (c),a

        ld      b,$ff               ; BC = $FFFD
        in      a,(c)
        cp      $55
        jr      nz,paz_no

        ; Test pattern $AA
        ld      b,$bf
        ld      a,$aa
        out     (c),a

        ld      b,$ff
        in      a,(c)
        cp      $aa
        jr      nz,paz_no

        ; Test pattern $3C
        ld      b,$bf
        ld      a,$3c
        out     (c),a

        ld      b,$ff
        in      a,(c)
        cp      $3c
        jr      nz,paz_no

        ; Restore original register value
        ld      b,$bf
        ld      a,d
        out     (c),a

        ld      a,1
        ret

paz_no:
        ld      b,$bf
        ld      a,d
        out     (c),a

        xor     a
        ret


;------------------------------------------------------------
; Detect an added AY in a TC2048
;
; Standard TC2048 AY expansions generally use the ZX128/Melodik mapping, so
; this is the same hardware test. Call only after the SCLD/model test
; identifies a TC2048.
;
; Returns:
;       A = 1  AY present at $FFFD/$BFFD
;       A = 0  no AY present
;------------------------------------------------------------

probe_ay_tc2048:
        jp      probe_ay_zx
