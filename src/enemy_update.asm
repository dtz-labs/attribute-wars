; enemy_update.asm -- hand-written enemies_update for the Z80 target.
;
; WHY: sdcc compiled the C enemies_update to ~351 instructions (158 ld). It
; spilled the per-enemy SKELETON -- alive-check, integrate, 4-wall bounce,
; write-back -- to the IX frame, costing ~1600 T per enemy even for a BOUNCE
; enemy with no AI (measured: bounce 9610 T, chase 10864, hunter 16353, for 6
; enemies). This version keeps the current enemy in IX and works in registers.
;
; The C version stays in enemy.c (compiled on the host, where it is the unit-
; tested reference) and is replaced by this asm only on the sdcc/__SDCC target.
; Verified byte-identical to the C reference over randomized frames (bench).
;
; enemy_t  : x(+0) y(+1) dx(+2) dy(+3) level(+4) alive(+5)   (6 bytes)
; bullet_t : x(+0) y(+1) dx(+2) dy(+3) active(+4)            (5 bytes)
; Inputs via globals (set by the thin C wrapper): _eu_es _eu_px _eu_py _eu_bs.
;
; Registers: IX = current enemy (saved/restored; IY is never touched -- it is
; the sdcc_iy reserved register). B = enemy countdown (djnz). C = dx, plus a
; 1-byte scratch t_dy for dy.

        SECTION code_user
        PUBLIC  _enemies_update_asm
        EXTERN  _eu_es          ; enemies_t* (-> e[0])
        EXTERN  _eu_bs          ; bullets_t* (-> b[0])
        EXTERN  _eu_px          ; u8 player x
        EXTERN  _eu_py          ; u8 player y

ARENA_L   equ 8
ARENA_R   equ 240
ARENA_T   equ 8
ARENA_B   equ 176
LVL_CHASE equ 2
LVL_HUNT  equ 3
DODGE     equ 32

_enemies_update_asm:
        push    ix
        ld      ix,(_eu_es)     ; ix -> e[0]
        ld      b,6             ; MAX_ENEMIES
eu_loop:
        ld      a,(ix+5)        ; alive?
        or      a
        jp      z,eu_next

        ld      a,(ix+4)        ; level
        cp      LVL_CHASE
        jr      z,eu_chase
        cp      LVL_HUNT
        jr      z,eu_hunter
        ; ---- BOUNCE: keep the stored dx/dy ----
        ld      c,(ix+2)        ; dx -> C
        ld      a,(ix+3)        ; dy -> t_dy
        ld      (t_dy),a
        jr      eu_move

eu_chase:
        ld      a,(_eu_px)
        ld      e,(ix+0)        ; ex
        call    sgn_sub         ; A = sgn(px-ex)
        ld      c,a
        ld      a,(_eu_py)
        ld      e,(ix+1)        ; ey
        call    sgn_sub         ; A = sgn(py-ey)
        ld      (t_dy),a
        jr      eu_move

eu_hunter:
        ; default = chase ...
        ld      a,(_eu_px)
        ld      e,(ix+0)
        call    sgn_sub
        ld      c,a
        ld      a,(_eu_py)
        ld      e,(ix+1)
        call    sgn_sub
        ld      (t_dy),a
        ; ... then flee the first bullet within DODGE on both axes (2 bullets).
        ld      hl,(_eu_bs)     ; b[0]
        call    dodge_test
        jr      z,eu_move       ; fled -> done (break)
        ld      hl,(_eu_bs)
        ld      de,5
        add     hl,de           ; b[1]
        call    dodge_test
        jr      eu_move         ; dir finalised either way

eu_move:
        ; ---- X: nx = ex + dx(C), clamp [L,R], reflect dx on a clamp ----
        ld      a,(ix+0)        ; ex
        add     a,c             ; + dx  (C = 0x01/0x00/0xFF)
        ld      e,a             ; E = nx
        cp      ARENA_L
        jr      nc,eu_x_hi
        ld      e,ARENA_L
        ld      a,c
        neg
        ld      c,a             ; dx = -dx
        jr      eu_y
eu_x_hi:
        ld      a,e
        cp      ARENA_R+1       ; nx > R ?
        jr      c,eu_y
        ld      e,ARENA_R
        ld      a,c
        neg
        ld      c,a
eu_y:
        ; ---- Y: ny = ey + dy(t_dy), clamp [T,B], reflect dy ----
        ld      a,(t_dy)
        ld      l,a             ; L = dy
        ld      a,(ix+1)        ; ey
        add     a,l
        ld      h,a             ; H = ny
        cp      ARENA_T
        jr      nc,eu_y_hi
        ld      h,ARENA_T
        ld      a,l
        neg
        ld      l,a
        jr      eu_store
eu_y_hi:
        ld      a,h
        cp      ARENA_B+1       ; ny > B ?
        jr      c,eu_store
        ld      h,ARENA_B
        ld      a,l
        neg
        ld      l,a
eu_store:
        ld      (ix+0),e        ; x  = nx
        ld      (ix+1),h        ; y  = ny
        ld      (ix+2),c        ; dx
        ld      (ix+3),l        ; dy
eu_next:
        ld      de,6
        add     ix,de           ; -> next enemy
        dec     b               ; (loop body is too long for djnz's range)
        jp      nz,eu_loop
        pop     ix
        ret

; ---------------------------------------------------------------------------
; dodge_test -- HL -> bullet. If active AND |bx-ex| < DODGE AND |by-ey| < DODGE,
; set C = sgn(ex-bx), (t_dy) = sgn(ey-by) and return Z SET (flee). Otherwise
; leave C/(t_dy) untouched and return Z CLEAR. ex=(ix+0) ey=(ix+1).
; Clobbers A,DE,HL,F. Preserves B, IX, and (on no-flee) C.
; ---------------------------------------------------------------------------
dodge_test:
        push    hl
        ld      de,4
        add     hl,de
        ld      a,(hl)          ; active (offset 4)
        pop     hl              ; hl -> bullet.x
        or      a
        jr      z,dt_no         ; inactive slot
        ld      a,(hl)          ; bx
        ld      e,(ix+0)        ; ex
        sub     e               ; bx - ex
        jr      nc,dt_adx
        neg                     ; |bx - ex|
dt_adx:
        cp      DODGE
        jr      nc,dt_no        ; adx >= DODGE
        inc     hl
        ld      a,(hl)          ; by
        dec     hl
        ld      e,(ix+1)        ; ey
        sub     e
        jr      nc,dt_ady
        neg                     ; |by - ey|
dt_ady:
        cp      DODGE
        jr      nc,dt_no
        ; flee: dir away from the bullet
        ld      a,(ix+0)        ; ex
        ld      e,(hl)          ; bx
        call    sgn_sub         ; sgn(ex-bx)
        ld      c,a
        ld      a,(ix+1)        ; ey
        inc     hl
        ld      e,(hl)          ; by
        dec     hl
        call    sgn_sub         ; sgn(ey-by)
        ld      (t_dy),a
        xor     a               ; Z set -> "fled"
        ret
dt_no:
        or      0xff            ; Z clear -> "no flee"
        ret

; ---------------------------------------------------------------------------
; sgn_sub -- A = sign(A - E) as 0x01 / 0x00 / 0xFF (unsigned compare of u8s).
; Clobbers A,F.  (Matches the C SGN_U8(minuend, subtrahend).)
; ---------------------------------------------------------------------------
sgn_sub:
        cp      e
        jr      z,sgn_zero
        jr      c,sgn_neg
        ld      a,1             ; minuend > subtrahend
        ret
sgn_neg:
        ld      a,0xff          ; minuend < subtrahend
        ret
sgn_zero:
        xor     a               ; equal
        ret

t_dy:   defb    0               ; scratch: dy for the current enemy
