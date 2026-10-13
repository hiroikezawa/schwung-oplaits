/* oPlaits UI test: ui_core.mjs driving Schwung's REAL page controller and
 * input decoder, drawn through the device-font harness, against a fake param
 * store serving the DSP's real contract (grid_hierarchy is dumped by smoke.py
 * from the built dsp.so). Run by scripts/test.sh. */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
/* Schwung's page engine and draw harness come from a Schwung checkout:
 * SCHWUNG_ROOT, else ../schwung beside this repository. scripts/test.sh
 * skips this test when neither exists. */
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const load = (p) => import(pathToFileURL(p).href);
/* OB_SHARED points at another copy of shared/ -- e.g. one mirrored from the
 * device, whose page engine can differ from this checkout's. */
const shared = process.env.OB_SHARED || path.join(root, "src/shared");
const { createController } = await load(path.join(shared, "param_pages/page_controller.mjs"));
const { decodeInput, applyInput } = await load(path.join(shared, "param_pages/page_input.mjs"));
const { createFramebuffer, drawContext } = await load(path.join(root, "tools/param-pages/harness.mjs"));
const { PRESET_PATH, PRESET_FIRST, parsePresets } = await load(path.join(here, "../ui_core.mjs"));
const { createUi, MAIN_KEYS, STATUS_EVERY_TICKS, HEARTBEAT_MS, LED, SETTINGS_PATH,
        parseEngineSettings, serializeEngineSettings, visibleEngines, ENGINE_COUNT } =
    await load(path.join(here, "../ui_core.mjs"));
const { ENGINE_NAMES } = await load(path.join(here, "../ui_params.mjs"));

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
}

const build = path.join(here, "../build/native");
const manifest = JSON.parse(fs.readFileSync(path.join(here, "../module.json"), "utf8"));
const chainParams = manifest.capabilities.chain_params;
const gridHierarchy = fs.readFileSync(path.join(build, "grid_hierarchy.json"), "utf8");

/* ---- fake DSP ---------------------------------------------------------- */
const PHRYGIAN = ["C", "Db", "Eb", "F", "G", "Ab", "Bb"];
const CHROMATIC = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
const namesFor = (set) => {
    const out = [];
    for (let oct = 1; oct <= 5; oct++) for (const n of set) out.push(n + oct);
    return out.concat(["C6"]).join(",");
};
let pitchNames = namesFor(PHRYGIAN);
const values = {};
for (const p of chainParams) values[p.key] = String(p.type === "enum" ? p.options.indexOf(p.default) : p.default);
const gates = new Array(16).fill(true);
const writes = [];
const reads = [];
let status = "0,-1,0,8,0";
let hwShift = false, shown = true, padBlocked = false;
const leds = {};
const files = new Map();
const store = {
    get(key) {
        reads.push(key);
        if (key === "ui_hierarchy") return "";
        if (key === "grid_hierarchy") return gridHierarchy;
        if (key === "chain_params") return JSON.stringify(chainParams);
        if (key === "motion_status") return status;
        if (key === "pitch_names") return pitchNames;
        if (key === "gate_status") return gates.map((g) => g ? "1" : "0").join("");
        if (key === "state") return JSON.stringify({ v: 1, engine: Number(values.engine) });
        if (key.startsWith("eff_")) return values[key.slice(4)] ?? null;
        if (key in values) return values[key];
        return "";
    },
    set(key, value) {
        writes.push([key, String(value)]);
        if (key === "gate_toggle") gates[Number(value)] = !gates[Number(value)];
        if (key === "state") values.engine = String(JSON.parse(value).engine);
        if (key in values) values[key] = String(value);
        return true;
    },
};

let clock = 1000;
const fb = createFramebuffer();
const ctx = drawContext(fb);
const ui = createUi({
    createController, decodeInput, applyInput,
    getParam: (k) => store.get(k),
    setParam: (k, v) => store.set(k, v),
    draw: ctx,
    clear: () => fb.clearScreen(),
    layout: () => "movy",
    now: () => clock,
    shiftHeld: () => hwShift,
    displayOn: () => shown,
    padBlock: (on) => { padBlocked = on; },
    sendLed: (n, col) => { leds[n] = col; return true; },
    padSnapshot: () => ({ "68": 5, "99": 7 }),
    readFile: (p) => files.get(p) ?? null,
    writeFile: (p, t) => { files.set(p, t); return true; },
    ensureDir: () => true,
});

function ticks(n) { for (let i = 0; i < n; i++) { clock += 17; ui.tick(); } }
const touchesSent = () => writes.filter(([k]) => k === "touch").map(([, v]) => v);
const bare = (k) => k.slice(k.indexOf(":") + 1);

ui.init();
ticks(40);
const c = ui.state.controller;
check(JSON.stringify(c.page.keys.map(bare)) === JSON.stringify(MAIN_KEYS),
      "page 1 = VOLUME ENGINE HARMONICS TIMBRE MORPH DECAY COLOR PITCH: " + c.page.keys);
check(c.pages.length >= 3, `Main / Plaits / Scale: ${c.pages.length} pages`);
check(!reads.includes("ui_hierarchy"), "the grid must read grid_hierarchy, never the empty ui_hierarchy");
check(fb.countLit() > 200, "page 1 draws");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
check(ui.title() === "VA", "title = the engine (factory default VA): " + ui.title());

/* ---- PITCH is a note of the scale: the DSP's names, by index ------------- */
check(ui.state.names && ui.state.names.length === 36 && ui.state.names[14] === "C3", "names from the DSP");

/* ---- ENGINE: knob 2 steps over the engines switched on ---------------------- */
check(ui.state.vis.length === ENGINE_COUNT, "all 24 engines offered by default");
for (let i = 0; i < 6; i++) { ui.onMidi([0xb0, 72, 1]); ticks(2); }
ticks(10);
check(writes.some(([k, v]) => k === "engine" && v === "9"), "ENGINE +1 writes the real index 9: " + writes.filter(([k]) => k === "engine"));

/* ---- knobs 3..7 are HARMONICS TIMBRE MORPH DECAY COLOR ---------------------- */
const wk = writes.length;
const knobKeys = ["harmonics", "timbre", "morph", "decay", "colour"];
for (let k = 0; k < 5; k++) { ui.onMidi([0xb0, 73 + k, 1]); ticks(10); }
for (const key of knobKeys)
    check(writes.slice(wk).some(([k2]) => k2 === key), `knob writes ${key}: ` + writes.slice(wk));

/* ---- playing: step in the header; no REC until the REC pad ---------------- */
status = "0,4,0,8,0";
ticks(STATUS_EVERY_TICKS);
check(/ 5\/8$/.test(ui.title()), "step in header: " + ui.title());
ui.onMidi([0x90, 3, 127]);                            /* knob 4 touch = TIMBRE */
check(touchesSent().at(-1) === "timbre,1", "touch reported: " + touchesSent());
check(!/REC/.test(ui.title()), "no REC without the REC pad: " + ui.title());
ui.onMidi([0x80, 3, 0]);
check(touchesSent().at(-1) === "timbre,0", "release reported");
ui.onMidi([0x90, 1, 127]); ui.onMidi([0x80, 1, 0]);    /* knob 2 = ENGINE */
check(!touchesSent().some((t) => t.startsWith("engine")), "ENGINE is never recorded");

/* ---- the pads ----------------------------------------------------------- */
check(padBlocked === true, "pads taken while the screen is up");
ticks(8);
check(leds[99] === LED.RECORD_OFF, "REC off = dim: " + leds[99]);
check(leds[98] === LED.RANDOM, "RANDOM lit");
check(leds[97] === LED.ENGINES, "ENGINES lit");
check(leds[68] === LED.GATE_ON && leds[72] === LED.PLAYHEAD, "gates lit, playhead on step 5");
check(leds[76] === LED.OFF && leds[83] === LED.OFF, "8-step loop: steps 9-16 dark");
ui.onMidi([0x90, 99, 100]);                           /* REC pad */
check(writes.at(-1)[0] === "write" && writes.at(-1)[1] === "1" && leds[99] === LED.RECORD_ON, "REC on = solid red");
check(LED.RECORD_ON === 127 && LED.RECORD_OFF === 65, "REC red / dim palette");
ui.onMidi([0x90, 99, 100]);                           /* still held: no repeat */
check(writes.filter(([k]) => k === "write").length === 1, "held pad fires once");
ui.onMidi([0x80, 99, 0]);
status = "0,4,0,8,1";
ticks(STATUS_EVERY_TICKS);
check(/ ARM 5\/8$/.test(ui.title()), "armed: " + ui.title());
ui.onMidi([0x90, 3, 127]);
check(/ REC 5\/8$/.test(ui.title()), "REC under the finger at once: " + ui.title());
const n0 = touchesSent().filter((t) => t === "timbre,1").length;
ticks(Math.ceil(HEARTBEAT_MS * 4 / 17));
const beats = touchesSent().filter((t) => t === "timbre,1").length - n0;
check(beats >= 3 && beats <= 5, `heartbeat restates the touch (${beats} in ${HEARTBEAT_MS * 4} ms)`);
ui.onMidi([0x80, 3, 0]);
const quiet = touchesSent().length;
ticks(40);
check(touchesSent().length === quiet, "no heartbeat once released");

ui.onMidi([0x90, 98, 100]);                           /* RANDOM */
check(writes.some(([k]) => k === "randomize"), "RANDOM pad writes a phrase");
check(leds[98] === LED.PRESSED, "RANDOM lights while pressed");
ui.onMidi([0x80, 98, 0]);
check(leds[98] === LED.RANDOM, "...and back");
ui.onMidi([0x90, 69, 100]);                           /* step 2 */
check(writes.at(-1)[0] === "gate_toggle" && writes.at(-1)[1] === "1", "step 2 toggled");
check(leds[69] === LED.GATE_OFF, "step 2 now off: " + leds[69]);
ui.onMidi([0x80, 69, 0]);
ticks(STATUS_EVERY_TICKS);
check(ui.state.gates[1] === "0", "the poll agrees: " + ui.state.gates);
ui.onMidi([0x90, 80, 100]); ui.onMidi([0x80, 80, 0]);   /* step 13 in an 8-step loop */
check(!(writes.at(-1)[0] === "gate_toggle" && writes.at(-1)[1] === "12"), "a dark step pad does nothing");
const w0 = writes.length;
ui.onMidi([0x90, 92, 100]); ui.onMidi([0x80, 92, 0]);   /* an unassigned pad (top row, left) */
check(writes.length === w0, "unassigned pads do nothing");
status = "0,12,0,16,1";
ticks(STATUS_EVERY_TICKS + 8);
check(leds[80] === LED.PLAYHEAD && leds[83] !== LED.OFF, "16 steps: second row lit, step 13 playing");

/* ---- PITCH: note in the header while held -------------------------------- */
c.state.values.pitch = "16";
ui.onMidi([0x90, 7, 127]);                            /* knob 8 = pitch */
check(touchesSent().at(-1) === "pitch,1", "knob 8 is pitch");
check(ui.title() === "PITCH Eb3 REC 13/16", "pitch note while held: " + ui.title());
ui.onMidi([0x90, 7, 0]);
check(touchesSent().at(-1) === "pitch,0", "note-on vel 0 releases");

/* ---- an automated knob is drawn as modulated, from eff_<key> -------------- */
status = `${1 << 1},3,0,16,1`;                       /* lane 1 = timbre */
reads.length = 0;
ticks(STATUS_EVERY_TICKS * 30);
check(c.isModulatedCached("timbre"), "automated knob marked modulated");
check(reads.includes("eff_timbre"), "its playing value is read as eff_timbre");

/* ---- Shift + touch clears one lane; on VOLUME, all ------------------------ */
const before = touchesSent().length;
hwShift = true;
ui.onMidi([0x90, 3, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "timbre", "shift+touch clears the lane: " + writes.at(-1));
ui.onMidi([0x80, 3, 0]);
ui.onMidi([0x90, 0, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "all", "shift+touch VOLUME clears all");
ui.onMidi([0x80, 0, 0]);
hwShift = false;
ticks(1);
check(touchesSent().length === before, "shift+touch never starts a recording");
ui.onMidi([0x90, 0, 127]); ui.onMidi([0x80, 0, 0]);
check(touchesSent().length === before, "VOLUME touch is never a recording");

/* ---- page 2 (Plaits): the attenuverters record, VELO does not ------------ */
ui.onMidi([0xb0, 14, 1]);
ticks(20);
check(JSON.stringify(c.page.keys.map(bare)) === JSON.stringify(["fm_amt", "timbre_amt", "morph_amt", "aux_mix", "coarse", "fine", "velo", "trig"]),
      "page 2: " + c.page.keys);
ui.onMidi([0x90, 0, 127]);
check(touchesSent().at(-1) === "fm_amt,1", "FM> records from page 2");
ui.onMidi([0x80, 0, 0]);
ui.onMidi([0x90, 3, 127]);
check(touchesSent().at(-1) === "aux_mix,1", "AUX MIX records from page 2");
ui.onMidi([0x80, 3, 0]);
const n2 = touchesSent().length;
ui.onMidi([0x90, 6, 127]); ui.onMidi([0x80, 6, 0]);
check(touchesSent().length === n2, "VELO is not recorded");
check(ui.state.controller && typeof c.state === "object", "controller alive");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());

/* ---- page 3: ROOT, SCALE, STEPS; a scale change re-reads names ----------- */
ui.onMidi([0xb0, 14, 1]); ticks(20);
check(bare(c.page.keys[0]) === "root" && bare(c.page.keys[1]) === "scale" && bare(c.page.keys[2]) === "steps",
      "page 3 = ROOT SCALE STEPS: " + c.page.keys);
pitchNames = namesFor(CHROMATIC);
for (let i = 0; i < 4; i++) ui.onMidi([0xb0, 72, 1]);   /* turn SCALE one step (4 detents) */
ticks(30);
check(writes.some(([k]) => k === "scale"), "SCALE written");
check(ui.state.names.length === 61, "names re-read after a scale change: " + (ui.state.names && ui.state.names.length));
ui.onMidi([0xb0, 14, 127]); ticks(20);
ui.onMidi([0xb0, 14, 127]); ticks(20);               /* back to page 1 */

/* ---- the ENGINES list (pad 97): the knob then offers only what is on ----- */
check(parseEngineSettings(null).every(Boolean) && parseEngineSettings("{junk").every(Boolean), "settings robust");
check(JSON.stringify(visibleEngines([true, false, true, ...new Array(21).fill(false)], 5)) === "[0,2,5]",
      "visible = enabled + the one playing");
values.engine = "3";
ticks(STATUS_EVERY_TICKS);
ui.onMidi([0x90, 97, 100]); ui.onMidi([0x80, 97, 0]);
check(ui.state.listOpen, "pad 97 opens the ENGINES list");
ticks(2);
check(leds[97] === LED.ENGINES_OPEN, "ENGINES pad lit while open");
check(fb.countLit() > 100, "the list draws");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
const knobWrites = writes.length;
ui.onMidi([0xb0, 71, 1]);                               /* knobs do nothing in the list */
ticks(3);
check(writes.length === knobWrites, "knobs inert while the list is open");
for (let i = 0; i < 40; i++) ui.onMidi([0xb0, 14, 127]);   /* to ALL ON */
ui.onMidi([0xb0, 14, 1]);                                  /* ALL OFF */
ui.onMidi([0xb0, 3, 127]);
check(ui.state.enabled.filter(Boolean).length === 1 && ui.state.enabled[3], "ALL OFF keeps the playing engine");
for (let i = 0; i < 21; i++) ui.onMidi([0xb0, 14, 1]);     /* row 1 -> MODAL (row 22) */
ui.onMidi([0xb0, 3, 127]);
check(ui.state.enabled[20], "MODAL on");
check(files.get(SETTINGS_PATH) === serializeEngineSettings(ui.state.enabled), "saved on every toggle");
check(ui.handleBack() === true, "Back closes the list (consumed)");
ticks(STATUS_EVERY_TICKS * 2);
check(JSON.stringify(ui.state.vis) === "[3,20]", "the knob offers 6OP2 and MODAL: " + ui.state.vis);
const mw = writes.filter(([k]) => k === "engine").length;
for (let i = 0; i < 30; i++) ui.onMidi([0xb0, 72, 1]);     /* spin ENGINE up */
ticks(10);
const engineWrites = writes.filter(([k]) => k === "engine").slice(mw).map(([, v]) => v);
check(engineWrites.length > 0 && engineWrites.every((v) => v === "3" || v === "20"),
      "ENGINE only ever writes enabled engines: " + engineWrites);
check(values.engine === "20", "and stops at the last one: " + values.engine);

/* ---- the preset pads (second row, 84-91) ------------------------------------ */
ticks(8);
check(leds[84] === LED.OFF && leds[91] === LED.OFF, "empty presets are dark");
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);  /* tap an empty one */
check(!writes.some(([k]) => k === "state"), "an empty preset loads nothing");
check(/EMPTY 3/.test(ui.title()), "and says so: " + ui.title());
values.engine = "20";
hwShift = true; ticks(1);
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);  /* Shift+tap saves */
hwShift = false; ticks(1);
const savedList = parsePresets(files.get(PRESET_PATH));
check(savedList[2] && JSON.parse(savedList[2]).engine === 20, "Shift+pad stores the state: " + files.get(PRESET_PATH));
check(/SAVED PRESET 3/.test(ui.title()), "save confirmed: " + ui.title());
ticks(8);
check(leds[86] === LED.PRESET_CURRENT && leds[84] === LED.OFF, "saved preset lit white");
values.engine = "3";
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);  /* tap recalls */
check(writes.at(-1)[0] === "state" && values.engine === "20", "tap recalls the state: " + writes.at(-1));
check(PRESET_FIRST === 84, "presets on the second row");

/* ---- the screen closes: pads, colours and touches go back ---------------- */
ui.onMidi([0x90, 3, 127]);                           /* hold TIMBRE */
shown = false;
ticks(1);
check(padBlocked === false, "pads handed back");
check(leds[68] === 5 && leds[99] === 7 && leds[80] === 0, "Move's pad colours restored");
check(touchesSent().at(-1) === "timbre,0", "a held knob is released on leave");
shown = true;
ticks(1);
check(padBlocked === true, "taken again when shown");

/* ---- Back at the grid leaves ---------------------------------------------- */
check(ui.handleBack() === false, "Back at the grid exits");
ui.releaseAll();
check(padBlocked === false, "leaving releases the pads");

/* a failed status read keeps the last answer */
status = null;
ticks(STATUS_EVERY_TICKS);
check(ui.state.step === 3, "null status read changes nothing");
check(ENGINE_NAMES.length === 24, "24 engine names");

check(fb.clipped() === 0, `no pixels off screen (${fb.clipped()})`);
if (failures) { console.error(`${failures} UI check(s) failed`); process.exit(1); }
console.log("PASS: UI drives the page engine, pads (REC/RANDOM/ENGINES/16 gates/presets), touch + heartbeat, shift+touch clear, scale-index PITCH names, ENGINES filter, pages");
