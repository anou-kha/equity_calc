// The card picker for the board and the dead cards: the 52 cards in four rows, one per suit, in a
// dialog. Clicking a card adds or removes it; the chosen cards keep the order they were clicked
// in, so a flop clicked as J♠ T♠ 2♥ reads "Js Ts 2h". Cards that can't be chosen (held by a known
// hand, or already used by the other field) are disabled, with the reason as their tooltip.
import { pageModule } from "./page_module.js";

const RANKS = "23456789TJQKA";
const SUITS = "cdhs";
const SUIT_SYMBOLS = { c: "♣", d: "♦", h: "♥", s: "♠" };
const ROW_SUITS = [3, 2, 1, 0];  // ♠, ♥, ♦, ♣: one row each, aces first

const byId = (id) => document.getElementById(id);
const dialog = byId("card-picker");
const grid = byId("card-grid");
const countElement = byId("card-picker-count");
const textElement = byId("card-picker-text");
const noteElement = byId("card-picker-note");
const unavailableElement = byId("card-picker-unavailable");

let chosen = [];             // card indices (rank * 4 + suit), in the order clicked
let unavailable = new Map(); // card index -> why it can't be chosen
let limit = 52;              // the most cards that can be chosen (5 for the board)
let isBoard = false;
let onUse = null;

const cardText = (card) => RANKS[card >> 2] + SUITS[card & 3];

const buttons = new Map();
for (const suit of ROW_SUITS) {
  for (let rank = 12; rank >= 0; rank--) {
    const card = rank * 4 + suit;
    const button = document.createElement("button");
    button.type = "button";
    button.className = `card-cell${suit === 1 || suit === 2 ? " red" : ""}`;
    button.textContent = RANKS[rank] + SUIT_SYMBOLS[SUITS[suit]];
    button.dataset.card = String(card);
    button.setAttribute("aria-label", cardText(card));
    grid.append(button);
    buttons.set(card, button);
  }
}

function draw() {
  for (const [card, button] of buttons) {
    const isChosen = chosen.includes(card);
    const reason = unavailable.get(card);
    button.setAttribute("aria-pressed", String(isChosen));
    button.disabled = !isChosen && (reason !== undefined || chosen.length >= limit);
    button.title = reason ? `${cardText(card)}: ${reason}` : cardText(card);
  }
  const n = chosen.length;
  countElement.textContent = isBoard ? `${n} of 5 cards` : `${n} ${n === 1 ? "card" : "cards"}`;
  textElement.textContent = chosen.map(cardText).join(" ") || "(no cards)";
  // A board has 0, 3, 4 or 5 cards; say so while it has 1 or 2.
  noteElement.textContent =
    isBoard && (n === 1 || n === 2) ? "A board has 0, 3, 4 or 5 cards." : noteElement.dataset.fixed;
}

grid.addEventListener("click", (event) => {
  const button = event.target.closest(".card-cell");
  if (!button || button.disabled) return;
  const card = Number(button.dataset.card);
  chosen = chosen.includes(card) ? chosen.filter((c) => c !== card) : [...chosen, card];
  draw();
});

byId("card-picker-clear").addEventListener("click", () => {
  chosen = [];
  draw();
});
byId("card-picker-cancel").addEventListener("click", () => dialog.close());
byId("card-picker-use").addEventListener("click", () => {
  onUse(chosen.map(cardText).join(" "));
  dialog.close();
});

// Opens the picker on `text` (cards, possibly none). `taken` maps the cards that can't be chosen
// to the reason; "Use cards" calls use(text) with the chosen cards, like "Js Ts 2h".
export async function openCardPicker({ title, text, board, taken, use }) {
  const poker = await pageModule();
  byId("card-picker-title").textContent = title;
  isBoard = board;
  limit = board ? 5 : 52;
  unavailable = taken;
  onUse = use;
  chosen = [];
  noteElement.dataset.fixed = "";
  const cards = JSON.parse(poker.parseCards(text));
  if (cards.ok) {
    chosen = cards.cards.slice(0, limit);
  } else {
    noteElement.dataset.fixed =
      `The picker starts empty, because the cards couldn't be read: ${cards.message}.`;
  }
  // "Greyed-out cards are held by Player 1, held by Player 2 and in the dead cards."
  const reasons = [...new Set(taken.values())];
  const listed =
    reasons.length > 1 ? `${reasons.slice(0, -1).join(", ")} and ${reasons.at(-1)}` : reasons[0];
  unavailableElement.textContent = reasons.length > 0 ? `Greyed-out cards are ${listed}.` : "";
  draw();
  dialog.showModal();
  dialog.focus();  // not the first card, which would look highlighted; Tab moves into the grid
}
