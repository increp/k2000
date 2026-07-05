import { test } from "node:test";
import assert from "node:assert/strict";
import { catalogLookup, explainChzLabel } from "./franklinExplain.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

function entries(): CatalogEntry[] {
  return [
    {
      key: "Smoke / test harness is wired",
      file: "tests/SmokeTests.cpp",
      what: "Asserts 1+1==2.",
      why: "Canary for the harness itself.",
      deviationMeans: "The harness is broken.",
      links: [],
    },
    {
      key: "ParamSnapshot / defaults match expected values",
      file: "tests/ParamSnapshotTests.cpp",
      what: "Reads defaults.",
      why: "Pins the factory default patch.",
      deviationMeans: "A default changed.",
      links: ["docs/decisions/ADR-0001.md"],
    },
  ];
}

// --- catalogLookup -----------------------------------------------------------

test("catalogLookup: hits when `${name} / ${sub}` matches a catalog key exactly", () => {
  const hit = catalogLookup(entries(), "Smoke", "test harness is wired");
  assert.ok(hit);
  assert.equal(hit?.file, "tests/SmokeTests.cpp");
  assert.equal(hit?.why, "Canary for the harness itself.");
});

test("catalogLookup: misses (returns null) when no key matches", () => {
  const miss = catalogLookup(entries(), "Nope", "not in catalog");
  assert.equal(miss, null);
});

// --- explainChzLabel: B1 full shape ------------------------------------------
// Real production shape: "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200"

test("explainChzLabel: B1 full shape — title matches the brief, body mentions model/mode/fc/res/drv/os/rate/render", () => {
  const label = "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200";
  const result = explainChzLabel(label);
  assert.equal(result.title, "Magnitude response (dual-method)");
  assert.match(result.body, /moog/);
  assert.match(result.body, /LP12/);
  assert.match(result.body, /50/);
  assert.match(result.body, /0(\.00)?/); // res/drv both 0.00
  assert.match(result.body, /2/); // os2
  assert.match(result.body, /88200/);
  assert.match(result.body, /render/);
  assert.doesNotMatch(result.body, /undefined/);
});

test("explainChzLabel: B1 full shape with 'live' — body reflects live not render", () => {
  const label = "[moog] moog/LP24/fc1000 B1 res0.90 drv0.50 os4 live 96000";
  const result = explainChzLabel(label);
  assert.equal(result.title, "Magnitude response (dual-method)");
  assert.match(result.body, /moog/);
  assert.match(result.body, /LP24/);
  assert.match(result.body, /1000/);
  assert.match(result.body, /0\.9/);
  assert.match(result.body, /0\.5/);
  assert.match(result.body, /4/);
  assert.match(result.body, /96000/);
  assert.match(result.body, /live/);
  assert.doesNotMatch(result.body, /undefined/);
});

test("explainChzLabel: B1 body is byte-for-byte the brief's pinned template with fields substituted", () => {
  const label = "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200";
  const result = explainChzLabel(label);
  assert.equal(
    result.body,
    "Measures the frequency response of moog LP12 at cutoff 50 Hz (res 0.00, drive 0.00, 2x oversampling, 88200 Hz, render). Two independent methods — stepped-sine and ESS deconvolution — must agree within 1 dB; disagreement means the measurement itself cannot be trusted (see docs/filter-validation/acceptance-criterion.md).",
  );
});

test("explainChzLabel: B2/B3/B4 bodies are byte-for-byte the brief's pinned fixed templates", () => {
  assert.equal(
    explainChzLabel("[moog] moog/LP24/fc250 B2 selfosc").body,
    "At maximum resonance, measures the self-oscillation pitch against the commanded cutoff (±3% gate below ~4 kHz per the ratified standard) and the resonant peak behavior.",
  );
  assert.equal(
    explainChzLabel("[huggett] huggett/LP24/fc1000 B3").body,
    "Drives the filter into its nonlinearity and splits distortion into harmonic content vs aliased content per oversampling tier — aliasing must fall as the OS factor rises.",
  );
  assert.equal(
    explainChzLabel("[moog] moog/LP12/fc50 B4").body,
    "Records phase and group delay from the deconvolved impulse response. Descriptive-only per register Q20 (IR not time-aligned); numbers are reported, not gated.",
  );
});

// --- explainChzLabel: B2 short shape (no res/drv/os/rate) --------------------
// Real production shape: "[moog] moog/LP24/fc250 B2 selfosc"

test("explainChzLabel: B2 short shape — title matches the brief, body is the fixed template, no 'undefined' anywhere", () => {
  const label = "[moog] moog/LP24/fc250 B2 selfosc";
  const result = explainChzLabel(label);
  assert.equal(result.title, "Resonance & self-oscillation");
  assert.match(result.body, /self-oscillation pitch/);
  assert.doesNotMatch(result.body, /undefined/);
});

test("explainChzLabel: B2 short shape with huggett model, no leading [model] prefix, still parses (fc/mode present in the match even though the template doesn't interpolate them)", () => {
  const label = "huggett/HP12/fc4000 B2 selfosc";
  const result = explainChzLabel(label);
  assert.equal(result.title, "Resonance & self-oscillation");
  assert.match(result.body, /self-oscillation pitch/);
  assert.doesNotMatch(result.body, /undefined/);
});

// --- explainChzLabel: B3 --------------------------------------------------

test("explainChzLabel: B3 — title matches the brief and body mentions aliasing/OS factor", () => {
  const label = "[huggett] huggett/LP24/fc1000 B3";
  const result = explainChzLabel(label);
  assert.equal(result.title, "THD & aliasing split");
  assert.match(result.body, /aliasing/i);
  assert.doesNotMatch(result.body, /undefined/);
});

// --- explainChzLabel: B4 --------------------------------------------------

test("explainChzLabel: B4 — body mentions register Q20 (descriptive-only)", () => {
  const label = "[moog] moog/LP12/fc50 B4";
  const result = explainChzLabel(label);
  assert.equal(result.title, "Phase / group delay (descriptive only)");
  assert.match(result.body, /Q20/);
  assert.doesNotMatch(result.body, /undefined/);
});

// --- explainChzLabel: unrecognized/garbage -----------------------------------

test("explainChzLabel: unrecognized garbage label falls back to {title: label, body: unrecognized}", () => {
  const label = "not a chz label at all";
  const result = explainChzLabel(label);
  assert.equal(result.title, label);
  assert.equal(result.body, "Unrecognized operating-point label.");
});

test("explainChzLabel: empty string falls back to the unrecognized shape", () => {
  const result = explainChzLabel("");
  assert.equal(result.title, "");
  assert.equal(result.body, "Unrecognized operating-point label.");
});
