#!/usr/bin/env python3
"""dlopen the real dsp.so through the v2 ABI, the way the chain host does."""
import ctypes as c
import json
import pathlib
import sys

LIB = sys.argv[1]
BLOCK = 128
KEEPALIVE = 6            # the DC keepalive a silent block carries (LSB)

BPMFN = c.CFUNCTYPE(c.c_float)
BEATFN = c.CFUNCTYPE(c.c_double)


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("mapped_memory", c.c_void_p),
                ("audio_out_offset", c.c_int), ("audio_in_offset", c.c_int),
                ("log", c.c_void_p), ("midi_send_internal", c.c_void_p),
                ("midi_send_external", c.c_void_p), ("get_clock_status", c.c_void_p),
                ("mod_emit_value", c.c_void_p), ("mod_clear_source", c.c_void_p),
                ("mod_host_ctx", c.c_void_p), ("get_bpm", BPMFN),
                ("midi_inject_to_move", c.c_void_p), ("slot_recv_channel", c.c_void_p),
                ("get_beat_position", BEATFN), ("reserved", c.c_void_p * 8)]


assert Host.get_beat_position.offset == 112 and Host.reserved.offset == 120


class API(c.Structure):
    _fields_ = [("version", c.c_uint32)] + [(k, c.c_void_p) for k in
               ("create", "destroy", "midi", "set", "get", "error", "render")]


transport = {"beat": -1.0, "bpm": 120.0}
bpm_cb = BPMFN(lambda: transport["bpm"])
beat_cb = BEATFN(lambda: transport["beat"])

lib = c.CDLL(LIB)
lib.move_plugin_init_v2.restype = c.POINTER(API)
lib.move_plugin_init_v2.argtypes = [c.c_void_p]
bad = Host(api_version=1, sample_rate=48000, frames_per_block=BLOCK)
assert not lib.move_plugin_init_v2(c.byref(bad)), "must refuse a 48 kHz host"
host = Host(api_version=1, sample_rate=44100, frames_per_block=BLOCK,
            get_bpm=bpm_cb, get_beat_position=beat_cb)
api = lib.move_plugin_init_v2(c.byref(host)).contents
assert api.version == 2

create = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)(api.create)
destroy = c.CFUNCTYPE(None, c.c_void_p)(api.destroy)
midi_fn = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_int)(api.midi)
set_param = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)(api.set)
get_param = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)(api.get)
render = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_int16), c.c_int)(api.render)

buf = (c.c_int16 * (BLOCK * 2))()


def read(p, key, size=16384):
    out = c.create_string_buffer(size)
    n = get_param(p, key.encode(), out, size)
    return None if n < 0 else out.value.decode()


def write(p, key, value):
    set_param(p, key.encode(), str(value).encode())


def midi(p, *msg):
    arr = (c.c_uint8 * len(msg))(*msg)
    midi_fn(p, arr, len(msg), 0)


def blocks(p, n):
    """Render n blocks, advancing the transport if it runs; returns the peak."""
    peak = 0
    for _ in range(n):
        render(p, buf, BLOCK)
        assert all(buf[2 * i] == buf[2 * i + 1] for i in range(BLOCK)), "mono to both sides"
        peak = max(peak, max(abs(v) for v in buf))
        if transport["beat"] >= 0:
            transport["beat"] += BLOCK / 44100 * transport["bpm"] / 60
    return peak


manifest = json.loads((pathlib.Path(__file__).resolve().parent.parent / "module.json").read_text())
params = manifest["capabilities"]["chain_params"]

p = create(b".", None)
assert p

# ---- contract reads ----------------------------------------------------------
assert json.loads(read(p, "chain_params")) == params, "served chain_params == module.json (C Phrygian)"
out = c.create_string_buffer(64)
assert get_param(p, b"ui_hierarchy", out, 64) == 0 and out.value == b"", "ui_hierarchy must be served empty"
h = json.loads(read(p, "grid_hierarchy"))
(pathlib.Path(LIB).parent / "grid_hierarchy.json").write_text(read(p, "grid_hierarchy"))   # for test_ui.mjs
assert h["levels"]["root"]["knobs"] == ["level", "engine", "harmonics", "timbre", "morph", "decay", "colour", "pitch"]
assert h["levels"]["plaits"]["knobs"] == ["fm_amt", "timbre_amt", "morph_amt", "aux_mix", "coarse", "fine", "velo", "trig"]
assert read(p, "no_such_key") is None
for prm in params:
    got = read(p, prm["key"])
    want = prm["options"].index(prm["default"]) if prm["type"] == "enum" else prm["default"]
    assert abs(float(got) - float(want)) < 1e-4, (prm["key"], got, want)
names = read(p, "pitch_names").split(",")
assert len(names) == 36 and names[0] == "C1" and names[14] == "C3" and names[-1] == "C6", names
assert read(p, "motion_status") == "0,-1,0,8,0", read(p, "motion_status")
assert read(p, "gate_status") == "1" * 16
assert read(p, "active_engine") in ("-1", "8"), read(p, "active_engine")

# Hardware ranges in, hardware ranges out.
write(p, "engine", 20)
write(p, "fm_amt", -0.5)
assert read(p, "engine") == "20" and read(p, "fm_amt") == "-0.5000"
write(p, "decay", "nan")            # refused, not applied
write(p, "decay", "7junk")
assert read(p, "decay") == "0.5000"
write(p, "engine", 99)              # clamped
assert read(p, "engine") == "23"
write(p, "pitch", 99)               # clamped to the scale's top note
assert read(p, "pitch") == "35"
write(p, "engine", 8)
write(p, "fm_amt", 0)
write(p, "pitch", 14)

# ---- MIDI: a note plays, the engine the voice renders is the one set ---------
assert blocks(p, 20) <= KEEPALIVE, "silent while nothing plays"
midi(p, 0x90, 48, 100)
assert blocks(p, 40) > 1000, "a MIDI note plays"
assert read(p, "active_engine") == "8"
midi(p, 0x80, 48, 0)
blocks(p, 1500)
assert blocks(p, 20) <= KEEPALIVE, "released note decays to silence"

# ---- the auto 1/16 ------------------------------------------------------------
transport["beat"] = 0.0
assert blocks(p, 100) > 1000, "plays every 1/16 while the transport runs"
st = read(p, "motion_status").split(",")
assert 0 <= int(st[1]) < 8, st
for i in range(16):
    write(p, "gate_toggle", i)       # every trigger pad off
assert read(p, "gate_status") == "0" * 16
blocks(p, 1500)                      # the last hit rings out through the LPG
assert blocks(p, 40) <= KEEPALIVE, "all 16 trigger pads off = silence"
for i in range(16):
    write(p, "gate_toggle", i)
assert blocks(p, 60) > 1000

# ---- every engine plays through the plugin ------------------------------------
for e in range(24):
    write(p, "engine", e)
    pk = blocks(p, 50)
    assert pk > 300, ("engine", e, pk)
    assert read(p, "active_engine") == str(e)
write(p, "engine", 8)

# ---- RANDOM, and touch recording ----------------------------------------------
write(p, "randomize", 1)
assert int(read(p, "motion_status").split(",")[0]) & (1 << 5), "RANDOM fills the PITCH lane"
write(p, "write", 1)
write(p, "timbre", 0.9)
for _ in range(9):                   # one whole 8-step loop, restated like the UI
    write(p, "touch", "timbre,1")
    blocks(p, 40)
assert int(read(p, "motion_status").split(",")[2]) & (1 << 1), "TIMBRE records under the finger"
write(p, "touch", "timbre,0")
write(p, "timbre", 0.1)
blocks(p, 5)
assert abs(float(read(p, "eff_timbre")) - 0.9) < 0.01, read(p, "eff_timbre")
assert read(p, "timbre") == "0.1000", "playback never writes the knob"
write(p, "motion_clear", "timbre")
blocks(p, 2)
assert read(p, "eff_timbre") == "0.1000"

# ENGINE is never recorded: a touch on it writes nothing, and a state's lane
# for a key that is not recorded is dropped while its other lanes are kept.
write(p, "touch", "engine,1")
blocks(p, 40)
assert not (int(read(p, "motion_status").split(",")[0]) & ~((1 << 10) - 1)), "no lane beyond the ten"
write(p, "touch", "engine,0")
r = create(b".", None)
lane = "AA" * 128
write(r, "state", '{"v":1,"engine":5,"m":"engine:%s;timbre:%s","g":65535}' % (lane, lane))
assert read(r, "motion_status").split(",")[0] == "2", read(r, "motion_status")   # timbre only
assert '"m":"timbre:' in read(r, "state") and read(r, "engine") == "5"
destroy(r)

# ---- state ---------------------------------------------------------------------
write(p, "gate_toggle", 3)
write(p, "steps", 1)
write(p, "engine", 13)
write(p, "aux_mix", 0.3)
state = read(p, "state")
assert '"engine":13,' in state and '"aux_mix":0.3000,' in state and '"m":"pitch:' in state and '"g":65527' in state, state[:200]
q = create(b".", None)
assert q and q != p
write(q, "state", state)
assert read(q, "state") == state, "state round trip"
assert read(q, "motion_status").split(",")[3] == "16"
assert read(q, "engine") == "13"

# ---- stop: silence ---------------------------------------------------------------
transport["beat"] = -1.0
blocks(p, 1500)
assert blocks(p, 10) <= KEEPALIVE, "silent after Stop"

# ---- the pool: six instances, and a freed one is reused clean -------------------
extra = [create(b".", None) for _ in range(4)]
assert all(extra) and create(b".", None) is None, "pool must hold exactly six"
write(extra[0], "engine", 7)
destroy(extra[0])
again = create(b".", None)
assert again == extra[0]
assert read(again, "engine") == "8", "a reused slot must start clean"
for x in [again] + extra[1:] + [q, p]:
    destroy(x)

print("PASS: dlopen smoke (v2 ABI, grid contract, MIDI, all 24 engines, auto 1/16 + gates, RANDOM, touch motion, state, pool)")
