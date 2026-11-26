// The page's logic: the player inputs, the calculator's Web Worker (worker.js), results and errors,
// and the URL, which holds the inputs so that a calculation can be shared as a link.
//
// Everything typed into the page is shown with textContent, never innerHTML, so input like
// "<img onerror=...>" is displayed as text and can't add markup or scripts to the page.

import { openRangePicker } from "./range_picker.js";

const MIN_PLAYERS = 2;
const MAX_PLAYERS = 9;

// The browser runs the calculator on one thread, at about 15 million heads-up showdowns a second
// (see README.md, Benchmarks). So "automatic" enumerates at most 10 million showdowns exactly,
// under a second, and estimates anything bigger. The command-line tool uses every core and
// enumerates up to 200 million.
const EXACT_LIMIT = 10_000_000;

const DEFAULT_METHOD = "auto";
const DEFAULT_TRIALS = "1000000";
const DEFAULT_SEED = "1";

const EXAMPLES = [
  { label: "AsKs vs QhQd", ranges: ["AsKs", "QhQd"] },
  { label: "AKs vs QQ on Js Ts 2h", ranges: ["AsKs", "QhQd"], board: "Js Ts 2h" },
  { label: "AA vs KK (all suits)", ranges: ["AA", "KK"] },
  { label: "Three players, a dead card", ranges: ["AsKs", "QhQd", "7c7d"], dead: "9c" },
  { label: "Ranges on an ace-high flop", ranges: ["QQ+, AKs", "JJ-99"], board: "Ah 7d 2c" },
  { label: "QQ+, AKs vs two random hands", ranges: ["QQ+, AKs", "random", "random"] },
];

const byId = (id) => document.getElementById(id);
const form = byId("form");
const playerRowsElement = byId("player-rows");
const playerTemplate = byId("player-template");
const addPlayerButton = byId("add-player");
const boardInput = byId("board");
const deadInput = byId("dead");
const trialsSelect = byId("trials");
const seedInput = byId("seed");
const calculateButton = byId("calculate");
const stopButton = byId("stop");
const statusElement = byId("status");
const setupError = byId("setup-error");
const resultsSection = byId("results");

const numberFormat = new Intl.NumberFormat("en-US");

// ---- Player inputs ------------------------------------------------------------------------------

function playerRows() {
  return [...playerRowsElement.querySelectorAll(".player")];
}

function addPlayer(range = "") {
  const row = playerTemplate.content.firstElementChild.cloneNode(true);
  const input = row.querySelector(".range");
  input.value = range;
  row.querySelector(".pick").addEventListener("click", () => {
    openRangePicker({
      title: `${row.querySelector(".player-label").textContent}'s range`,
      text: input.value,
      use: (text) => {
        input.value = text;
      },
    });
  });
  row.querySelector(".remove").addEventListener("click", () => {
    row.remove();
    renumberPlayers();
  });
  playerRowsElement.append(row);
  renumberPlayers();
  return row;
}

function setPlayers(ranges) {
  playerRowsElement.replaceChildren();
  for (const range of ranges.slice(0, MAX_PLAYERS)) addPlayer(range);
  while (playerRows().length < MIN_PLAYERS) addPlayer();
}

const PLACEHOLDERS = ["e.g. AsKs or QQ+, AKs", "e.g. QhQd or random"];

// Gives every row its number, ids and labels, and shows the remove buttons only above 2 players.
function renumberPlayers() {
  const rows = playerRows();
  rows.forEach((row, i) => {
    const id = `player-${i + 1}`;
    const input = row.querySelector(".range");
    input.id = id;
    input.placeholder = PLACEHOLDERS[i] ?? "e.g. 7c7d or 22+";
    input.setAttribute("aria-describedby", `${id}-error`);
    row.querySelector(".error").id = `${id}-error`;
    const label = row.querySelector(".player-label");
    label.htmlFor = id;
    label.textContent = `Player ${i + 1}`;
    row.querySelector(".pick").setAttribute("aria-label", `Pick player ${i + 1}'s range`);
    const remove = row.querySelector(".remove");
    remove.setAttribute("aria-label", `Remove player ${i + 1}`);
    remove.hidden = rows.length <= MIN_PLAYERS;
  });
  addPlayerButton.disabled = rows.length >= MAX_PLAYERS;
}

addPlayerButton.addEventListener("click", () => addPlayer().querySelector(".range").focus());

function selectedMethod() {
  return form.elements.method.value;
}

// Trials and seed only matter for Monte Carlo.
function updateMonteCarloOptions() {
  const exact = selectedMethod() === "exact";
  trialsSelect.disabled = exact;
  seedInput.disabled = exact;
}

for (const radio of form.elements.method) radio.addEventListener("change", updateMonteCarloOptions);

// ---- Errors -------------------------------------------------------------------------------------

function sentence(message) {
  return message.charAt(0).toUpperCase() + message.slice(1) + (message.endsWith(".") ? "" : ".");
}

function showFieldError(input, errorElement, message) {
  input.setAttribute("aria-invalid", "true");
  errorElement.textContent = sentence(message);
}

function clearErrors() {
  for (const input of form.querySelectorAll("[aria-invalid]")) {
    input.removeAttribute("aria-invalid");
  }
  for (const element of form.querySelectorAll(".error")) element.textContent = "";
}

// Shows an error from the calculator next to the input it's about.
function showCalculatorError(result) {
  if (result.field === "range" && playerRows()[result.player]) {
    const row = playerRows()[result.player];
    const input = row.querySelector(".range");
    showFieldError(input, row.querySelector(".error"), result.message);
    input.focus();
  } else if (result.field === "board") {
    showFieldError(boardInput, byId("board-error"), result.message);
    boardInput.focus();
  } else if (result.field === "dead") {
    showFieldError(deadInput, byId("dead-error"), result.message);
    deadInput.focus();
  } else {
    setupError.textContent = sentence(result.message);
  }
}

// ---- Reading the inputs and the URL -------------------------------------------------------------

// The request for calculate() (wasm/bindings.cpp), or null after showing what's wrong.
function readRequest() {
  let valid = true;
  const ranges = playerRows().map((row) => {
    const input = row.querySelector(".range");
    const range = input.value.trim();
    if (range === "") {
      const message = "enter a hand like AsKs or a range like QQ+, AKs";
      showFieldError(input, row.querySelector(".error"), message);
      valid = false;
    }
    return range;
  });
  const seedText = seedInput.value.trim();
  const seed = Number(seedText);
  if (seedText === "" || !Number.isSafeInteger(seed) || seed < 0) {
    showFieldError(seedInput, byId("seed-error"), "the seed must be a whole number, 0 or more");
    valid = false;
  }
  if (!valid) return null;
  return {
    ranges,
    board: boardInput.value.trim(),
    dead: deadInput.value.trim(),
    method: selectedMethod(),
    exactLimit: EXACT_LIMIT,
    trials: Number(trialsSelect.value),
    seed,
  };
}

// Keeps the inputs in the address bar (?p=AsKs&p=QhQd&board=Js+Ts+2h), leaving out defaults.
function saveToUrl(request) {
  const params = new URLSearchParams();
  for (const range of request.ranges) params.append("p", range);
  if (request.board) params.set("board", request.board);
  if (request.dead) params.set("dead", request.dead);
  if (request.method !== DEFAULT_METHOD) params.set("method", request.method);
  if (String(request.trials) !== DEFAULT_TRIALS) params.set("trials", request.trials);
  if (String(request.seed) !== DEFAULT_SEED) params.set("seed", request.seed);
  history.replaceState(null, "", `${location.pathname}?${params}`);
}

// Fills the inputs from the URL, or with a first example if it has no players.
function loadFromUrl() {
  const params = new URLSearchParams(location.search);
  const ranges = params.getAll("p");
  setPlayers(ranges.length > 0 ? ranges : EXAMPLES[0].ranges);
  boardInput.value = params.get("board") ?? "";
  deadInput.value = params.get("dead") ?? "";
  const method = [...form.elements.method].find((radio) => radio.value === params.get("method"));
  if (method) method.checked = true;
  if ([...trialsSelect.options].some((option) => option.value === params.get("trials"))) {
    trialsSelect.value = params.get("trials");
  }
  if (params.has("seed")) seedInput.value = params.get("seed");
  updateMonteCarloOptions();
}

// ---- Showing results ----------------------------------------------------------------------------

function percent(fraction) {
  return `${(100 * fraction).toFixed(2)}%`;
}

function duration(milliseconds) {
  if (milliseconds < 1) return `${milliseconds.toFixed(3)} ms`;
  if (milliseconds < 1000) return `${milliseconds.toFixed(1)} ms`;
  return `${(milliseconds / 1000).toFixed(2)} s`;
}

const SUITS = { c: "♣", d: "♦", h: "♥", s: "♠" };

// The board as little cards: "Js Ts 2h" becomes J♠ T♠ 2♥, hearts and diamonds in red.
function showCards(element, cards) {
  element.replaceChildren();
  if (cards === "") {
    element.textContent = "none (preflop)";
    return;
  }
  for (const card of cards.split(" ")) {
    const span = document.createElement("span");
    const suit = card.charAt(1);
    span.className = suit === "h" || suit === "d" ? "card red" : "card";
    span.textContent = card.charAt(0) + SUITS[suit];
    span.setAttribute("aria-label", card);
    element.append(span);
  }
}

function cell(text, className) {
  const td = document.createElement("td");
  td.textContent = text;
  if (className) td.className = className;
  return td;
}

function equityCell(player, monteCarlo) {
  const td = document.createElement("td");
  td.className = "equity";
  const value = document.createElement("span");
  value.className = "equity-value";
  value.textContent = percent(player.equity);
  td.append(value);
  if (monteCarlo && player.margin95 !== null) {
    const margin = document.createElement("span");
    margin.className = "margin";
    margin.textContent = `± ${percent(player.margin95)}`;
    td.append(" ", margin);  // a real space, for screen readers and copying
  }
  const bar = document.createElement("div");
  bar.className = "bar";
  const fill = document.createElement("div");
  fill.className = "bar-fill";
  fill.style.width = `${(100 * player.equity).toFixed(2)}%`;
  bar.append(fill);
  td.append(bar);
  return td;
}

// How the answer was found, in the same words as the command-line tool.
function describeMethod(result) {
  const time = duration(result.milliseconds);
  if (result.method === "exact") {
    if (result.deals === 1) {
      return `Exact: all ${numberFormat.format(result.showdowns)} boards enumerated in ${time}.`;
    }
    const boards = result.showdowns / result.deals;
    return (
      `Exact: ${numberFormat.format(result.showdowns)} showdowns ` +
      `(${numberFormat.format(result.deals)} deals of the hands × ` +
      `${numberFormat.format(boards)} boards) enumerated in ${time}.`
    );
  }
  let text =
    `Monte Carlo: ${numberFormat.format(result.showdowns)} random ` +
    `${result.knownHands ? "boards" : "deals"} (seed ${result.seed}) in ${time}. ` +
    "The true equity is within the ± margin for 95% of seeds.";
  if (result.automatic) {
    text +=
      ` Exact enumeration would take over ${numberFormat.format(EXACT_LIMIT)} showdowns; ` +
      "choose Exact under “Method and options” to force it.";
  }
  return text;
}

function showResult(result) {
  if (!result.ok) {
    resultsSection.hidden = true;
    showCalculatorError(result);
    return;
  }
  const monteCarlo = result.method === "montecarlo";
  showCards(byId("result-board"), result.board);
  byId("result-rows").replaceChildren(
    ...result.players.map((player, i) => {
      const tr = document.createElement("tr");
      tr.append(
        cell(`Player ${i + 1}`, "player-column"),
        cell(player.range, "range-text"),
        cell(numberFormat.format(player.hands), "number hands-column"),
        cell(percent(player.win), "number"),
        cell(percent(player.tie), "number"),
        equityCell(player, monteCarlo),
      );
      return tr;
    }),
  );
  byId("summary").textContent = describeMethod(result);
  resultsSection.hidden = false;
}

// ---- The worker ---------------------------------------------------------------------------------

let worker = null;
let workerReady = false;
let runWhenReady = false;  // calculate as soon as the worker is ready
let running = null;        // the calculation in progress: {id, started, timer}
let nextId = 1;

function setStatus(text) {
  statusElement.textContent = text;
}

function updateButtons() {
  calculateButton.disabled = !workerReady || running !== null;
  stopButton.hidden = running === null;
}

function startWorker() {
  workerReady = false;
  worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });
  worker.onmessage = (event) => handleWorkerMessage(event.data);
  worker.onerror = (event) => {
    setStatus(`Couldn't start the calculator${event.message ? `: ${event.message}` : ""}.`);
  };
  updateButtons();
}

function finishRunning() {
  clearInterval(running.timer);
  running = null;
  updateButtons();
}

function handleWorkerMessage(message) {
  switch (message.type) {
    case "ready":
      workerReady = true;
      byId("version").textContent = `poker-equity ${message.version}`;
      if (statusElement.textContent.startsWith("Loading")) setStatus("");
      updateButtons();
      if (runWhenReady) {
        runWhenReady = false;
        calculate();
      }
      break;
    case "failed":
      setStatus(`Couldn't load the calculator: ${message.message}`);
      break;
    case "result":
      if (running === null || message.id !== running.id) return;  // from a stopped calculation
      finishRunning();
      setStatus("");
      showResult(message.result);
      break;
    case "crashed":
      if (running !== null) finishRunning();
      setStatus("");
      setupError.textContent =
        `The calculator stopped unexpectedly (${message.message}), so it has been restarted.`;
      worker.terminate();
      startWorker();
      break;
  }
}

function calculate() {
  if (!workerReady) {
    runWhenReady = true;
    return;
  }
  if (running !== null) return;
  clearErrors();
  const request = readRequest();
  if (request === null) return;
  saveToUrl(request);
  const started = performance.now();
  running = {
    id: nextId++,
    started,
    timer: setInterval(() => {
      setStatus(`Calculating… ${((performance.now() - started) / 1000).toFixed(1)} s`);
    }, 100),
  };
  setStatus("Calculating…");
  updateButtons();
  worker.postMessage({ id: running.id, request });
}

// A calculation can't be interrupted from outside while it runs, so stopping it means throwing the
// worker away (terminate() ends its thread immediately) and starting a fresh one.
function stop() {
  if (running === null) return;
  worker.terminate();
  finishRunning();
  setStatus("Stopped.");
  startWorker();
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  calculate();
});
stopButton.addEventListener("click", stop);

// ---- Examples -----------------------------------------------------------------------------------

for (const example of EXAMPLES) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "secondary";
  button.textContent = example.label;
  button.addEventListener("click", () => {
    setPlayers(example.ranges);
    boardInput.value = example.board ?? "";
    deadInput.value = example.dead ?? "";
    stop();
    calculate();
  });
  byId("example-buttons").append(button);
}

// ---- Start --------------------------------------------------------------------------------------

loadFromUrl();
startWorker();
calculate();  // runs once the worker is ready: the URL's spot, or the first example
