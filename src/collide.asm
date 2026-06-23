; collide.asm -- hand-written collide_bullets_enemies for the Z80 target.
;
; WHY: the pointer-walk C compiled to ~6500 T worst-case (no hits) -- sdcc spent
; ~540 T per bullet-vs-enemy box test, which is a couple of u8 subtracts. This
; keeps the current enemy in IX and the box test in registers.
;
; The C version stays in collision.c (host-tested reference) and is replaced by
; this asm only on the sdcc/__SDCC target. Verified byte-identical to the C ref
; (return value AND both mutated pools) over randomized frames (bench).
;
; bullet_t : x(+0) y(+1) dx(+2) dy(+3) active(+4)            (5 bytes), 2 of them
; enemy_t  : x(+0) y(+1) dx(+2) dy(+3) level(+4) alive(+5)   (6 bytes), 7 of them
; A hit (|bx-ex|<8 AND |by-ey|<8) consumes the bullet. Fresh chasers
; (level=2, alive>1) are wounded to alive=1; other hits clear enemy.alive,
; ++kills, and end that bullet (one bullet affects at most one enemy).
;
; Inputs via globals (C wrapper): _cbe_bs, _cbe_es.
; Outputs: _cbe_kills, _cbe_kill_mask (bit N = enemy slot N died), and
; _cbe_wound_mask (bit N = chaser slot N absorbed a first hit).

        SECTION code_user
        PUBLIC  _collide_asm
        EXTERN  _cbe_bs         ; bullets_t* (-> b[0])
        EXTERN  _cbe_es         ; enemies_t* (-> e[0])
        EXTERN  _cbe_kills      ; u8 out: number of hits
        EXTERN  _cbe_kill_mask  ; u8 out: destroyed enemy slots
        EXTERN  _cbe_wound_mask ; u8 out: wounded chaser slots

SPR_SIZE equ 8
LVL_CHASE equ 2

_collide_asm:
        push    ix
        xor     a
        ld      (_cbe_kills),a          ; kills = 0
        ld      (_cbe_kill_mask),a      ; kill_mask = 0
        ld      (_cbe_wound_mask),a     ; wound_mask = 0
        ld      hl,(_cbe_bs)            ; bullet 0
        call    do_bullet
        ld      hl,(_cbe_bs)
        ld      de,5
        add     hl,de                  ; bullet 1  (MAX_BULLETS == 2, see C guard)
        call    do_bullet
        pop     ix
        ret

; do_bullet -- HL = bullet base. If active, test it against every live enemy;
; on the first overlap, kill both and return. Clobbers A,BC,DE,HL,IX.
do_bullet:
        ld      a,(hl)                 ; bx
        ld      (t_bx),a
        inc     hl
        ld      a,(hl)                 ; by
        ld      (t_by),a
        inc     hl
        inc     hl
        inc     hl                     ; hl -> bullet+4 (active)
        ld      (t_bact),hl            ; remember where to clear on a hit
        ld      a,(hl)                 ; active
        or      a
        ret     z                      ; free slot -> nothing to do

        ld      ix,(_cbe_es)           ; e[0]
        ld      b,7                    ; MAX_ENEMIES (see C guard)
        ld      c,1                    ; current enemy bit
        ld      de,6                   ; enemy stride (add ix,de keeps DE)
db_inner:
        ld      a,(ix+5)               ; alive?
        or      a
        jr      z,db_cont
        ld      a,(t_bx)
        sub     (ix+0)                 ; bx - ex
        jr      nc,db_dxp
        neg                            ; |bx - ex|
db_dxp:
        cp      SPR_SIZE
        jr      nc,db_cont             ; dx >= 8 -> miss
        ld      a,(t_by)
        sub     (ix+1)                 ; by - ey
        jr      nc,db_dyp
        neg
db_dyp:
        cp      SPR_SIZE
        jr      nc,db_cont             ; dy >= 8 -> miss
        ; ---- HIT ----
        ld      hl,(t_bact)
        ld      (hl),0                 ; bullet.active = 0
        ld      a,(ix+4)
        cp      LVL_CHASE
        jr      nz,db_kill
        ld      a,(ix+5)
        cp      2
        jr      c,db_kill
        ld      (ix+5),1               ; chaser wounded, next hit kills
        ld      a,(_cbe_wound_mask)
        or      c
        ld      (_cbe_wound_mask),a    ; mark wounded enemy slot
        ret
db_kill:
        ld      (ix+5),0               ; enemy.alive = 0
        ld      a,(_cbe_kills)
        inc     a
        ld      (_cbe_kills),a         ; kills++
        ld      a,(_cbe_kill_mask)
        or      c
        ld      (_cbe_kill_mask),a     ; mark this enemy slot
        ret                            ; this bullet is spent (break)
db_cont:
        add     ix,de                  ; -> next enemy
        sla     c                      ; next enemy bit
        djnz    db_inner
        ret                            ; no enemy hit

t_bx:   defb    0
t_by:   defb    0
t_bact: defw    0                      ; address of the current bullet's active byte
