// The range picker: a 13×13 grid of hand classes (pairs on the diagonal, suited hands above it,
// offsuit below), in a dialog. Clicking or dragging across cells adds or removes whole classes;
// the suit buttons below the grid add or remove single hands of the last class touched.
//
// The selection is one Set of hands, numbered as in the C++ code. Everything on screen (cell
// shading, suit buttons, the range text and the count) is redrawn from it after every change, so
// the views can't disagree. The text comes from the C++ formatRange() (via page_module.js), so it
// is exactly what the calculator reads back.
import { pageModule } from "./page_module.js";

const RANKS = "23456789TJQKA";
const SUITS = "cdhs";
const SUIT_SYMBOLS = { c: "♣", d: "♦", h: "♥", s: "♠" };
const ALL_HANDS = 1326;

// A hand's number, as in range.cpp: with card indices hi > lo, hi * (hi - 1) / 2 + lo.
function handKey(a, b) {
  const high = Math.max(a, b);
  const low = Math.min(a, b);
  return (high * (high - 1)) / 2 + low;
}

// The 169 classes in grid order: row and column 0 is the ace. A cell above the diagonal is
// suited (its row's rank is the high card), one below it offsuit.
function makeClasses() {
  const classes = [];
  for (let row = 0; row < 13; row++) {
    for (let column = 0; column < 13; column++) {
      const high = 12 - Math.min(row, column);
      const low = 12 - Math.max(row, column);
      const kind = row === column ? "pair" : column > row ? "suited" : "offsuit";
      const suffix = kind === "suited" ? "s" : kind === "offsuit" ? "o" : "";
      const hands = [];
      for (let suit1 = 0; suit1 < 4; suit1++) {
        for (let suit2 = 0; suit2 < 4; suit2++) {
          if (kind === "pair" && suit2 <= suit1) continue;
          if (kind === "suited" && suit1 !== suit2) continue;
          if (kind === "offsuit" && suit1 === suit2) continue;
          const a = high * 4 + suit1;
          const b = low * 4 + suit2;
          hands.push([Math.max(a, b), Math.min(a, b)]);  // higher card first
        }
      }
      classes.push({ name: RANKS[high] + RANKS[low] + suffix, kind, high, low, hands });
    }
  }
  return classes;
}

const CLASSES = makeClasses();
const HAND_BY_KEY = new Map(CLASSES.flatMap((c) => c.hands.map((h) => [handKey(...h), h])));

const QUICK_PICKS = {
  pairs: (c) => c.kind === "pair",
  broadway: (c) => c.low >= RANKS.indexOf("T"),
  "suited-aces": (c) => c.kind === "suited" && c.high === 12,
};

const byId = (id) => document.getElementById(id);
const dialog = byId("range-picker");
const grid = byId("hand-grid");
const suitButtons = byId("suit-buttons");
const suitLabel = byId("suit-row-label");
const textElement = byId("range-picker-text");
const countElement = byId("range-picker-count");
const noteElement = byId("range-picker-note");

const selected = new Set();  // hand keys
let current = CLASSES[0];    // the class whose single hands the suit buttons show
let paintMode = null;        // while dragging: "add" or "remove"
let lastPainted = null;      // the cell painted last during a drag
let poker = null;            // the page's module, once loaded
let onUse = null;            // what "Use range" does with the text

function count(handClass) {
  return handClass.hands.filter((hand) => selected.has(handKey(...hand))).length;
}

function cardLabel(card) {
  const suit = SUITS[card & 3];
  return { text: RANKS[card >> 2] + SUIT_SYMBOLS[suit], red: suit === "h" || suit === "d" };
}

// ---- Drawing ------------------------------------------------------------------------------------

const cells = CLASSES.map((handClass, index) => {
  const cell = document.createElement("button");
  cell.type = "button";
  cell.className = `hand-cell ${handClass.kind}`;
  cell.textContent = handClass.name;
  cell.dataset.index = String(index);
  grid.append(cell);
  return cell;
});

function drawSuitButtons() {
  suitLabel.textContent = `Single hands of ${current.name}:`;
  suitButtons.replaceChildren(
    ...current.hands.map((hand) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "suit-hand";
      for (const card of hand) {
        const { text, red } = cardLabel(card);
        const span = document.createElement("span");
        span.textContent = text;
        if (red) span.className = "red";
        button.append(span);
      }
      const key = handKey(...hand);
      button.setAttribute("aria-pressed", String(selected.has(key)));
      const name = hand.map((card) => RANKS[card >> 2] + SUITS[card & 3]).join("");
      button.setAttribute("aria-label", name);
      button.addEventListener("click", () => {
        if (selected.has(key)) {
          selected.delete(key);
        } else {
          selected.add(key);
        }
        draw();
      });
      return button;
    }),
  );
}

function draw() {
  CLASSES.forEach((handClass, index) => {
    const chosen = count(handClass);
    const total = handClass.hands.length;
    const cell = cells[index];
    cell.style.setProperty("--fill", `${(100 * chosen) / total}%`);
    cell.setAttribute("aria-pressed", chosen === total ? "true" : chosen > 0 ? "mixed" : "false");
    cell.setAttribute("aria-label", `${handClass.name}: ${chosen} of ${total} hands`);
    cell.title = `${handClass.name}: ${chosen} of ${total} hands`;
  });
  drawSuitButtons();
  const hands = [...selected].map((key) => HAND_BY_KEY.get(key));
  const range = JSON.parse(poker.formatRange(hands));
  textElement.textContent = range.text || "(no hands)";
  const n = selected.size;
  const percent = ((100 * n) / ALL_HANDS).toFixed(1);
  const amount = `${n.toLocaleString("en-US")} ${n === 1 ? "hand" : "hands"}`;
  countElement.textContent = `${amount} · ${percent}%`;
}

// ---- Changing the selection ---------------------------------------------------------------------

function setClass(handClass, add) {
  for (const hand of handClass.hands) {
    const key = handKey(...hand);
    if (add) {
      selected.add(key);
    } else {
      selected.delete(key);
    }
  }
  current = handClass;
}

function cellAt(x, y) {
  return document.elementFromPoint(x, y)?.closest(".hand-cell") ?? null;
}

// Pointer (mouse, pen or touch): the first cell decides whether this drag adds or removes, and
// every cell the pointer then passes over gets the same. elementFromPoint finds the cell under
// the pointer, which also works for touch, where all pointer events go to the first element.
grid.addEventListener("pointerdown", (event) => {
  const cell = event.target.closest(".hand-cell");
  if (!cell || event.button !== 0) return;
  event.preventDefault();  // no text selection while dragging
  cell.focus();
  const handClass = CLASSES[Number(cell.dataset.index)];
  paintMode = count(handClass) === handClass.hands.length ? "remove" : "add";
  lastPainted = cell;
  setClass(handClass, paintMode === "add");
  draw();
});

grid.addEventListener("pointermove", (event) => {
  if (paintMode === null) return;
  const cell = cellAt(event.clientX, event.clientY);
  if (!cell || cell === lastPainted) return;
  lastPainted = cell;
  setClass(CLASSES[Number(cell.dataset.index)], paintMode === "add");
  draw();
});

const stopPainting = () => {
  paintMode = null;
  lastPainted = null;
};
window.addEventListener("pointerup", stopPainting);
window.addEventListener("pointercancel", stopPainting);

// Keyboard: Enter or Space on a cell "clicks" it with event.detail 0 (a mouse click has 1 or more
// and was already handled on pointerdown).
grid.addEventListener("click", (event) => {
  const cell = event.target.closest(".hand-cell");
  if (!cell || event.detail !== 0) return;
  const handClass = CLASSES[Number(cell.dataset.index)];
  setClass(handClass, count(handClass) !== handClass.hands.length);
  draw();
});

for (const button of dialog.querySelectorAll("[data-quick]")) {
  button.addEventListener("click", () => {
    const pick = button.dataset.quick;
    if (pick === "clear") {
      selected.clear();
    } else {
      for (const handClass of CLASSES.filter(QUICK_PICKS[pick])) setClass(handClass, true);
    }
    draw();
  });
}

byId("range-picker-cancel").addEventListener("click", () => dialog.close());
byId("range-picker-use").addEventListener("click", () => {
  onUse(JSON.parse(poker.formatRange([...selected].map((key) => HAND_BY_KEY.get(key)))).text);
  dialog.close();
});

// ---- Opening ------------------------------------------------------------------------------------

// Opens the picker on `text` (a range in notation, possibly empty). "Use range" calls use(text)
// with the new range's notation.
export async function openRangePicker({ title, text, use }) {
  poker = await pageModule();
  onUse = use;
  byId("range-picker-title").textContent = title;
  selected.clear();
  noteElement.textContent = "";
  current = CLASSES[0];
  if (text.trim() !== "") {
    const range = JSON.parse(poker.parseRange(text));
    if (range.ok) {
      for (const hand of range.hands) selected.add(handKey(...hand));
      // Start the suit buttons on the first class in the grid that has any of the range's hands.
      current = CLASSES.find((handClass) => count(handClass) > 0) ?? current;
    } else {
      noteElement.textContent =
        `The picker starts empty, because the range couldn't be read: ${range.message}.`;
    }
  }
  draw();
  dialog.showModal();
  dialog.focus();  // not the first cell, which would look highlighted; Tab moves into the grid
}
