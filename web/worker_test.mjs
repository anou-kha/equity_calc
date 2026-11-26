// Test of the Web Worker's messages (worker.js), run by CTest under Node.js in the WebAssembly
// build, on the assembled site:
//
//   node web/worker_test.mjs <site folder>
//
// Node doesn't give scripts a Web Worker's global scope, so the test gives worker.js a stand-in
// `self` that collects what it posts, then sends it messages the way the page does.
import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const site = process.argv[2];
if (!site) {
  console.error("usage: node worker_test.mjs <site folder>");
  process.exit(2);
}

const posted = [];
let wakeUp = null;
globalThis.self = {
  onmessage: null,
  postMessage(message) {
    posted.push(message);
    wakeUp?.();
  },
};

// The next message the worker posts.
async function nextMessage() {
  while (posted.length === 0) await new Promise((resolve) => (wakeUp = resolve));
  return posted.shift();
}

function send(id, request) {
  self.onmessage({ data: { id, request } });
}

await import(pathToFileURL(path.join(site, "worker.js")).href);

const ready = await nextMessage();
assert.equal(ready.type, "ready");
assert.match(ready.version, /^\d+\.\d+\.\d+$/);

send(7, { ranges: ["AsKs", "QhQd"], board: "Js Ts 2h" });
const result = await nextMessage();
assert.equal(result.type, "result");
assert.equal(result.id, 7);
assert.equal(result.result.ok, true);
assert.equal(result.result.players[0].win, 0.5606060606060606);

send(8, { ranges: ["AsKs", "AKx"] });
const error = await nextMessage();
assert.deepEqual(
  [error.type, error.id, error.result.ok, error.result.field],
  ["result", 8, false, "range"],
);

// A JavaScript exception thrown into the module stands in for an abort inside it.
send(9, {
  get ranges() {
    throw new Error("boom");
  },
});
const crash = await nextMessage();
assert.deepEqual([crash.type, crash.id], ["crashed", 9]);
assert.match(crash.message, /boom/);

console.log("worker messages: all checks passed");
