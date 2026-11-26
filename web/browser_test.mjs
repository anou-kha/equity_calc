// End-to-end test of the web page in headless Chrome, run by CTest in the WebAssembly build when
// Chrome is installed (see CMakeLists.txt):
//
//   node web/browser_test.mjs <chrome> <site folder> [screenshot folder]
//
// It serves the site on localhost, starts Chrome, and drives it through the DevTools protocol (the
// JSON-over-WebSocket interface Chrome's developer tools use): it types into the inputs, clicks
// buttons, waits for the worker's results and reads the page, like a user would. It also fails on
// any error or warning in the browser console.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";

const [chromePath, siteFolder, screenshotFolder] = process.argv.slice(2);
if (!chromePath || !siteFolder) {
  console.error("usage: node browser_test.mjs <chrome> <site folder> [screenshot folder]");
  process.exit(2);
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// ---- A static file server for the site ----------------------------------------------------------

const CONTENT_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
};
const server = http.createServer((request, response) => {
  const name = new URL(request.url, "http://localhost").pathname.replace(/^\/$/, "/index.html");
  const file = path.join(siteFolder, path.normalize(name));
  try {
    const body = readFileSync(file);
    const contentType = CONTENT_TYPES[path.extname(file)] ?? "application/octet-stream";
    response.writeHead(200, { "Content-Type": contentType });
    response.end(body);
  } catch {
    response.writeHead(404).end();
  }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const baseUrl = `http://127.0.0.1:${server.address().port}/`;

// ---- Chrome and the DevTools protocol -----------------------------------------------------------

// --remote-debugging-port=0 lets Chrome pick a free port and write it to DevToolsActivePort.
// --no-sandbox: CI's Ubuntu restricts the user namespaces Chrome's sandbox needs; the only page
// loaded is our own, from localhost.
const profile = mkdtempSync(path.join(os.tmpdir(), "poker-chrome-"));
const chrome = spawn(
  chromePath,
  ["--headless=new", "--remote-debugging-port=0", `--user-data-dir=${profile}`, "--no-sandbox",
   "--no-first-run", "--no-default-browser-check", "--window-size=1000,1800", "about:blank"],
  { stdio: "ignore" },
);

let failed = false;
try {
  await runTests();
  console.log("web page: all checks passed");
} catch (error) {
  failed = true;
  console.error(error.stack ?? error);
} finally {
  chrome.kill();
  server.close();
  await sleep(500);  // let Chrome release its profile before deleting it
  try {
    rmSync(profile, { recursive: true, force: true });
  } catch {}
}
process.exit(failed ? 1 : 0);

async function runTests() {
  let port = null;
  for (let i = 0; i < 200 && port === null; i++) {
    try {
      port = readFileSync(path.join(profile, "DevToolsActivePort"), "utf8").split("\n")[0];
    } catch {
      await sleep(100);
    }
  }
  assert.ok(port, "Chrome didn't start");
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const pageTarget = targets.find((target) => target.type === "page");
  const socket = new WebSocket(pageTarget.webSocketDebuggerUrl);
  await new Promise((resolve) => socket.addEventListener("open", resolve, { once: true }));

  let nextId = 0;
  const replies = new Map();
  const consoleProblems = [];
  socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (replies.has(message.id)) {
      replies.get(message.id)(message);
      replies.delete(message.id);
    } else if (message.method === "Runtime.exceptionThrown") {
      consoleProblems.push(message.params.exceptionDetails.exception?.description);
    } else if (message.method === "Runtime.consoleAPICalled" &&
               ["error", "warning"].includes(message.params.type)) {
      const args = message.params.args.map((arg) => arg.value ?? arg.description);
      consoleProblems.push(args.join(" "));
    } else if (message.method === "Log.entryAdded" && message.params.entry.level === "error") {
      consoleProblems.push(`${message.params.entry.text} ${message.params.entry.url ?? ""}`);
    }
  });
  const send = (method, params = {}) =>
    new Promise((resolve) => {
      const id = ++nextId;
      replies.set(id, resolve);
      socket.send(JSON.stringify({ id, method, params }));
    });

  // Runs JavaScript in the page and returns its value.
  async function page(expression) {
    const reply = await send("Runtime.evaluate",
                             { expression, awaitPromise: true, returnByValue: true });
    if (reply.result.exceptionDetails) {
      throw new Error(`${expression}\n${reply.result.exceptionDetails.exception?.description}`);
    }
    return reply.result.result.value;
  }
  async function waitFor(expression, what) {
    for (const start = Date.now(); Date.now() - start < 60_000; await sleep(50)) {
      if (await page(expression)) return;
    }
    throw new Error(`timed out waiting for ${what}`);
  }
  async function open(query = "") {
    await send("Page.navigate", { url: baseUrl + query });
    await waitFor("document.readyState === 'complete'", "the page to load");
  }
  async function screenshot(name) {
    if (!screenshotFolder) return;
    mkdirSync(screenshotFolder, { recursive: true });
    const { width, height } = (await send("Page.getLayoutMetrics")).result.cssContentSize;
    const shot = await send("Page.captureScreenshot", {
      format: "png", captureBeyondViewport: true, clip: { x: 0, y: 0, width, height, scale: 1 },
    });
    writeFileSync(path.join(screenshotFolder, name), Buffer.from(shot.result.data, "base64"));
  }
  // el("board") is the JavaScript expression for that element, to use inside page(...).
  const el = (id) => `document.getElementById(${JSON.stringify(id)})`;
  const type = (id, value) => page(`${el(id)}.value = ${JSON.stringify(value)}`);
  const click = (id) => page(`${el(id)}.click()`);
  const text = (id) => page(`${el(id)}.textContent`);
  const results = () => page(`[...document.querySelectorAll('#result-rows tr')].map((row) =>
      [...row.cells].map((cell) => cell.textContent.trim().replace(/\\s+/g, ' ')))`);
  const waitForResults = (what) =>
    waitFor(`!${el("results")}.hidden && !${el("calculate")}.disabled`, what);
  const colorScheme = (value) =>
    send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value }] });

  await send("Runtime.enable");
  await send("Log.enable");
  await send("Page.enable");
  await colorScheme("light");

  // The first visit calculates the first example. 46.02% / 53.59% / 0.39% is the published value.
  await open();
  await waitForResults("the first example");
  assert.deepEqual(await results(), [
    ["Player 1", "AsKs", "1", "46.02%", "0.39%", "46.21%"],
    ["Player 2", "QhQd", "1", "53.59%", "0.39%", "53.79%"],
  ]);
  assert.match(await text("summary"), /^Exact: all 1,712,304 boards enumerated in /);
  assert.equal(await page("location.search"), "?p=AsKs&p=QhQd");
  await screenshot("desktop-light.png");

  // A shared link: ranges on a flop.
  await open("?p=QQ%2B%2C+AKs&p=JJ-99&board=ah7d2c");
  await waitForResults("the shared link");
  assert.deepEqual((await results()).map((row) => row.slice(1, 6)), [
    ["QQ+, AKs", "22", "92.97%", "0.00%", "92.97%"],
    ["JJ-99", "18", "7.03%", "0.00%", "7.03%"],
  ]);
  assert.equal(await text("result-board"), "A♥7♦2♣");
  assert.match(await text("summary"), /320,760 showdowns \(324 deals of the hands × 990 boards\)/);

  // An example button. AA vs KK as ranges is over the exact limit, so Monte Carlo is chosen.
  await page(`[...document.querySelectorAll('#example-buttons button')]
      .find((button) => button.textContent.startsWith('AA vs KK')).click()`);
  await waitFor(`${el("summary")}.textContent.startsWith('Monte Carlo')`, "AA vs KK");
  assert.match(await text("summary"), /Exact enumeration would take over 10,000,000 showdowns/);
  assert.match((await results())[0][5], /^81\.\d\d% ± 0\.\d\d%$/);

  // An invalid range: the message goes under that player's input, which gets the focus.
  await type("player-2", "QQ+, AKx");
  await click("calculate");
  await waitFor(`${el("player-2-error")}.textContent !== ''`, "the range error");
  assert.match(await text("player-2-error"), /at "AKx": the last letter must be 's'/);
  assert.equal(await page(`${el("player-2")}.getAttribute('aria-invalid')`), "true");
  assert.equal(await page("document.activeElement.id"), "player-2");
  assert.equal(await page(`${el("results")}.hidden`), true);

  // Markup typed into the page is shown as text, never turned into elements.
  await type("player-2", `<img src=x onerror="window.injected = true">`);
  await page(`${el("player-2-error")}.textContent = ''`);
  await click("calculate");
  await waitFor(`${el("player-2-error")}.childNodes.length > 0`, "the error");
  await sleep(200);
  const images = await page("document.querySelectorAll('img').length");
  assert.equal(images, 0, "markup became an element");
  assert.equal(await page("window.injected === undefined"), true, "a script from the input ran");
  assert.match(await text("player-2-error"), /"<img src=x onerror=/);

  // Empty inputs are caught by the page itself; board errors go under the board.
  await type("player-2", "");
  await click("calculate");
  assert.match(await text("player-2-error"), /^Enter a hand like AsKs/);
  await type("player-2", "QhQd");
  await type("board", "Js Ts 2x");
  await click("calculate");
  await waitFor(`${el("board-error")}.textContent !== ''`, "the board error");
  assert.match(await text("board-error"), /^Invalid board cards: Js Ts 2x/);

  // Stop: a long exact calculation (AA vs KK vs QQ preflop, 296 million showdowns) is stopped, and
  // the restarted worker then calculates normally.
  await open("?p=AA&p=KK&p=QQ&method=exact");
  await waitFor(`!${el("stop")}.hidden`, "the calculation to start");
  const secondsShown =
    `Number(/Calculating… ([\\d.]+) s/.exec(${el("status")}.textContent)?.[1])`;
  await waitFor(`${secondsShown} >= 0.5`, "half a second of progress");
  await click("stop");
  assert.equal(await text("status"), "Stopped.");
  assert.equal(await page(`${el("stop")}.hidden`), true);
  await waitFor(`!${el("calculate")}.disabled`, "the restarted worker");
  await page("document.querySelectorAll('.player .remove')[2].click()");
  await type("player-1", "AsKs");
  await type("player-2", "QhQd");
  await type("board", "Js Ts 2h");
  await click("calculate");
  await waitFor(`${el("summary")}.textContent.includes(' 990 boards')`, "the next result");

  // Exact enumeration of three random hands has far too many deals: a clear error, not a crash.
  await open("?p=random&p=random&p=random&method=exact");
  await waitFor(`${el("setup-error")}.textContent !== ''`, "the too-many-deals error");
  assert.match(await text("setup-error"), /^Too many deals of the hands to enumerate exactly/);
  assert.equal(await page(`${el("results")}.hidden`), true);

  // Players: at most 9; the remove buttons appear above 2.
  await open();
  await waitForResults("the first example again");
  const visibleRemoveButtons = "[...document.querySelectorAll('.player .remove')]" +
                               ".filter((button) => !button.hidden).length";
  assert.equal(await page(visibleRemoveButtons), 0);
  for (let i = 0; i < 9; i++) await click("add-player");
  assert.equal(await page("document.querySelectorAll('.player').length"), 9);
  assert.equal(await page(`${el("add-player")}.disabled`), true);

  // ---- The range picker ----

  // A cell of the hand grid, as a CSS selector, by its name ("AKs").
  const cell = async (name) => {
    const index = await page(`[...document.querySelectorAll('.hand-cell')]
        .findIndex((cell) => cell.textContent === ${JSON.stringify(name)})`);
    assert.ok(index >= 0, `no cell ${name}`);
    return `.hand-cell[data-index="${index}"]`;
  };
  const cellState = async (name) =>
    page(`document.querySelector('${await cell(name)}').getAttribute('aria-pressed')`);
  const centre = (selector) => page(`(() => {
      const box = document.querySelector('${selector}').getBoundingClientRect();
      return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
    })()`);
  // Real mouse input through Chrome: press on the first cell, move over the others, release.
  const drag = async (names) => {
    const points = [];
    for (const name of names) points.push(await centre(await cell(name)));
    const mouse = (type, { x, y }, pressed) => send("Input.dispatchMouseEvent", {
      type, x, y, button: type === "mouseMoved" ? "none" : "left", buttons: pressed ? 1 : 0,
      clickCount: 1,
    });
    await mouse("mousePressed", points[0], true);
    for (const point of points.slice(1)) await mouse("mouseMoved", point, true);
    await mouse("mouseReleased", points[points.length - 1], false);
  };
  const quick = (pick) => page(`document.querySelector('[data-quick="${pick}"]').click()`);
  const suitButtons = () => page(`[...document.querySelectorAll('#suit-buttons button')]
      .map((button) => button.getAttribute('aria-label') +
                       (button.getAttribute('aria-pressed') === 'true' ? '*' : ''))`);
  const pickerText = () => text("range-picker-text");
  const pickerOpen = `${el("range-picker")}.open`;

  // Opening shows the player's current range: AsKs is one of the four AKs hands.
  await page("document.querySelector('.player .pick').click()");
  await waitFor(pickerOpen, "the range picker");
  assert.equal(await text("range-picker-title"), "Player 1's range");
  assert.equal(await text("range-picker-count"), "1 hand · 0.1%");
  assert.equal(await pickerText(), "AsKs");
  assert.equal(await cellState("AKs"), "mixed");
  assert.equal(await text("suit-row-label"), "Single hands of AKs:");
  assert.deepEqual(await suitButtons(), ["AcKc", "AdKd", "AhKh", "AsKs*"]);
  await screenshot("picker-desktop.png");

  // Dragging across cells adds whole classes; a drag that starts on a chosen cell removes.
  await quick("clear");
  await drag(["AKs", "AQs", "AJs"]);
  assert.equal(await pickerText(), "AJs+");
  assert.equal(await text("range-picker-count"), "12 hands · 0.9%");
  await drag(["AQs", "AJs"]);
  assert.equal(await pickerText(), "AKs");
  assert.equal(await cellState("AQs"), "false");

  // The suit buttons show the class touched last (AJs) and toggle single hands.
  assert.equal(await text("suit-row-label"), "Single hands of AJs:");
  await page(`[...document.querySelectorAll('#suit-buttons button')]
      .find((button) => button.getAttribute('aria-label') === 'AhJh').click()`);
  assert.equal(await pickerText(), "AKs, AhJh");
  assert.equal(await cellState("AJs"), "mixed");

  // Keyboard: Enter or Space on a cell is a click with event.detail 0, which toggles it.
  await page(`document.querySelector('${await cell("KK")}').click()`);
  assert.equal(await pickerText(), "KK, AKs, AhJh");
  await quick("pairs");
  assert.equal(await pickerText(), "22+, AKs, AhJh");

  // "Use range" writes the text into the player's box.
  await click("range-picker-use");
  assert.equal(await page(pickerOpen), false);
  assert.equal(await page(`${el("player-1")}.value`), "22+, AKs, AhJh");

  // Opening again shows that range; Cancel leaves the box alone.
  await page("document.querySelector('.player .pick').click()");
  await waitFor(pickerOpen, "the range picker again");
  assert.equal(await text("range-picker-count"), "83 hands · 6.3%");
  await quick("clear");
  await click("range-picker-cancel");
  assert.equal(await page(`${el("player-1")}.value`), "22+, AKs, AhJh");

  // A range that can't be read: the picker says why and starts empty.
  await type("player-2", "QQ+, AKx");
  await page("document.querySelectorAll('.player .pick')[1].click()");
  await waitFor(pickerOpen, "the picker for player 2");
  assert.match(await text("range-picker-note"), /couldn't be read: invalid range .* at "AKx"/);
  assert.equal(await text("range-picker-count"), "0 hands · 0.0%");
  await click("range-picker-cancel");

  // ---- The card picker ----

  const cardSelector = (card) => `.card-cell[aria-label="${card}"]`;
  const clickCard = (card) => page(`document.querySelector('${cardSelector(card)}').click()`);
  const cardState = (card) => page(`(() => {
      const button = document.querySelector('${cardSelector(card)}');
      return button.disabled ? 'disabled' : button.getAttribute('aria-pressed');
    })()`);
  const cardPickerOpen = `${el("card-picker")}.open`;

  // Cards held by known hands, or dead, can't go on the board.
  await type("player-1", "AsKs");
  await type("player-2", "QhQd");
  await type("board", "");
  await type("dead", "9c");
  await click("pick-board");
  await waitFor(cardPickerOpen, "the board picker");
  assert.equal(await text("card-picker-title"), "Board");
  assert.equal(await page("document.activeElement.id"), "card-picker");  // no card looks focused
  for (const card of ["As", "Ks", "Qh", "Qd", "9c"]) {
    assert.equal(await cardState(card), "disabled", card);
  }
  assert.equal(await text("card-picker-unavailable"),
               "Greyed-out cards are held by Player 1, held by Player 2 and in the dead cards.");

  // Cards keep the order they were clicked in; a board of 1 or 2 cards gets a reminder.
  await clickCard("Js");
  assert.equal(await text("card-picker-note"), "A board has 0, 3, 4 or 5 cards.");
  await clickCard("Ts");
  await clickCard("2h");
  assert.equal(await text("card-picker-text"), "Js Ts 2h");
  assert.equal(await text("card-picker-count"), "3 of 5 cards");
  assert.equal(await text("card-picker-note"), "");
  assert.equal(await cardState("Js"), "true");
  await screenshot("card-picker-desktop.png");

  // At five cards the rest are disabled; removing one frees them again.
  await clickCard("3c");
  await clickCard("4d");
  assert.equal(await cardState("5h"), "disabled");
  await clickCard("4d");
  await clickCard("3c");
  assert.equal(await cardState("5h"), "false");
  await click("card-picker-use");
  assert.equal(await page(cardPickerOpen), false);
  assert.equal(await page(`${el("board")}.value`), "Js Ts 2h");

  // The dead-card picker greys out the board instead; Cancel leaves the box alone.
  await click("pick-dead");
  await waitFor(cardPickerOpen, "the dead-card picker");
  assert.equal(await text("card-picker-title"), "Dead cards");
  assert.equal(await cardState("Js"), "disabled");
  assert.equal(await cardState("9c"), "true");
  await clickCard("8c");
  assert.equal(await text("card-picker-text"), "9c 8c");
  assert.equal(await text("card-picker-count"), "2 cards");
  await click("card-picker-cancel");
  assert.equal(await page(`${el("dead")}.value`), "9c");

  // Cards that can't be read: the picker says why and starts empty.
  await type("board", "Js Zz");
  await click("pick-board");
  await waitFor(cardPickerOpen, "the board picker again");
  assert.match(await text("card-picker-note"), /couldn't be read: invalid cards: Js Zz/);
  assert.equal(await text("card-picker-count"), "0 of 5 cards");
  await click("card-picker-cancel");

  // At phone width, nothing is wider than the screen, the results table included.
  await open("?p=QQ%2B%2C+AKs&p=random&p=random");
  await waitForResults("three players");
  await send("Emulation.setDeviceMetricsOverride",
             { width: 390, height: 1500, deviceScaleFactor: 1, mobile: true });
  await sleep(300);
  assert.equal(await page("document.documentElement.scrollWidth <= window.innerWidth"), true);
  assert.equal(await page(`[...document.querySelectorAll('.table-scroll')]
      .every((element) => element.scrollWidth <= element.clientWidth)`), true);
  await screenshot("phone-light.png");
  await colorScheme("dark");
  await screenshot("phone-dark.png");

  // The picker fits a phone too, with cells big enough to tap.
  await page("document.querySelector('.player .pick').click()");
  await waitFor(pickerOpen, "the range picker on a phone");
  const fits = await page(`(() => {
      const picker = document.getElementById('range-picker');
      const cell = document.querySelector('.hand-cell').getBoundingClientRect();
      const inside = picker.getBoundingClientRect().right <= window.innerWidth;
      return { fits: inside && picker.scrollWidth <= picker.clientWidth, cellWidth: cell.width };
    })()`);
  assert.equal(fits.fits, true, "the picker is wider than the screen");
  assert.ok(fits.cellWidth >= 22, `cells are only ${fits.cellWidth}px wide`);
  await screenshot("picker-phone-dark.png");
  await click("range-picker-cancel");

  await click("pick-board");
  await waitFor(cardPickerOpen, "the board picker on a phone");
  assert.equal(await page(`(() => {
      const picker = document.getElementById('card-picker');
      return picker.getBoundingClientRect().right <= window.innerWidth &&
             picker.scrollWidth <= picker.clientWidth;
    })()`), true, "the card picker is wider than the screen");
  await screenshot("card-picker-phone-dark.png");
  await click("card-picker-cancel");

  assert.deepEqual(consoleProblems, [], "errors or warnings in the browser console");
  socket.close();
}
