// Smoke test of the browser module (poker_wasm.js) under Node.js, run by CTest in the WebAssembly
// build:
//
//   node wasm/smoke_test.mjs <path to poker_wasm.js>
//
// The expected numbers come from the native build (`poker_cli ... --json`), and must match
// exactly, not just closely: exact results are ratios of whole-number counts, and Monte Carlo uses
// the project's own portable random numbers (random.hpp), so every platform computes the same
// doubles. A mismatch means the WebAssembly build computes something different.
import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const modulePath = process.argv[2];
if (!modulePath) {
  console.error("usage: node smoke_test.mjs <path to poker_wasm.js>");
  process.exit(2);
}
const { default: createPokerModule } = await import(pathToFileURL(modulePath).href);
const poker = await createPokerModule();

let failures = 0;
function check(name, test) {
  try {
    test();
    console.log(`ok    ${name}`);
  } catch (error) {
    failures += 1;
    console.log(`FAIL  ${name}\n${error.message}`);
  }
}

function calculate(request) {
  return JSON.parse(poker.calculate(request));
}

// Compares the per-player numbers with the native build's.
function expectPlayers(result, expected) {
  assert.equal(result.ok, true, JSON.stringify(result));
  assert.equal(result.players.length, expected.length);
  expected.forEach((player, i) => {
    for (const key of Object.keys(player)) {
      assert.equal(result.players[i][key], player[key], `player ${i} ${key}`);
    }
  });
}

check("version", () => {
  assert.match(poker.version(), /^\d+\.\d+\.\d+$/);
});

check("known hands on the flop, exact", () => {
  const result = calculate({ ranges: ["AsKs", "QhQd"], board: "Js Ts 2h" });
  assert.equal(result.method, "exact");
  assert.equal(result.automatic, false);
  assert.equal(result.showdowns, 990);
  assert.equal(result.threads, 1);
  expectPlayers(result, [
    { win: 0.5606060606060606, tie: 0, equity: 0.5606060606060606, margin95: null },
    { win: 0.4393939393939394, tie: 0, equity: 0.4393939393939394, margin95: null },
  ]);
});

check("three players preflop with a dead card, exact", () => {
  const result = calculate({ ranges: ["AsKs", "QhQd", "7c7d"], dead: "9c" });
  assert.equal(result.showdowns, 1221759);  // C(45, 5) boards
  assert.equal(result.dead, "9c");
  expectPlayers(result, [
    { win: 0.39633675708548083, tie: 0.001979932212490352, equity: 0.3969967344896443 },
    { win: 0.42004110466957884, tie: 0.001979932212490352, equity: 0.4207010820737423 },
    { win: 0.18164220603244993, tie: 0.001979932212490352, equity: 0.1823021834366134 },
  ]);
});

check("ranges on the flop, exact", () => {
  const result = calculate({ ranges: ["QQ+, AKs", "JJ-99"], board: "ah7D2c" });
  assert.equal(result.board, "Ah 7d 2c");
  assert.equal(result.deals, 324);
  assert.equal(result.showdowns, 320760);
  expectPlayers(result, [
    { range: "QQ+, AKs", hands: 22, win: 0.9297418630751965, equity: 0.9297418630751965 },
    { range: "JJ-99", hands: 18, win: 0.0702581369248036, equity: 0.0702581369248036 },
  ]);
});

check("seeded Monte Carlo matches the native build exactly", () => {
  const result = calculate({
    ranges: ["QQ+, AKs", "random", "random"],
    method: "montecarlo",
    trials: 100000,
    seed: 1,
  });
  assert.equal(result.method, "montecarlo");
  assert.equal(result.seed, 1);
  assert.equal(result.deals, null);
  expectPlayers(result, [
    { win: 0.65254, tie: 0.00847, equity: 0.65624, margin95: 0.0029305963304033502 },
    { win: 0.16609, tie: 0.01063, equity: 0.17087, margin95: 0.0023116910122137056 },
    { win: 0.16804, tie: 0.01077, equity: 0.17289, margin95: 0.0023223900847510795 },
  ]);
});

check("auto switches to Monte Carlo above exactLimit", () => {
  const result = calculate({
    ranges: ["AsKs", "QhQd"],
    board: "Js Ts 2h",
    exactLimit: 989,  // one less than the 990 showdowns exact would take
    trials: 1000,
  });
  assert.equal(result.method, "montecarlo");
  assert.equal(result.automatic, true);
  assert.equal(result.showdowns, 1000);
});

check("input errors name the field", () => {
  const range = calculate({ ranges: ["AsKs", "QQ+, AKx"] });
  assert.deepEqual([range.ok, range.field, range.player], [false, "range", 1]);
  assert.match(range.message, /at "AKx"/);

  const board = calculate({ ranges: ["AsKs", "QhQd"], board: "Js Ts 2x" });
  assert.deepEqual([board.field, board.player], ["board", null]);

  const setup = calculate({ ranges: ["AsKs", "AsQd"] });
  assert.deepEqual([setup.field, setup.message], ["setup", "the same card appears more than once"]);
});

check("requests of the wrong shape are errors, not crashes", () => {
  for (const request of [
    undefined,
    "AA KK",
    { ranges: "AA KK" },
    { ranges: ["AA", 7] },
    { ranges: ["AA", "KK"], board: 5 },
    { ranges: ["AA", "KK"], method: "fast" },
    { ranges: ["AA", "KK"], trials: -1 },
    { ranges: ["AA", "KK"], trials: 1.5 },
    { ranges: ["AA", "KK"], seed: NaN },
    { ranges: ["AA", "KK"], seed: 2 ** 60 },
  ]) {
    const result = calculate(request);
    assert.equal(result.ok, false, JSON.stringify(request));
    assert.equal(result.field, "setup");
    assert.match(result.message, /^request: /);
  }
  // The module still works afterwards.
  assert.equal(calculate({ ranges: ["AsKs", "QhQd"], board: "Js Ts 2h" }).ok, true);
});

if (failures > 0) {
  console.log(`${failures} check(s) failed`);
  process.exit(1);
}
console.log("all checks passed");
