; blit.asm -- 8x8 sprite blitter, asm. DRAW uses PRE-SHIFTED sprite data so there
; is no per-row shift at draw time (the old hot path). The C side pre-shifts each
; sprite once into a 128-byte table: 8 shift slices x 8 rows x {left,right} byte.
;
; Parameterless (spec 15.5): reads globals. Never touches IY (sdcc_iy frame
; pointer). Interrupts stay on (no SP tricks).

        SECTION code_user

        PUBLIC  _blit8_asm
        PUBLIC  _erase8_asm
        PUBLIC  _bul_draw_asm
        PUBLIC  _bul_erase_asm

        EXTERN  _spr_base       ; u16  back-buffer base (0x4000 / 0x6000)
        EXTERN  _spr_x          ; u8   pixel x
        EXTERN  _spr_y          ; u8   pixel y (0..191)
        EXTERN  _spr_ptr        ; u16  pointer to 128-byte pre-shifted sprite
        IFNDEF  ZX128_PAGE_FLIP
        EXTERN  _scld_row_off   ; u16[192] interleaved scanline offsets
        ENDIF

; DOWN -- inline "HL := scanline below" (the old down_line subroutine, expanded
; at each row so the hot loops pay no call/ret per row). `skip` is a caller-
; supplied unique label. Common case (no cell-boundary cross) is inc h + test.
        MACRO DOWN skip
        inc     h
        ld      a,h
        and     7
        jr      nz, skip
        ld      a,l
        add     a,32
        ld      l,a
        jr      c, skip
        ld      a,h
        sub     8
        ld      h,a
skip:
        ENDM

; ===========================================================================
; void blit8_asm(void) -- OR the pre-shifted sprite into the back buffer.
;   HL = screen addr, DE = pre-shifted row pointer, B = rows, C = sh.
; ===========================================================================
_blit8_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx set; Z if rows==0
        ret     z
        push    hl              ; save screen addr
        ld      a,c             ; sh
        add     a,a
        add     a,a
        add     a,a
        add     a,a             ; sh * 16  (slice offset; sh<=7 -> <=112)
        ld      e,a
        ld      d,0
        ld      hl,(_spr_ptr)
        add     hl,de           ; HL = &preshift[sh*16]
        ex      de,hl           ; DE = pre-shifted row pointer
        pop     hl              ; HL = screen addr
        ld      a,c
        or      a
        jr      z,bp_narrow     ; sh==0 -> right byte is 0, skip it
        ld      a,(t_bx)
        cp      31
        jr      nc,bp_narrow    ; right edge -> clip the spilled byte

bp_wide:
        ld      a,(de)          ; left byte
        or      (hl)
        ld      (hl),a
        inc     hl
        inc     de
        ld      a,(de)          ; right byte
        or      (hl)
        ld      (hl),a
        dec     hl
        inc     de
        DOWN    dl_bpw
        djnz    bp_wide
        ret

bp_narrow:
        ld      a,(de)          ; left byte only
        or      (hl)
        ld      (hl),a
        inc     de
        inc     de              ; keep stride 2 (skip unused right byte)
        DOWN    dl_bpn
        djnz    bp_narrow
        ret

; ===========================================================================
; void erase8_asm(void) -- clear the sprite's 8x8 box.
;
; ZERO-FILL erase: the arena bitmap background is solid black and the render
; loop erases-all-then-draws-all every frame, so we blind-zero the sprite's
; byte column(s) rather than AND-masking the exact sprite bits. Any neighbour
; bits we over-clear get repainted by the draw pass the same frame. This drops
; the per-call nm0/nm1 shift-loop mask computation AND turns each row's two
; read-AND-write byte ops into plain stores. (Only ever called for player +
; enemy sprites, which clamp to [ARENA_T..ARENA_B] -> char rows 1..22, so it
; never reaches the HUD row 0 or the score-digit bitmap on row 23.)
; ===========================================================================
_erase8_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx; Z if rows==0
        ret     z
        ld      a,(t_sh)
        or      a
        jr      z,erase_narrow
        ld      a,(t_bx)
        cp      31
        jr      nc,erase_narrow
        ld      c,0             ; C = 0 fill byte (DOWN preserves C)
erase_wide:
        ld      (hl),c          ; left  byte = 0
        inc     hl
        ld      (hl),c          ; right byte = 0
        dec     hl
        DOWN    dl_erw
        djnz    erase_wide
        ret

erase_narrow:
        ld      c,0             ; C = 0 fill byte
erase_narrow_l:
        ld      (hl),c
        DOWN    dl_ern
        djnz    erase_narrow_l
        ret

; ===========================================================================
; CHEAP BULLET: a BUL_PAT-wide x 3-row dot. Reuses setup (addr/sh/bx), caps the
; row count at 3, shifts the fixed pattern by sh, and ORs/ANDs 3 rows. ~7x
; cheaper than a full sprite. BUL_PAT = 0xE0 (3 pixels).
; ===========================================================================
BUL_PAT equ 0xE0

; void bul_draw_asm(void)
_bul_draw_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx; Z if 0
        ret     z
        ld      a,b
        cp      3
        jr      c,bd_go
        ld      b,3             ; cap at 3 rows
bd_go:
        ld      a,BUL_PAT       ; shift the pattern: D=left, E=right(spill)
        ld      d,a
        ld      e,0
        ld      a,c
        or      a
        jr      z,bd_pat_done
bd_pat:
        srl     d
        rr      e
        dec     a
        jr      nz,bd_pat
bd_pat_done:
        ld      a,e             ; spill into the right byte?
        or      a
        jr      z,bd_narrow
        ld      a,(t_bx)
        cp      31
        jr      nc,bd_narrow
bd_wide:
        ld      a,d
        or      (hl)
        ld      (hl),a
        inc     hl
        ld      a,e
        or      (hl)
        ld      (hl),a
        dec     hl
        DOWN    dl_bdw
        djnz    bd_wide
        ret
bd_narrow:
        ld      a,d
        or      (hl)
        ld      (hl),a
        DOWN    dl_bdn
        djnz    bd_narrow
        ret

; void bul_erase_asm(void)
_bul_erase_asm:
        call    setup
        ret     z
        ld      a,b
        cp      3
        jr      c,be_go
        ld      b,3
be_go:
        ld      a,BUL_PAT
        ld      d,a
        ld      e,0
        ld      a,c
        or      a
        jr      z,be_pat_done
be_pat:
        srl     d
        rr      e
        dec     a
        jr      nz,be_pat
be_pat_done:
        ld      a,e             ; spill present? (decide wide BEFORE complementing)
        or      a
        jr      z,be_narrow_pre
        ld      a,(t_bx)
        cp      31
        jr      nc,be_narrow_pre
        ld      a,d             ; keep-masks = ~set-bits
        cpl
        ld      d,a
        ld      a,e
        cpl
        ld      e,a
be_wide:
        ld      a,(hl)
        and     d
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        and     e
        ld      (hl),a
        dec     hl
        DOWN    dl_bew
        djnz    be_wide
        ret
be_narrow_pre:
        ld      a,d
        cpl
        ld      d,a
be_narrow:
        ld      a,(hl)
        and     d
        ld      (hl),a
        DOWN    dl_ben
        djnz    be_narrow
        ret

; ===========================================================================
; setup -- from globals compute: t_sh, t_bx, B=rows, HL=start address, C=sh.
; Returns Z set if rows==0. Clobbers A,D,E,HL,BC.
; ===========================================================================
setup:
        ld      a,(_spr_x)
        ld      b,a             ; B = x (temp)
        and     7
        ld      (t_sh),a        ; sh
        ld      a,b
        srl     a
        srl     a
        srl     a
        ld      (t_bx),a        ; bx = x >> 3

        ld      a,(_spr_y)
        ld      d,a             ; D = y
        cp      185             ; y > 184 ?
        jr      c,setup_full
        ld      a,192
        sub     d               ; rows = 192 - y (bottom clip)
        jr      setup_rows
setup_full:
        ld      a,8
setup_rows:
        ld      b,a             ; B = rows

        IFDEF   ZX128_PAGE_FLIP
        ; Low-memory ZX128 build: save the 384-byte scld_row_off table and
        ; compute the standard Spectrum scanline offset directly:
        ;   ((y&0xC0)<<5) + ((y&0x07)<<8) + ((y&0x38)<<2)
        ld      a,d
        and     0x38
        add     a,a
        add     a,a
        ld      l,a
        ld      a,d
        and     0xC0
        rrca
        rrca
        rrca
        ld      h,a
        ld      a,d
        and     0x07
        add     a,h
        ld      h,a             ; HL = scanline offset
        ELSE
        ld      a,d             ; y
        ld      l,a
        ld      h,0
        add     hl,hl           ; y * 2
        ld      de,_scld_row_off
        add     hl,de
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl           ; HL = scld_row_off[y]
        ENDIF
        ld      de,(_spr_base)
        add     hl,de           ; + base
        ld      a,(t_bx)
        ld      e,a
        ld      d,0
        add     hl,de           ; + bx  -> HL = start address

        ld      a,(t_sh)
        ld      c,a             ; C = sh
        ld      a,b             ; rows -> set Z flag for caller
        or      a
        ret

; (down_line is now inlined via the DOWN macro at each row -- no call/ret.)

; Scratch RAM (program loads in RAM at 0x8000; these sit after the last RET).
t_sh:   defb    0
t_bx:   defb    0
