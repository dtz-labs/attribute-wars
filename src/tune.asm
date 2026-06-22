; tune.asm -- the AY music module data, included verbatim for z88dk's
; VortexTracker2 PT3 player. ay_vt2_init() is handed &spectrumizer_pt3 (the
; address of this blob). Asset fetched from ZX-Art (Pator, "Spectrumizer",
; 2023); see assets/spectrumizer.pt3 and the README music credit.
;
; rodata_user (matches z88dk's own vt2 example) -> linker-placed after code,
; in RAM above 0x8000, clear of the 0x4000-0x7FFF screen/back-buffer region.

        SECTION rodata_user

        PUBLIC  _spectrumizer_pt3
_spectrumizer_pt3:
        BINARY  "../assets/spectrumizer.pt3"
