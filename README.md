# Attribute Wars

![Attribute Wars loading screen](assets/loading.png)

**Attribute Wars version 1.0** is available from:
https://github.com/dtz-labs/attribute-wars/

Attribute Wars is a twin-joystick shooter for Timex and ZX Spectrum computers.
Twin-joystick shooters became popular in the Xbox 360 / PlayStation 3 era:
one stick moves the player, while the other stick chooses the direction of fire.
It asks your brain to coordinate two independent streams of movement at once,
which is exactly where the magic happens.

## Version 1.0 Targets

| Computer | Video | Sound | Joysticks |
|---|---|---|---|
| Timex TC2048 | Timex SCLD double buffering | beeper; AY if an expansion is selected by the player | keyboard / Kempston-style schemes |
| Timex TC2068 / TS2068 | Timex SCLD double buffering | AY-3-8910 music + FX | two native Timex joystick ports |
| ZX Spectrum 128K / +2 | 128K shadow-screen double buffering | beeper in the current 1.0 page-flip TAP; the machine has AY, but the bundled PT3 player/tune needs a banked layout before it can ship with this renderer | Sinclair 1/2 joystick ports on +2 |
| ZX Spectrum 48K | single-buffered display | beeper | keyboard / Sinclair-style schemes |

The ZX Spectrum 48K version flickers like crazy. That is expected: the machine
does not have a second hardware display page for true double buffering.

## Credits

The game was fully created by AI tools, **Claude Code** and **OpenAI Codex**,
from the original idea and direction of **Michał Pasternak**,
[@mpasternak79](https://x.com/mpasternak79).

Music: **Pator**, [@paatorr](https://x.com/paatorr).

## Music License

The bundled PT3 tune is **Spectrumizer** by Pator. Pator granted a personal,
exclusive license to use this tune in this single game. The tune is not licensed
under MIT and is not reusable outside Attribute Wars without separate permission
from the composer.

## Developer Notes

- Build and run instructions: [docs/build.md](docs/build.md)
- Architecture and hardware notes: [docs/architecture.md](docs/architecture.md)

## License

The source code is [MIT licensed](LICENSE) © 2026 Michał Pasternak.
