// Runs the calculator in a Web Worker: a background thread with its own copy of the WebAssembly
// module. A calculation blocks the thread it runs on for as long as it takes (the module is
// single-threaded, see CMakeLists.txt), and running it here keeps the page responsive meanwhile.
// To stop a calculation, the page terminates this worker and starts a new one.
//
// Messages:
//   page -> worker   {id, request}              request: see wasm/bindings.cpp
//   worker -> page   {type: "ready", version}   the module has loaded
//                    {type: "failed", message}  it couldn't be loaded
//                    {type: "result", id, result}   the parsed JSON from calculate()
//                    {type: "crashed", id, message} the module aborted; the worker is unusable
import createPokerModule from "./poker_wasm.js";

const modulePromise = createPokerModule();

modulePromise.then(
  (poker) => self.postMessage({ type: "ready", version: poker.version() }),
  (error) => self.postMessage({ type: "failed", message: String(error) }),
);

self.onmessage = async (event) => {
  const { id, request } = event.data;
  const poker = await modulePromise;
  try {
    const result = JSON.parse(poker.calculate(request));
    self.postMessage({ type: "result", id, result });
  } catch (error) {
    // Only an abort inside the module (e.g. running out of memory) gets here: the calculator
    // reports bad input as JSON, not by throwing.
    self.postMessage({ type: "crashed", id, message: String(error) });
  }
};
