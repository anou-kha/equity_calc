// A copy of the calculator module on the page's own thread, for quick jobs like the range picker
// (reading and writing range notation takes well under a millisecond). Calculations run in the
// Web Worker instead (worker.js), and this copy keeps working while one of them is running. It is
// loaded the first time it's needed.
import createPokerModule from "./poker_wasm.js";

let loading = null;

export function pageModule() {
  loading ??= createPokerModule();
  return loading;
}
