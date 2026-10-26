// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios
// Run: node --test workspace-page/tests/
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const W = require("../workspace-model.js");

const card = (over = {}) => ({ kind: "image", name: "Scale.png", x: 0.1, y: 0.1, w: 0.4, aspect: 1.3, page: 1, hidden: false, ...over });
const stroke = (cardId, over = {}) => ({ card: cardId, kind: "pen", colour: "#f2a541", points: [[0.1, 0.1], [0.2, 0.3]], ...over });

function pair() {
  const t = W.emptyState(), s = W.emptyState();
  return { t, s, teacher: W.createSession(t, "Ms Lee", "teacher"), student: W.createSession(s, "Ava", "student") };
}

// apply an op everywhere, as the network would
const everywhere = (states, op) => states.map((st) => W.applyOp(st, op));

test("cards and marks are added and drawn in order", () => {
  const st = W.emptyState();
  const me = W.createSession(st, "Ms Lee", "teacher");
  const add = me.addCard(card());
  assert.equal(W.applyOp(st, add), true);
  const m1 = me.addMark(stroke(add.id));
  const m2 = me.addMark(stroke(add.id, { kind: "highlight", colour: "#f5e663" }));
  W.applyOp(st, m2);
  W.applyOp(st, m1);  // arrives out of order
  assert.deepEqual(W.marksOf(st, add.id).map((m) => m.id), [m1.id, m2.id]);
  // a duplicate add is ignored
  assert.equal(W.applyOp(st, m1), false);
});

test("validation rejects bad or hostile operations", () => {
  const st = W.emptyState();
  const me = W.createSession(st, "Ms Lee", "teacher");
  const good = me.addCard(card());
  assert.equal(W.validateOp(good), null);
  const bad = [
    null, 42, [], { ...good, v: 2 }, { ...good, type: "dropTables" }, { ...good, extra: 1 },
    { ...good, id: "../etc" }, { ...good, id: "x".repeat(65) }, { ...good, by: "" }, { ...good, ts: 0 }, { ...good, ts: 1.5 },
    { ...good, role: "admin" }, { ...good, card: card({ kind: "video" }) }, { ...good, card: card({ w: 0 }) },
    { ...good, card: card({ x: NaN }) }, { ...good, card: card({ name: "n".repeat(121) }) }, { ...good, card: card({ page: 0 }) },
  ];
  for (const op of bad) assert.notEqual(W.validateOp(op), null, JSON.stringify(op));
  for (const op of bad) assert.equal(W.applyOp(st, op), false);

  W.applyOp(st, good);
  const okMark = me.addMark(stroke(good.id));
  const badMarks = [
    stroke(good.id, { colour: "red" }), stroke(good.id, { kind: "laser" }), stroke(good.id, { points: [] }),
    stroke(good.id, { points: [[0.1, 9]] }), stroke(good.id, { points: [[0.1]] }), stroke(good.id, { points: Array(2001).fill([0.5, 0.5]) }),
    stroke(good.id, { kind: "arrow", points: [[0, 0], [1, 1], [0.5, 0.5]] }), stroke(good.id, { kind: "text", points: [[0.5, 0.5]], text: " " }),
    stroke(good.id, { kind: "text", points: [[0.5, 0.5]], text: "t".repeat(501) }), stroke(good.id, { text: "pens have no text" }),
  ];
  assert.equal(W.validateOp(okMark), null);
  for (const m of badMarks) assert.notEqual(W.validateOp({ ...okMark, mark: m }), null, JSON.stringify(m).slice(0, 80));
  // a mark on a card that doesn't exist is dropped
  assert.equal(W.applyOp(st, me.addMark(stroke("c.nope.1"))), false);
});

test("students can't remove the teacher's things, clear the teacher's marks or reset", () => {
  const { t, s, teacher, student } = pair();
  const c = teacher.addCard(card());
  everywhere([t, s], c);
  const tm = teacher.addMark(stroke(c.id));
  const sm = student.addMark(stroke(c.id, { colour: "#47b0f8" }));
  everywhere([t, s], tm);
  everywhere([t, s], sm);

  for (const op of [student.removeCard(c.id), student.removeMark(tm.id), student.reset(), student.updateCard(c.id, { name: "Mine now" })]) {
    everywhere([t, s], op);
  }
  assert.ok(t.cards[c.id] && t.marks[tm.id]);
  assert.equal(t.cards[c.id].name, "Scale.png");

  // a student's clear only clears their own marks, even when they ask for more
  everywhere([t, s], student.clearMarks(null, null));
  assert.ok(t.marks[tm.id]);
  assert.equal(t.marks[sm.id], undefined);

  // students can remove their own things; the teacher can remove anything
  const sm2 = student.addMark(stroke(c.id));
  everywhere([t, s], sm2);
  everywhere([t, s], student.removeMark(sm2.id));
  assert.equal(t.marks[sm2.id], undefined);
  everywhere([t, s], teacher.removeCard(c.id));
  assert.deepEqual(Object.keys(t.cards), []);
  assert.deepEqual(Object.keys(t.marks), []);
});

test("moves and page turns: the later write wins on both sides, whatever the arrival order", () => {
  const { t, s, teacher, student } = pair();
  const c = teacher.addCard(card({ kind: "pdf", aspect: 1.29 }));
  everywhere([t, s], c);
  // both move the card "at the same time" (same Lamport time): the tie goes to the later name
  t.clock = s.clock = 10;
  const tMove = teacher.updateCard(c.id, { x: 0.5 });
  const sMove = student.updateCard(c.id, { x: 0.7 });
  assert.equal(tMove.ts, sMove.ts);
  W.applyOp(t, tMove); W.applyOp(t, sMove);
  W.applyOp(s, sMove); W.applyOp(s, tMove);
  assert.equal(t.cards[c.id].x, s.cards[c.id].x);
  assert.equal(t.cards[c.id].x, "Ms Lee" > "Ava" ? 0.5 : 0.7);
  // a stale page turn doesn't undo a newer one
  const p3 = teacher.updateCard(c.id, { page: 3 });
  const p5 = teacher.updateCard(c.id, { page: 5 });
  W.applyOp(s, p5); W.applyOp(s, p3);
  assert.equal(s.cards[c.id].page, 5);
  assert.equal(W.digest(t) === W.digest(s), false);  // t hasn't seen the page turns yet
  W.applyOp(t, p3); W.applyOp(t, p5);
  assert.equal(W.digest(t), W.digest(s));
});

test("removals win over late adds, and clear keeps marks drawn afterwards", () => {
  const { t, s, teacher, student } = pair();
  const c = teacher.addCard(card());
  everywhere([t, s], c);
  const m = student.addMark(stroke(c.id));
  // the teacher removes the mark before the student's add reaches them
  W.applyOp(t, teacher.removeMark(m.id));
  W.applyOp(t, m);
  assert.equal(t.marks[m.id], undefined);

  const before = teacher.addMark(stroke(c.id));
  everywhere([t, s], before);
  const clear = teacher.clearMarks(c.id, null);
  t.clock = s.clock = clear.ts + 5;
  const after = student.addMark(stroke(c.id));
  // the student's new mark arrives at the teacher before the clear arrives at the student
  W.applyOp(t, clear); W.applyOp(t, after);
  W.applyOp(s, after); W.applyOp(s, clear);
  assert.ok(t.marks[after.id] && s.marks[after.id]);
  assert.equal(s.marks[before.id], undefined);
});

test("reset clears the board for everyone but only when the teacher does it", () => {
  const { t, s, teacher } = pair();
  for (let i = 0; i < 3; i++) everywhere([t, s], teacher.addCard(card({ name: `Card ${i}` })));
  everywhere([t, s], teacher.reset());
  assert.deepEqual(Object.keys(s.cards), []);
});

test("snapshots bring a joining or reconnecting person up to date", () => {
  const { t, teacher } = pair();
  const c1 = teacher.addCard(card());
  const c2 = teacher.addCard(card({ kind: "board", name: "Whiteboard" }));
  [c1, c2].forEach((op) => W.applyOp(t, op));
  W.applyOp(t, teacher.updateCard(c1.id, { page: 4, x: 0.3 }));
  const m = teacher.addMark(stroke(c1.id, { kind: "text", points: [[0.5, 0.5]], text: "Breathe here" }));
  W.applyOp(t, m);
  const gone = teacher.addMark(stroke(c2.id));
  W.applyOp(t, gone);
  W.applyOp(t, teacher.removeMark(gone.id));

  // a fresh student merges the teacher's snapshot (sent as JSON over the network)
  const snap = JSON.parse(JSON.stringify(W.snapshot(t)));
  const fresh = W.emptyState();
  assert.equal(W.mergeSnapshot(fresh, snap, "teacher"), true);
  assert.equal(W.digest(fresh), W.digest(t));
  assert.equal(fresh.marks[m.id].text, "Breathe here");
  // merging again changes nothing
  assert.equal(W.mergeSnapshot(fresh, snap, "teacher"), false);

  // a student who missed the removal while disconnected loses the mark on rejoin
  const stale = W.emptyState();
  W.mergeSnapshot(stale, snap, "teacher");
  W.applyOp(stale, gone);  // replayed late: tombstoned, ignored
  assert.equal(stale.marks[gone.id], undefined);
});

test("a snapshot can't be used to grant rights or delete the teacher's things", () => {
  const { t, s, teacher, student } = pair();
  const c = teacher.addCard(card());
  everywhere([t, s], c);
  const tm = teacher.addMark(stroke(c.id));
  everywhere([t, s], tm);

  // a forged snapshot from a student claiming teacher rights
  const forged = {
    v: 1, clock: 999,
    removed: [c.id, tm.id],
    ops: [
      { v: 1, type: "updateCard", id: c.id, by: "Ava", ts: 500, role: "teacher", changes: { name: "Hacked" } },
      { v: 1, type: "reset", id: "r.x.1", by: "Ava", ts: 600, role: "teacher" },
    ],
  };
  W.mergeSnapshot(t, forged, "student");  // the app knows the sender is the student
  assert.ok(t.cards[c.id] && t.marks[tm.id]);
  assert.equal(t.cards[c.id].name, "Scale.png");
  // nonsense is ignored
  assert.equal(W.mergeSnapshot(t, { v: 1, ops: "x" }, "student"), false);
  assert.equal(W.mergeSnapshot(t, forged, "admin"), false);
  assert.equal(W.mergeSnapshot(t, null, "teacher"), false);
  assert.ok(student);  // (session unused beyond setup)
});

test("limits stop a flood of cards or marks", () => {
  const st = W.emptyState();
  const me = W.createSession(st, "Ms Lee", "teacher");
  let added = 0;
  for (let i = 0; i < W.LIMITS.cards + 5; i++) if (W.applyOp(st, me.addCard(card()))) added++;
  assert.equal(added, W.LIMITS.cards);
});
