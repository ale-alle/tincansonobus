// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios
//
// CanDuet lesson workspace page (T-037): renders workspace-model.js and turns clicks into
// operations. Every change goes through W.applyOp, the same path the other person's changes will
// take in stage 2 (see `send` below).
(function () {
  "use strict";
  const W = window.CanDuetWorkspace;

  // ?role=teacher|student&name=… (the app sets these; alone, you are the teacher of your own board)
  const params = new URLSearchParams(location.hash.slice(1) || location.search.slice(1));
  const role = params.get("role") === "student" ? "student" : "teacher";
  const author = (params.get("name") || "Me").slice(0, 80);

  const state = W.emptyState();
  const me = W.createSession(state, author, role);
  const files = new Map();      // local file key -> { url, pages }
  const myMarks = [];           // my mark ids, newest last, for undo
  const order = [];             // card ids, front-most last (local only)
  let tool = "move";
  let colour = W.COLOURS[0];
  let activeCard = null;
  let fileCounter = 0;

  const $ = (id) => document.getElementById(id);
  const board = $("board"), area = $("canvasArea"), list = $("list"), statusEl = $("status");

  // ---------------------------------------------------------------------------
  // operations
  // ---------------------------------------------------------------------------

  // stage 2: also hand `op` to the app, which sends it to the other person
  function send(op) {
    const why = W.validateOp(op);
    if (why) { console.warn("workspace: rejected own op", why, op); return false; }
    const changed = W.applyOp(state, op);
    if (changed) render();
    return changed;
  }

  const setStatus = (text) => { statusEl.textContent = text; };
  setStatus(role === "teacher" ? "Only you can see this workspace for now." : "Student view");

  // ---------------------------------------------------------------------------
  // files
  // ---------------------------------------------------------------------------

  function imageAspect(url) {
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => resolve(img.naturalWidth > 0 ? Math.min(10, Math.max(0.1, img.naturalHeight / img.naturalWidth)) : 0.75);
      img.onerror = () => resolve(null);
      img.src = url;
    });
  }

  async function openFiles(fileList) {
    for (const f of fileList) {
      if (f.type === "application/pdf" || /\.pdf$/i.test(f.name)) {
        // the browser's PDF viewer can't keep marks lined up with the page; waiting on a decision (T-037)
        setStatus("PDFs can't be opened yet: images and whiteboards for now.");
        continue;
      }
      if (!/^image\/(png|jpeg|gif|webp)$/.test(f.type)) { setStatus(`${f.name}: only images can be opened.`); continue; }
      if (f.size > 100 * 1024 * 1024) { setStatus(`${f.name} is too big (over 100 MB).`); continue; }
      const url = URL.createObjectURL(f);
      const aspect = await imageAspect(url);
      if (aspect === null) { URL.revokeObjectURL(url); setStatus(`${f.name} couldn't be opened.`); continue; }
      const key = `f${++fileCounter}`;
      files.set(key, { url });
      addCard({ kind: "image", name: f.name.slice(0, W.LIMITS.name), aspect, file: key });
    }
  }

  function addCard({ kind, name, aspect, file }) {
    const n = Object.keys(state.cards).length;
    const w = kind === "board" ? 0.45 : 0.42;
    const op = me.addCard({ kind, name, x: 0.03 + (n % 5) * 0.04, y: 0.03 + (n % 5) * 0.04, w, aspect, page: 1, hidden: false, file: file || null });
    if (!send(op)) { setStatus("The board is full. Close something first."); return; }
    activeCard = op.id;  // render() has put it at the front
    render();
  }

  // ---------------------------------------------------------------------------
  // rendering
  // ---------------------------------------------------------------------------

  const cardEls = new Map();  // card id -> { el, content, canvas, viewer }
  const boardWidth = () => area.clientWidth || 800;

  function cardRect(c) {
    const bw = boardWidth();
    const w = c.w * bw;
    return { left: c.x * bw, top: c.y * bw, width: w, height: w * c.aspect + 30 };
  }

  function makeCardEl(c) {
    const el = document.createElement("section");
    el.className = "card";
    el.setAttribute("aria-label", c.name);
    const title = document.createElement("div");
    title.className = "title";
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = c.name;
    title.append(name);

    title.append(button("–", `Hide ${c.name}`, () => send(me.updateCard(c.id, { hidden: true }))));
    title.append(button("✕", `Close ${c.name}`, () => closeCard(c.id)));

    const content = document.createElement("div");
    content.className = "content";
    let viewer = null;
    if (c.kind !== "board") {
      const f = files.get(c.file);
      if (f) {
        viewer = document.createElement("img");
        viewer.src = f.url;
        viewer.alt = c.name;
        content.append(viewer);
      } else {
        content.style.background = "#2a2f39";
        content.innerHTML = '<p style="color:#9aa3b2;padding:12px;margin:0">This file is on the other computer.</p>';
      }
    }
    const canvas = document.createElement("canvas");
    canvas.setAttribute("aria-label", `Drawing layer for ${c.name}`);
    content.append(canvas);
    const resize = document.createElement("div");
    resize.className = "resize";
    resize.title = "Drag to resize";

    el.append(title, content, resize);
    area.append(el);

    el.addEventListener("pointerdown", () => { focusCard(c.id); }, true);
    dragToMove(title, c.id);
    dragToResize(resize, c.id);
    drawingInput(canvas, c.id);
    return { el, content, canvas, viewer };
  }

  function button(text, label, onClick) {
    const b = document.createElement("button");
    b.textContent = text;
    b.title = label;
    b.setAttribute("aria-label", label);
    b.addEventListener("click", (e) => { e.stopPropagation(); onClick(); });
    b.addEventListener("pointerdown", (e) => e.stopPropagation());
    return b;
  }

  function render() {
    const cards = state.cards;
    // remove cards that are gone
    for (const [id, v] of cardEls) {
      if (!cards[id]) { v.el.remove(); cardEls.delete(id); }
    }
    for (let i = order.length - 1; i >= 0; i--) if (!cards[order[i]]) order.splice(i, 1);
    for (const id of Object.keys(cards)) if (!order.includes(id)) order.push(id);
    if (activeCard && !cards[activeCard]) activeCard = null;

    let maxBottom = 0;
    order.forEach((id, z) => {
      const c = cards[id];
      let v = cardEls.get(id);
      if (!v) { v = makeCardEl(c); cardEls.set(id, v); }
      v.el.hidden = c.hidden;
      v.el.classList.toggle("active", id === activeCard);
      v.el.style.zIndex = String(z + 1);
      const r = cardRect(c);
      Object.assign(v.el.style, { left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`, height: `${r.height}px` });
      if (!c.hidden) maxBottom = Math.max(maxBottom, r.top + r.height);
      v.el.querySelector(".name").textContent = c.name;
      drawMarks(id);
    });
    area.style.height = `${Math.max(maxBottom + 40, board.clientHeight)}px`;
    $("hint").hidden = Object.keys(cards).length > 0;
    renderList();
    updateButtons();
  }

  function renderList() {
    list.textContent = "";
    for (const id of order) {
      const c = state.cards[id];
      const li = document.createElement("li");
      const cb = document.createElement("input");
      cb.type = "checkbox";
      cb.id = `show-${id}`;
      cb.checked = !c.hidden;
      cb.addEventListener("change", () => send(me.updateCard(id, { hidden: !cb.checked })));
      const label = document.createElement("label");
      label.htmlFor = cb.id;
      label.textContent = c.name;
      label.title = c.hidden ? `${c.name} (hidden)` : c.name;
      li.append(cb, label, button("✕", `Close ${c.name}`, () => closeCard(id)));
      list.append(li);
    }
    $("listEmpty").hidden = order.length > 0;
  }

  function updateButtons() {
    const mine = myMarks.filter((id) => state.marks[id]);
    $("undo").disabled = mine.length === 0;
    $("clearCard").disabled = !activeCard || W.marksOf(state, activeCard).length === 0;
    $("clearAll").disabled = Object.keys(state.marks).length === 0;
    $("reset").disabled = role !== "teacher" || Object.keys(state.cards).length === 0;
  }

  // ---------------------------------------------------------------------------
  // drawing marks
  // ---------------------------------------------------------------------------

  function sizeCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    return { w, h, dpr };
  }

  function drawMark(ctx, m, w, h, dpr) {
    const pts = m.points.map(([x, y]) => [x * w, y * h]);
    ctx.save();
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.strokeStyle = ctx.fillStyle = m.colour;
    if (m.kind === "pen" || m.kind === "highlight") {
      ctx.globalAlpha = m.kind === "highlight" ? 0.35 : 1;
      ctx.lineWidth = m.kind === "highlight" ? Math.max(8, w * 0.022) : Math.max(2, w * 0.004);
      ctx.beginPath();
      ctx.moveTo(pts[0][0], pts[0][1]);
      if (pts.length === 1) ctx.lineTo(pts[0][0] + 0.1, pts[0][1]);
      for (const p of pts.slice(1)) ctx.lineTo(p[0], p[1]);
      ctx.stroke();
    } else if (m.kind === "arrow") {
      const [[x1, y1], [x2, y2]] = pts;
      const lw = Math.max(2.5, w * 0.005);
      const head = Math.max(10, w * 0.025);
      const a = Math.atan2(y2 - y1, x2 - x1);
      ctx.lineWidth = lw;
      ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2 - Math.cos(a) * head * 0.6, y2 - Math.sin(a) * head * 0.6); ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(x2, y2);
      ctx.lineTo(x2 - head * Math.cos(a - 0.45), y2 - head * Math.sin(a - 0.45));
      ctx.lineTo(x2 - head * Math.cos(a + 0.45), y2 - head * Math.sin(a + 0.45));
      ctx.closePath(); ctx.fill();
    } else if (m.kind === "text") {
      const size = Math.max(12 * dpr, w * 0.03);
      ctx.font = `600 ${size}px "Segoe UI", sans-serif`;
      ctx.textBaseline = "top";
      ctx.lineWidth = Math.max(3, size * 0.18);
      ctx.strokeStyle = m.colour === "#ffffff" || m.colour === "#f5e663" ? "rgba(0,0,0,.75)" : "rgba(255,255,255,.9)";
      const lines = m.text.split("\n");
      lines.forEach((line, i) => {
        ctx.strokeText(line, pts[0][0], pts[0][1] + i * size * 1.2);
        ctx.fillText(line, pts[0][0], pts[0][1] + i * size * 1.2);
      });
    }
    ctx.restore();
  }

  function drawMarks(cardId, live) {
    const v = cardEls.get(cardId);
    if (!v || state.cards[cardId]?.hidden) return;
    const { w, h, dpr } = sizeCanvas(v.canvas);
    const ctx = v.canvas.getContext("2d");
    ctx.clearRect(0, 0, w, h);
    for (const m of W.marksOf(state, cardId)) drawMark(ctx, m, w, h, dpr);
    if (live && live.card === cardId) drawMark(ctx, live, w, h, dpr);
  }

  // ---------------------------------------------------------------------------
  // input
  // ---------------------------------------------------------------------------

  function focusCard(id) {
    activeCard = id;
    const i = order.indexOf(id);
    if (i >= 0 && i !== order.length - 1) { order.splice(i, 1); order.push(id); }
    render();
  }

  function dragToMove(handle, id) {
    handle.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      const c = state.cards[id];
      const bw = boardWidth();
      const start = { x: e.clientX, y: e.clientY, cx: c.x, cy: c.y };
      handle.setPointerCapture(e.pointerId);
      handle.style.cursor = "grabbing";
      const el = cardEls.get(id).el;
      const move = (ev) => {
        const nx = Math.max(0, start.cx + (ev.clientX - start.x) / bw);
        const ny = Math.max(0, start.cy + (ev.clientY - start.y) / bw);
        el.style.left = `${nx * bw}px`;
        el.style.top = `${ny * bw}px`;
      };
      const up = (ev) => {
        handle.removeEventListener("pointermove", move);
        handle.removeEventListener("pointerup", up);
        handle.style.cursor = "";
        const nx = Math.min(9, Math.max(0, start.cx + (ev.clientX - start.x) / bw));
        const ny = Math.min(9, Math.max(0, start.cy + (ev.clientY - start.y) / bw));
        if (nx !== c.x || ny !== c.y) send(me.updateCard(id, { x: nx, y: ny }));  // one operation per move
      };
      handle.addEventListener("pointermove", move);
      handle.addEventListener("pointerup", up);
    });
  }

  function dragToResize(handle, id) {
    handle.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.stopPropagation();
      const c = state.cards[id];
      const bw = boardWidth();
      const start = { x: e.clientX, w: c.w };
      handle.setPointerCapture(e.pointerId);
      const el = cardEls.get(id).el;
      const width = (ev) => Math.min(2, Math.max(0.12, start.w + (ev.clientX - start.x) / bw));
      const move = (ev) => {
        const w = width(ev) * bw;
        el.style.width = `${w}px`;
        el.style.height = `${w * c.aspect + 30}px`;
        drawMarks(id);
      };
      const up = (ev) => {
        handle.removeEventListener("pointermove", move);
        handle.removeEventListener("pointerup", up);
        send(me.updateCard(id, { w: width(ev) }));
      };
      handle.addEventListener("pointermove", move);
      handle.addEventListener("pointerup", up);
    });
  }

  const clamp01 = (v) => Math.min(1.05, Math.max(-0.05, v));
  function toUnit(canvas, ev) {
    const r = canvas.getBoundingClientRect();
    return [clamp01((ev.clientX - r.left) / r.width), clamp01((ev.clientY - r.top) / r.height)];
  }

  function drawingInput(canvas, cardId) {
    canvas.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || tool === "move") return;
      e.preventDefault();
      const p = toUnit(canvas, e);
      if (tool === "erase") return eraseAt(cardId, canvas, p);
      if (tool === "text") return askText(cardId, canvas, e, p);

      canvas.setPointerCapture(e.pointerId);
      const live = { card: cardId, kind: tool, colour: tool === "highlight" && colour === W.COLOURS[0] ? "#f5e663" : colour, points: [p] };
      const minStep = 0.003;
      const move = (ev) => {
        const q = toUnit(canvas, ev);
        if (tool === "arrow") live.points[1] = q;
        else {
          const last = live.points[live.points.length - 1];
          if (Math.hypot(q[0] - last[0], q[1] - last[1]) >= minStep) live.points.push(q);
          // very long strokes are split so each message stays small
          if (live.points.length >= W.LIMITS.points) { commit(live); live.points = [q]; }
        }
        drawMarks(cardId, live);
      };
      const up = () => {
        canvas.removeEventListener("pointermove", move);
        canvas.removeEventListener("pointerup", up);
        canvas.removeEventListener("pointercancel", up);
        if (tool === "arrow" && live.points.length < 2) { drawMarks(cardId); return; }
        commit(live);
      };
      canvas.addEventListener("pointermove", move);
      canvas.addEventListener("pointerup", up);
      canvas.addEventListener("pointercancel", up);
    });
    canvas.style.cursor = "crosshair";
  }

  function commit(live) {
    const mark = { card: live.card, kind: live.kind, colour: live.colour, points: live.points.map((p) => [round(p[0]), round(p[1])]) };
    if (live.kind === "text") mark.text = live.text;
    const op = me.addMark(mark);
    if (send(op)) myMarks.push(op.id);
    else drawMarks(live.card);
  }
  const round = (v) => Math.round(v * 10000) / 10000;

  function askText(cardId, canvas, e, p) {
    const input = document.createElement("textarea");
    input.className = "textinput";
    input.rows = 1;
    input.maxLength = W.LIMITS.text;
    input.setAttribute("aria-label", "Text to add. Enter to place it, Escape to cancel.");
    const br = board.getBoundingClientRect();
    input.style.left = `${e.clientX - br.left + board.scrollLeft}px`;
    input.style.top = `${e.clientY - br.top + board.scrollTop}px`;
    area.append(input);
    setTimeout(() => input.focus(), 0);
    let done = false;
    const finish = (keep) => {
      if (done) return;
      done = true;
      const text = input.value.trim();
      input.remove();
      if (keep && text) commit({ card: cardId, kind: "text", colour, points: [p], text });
    };
    input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" && !ev.shiftKey) { ev.preventDefault(); finish(true); }
      if (ev.key === "Escape") finish(false);
    });
    input.addEventListener("blur", () => finish(true));
  }

  function distanceToMark(m, [x, y], aspect) {
    // in card-width units, with y scaled by the card's height
    const P = m.points.map(([px, py]) => [px, py * aspect]);
    const q = [x, y * aspect];
    if (m.kind === "text") return Math.hypot(q[0] - P[0][0] - 0.05, q[1] - P[0][1] - 0.015) - 0.06;
    let best = Infinity;
    for (let i = 0; i < P.length; i++) {
      const a = P[i], b = P[Math.min(i + 1, P.length - 1)];
      const dx = b[0] - a[0], dy = b[1] - a[1];
      const t = dx || dy ? Math.max(0, Math.min(1, ((q[0] - a[0]) * dx + (q[1] - a[1]) * dy) / (dx * dx + dy * dy))) : 0;
      best = Math.min(best, Math.hypot(q[0] - a[0] - t * dx, q[1] - a[1] - t * dy));
    }
    return best;
  }

  function eraseAt(cardId, canvas, p) {
    const c = state.cards[cardId];
    const marks = W.marksOf(state, cardId).reverse();  // top-most first
    const hit = marks.find((m) => distanceToMark(m, p, c.aspect) < 0.02);
    if (!hit) return;
    if (role !== "teacher" && hit.owner !== author) { setStatus("You can only remove your own marks."); return; }
    send(me.removeMark(hit.id));
  }

  function closeCard(id) {
    const c = state.cards[id];
    if (!c) return;
    if (role !== "teacher" && c.owner !== author) { send(me.updateCard(id, { hidden: true })); return; }
    const marks = W.marksOf(state, id).length;
    if (marks > 0 && !confirm(`Close ${c.name}? Its ${marks} mark${marks === 1 ? "" : "s"} will be removed.`)) return;
    send(me.removeCard(id));
  }

  // ---------------------------------------------------------------------------
  // toolbar
  // ---------------------------------------------------------------------------

  function setTool(t) {
    tool = t;
    for (const b of document.querySelectorAll("[data-tool]")) b.setAttribute("aria-pressed", String(b.dataset.tool === t));
    for (const v of cardEls.values()) v.canvas.style.cursor = t === "move" ? "default" : t === "erase" ? "cell" : "crosshair";
  }
  for (const b of document.querySelectorAll("[data-tool]")) b.addEventListener("click", () => setTool(b.dataset.tool));

  const colourNames = { "#f2a541": "Orange", "#47b0f8": "Blue", "#e5645a": "Red", "#5ccf72": "Green", "#ffffff": "White", "#111317": "Black", "#f5e663": "Yellow" };
  for (const c of W.COLOURS) {
    const b = document.createElement("button");
    b.className = "swatch";
    b.style.background = c;
    b.title = colourNames[c] || c;
    b.setAttribute("aria-label", colourNames[c] || c);
    b.setAttribute("aria-pressed", String(c === colour));
    b.addEventListener("click", () => {
      colour = c;
      for (const s of document.querySelectorAll(".swatch")) s.setAttribute("aria-pressed", String(s === b));
    });
    $("colours").append(b);
  }

  $("open").addEventListener("click", () => $("fileInput").click());
  $("fileInput").addEventListener("change", (e) => { openFiles([...e.target.files]); e.target.value = ""; });
  $("addBoard").addEventListener("click", () => {
    const n = Object.values(state.cards).filter((c) => c.kind === "board").length;
    addCard({ kind: "board", name: n ? `Whiteboard ${n + 1}` : "Whiteboard", aspect: 0.62 });
  });
  $("toggleSide").addEventListener("click", (e) => {
    const shown = document.body.classList.toggle("no-side") === false;
    e.currentTarget.setAttribute("aria-pressed", String(shown));
    requestAnimationFrame(render);
  });
  $("undo").addEventListener("click", undo);
  $("clearCard").addEventListener("click", () => {
    if (activeCard) send(me.clearMarks(activeCard, role === "teacher" ? null : author));
  });
  $("clearAll").addEventListener("click", () => {
    if (confirm(role === "teacher" ? "Remove every mark on every card?" : "Remove all your marks?")) send(me.clearMarks(null, role === "teacher" ? null : author));
  });
  $("reset").addEventListener("click", () => {
    if (confirm("Close every card and remove all marks?")) send(me.reset());
  });

  function undo() {
    while (myMarks.length) {
      const id = myMarks.pop();
      if (state.marks[id]) { send(me.removeMark(id)); return; }
    }
  }

  document.addEventListener("keydown", (e) => {
    if (e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLInputElement) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); undo(); return; }
    const keys = { m: "move", p: "pen", h: "highlight", a: "arrow", t: "text", e: "erase" };
    if (!e.ctrlKey && !e.altKey && keys[e.key.toLowerCase()]) setTool(keys[e.key.toLowerCase()]);
  });

  // drop files on the board
  board.addEventListener("dragover", (e) => { e.preventDefault(); board.classList.add("dragover"); });
  board.addEventListener("dragleave", () => board.classList.remove("dragover"));
  board.addEventListener("drop", (e) => {
    e.preventDefault();
    board.classList.remove("dragover");
    if (e.dataTransfer?.files?.length) openFiles([...e.dataTransfer.files]);
  });

  let resizeTimer = 0;
  window.addEventListener("resize", () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(render, 100);
  });
  render();

  // for checks from the app or a test harness
  window.canduetWorkspace = { state, addCard, setTool, digest: () => W.digest(state), apply: send, session: me };
})();
