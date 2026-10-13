#!/usr/bin/env python3
"""Tie module.json, the DSP's parameter table and the UI together.

  - Cross-checks module.json's chain_params against src/wrapper/params.cpp:
    same keys in the same order, same type, same range, same default.
  - Checks engine_names against the vendored Plaits source: one name per
    engine registered in plaits/dsp/voice.cc, in registration order (the
    order of patch.engine, i.e. the hardware's).
  - Writes <gen>/oplaits_metadata.h: chain_params embedded in the DSP, split
    around PITCH's max so the plugin serves the range of the scale in use.
  - Writes ui_params.mjs (module root): the same metadata, plus the engine
    names, for the UI.

usage: metadata.py <generated-dir>
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
manifest = json.loads((root / "module.json").read_text())
params = manifest["capabilities"]["chain_params"]
engines = manifest["engine_names"]

src = (root / "src" / "wrapper" / "params.cpp").read_text()
table = src.split("const ParamInfo kParams[P_COUNT] = {", 1)[1].split("};", 1)[0]
row_re = re.compile(r'\{"(\w+)",\s*(PT_\w+),\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\}')
rows = row_re.findall(table)
assert len(rows) == len(params), ("params.cpp rows != module.json chain_params", len(rows), len(params))

TYPES = {"PT_FLOAT": "float", "PT_INT": "int", "PT_ENUM": "enum"}
for p, (key, ptype, lo, hi, default) in zip(params, rows):
    lo, hi, default = float(lo), float(hi), float(default)
    assert p["key"] == key, ("order/key drift", p["key"], key)
    assert p["type"] == TYPES[ptype], ("type drift", key, p["type"], ptype)
    if key == "pitch":
        # The DSP clamps to 60 (a chromatic scale's 61 notes in C1..C6);
        # module.json and the served chain_params carry the CURRENT scale's
        # top index (35 = C Phrygian's, the default).
        assert lo == 0 and hi == 60 and p["min"] == 0 and p["max"] == 35, p
        assert float(p["default"]) == default, ("default drift", key)
        continue
    if p["type"] == "enum":
        opts = p["options"]
        assert lo == 0 and hi == len(opts) - 1, ("enum range drift", key, len(opts), hi)
        assert opts.index(p["default"]) == default, ("enum default drift", key)
    else:
        assert float(p["min"]) == lo and float(p["max"]) == hi, ("range drift", key)
        assert float(p["default"]) == default, ("default drift", key, p["default"], default)

# The engine names: exactly the engines the vendored voice registers.
voice_cc = (root / "src" / "plaits" / "mi" / "plaits" / "dsp" / "voice.cc").read_text()
registered = re.findall(r"engines_\.RegisterInstance\(&(\w+),", voice_cc)
EXPECT = ["virtual_analog_vcf_engine_", "phase_distortion_engine_",
          "six_op_engine_", "six_op_engine_", "six_op_engine_",
          "wave_terrain_engine_", "string_machine_engine_", "chiptune_engine_",
          "virtual_analog_engine_", "waveshaping_engine_", "fm_engine_",
          "grain_engine_", "additive_engine_", "wavetable_engine_",
          "chord_engine_", "speech_engine_",
          "swarm_engine_", "noise_engine_", "particle_engine_", "string_engine_",
          "modal_engine_", "bass_drum_engine_", "snare_drum_engine_", "hi_hat_engine_"]
assert registered == EXPECT, ("voice.cc engine order changed: rename engine_names", registered)
assert len(engines) == len(registered) == 24, ("engine names vs voice.cc", len(engines))
assert all(re.fullmatch(r"[A-Z0-9]{2,5}", n) for n in engines), engines
engine = next(p for p in params if p["key"] == "engine")
# An enum NAMED by engine_names, so a screen that is not ours (Schwung's own
# knob card in the chain editor) shows "MODAL" rather than "20". Our UI turns
# it back into an int over the engines switched on (ui_core.mjs ioGet).
assert engine["type"] == "enum" and engine["options"] == engines

# Split around PITCH's max: head + "%d" + tail.
text = json.dumps(params, separators=(",", ":"))
marker = '"key":"pitch","name":"Pitch","short_name":"PITCH","type":"int","min":0,"max":'
assert text.count(marker) == 1 and text[text.index(marker) + len(marker):].startswith("35,")
cut = text.index(marker) + len(marker)
head, tail = text[:cut], text[cut + 2:]
lit = lambda t: json.dumps(t, ensure_ascii=True)

gen = pathlib.Path(sys.argv[1])
gen.mkdir(parents=True, exist_ok=True)
(gen / "oplaits_metadata.h").write_text(
    "/* Generated from module.json by scripts/metadata.py. Do not edit. */\n"
    "#pragma once\n"
    "static const char oplaits_chain_params_head[] = " + lit(head) + ";\n"
    "static const char oplaits_chain_params_tail[] = " + lit(tail) + ";\n")

ui = ["/* Generated from module.json by scripts/metadata.py. Do not edit. */",
      "export const PARAMS = " + json.dumps(params, indent=1) + ";",
      "export const ENGINE_NAMES = " + json.dumps(engines) + ";",
      ""]
(root / "ui_params.mjs").write_text("\n".join(ui))
