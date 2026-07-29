; blit.asm -- 8x8 sprite blitter, asm. DRAW uses PRE-SHIFTED sprite data so there
; is no per-row shift at draw time (the old hot path). The C side pre-shifts each
; sprite once into a 128-byte table: 8 shift slices x 8 rows x {left,right} byte.
;
; Parameterless (spec 15.5): reads globals. Never touches IY (sdcc_iy frame
; pointer). Interrupts stay on (no SP tricks).
;
; ---------------------------------------------------------------------------
; CHAR-BOUNDARY SPLIT (scanline advance optimisation)
; ---------------------------------------------------------------------------
; ZX scanline addressing: WITHIN one character cell (8 pixel rows) the next
; scanline is a bare `inc h` (4 T). The full arithmetic (l += 32; maybe h -= 8)
; is only needed on the once-per-8-lines cell boundary. An 8-row sprite crosses
; that boundary AT MOST ONCE, so instead of paying the old ~27 T per-row `DOWN`
; test we split each row loop into TWO djnz segments joined by a SINGLE boundary
; step:
;
;   rows_a  = 8 - (y & 7)            ; rows from the start row down to the cell edge
;   count_a = min(rows_a, rows)      ; segment A: cheap `inc h` between rows
;   count_b = rows - count_a         ; segment B: cheap `inc h` between rows
;
;   * full sprite (y <= 184): rows = 8  -> count_a = rows_a, count_b = y & 7
;   * bottom-clipped (y > 184): rows = 192 - y = rows_a -> count_b = 0
;   * bullets (cap 3): rows = min(rows,3) -> count_a = min(rows_a,rows)
;
; When count_b > 0 the extra `inc h` that segment A runs after its LAST row lands
; exactly on the cell boundary (h & 7 == 0, because count_a == rows_a there), so
; the boundary step degenerates to the tail of the old DOWN (l += 32; if no
; third-carry h -= 8) -- the `and 7 / jr nz` test is provably skippable. Segment
; B (<= 7 rows) never reaches another boundary, so it is pure `inc h`. When
; count_b == 0 there is no boundary step at all (the trailing `inc h` is
; harmless, HL is not reused after RET).
;
; setup() computes rows (B) and rows_a (scratch t_ra). The row count is turned
; into the two segment lengths just before the loop:
;   * SPLITS (sprites): rows is never clamped, so count_a == rows_a ALWAYS and
;     count_b == rows - rows_a >= 0 -- a branchless split.
;   * SPLIT  (bullets): rows is clamped to 3 first, so count_a == min(rows_a,rows)
;     needs the comparison branch.
; Both leave B = count_a and hold count_b in a FREE register: C for the OR/AND
; blits (sh is dead by then), E for the zero-fill erase (C is its fill byte).
;
; Each row loop ends with SEGTAIL, which either RETs (count_b == 0) or performs
; the one boundary step, reloads B = count_b, zeroes the count_b register, and
; re-enters the same loop body for segment B.
;
; The horizontal step in the "wide" (two-byte) bodies uses inc l / dec l rather
; than inc hl / dec hl: the wide path is gated on bx < 31, and L = RRRCCCCC has
; the byte-column bx in its low 5 bits, so bx+1 <= 31 never carries into the RRR
; scanline bits. This holds on Timex (0x4000/0x6000 -- the screen half lives in H)
; and on the ZX128 page-flip build alike.

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

; SPLITS -- sprite (never row-clamped) split. B (= rows) -> B = count_a = rows_a,
; `dst` = count_b = rows - rows_a (>= 0). Branchless: count_a is always rows_a.
; Clobbers A and dst; preserves everything else.
;   dst -- register to hold count_b (c or e)
        MACRO SPLITS dst
        ld      a,(t_ra)        ; rows_a
        ld      dst,a           ; dst = rows_a (count_a)
        ld      a,b             ; rows
        sub     dst             ; A = rows - rows_a = count_b
        ld      b,dst           ; B = count_a = rows_a
        ld      dst,a           ; dst = count_b
        ENDM

; SPLIT -- bullet split. B (= clamped total rows) -> B = count_a = min(rows_a,B),
; `dst` = count_b. Reads t_ra. Clobbers A and dst; preserves D/E (the shifted
; pattern) and HL.
;   dst -- register to hold count_b (c)
;   lge/ldone -- caller-supplied unique labels
        MACRO SPLIT dst, lge, ldone
        ld      a,(t_ra)        ; rows_a
        cp      b               ; rows_a - total
        jr      nc, lge         ; rows_a >= total -> count_a = total, count_b = 0
        ld      dst,a           ; dst = rows_a (count_a)
        ld      a,b
        sub     dst             ; A = total - rows_a = count_b
        ld      b,dst           ; B = count_a
        ld      dst,a           ; dst = count_b
        jr      ldone
lge:
        ld      dst,0           ; count_b = 0 (B stays = total = count_a)
ldone:
        ENDM

; SEGTAIL -- end of a row-loop segment. If count_b (in `reg`) is 0 we are done.
; Otherwise perform the single cell-boundary step on HL (the trailing `inc h`
; from the segment loop already advanced h onto the boundary, h & 7 == 0, so we
; only finish the address maths), reload B = count_b, zero `reg` so the next
; SEGTAIL hit returns, and re-enter the loop body for segment B.
;   reg -- register holding count_b (c or e)
;   seg -- loop-body entry label     bnd -- caller-supplied unique label
        MACRO SEGTAIL reg, seg, bnd
        ld      a,reg
        or      a
        ret     z
        ld      a,l
        add     a,32
        ld      l,a
        jr      c, bnd
        ld      a,h
        sub     8
        ld      h,a
bnd:
        ld      b,reg           ; B = count_b
        ld      reg,0           ; consume -> next SEGTAIL returns
        jr      seg
        ENDM

; ===========================================================================
; void blit8_asm(void) -- OR the pre-shifted sprite into the back buffer.
;   After setup: HL = screen addr, B = rows, C = sh; then DE = pre-shifted row
;   pointer. SPLITS at each loop head turns B/C into count_a/count_b.
; ===========================================================================
_blit8_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx/t_ra set; Z if rows==0
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

        SPLITS  c               ; C = count_b (sh no longer needed)
bp_wide:
        ld      a,(de)          ; left byte
        or      (hl)
        ld      (hl),a
        inc     l               ; wide: bx<31 -> inc l == inc hl (no RRR carry)
        inc     de
        ld      a,(de)          ; right byte
        or      (hl)
        ld      (hl),a
        dec     l
        inc     de
        inc     h               ; cheap intra-cell advance
        djnz    bp_wide
        SEGTAIL c, bp_wide, bpw_b

bp_narrow:
        SPLITS  c               ; C = count_b
bp_narrow_l:
        ld      a,(de)          ; left byte only
        or      (hl)
        ld      (hl),a
        inc     de
        inc     de              ; keep stride 2 (skip unused right byte)
        inc     h               ; cheap intra-cell advance
        djnz    bp_narrow_l
        SEGTAIL c, bp_narrow_l, bpn_b

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
;
; count_b lives in E here (C is the 0 fill byte).
; ===========================================================================
_erase8_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx/t_ra; Z if rows==0
        ret     z
        ld      a,(t_sh)
        or      a
        jr      z,erase_narrow
        ld      a,(t_bx)
        cp      31
        jr      nc,erase_narrow
        SPLITS  e               ; E = count_b
        ld      c,0             ; C = 0 fill byte (loops/SEGTAIL preserve C)
erase_wide:
        ld      (hl),c          ; left  byte = 0
        inc     l
        ld      (hl),c          ; right byte = 0
        dec     l
        inc     h
        djnz    erase_wide
        SEGTAIL e, erase_wide, erw_b

erase_narrow:
        SPLITS  e               ; E = count_b
        ld      c,0             ; C = 0 fill byte
erase_narrow_l:
        ld      (hl),c
        inc     h
        djnz    erase_narrow_l
        SEGTAIL e, erase_narrow_l, ern_b

; ===========================================================================
; CHEAP BULLET: a BUL_PAT-wide x 3-row dot. Reuses setup (addr/sh/bx), caps the
; row count at 3, shifts the fixed pattern by sh, and ORs/ANDs 3 rows. ~7x
; cheaper than a full sprite. BUL_PAT = 0xE0 (3 pixels). count_b lives in C.
; ===========================================================================
BUL_PAT equ 0xE0

; void bul_draw_asm(void)
_bul_draw_asm:
        call    setup           ; HL=addr, B=rows, C=sh, t_bx/t_ra; Z if 0
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
        jr      z,bd_narrow_pre
        ld      a,(t_bx)
        cp      31
        jr      nc,bd_narrow_pre
        SPLIT   c, bdw_ge, bdw_sp   ; C = count_b (sh consumed by pattern shift)
bd_wide:
        ld      a,d
        or      (hl)
        ld      (hl),a
        inc     l
        ld      a,e
        or      (hl)
        ld      (hl),a
        dec     l
        inc     h
        djnz    bd_wide
        SEGTAIL c, bd_wide, bdw_b
bd_narrow_pre:
        SPLIT   c, bdn_ge, bdn_sp   ; C = count_b
bd_narrow:
        ld      a,d
        or      (hl)
        ld      (hl),a
        inc     h
        djnz    bd_narrow
        SEGTAIL c, bd_narrow, bdn_b

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
        SPLIT   c, bew_ge, bew_sp   ; C = count_b
be_wide:
        ld      a,(hl)
        and     d
        ld      (hl),a
        inc     l
        ld      a,(hl)
        and     e
        ld      (hl),a
        dec     l
        inc     h
        djnz    be_wide
        SEGTAIL c, be_wide, bew_b
be_narrow_pre:
        ld      a,d
        cpl
        ld      d,a
        SPLIT   c, ben_ge, ben_sp   ; C = count_b
be_narrow:
        ld      a,(hl)
        and     d
        ld      (hl),a
        inc     h
        djnz    be_narrow
        SEGTAIL c, be_narrow, ben_b

; ===========================================================================
; setup -- from globals compute: t_sh, t_bx, t_ra=rows_a, B=rows, HL=start
; address, C=sh. Returns Z set if rows==0. Clobbers A,D,E,HL,BC.
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
        ld      a,d             ; y
        and     7
        ld      e,a
        ld      a,8
        sub     e               ; rows_a = 8 - (y & 7)
        ld      (t_ra),a

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

; Scratch RAM (program loads in RAM at 0x8000; these sit after the last RET).
t_sh:   defb    0
t_bx:   defb    0
t_ra:   defb    0               ; rows_a = 8 - (y & 7)  (set by setup)
