# Attribute Wars

[![ci](https://github.com/dtz-labs/attribute-wars/actions/workflows/ci.yml/badge.svg)](https://github.com/dtz-labs/attribute-wars/actions/workflows/ci.yml)

![Attribute Wars loading screen](assets/loading.png)

**Attribute Wars** is available from:
https://github.com/dtz-labs/attribute-wars/releases

Attribute Wars is a twin-joystick shooter for Timex and ZX Spectrum computers.
Twin-joystick shooters became popular in the Xbox 360 / PlayStation 3 era:
one stick moves the player, while the other stick chooses the direction of fire.
It asks your brain to coordinate two independent streams of movement at once,
which is exactly where the magic happens.

## Download And Use

Binary releases are published on the
[GitHub Releases page](https://github.com/dtz-labs/attribute-wars/releases).
Download the latest TAP bundle from that page.
Each release ZIP contains these TAP files:

- `aw-*-zx48k.tap` - ZX Spectrum 48K build
- `aw-*-zx128k.tap` - ZX Spectrum 128K / +2 build
- `aw-*-timex.tap` - Timex TC2048 / TC2068 / TS2068 build

Load the TAP file that matches your machine or emulator.

## Play In Browser

GitHub READMEs cannot embed the JSSpeccy 3 JavaScript emulator directly, but
you can play the Spectrum builds in a browser:

1. Open [JSSpeccy 3](https://jsspeccy.zxdemo.org/).
2. Download the latest TAP bundle from the
   [GitHub Releases page](https://github.com/dtz-labs/attribute-wars/releases).
3. In JSSpeccy 3, open the ZIP and select `aw-*-zx48k.tap` or
   `aw-*-zx128k.tap`.

Use `aw-*-timex.tap` in a Timex-capable emulator such as ZEsarUX.

> **Running the Timex build in ZEsarUX:** pick a Timex machine (TC2048 or
> TC2068) **and enable Timex video**, otherwise ZEsarUX pops a repeated
> "setting video mode" on-screen message every time the game flips its SCLD
> display. On the command line that switch is `--enabletimexvideo`, e.g.
> `zesarux --machine TC2048 --enabletimexvideo aw-1.2.0-timex.tap`; in the GUI
> it lives under Settings → Video. The `make run-tc2048` / `make run-tc2068`
> targets already pass it, which is why you don't see the message there.

## Build And Test

```sh
make all
make test
```

Full build and emulator notes are in [docs/build.md](docs/build.md).

## Supported Targets

| Computer | Video | Sound | Joysticks |
|---|---|---|---|
| Timex TC2048 | Timex SCLD double buffering | beeper; AY if an expansion is selected by the player | keyboard / Kempston-style schemes |
| Timex TC2068 / TS2068 | Timex SCLD double buffering | AY-3-8910 music + FX | two native Timex joystick ports |
| ZX Spectrum 128K / +2 | 128K shadow-screen double buffering | AY-3-8910 music + FX (PT3 tune banked into a spare RAM page) | Sinclair 1/2 joystick ports on +2 |
| ZX Spectrum 48K | single-buffered display | beeper | keyboard / Sinclair-style schemes |

The **ZX Spectrum 128K / +2** build now runs at full parity with the Timex
build: the same PT3 music and AY sound effects (the ~10 KB tune is parked in a
spare RAM bank and paged in for each 50 Hz player tick, so it coexists with the
shadow-screen page flip), plus the same enemy variety and difficulty. Choose
`MUSIC+FX`, `FX`, or `BEEPER` from the title screen.

The ZX Spectrum 48K version flickers like crazy. That is expected: the machine
does not have a second hardware display page for true double buffering.

## Credits

The game was fully created by AI tools, **Claude Code** and **OpenAI Codex**,
from the original idea and direction of **Michał Pasternak**,
[@mpasternak79](https://x.com/mpasternak79).

Music: **Pator**, [@paatorr](https://x.com/paatorr).

## Music License

The bundled PT3 tune is
**[Spectrumizer](https://zxart.ee/eng/authors/p/pator/spectrumizer/)** by Pator.
Pator granted a personal, exclusive license to use this tune in this single
game. The tune is not licensed under MIT and is not reusable outside Attribute
Wars without separate permission from the composer.

## Developer Notes

- Build and run instructions: [docs/build.md](docs/build.md)
- Architecture and hardware notes: [docs/architecture.md](docs/architecture.md)
- Project brief and constraints: [docs/project-brief.md](docs/project-brief.md)

## License

The source code is [MIT licensed](LICENSE) © 2026 Michał Pasternak.
