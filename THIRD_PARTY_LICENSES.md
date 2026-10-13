# Third-party code in oPlaits

Everything the module ships is MIT-licensed. Nothing copyleft is compiled in or
linked (`dsp.so` needs only `libc` and `libm`; `scripts/check_abi.py` fails the
build otherwise).

## Mutable Instruments Plaits and stmlib — MIT

| | |
|---|---|
| Upstream | https://github.com/pichenettes/eurorack (`plaits/`) at `f4cfa7697095a28d292bff758beb51deb49b5a86` (2026-10-12) |
| | https://github.com/pichenettes/stmlib at `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (the submodule commit eurorack pins) |
| Author | Emilie Gillet (emilie.o.gillet@gmail.com) |
| Copyright | Copyright 2012, 2014, 2015, 2016, 2017, 2021 Emilie Gillet (per file) |
| Licence | MIT (the eurorack README: "Code (STM32F projects): MIT license"; every vendored file carries the MIT notice) |
| Where | `src/plaits/mi/` |

Vendored: all of `plaits/dsp/` (every engine, oscillator, drum, physical
model, FM, speech, noise, chord and FX file), `plaits/resources.{h,cc}`,
`plaits/user_data.h`, the resource generators `plaits/resources/*.py`, and the
stmlib files the DSP includes (`stmlib.h`, `dsp/{cosine_oscillator,dsp,filter,
hysteresis_quantizer,limiter,parameter_interpolator,polyblep,rsqrt,units}.h`,
`dsp/units.cc`, `utils/{buffer_allocator,dsp,random}.h`, `utils/random.cc`) with
stmlib's `LICENSE`. Only DSP: no drivers, UI, settings storage, bootloader or
calibration code.

They are unmodified except for the patches in `patches/`, each of which is
also marked in the source with an `oPlaits patch` comment:

- `0001-fm-velocity-table-end.patch` (`plaits/dsp/fm/dx_units.h`) — at
  velocity 1.0 `NormalizeVelocity` read `lut_cube_root[17]`, one past the end,
  weighted by 0. Same value without the read.
- `0002-speech-frame-table-end.patch` (`plaits/dsp/speech/lpc_speech_synth.h`)
  — without interpolation `PlayFrame` read the frame after the last one,
  weighted by 0. Same value without the read.
- `0003-sine-negative-phase.patch` (`plaits/dsp/oscillator/sine_oscillator.h`)
  — `Sine()` with a negative phase (reachable from WaveTerrainEngine's AUX)
  indexed before `lut_sine`. Negative phases are wrapped first; phase >= 0 is
  untouched.
- `0004-six-op-clear-accumulator.patch` (`plaits/dsp/engine2/six_op_engine.cc`)
  — the staggered-rendering accumulator in the shared engine RAM was read on
  the first block after switching to a 6-op engine before being written.
  Cleared in `Reset()`, which the voice calls on every switch.

Also not part of the vendored tree but compiled with it: `-include stdio.h`
(`plaits/user_data.h`'s `TEST` branch calls `printf` without including it).

`scripts/fetch_plaits.sh --verify` re-downloads both commits and checks that the
vendored tree is exactly upstream plus these patches;
`scripts/compare_upstream.sh` shows what the patches change in the output.

### The MIT notice (as it appears in the vendored files)

```
Copyright 2016 Emilie Gillet.

Author: Emilie Gillet (emilie.o.gillet@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

See http://creativecommons.org/licenses/MIT/ for more information.
```

(The year differs per file: 2012-2021.)

### Names and trademarks

The eurorack README asks that derivative works not use the name
"Mutable Instruments" (a registered trademark) and recommends not keeping a
module's original name. This build is named **oPlaits** at its user's
request. It is not a Mutable Instruments product. If you publish a build,
consider renaming it (`name` in `module.json`).

## The phrase engine and source MIDI files — the author's own work

`src/phrase/` (the loader, analyser, generator, scale quantizer and sequencer)
and `patterns/` (the six source MIDI files) are the author's own work
(Hiro Ikezawa), under this repository's MIT licence. Only the loader,
analyser and generator are used, by the RANDOM pad
(`src/wrapper/random_phrase.cpp`); the sequencer is compiled but unused.

## Schwung — MIT

`third_party/schwung/host/plugin_api_v1.h` is Schwung's plugin API header
(Copyright 2025-2026 Charles Vestal, MIT), vendored unmodified. Its licence (also in
`third_party/schwung/LICENSE`):

```
MIT License

Copyright (c) 2025-2026 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
