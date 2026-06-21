/*
 * Twin-Stick Shooter for Timex TC2048 / ZX Spectrum
 *
 * main.c -- entry point.
 *
 * Milestone 1, step 1: BUILD SMOKE TEST.
 * The only goal of this version is to prove the toolchain pipeline:
 *   zcc (+zx) -> .tap -> loads & runs on Fuse-as-TC2048.
 * It prints a banner and halts. No video-mode / SCLD code yet -- that
 * comes only after the build + launch path is confirmed working.
 */

#include <stdio.h>

int main(void)
{
    printf("TWIN-STICK SHOOTER\n");
    printf("Timex TC2048 - build OK\n");

    /* Hang so the screen stays visible in the emulator instead of
     * dropping back to the BASIC editor. */
    for (;;) {
    }

    return 0;
}
