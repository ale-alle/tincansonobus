// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios
//
// CanDuet lesson workspace model (T-037). Pure data, no DOM: the page (index.html) renders it and
// feeds user actions in as operations; stage 2 sends the same operations to the other person.
//
// Rules
// - Every change is an operation {v, type, id, by, ts, ...}. Operations are validated before use,
//   whether they come from this page or the network (validateOp), so a bad or hostile message
//   can't break the workspace.
// - Ordering: each operation carries a Lamport time `ts`; ties are broken by author name. A field
//   changed by two people keeps the later write (last writer wins), and marks are drawn in
//   (ts, by) order, so both sides end up identical whatever order the messages arrive in.
// - Ownership: the teacher can do anything. A student can add cards and marks and change their
//   own, but can't remove other people's things, clear the teacher's marks or reset.
// - Removals leave a tombstone so a late "add" for something already removed is ignored.
// - Join / reconnect: snapshot() is the whole state; mergeSnapshot() folds one in with the same
//   rules, so sending a snapshot again is always safe.
// Classic script (not an ES module) so it also loads from file:// pages; CommonJS for tests.

(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.CanDuetWorkspace = factory();
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const VERSION = 1;
  const LIMITS = {
    cards: 40,          // cards on the board
    marks: 4000,        // marks in the whole workspace
    points: 2000,       // points in one stroke
    text: 500,          // characters in a text mark
    name: 120,          // characters in a card name
    id: 64,             // characters in an id
    author: 80,         // characters in an author name
    page: 2000,         // highest PDF page number
  };
  const CARD_KINDS = ["board", "image", "pdf"];
  const MARK_KINDS = ["pen", "highlight", "arrow", "text"];
  const COLOURS = ["#f2a541", "#47b0f8", "#e5645a", "#5ccf72", "#ffffff", "#111317", "#f5e663"];
  const ID_RE = /^[A-Za-z0-9_.:-]+$/;

  const isNum = (x) => typeof x === "number" && Number.isFinite(x);
  const isUnit = (x) => isNum(x) && x >= -0.05 && x <= 1.05;  // a little slack for strokes off the edge
  const isStr = (x, max) => typeof x === "string" && x.length <= max;
  const isId = (x) => isStr(x, LIMITS.id) && x.length > 0 && ID_RE.test(x);

  function emptyState() {
    return { v: VERSION, clock: 0, cards: {}, marks: {}, removed: {} };
  }

  // ---------------------------------------------------------------------------
  // validation
  // ---------------------------------------------------------------------------

  function validCardFields(c, partial) {
    const has = (k) => Object.prototype.hasOwnProperty.call(c, k);
    if (!partial || has("kind")) { if (!CARD_KINDS.includes(c.kind)) return false; }
    if (!partial || has("name")) { if (!isStr(c.name, LIMITS.name)) return false; }
    for (const k of ["x", "y"]) if (!partial || has(k)) { if (!isNum(c[k]) || c[k] < -1 || c[k] > 10) return false; }
    if (!partial || has("w")) { if (!isNum(c.w) || c.w < 0.05 || c.w > 2) return false; }
    if (!partial || has("aspect")) { if (!isNum(c.aspect) || c.aspect < 0.1 || c.aspect > 10) return false; }
    if (!partial || has("page")) { if (!Number.isInteger(c.page) || c.page < 1 || c.page > LIMITS.page) return false; }
    if (!partial || has("hidden")) { if (typeof c.hidden !== "boolean") return false; }
    if (has("file")) { if (c.file !== null && !isId(c.file)) return false; }  // local file key, stage 2 transfers it
    return true;
  }

  function validMark(m) {
    if (!MARK_KINDS.includes(m.kind) || !isId(m.card) || !COLOURS.includes(m.colour)) return false;
    if (!Array.isArray(m.points) || m.points.length < 1 || m.points.length > LIMITS.points) return false;
    for (const p of m.points) {
      if (!Array.isArray(p) || p.length !== 2 || !isUnit(p[0]) || !isUnit(p[1])) return false;
    }
    if (m.kind === "arrow" && m.points.length !== 2) return false;
    if (m.kind === "text") {
      if (m.points.length !== 1 || !isStr(m.text, LIMITS.text) || m.text.trim() === "") return false;
    } else if (m.text !== undefined) return false;
    if (m.kind !== "text" && m.kind !== "arrow" && m.points.length < 1) return false;
    return true;
  }

  const OP_FIELDS = {
    addCard: ["v", "type", "id", "by", "ts", "role", "card"],
    updateCard: ["v", "type", "id", "by", "ts", "role", "changes"],
    removeCard: ["v", "type", "id", "by", "ts", "role"],
    addMark: ["v", "type", "id", "by", "ts", "role", "mark"],
    removeMark: ["v", "type", "id", "by", "ts", "role"],
    clearMarks: ["v", "type", "id", "by", "ts", "role", "card", "onlyBy"],
    reset: ["v", "type", "id", "by", "ts", "role"],
  };

  // returns null when valid, else a short reason
  function validateOp(op) {
    if (!op || typeof op !== "object" || Array.isArray(op)) return "not_object";
    if (op.v !== VERSION) return "version";
    const fields = OP_FIELDS[op.type];
    if (!fields) return "type";
    for (const k of Object.keys(op)) if (!fields.includes(k)) return "field:" + k;
    if (!isId(op.id)) return "id";
    if (!isStr(op.by, LIMITS.author) || op.by.length === 0) return "by";
    if (!Number.isInteger(op.ts) || op.ts < 1 || op.ts > 1e12) return "ts";
    if (op.role !== "teacher" && op.role !== "student") return "role";
    switch (op.type) {
      case "addCard":
        if (!op.card || typeof op.card !== "object" || !validCardFields(op.card, false)) return "card";
        break;
      case "updateCard": {
        const ch = op.changes;
        if (!ch || typeof ch !== "object" || Array.isArray(ch)) return "changes";
        const keys = Object.keys(ch);
        if (keys.length === 0 || keys.some((k) => !["x", "y", "w", "page", "hidden", "name"].includes(k))) return "changes";
        if (!validCardFields(ch, true)) return "changes";
        break;
      }
      case "addMark":
        if (!op.mark || typeof op.mark !== "object" || !validMark(op.mark)) return "mark";
        break;
      case "clearMarks":
        if (op.card !== null && !isId(op.card)) return "card";
        if (op.onlyBy !== null && !isStr(op.onlyBy, LIMITS.author)) return "onlyBy";
        break;
    }
    return null;
  }

  // ---------------------------------------------------------------------------
  // applying operations
  // ---------------------------------------------------------------------------

  const later = (ts1, by1, ts2, by2) => ts1 > ts2 || (ts1 === ts2 && by1 > by2);

  function canRemove(op, owner) {
    return op.role === "teacher" || op.by === owner;
  }

  // Applies a validated operation; returns true if the state changed. Never throws on bad input
  // (it's validated again here), so network data can be fed straight in.
  function applyOp(state, op) {
    if (validateOp(op) !== null) return false;
    state.clock = Math.max(state.clock, op.ts);

    switch (op.type) {
      case "addCard": {
        if (state.removed[op.id] || state.cards[op.id]) return false;
        if (Object.keys(state.cards).length >= LIMITS.cards) return false;
        const c = op.card;
        const card = {
          id: op.id, owner: op.by, ts: op.ts,
          kind: c.kind, name: c.name, x: c.x, y: c.y, w: c.w, aspect: c.aspect, page: c.page, hidden: c.hidden,
          file: c.file === undefined ? null : c.file,
          fieldTs: {},  // per-field last write, for last-writer-wins
        };
        for (const k of ["x", "y", "w", "page", "hidden", "name"]) card.fieldTs[k] = [op.ts, op.by];
        state.cards[op.id] = card;
        return true;
      }
      case "updateCard": {
        const card = state.cards[op.id];
        if (!card) return false;
        // moving, resizing and turning pages is shared; renaming is the owner's (or the teacher's)
        if (Object.prototype.hasOwnProperty.call(op.changes, "name") && !canRemove(op, card.owner)) return false;
        let changed = false;
        for (const [k, val] of Object.entries(op.changes)) {
          const [ts, by] = card.fieldTs[k] || [0, ""];
          if (later(op.ts, op.by, ts, by)) {
            card[k] = val;
            card.fieldTs[k] = [op.ts, op.by];
            changed = true;
          }
        }
        return changed;
      }
      case "removeCard": {
        const card = state.cards[op.id];
        if (!card) {
          // removal seen before the card: remember it so the late add is ignored (teacher only)
          if (op.role === "teacher" && !state.removed[op.id]) { state.removed[op.id] = op.ts; return true; }
          return false;
        }
        if (!canRemove(op, card.owner)) return false;
        delete state.cards[op.id];
        state.removed[op.id] = op.ts;
        for (const [mid, m] of Object.entries(state.marks)) {
          if (m.card === op.id) { delete state.marks[mid]; state.removed[mid] = op.ts; }
        }
        return true;
      }
      case "addMark": {
        if (state.removed[op.id] || state.marks[op.id]) return false;
        if (!state.cards[op.mark.card]) return false;
        if (Object.keys(state.marks).length >= LIMITS.marks) return false;
        const m = op.mark;
        state.marks[op.id] = {
          id: op.id, owner: op.by, ts: op.ts, card: m.card, kind: m.kind, colour: m.colour,
          points: m.points.map((p) => [p[0], p[1]]), text: m.kind === "text" ? m.text : undefined,
        };
        return true;
      }
      case "removeMark": {
        const m = state.marks[op.id];
        if (!m) {
          if (op.role === "teacher" && !state.removed[op.id]) { state.removed[op.id] = op.ts; return true; }
          return false;
        }
        if (!canRemove(op, m.owner)) return false;
        delete state.marks[op.id];
        state.removed[op.id] = op.ts;
        return true;
      }
      case "clearMarks": {
        // students may only clear their own marks
        const onlyBy = op.role === "teacher" ? op.onlyBy : op.by;
        let changed = false;
        for (const [mid, m] of Object.entries(state.marks)) {
          if (op.card !== null && m.card !== op.card) continue;
          if (onlyBy !== null && m.owner !== onlyBy) continue;
          if (m.ts > op.ts) continue;  // drawn after the clear was made: keep it
          delete state.marks[mid];
          state.removed[mid] = op.ts;
          changed = true;
        }
        return changed;
      }
      case "reset": {
        if (op.role !== "teacher") return false;
        let changed = false;
        for (const [cid, c] of Object.entries(state.cards)) {
          if (c.ts > op.ts) continue;
          delete state.cards[cid];
          state.removed[cid] = op.ts;
          changed = true;
        }
        for (const [mid, m] of Object.entries(state.marks)) {
          if (m.ts > op.ts && state.cards[m.card]) continue;
          delete state.marks[mid];
          state.removed[mid] = op.ts;
          changed = true;
        }
        return changed;
      }
    }
    return false;
  }

  // ---------------------------------------------------------------------------
  // making operations (used by the page)
  // ---------------------------------------------------------------------------

  // A session makes ids and timestamps for one person.
  function createSession(state, author, role) {
    let counter = 0;
    const prefix = Math.random().toString(36).slice(2, 8);
    const base = (type, id) => {
      state.clock += 1;
      return { v: VERSION, type, id, by: author, ts: state.clock, role };
    };
    const newId = (kind) => `${kind}.${prefix}.${++counter}`;
    return {
      author, role,
      addCard: (card) => ({ ...base("addCard", newId("c")), card }),
      updateCard: (id, changes) => ({ ...base("updateCard", id), changes }),
      removeCard: (id) => base("removeCard", id),
      addMark: (mark) => ({ ...base("addMark", newId("m")), mark }),
      removeMark: (id) => base("removeMark", id),
      clearMarks: (card, onlyBy) => ({ ...base("clearMarks", newId("x")), card, onlyBy }),
      reset: () => base("reset", newId("r")),
    };
  }

  // marks of a card in drawing order
  function marksOf(state, cardId) {
    return Object.values(state.marks)
      .filter((m) => m.card === cardId)
      .sort((a, b) => (a.ts - b.ts) || (a.owner < b.owner ? -1 : a.owner > b.owner ? 1 : 0) || (a.id < b.id ? -1 : 1));
  }

  // ---------------------------------------------------------------------------
  // snapshots (join and reconnect)
  // ---------------------------------------------------------------------------

  // The whole state as operations: replaying them on any state gives the same result as merging.
  function snapshot(state) {
    const ops = [];
    for (const c of Object.values(state.cards)) {
      ops.push({ v: VERSION, type: "addCard", id: c.id, by: c.owner, ts: c.ts, role: "student",
        card: { kind: c.kind, name: c.name, x: c.x, y: c.y, w: c.w, aspect: c.aspect, page: c.page, hidden: c.hidden, file: c.file } });
    }
    for (const c of Object.values(state.cards)) {
      // later field writes, so last-writer-wins carries over
      for (const [k, [ts, by]] of Object.entries(c.fieldTs)) {
        if (ts > c.ts || by !== c.owner) {
          ops.push({ v: VERSION, type: "updateCard", id: c.id, by, ts, role: "student", changes: { [k]: c[k] } });
        }
      }
    }
    for (const m of Object.values(state.marks)) {
      const mark = { card: m.card, kind: m.kind, colour: m.colour, points: m.points };
      if (m.kind === "text") mark.text = m.text;
      ops.push({ v: VERSION, type: "addMark", id: m.id, by: m.owner, ts: m.ts, role: "student", mark });
    }
    return { v: VERSION, clock: state.clock, ops, removed: Object.keys(state.removed).slice(-2000) };
  }

  // senderRole comes from what the app knows about the sender (the lesson's teacher), never from
  // the snapshot itself. Only the teacher's snapshot can remove things the receiver still has;
  // anyone's can add what the receiver missed.
  function mergeSnapshot(state, snap, senderRole) {
    if (senderRole !== "teacher" && senderRole !== "student") return false;
    if (!snap || snap.v !== VERSION || !Array.isArray(snap.ops) || snap.ops.length > LIMITS.cards * 8 + LIMITS.marks) return false;
    let changed = false;
    if (Array.isArray(snap.removed)) {
      for (const id of snap.removed.slice(0, 4000)) {
        if (!isId(id) || state.removed[id]) continue;
        const has = state.cards[id] || state.marks[id];
        if (has && senderRole !== "teacher") continue;
        state.removed[id] = 1;
        if (state.cards[id]) { applyOp(state, { v: VERSION, type: "removeCard", id, by: "snapshot", ts: Math.max(1, state.clock), role: "teacher" }); changed = true; }
        if (state.marks[id]) { delete state.marks[id]; changed = true; }
      }
    }
    for (const raw of snap.ops) {
      if (!raw || typeof raw !== "object") continue;
      if (applyOp(state, { ...raw, role: senderRole })) changed = true;
    }
    if (isNum(snap.clock)) state.clock = Math.max(state.clock, Math.min(snap.clock, 1e12));
    return changed;
  }

  // a stable fingerprint of what's visible, to check two sides agree
  function digest(state) {
    const cards = Object.values(state.cards).map((c) => [c.id, c.kind, c.name, c.x, c.y, c.w, c.page, c.hidden]).sort();
    const marks = Object.keys(state.marks).sort();
    return JSON.stringify([cards, marks]);
  }

  return { VERSION, LIMITS, COLOURS, CARD_KINDS, MARK_KINDS, emptyState, validateOp, applyOp, createSession, marksOf, snapshot, mergeSnapshot, digest };
});
