; sfx.asm -- 1-bit beeper square-wave generator for the Timex TC2048.
;
; sfx_blip: toggles ONLY bit 4 of ULA port 0xFE for sfx_dur half-periods,
; each of length sfx_hp (busy-loop count). All other port bits stay 0 so the
; border remains black (bits 0-2 = 0) and MIC (bit 3) is not disturbed.
;
; Output values used: 0x10 (bit 4 high = speaker on) and 0x00 (bit 4 low).
; No other bits are ever set -- border-safe by construction.
;
; Conventions (mirrors blit.asm):
;   SECTION code_user   -- lives in the same code segment as the blitter
;   PUBLIC _sfx_blip    -- exported with C-linkage underscore prefix
;   EXTERN _sfx_hp      -- u8 half-period delay count (read from C global)
;   EXTERN _sfx_dur     -- u8 half-periods to generate (read from C global)
;   Never touches IY    -- sdcc_iy uses IY as the frame pointer
;   Interrupts stay ON  -- called from the main loop, not from ISR
;   Parameterless       -- reads globals, returns void
;
; Timing (Z80A @ 3.5 MHz, one T = ~285 ns):
;   Each half-period = (13 + sfx_hp * 7) T approximately.
;   sfx_hp=16  -> ~125 T/half-period  -> ~28 kHz toggle (audible click burst)
;   sfx_hp=32  -> ~237 T/half-period  -> ~14.7 kHz toggle (low rumble)
;   The outer loop (djnz sfx_dur) adds ~13 T per half-period.
;   For short SFX (sfx_dur<=220) total T < ~60 k T -- safely within frame budget.

        SECTION code_user

        PUBLIC  _sfx_blip

        EXTERN  _sfx_hp     ; u8  half-period delay (C global, SDCC only)
        EXTERN  _sfx_dur    ; u8  half-period count  (C global, SDCC only)

ULA_PORT equ 0xFE           ; ULA port: bits 0-2=border, 3=MIC, 4=beeper

; ---------------------------------------------------------------------------
; void sfx_blip(void)
;
; Registers used: A, B, C  (all scratch; IY untouched).
; C tracks the current output byte (0x00 or 0x10) so we can toggle with XOR.
; ---------------------------------------------------------------------------
_sfx_blip:
        ld      a,(_sfx_dur)    ; outer loop count (number of half-periods)
        or      a
        ret     z               ; sfx_dur == 0: nothing to do

        ld      b,a             ; B = sfx_dur  (outer counter)
        ld      c,0x10          ; C = first output byte: bit 4 high

sfx_outer:
        ld      a,c
        out     (ULA_PORT),a    ; toggle the speaker (bit 4 only)

        ; Inner delay loop: each iteration = 7 T (dec B' + jr nz).
        ; We reuse A as the inner counter.
        ld      a,(_sfx_hp)     ; A = sfx_hp
        or      a
        jr      z,sfx_no_delay  ; sfx_hp == 0: skip inner loop

sfx_inner:
        dec     a               ; 4 T
        jr      nz,sfx_inner    ; 12 T (taken) / 7 T (not taken)
                                ; net per iteration: 7 T (taken: dec 4 + jr 12 - 2 = 10+...
                                ; actually dec=4T, jr nz taken=12T -> 16T total?
                                ; z80: dec r = 4T, jr nz = 12T taken, 7T not taken
                                ; so each iteration (except last) = 4+12 = 16T
                                ; last iteration = 4+7 = 11T

sfx_no_delay:
        ld      a,c
        xor     0x10            ; toggle: 0x10 <-> 0x00  (bit 4 only, no other bits)
        ld      c,a             ; save toggled value for next iteration

        djnz    sfx_outer       ; 13 T taken, 8 T not taken

        ; Ensure speaker ends on 0x00 (bit 4 low = silent, border stays black).
        xor     a               ; A = 0x00
        out     (ULA_PORT),a

        ret

; Scratch storage (sits after the last RET, in RAM at 0x8000+).
; sfx_hp and sfx_dur are C globals defined in sfx.c; no scratch bytes needed here.
