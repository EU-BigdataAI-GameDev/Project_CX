// 화면: 월드 그리기(사각형·원·글자), HUD, 말풍선, 증거창, 제안 박스, 퍼즐 표시, 대화 로그(DOM).
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K;
  const H = CX.hack;

  const C = {
    bg1: "#0b0f1d", bg2: "#1a1030", ground: "#1d2238", groundLine: "#2de2ff", platform: "#27304f",
    brother: "#ff9f43", sister: "#ff6bd6", spider: "#d6d84a", elite: "#ff4d4d", stun: "#7d8597",
    text: "#e8ecff", dim: "#8791b5", gold: "#ffd166", red: "#ff5d5d", green: "#5dff9a", cyan: "#59d9ff",
  };

  let canvas, g, logEl, inputEl, statusEl;
  const bubbles = { jemini: null };

  function init(elements) {
    canvas = elements.canvas; g = canvas.getContext("2d");
    logEl = elements.log; inputEl = elements.input; statusEl = elements.status;
  }

  // ---------------------------------------------------------------- 대화 로그(DOM)
  const WHO = { jemini: "제미니", warden: "WARDEN", me: "나", system: "시스템" };
  function addLog(who, text, status) {
    if (!logEl) return;
    const row = document.createElement("div");
    row.className = "msg " + who + (status ? " s-" + status : "");
    const name = document.createElement("span");
    name.className = "who";
    name.textContent = WHO[who] || who;
    const body = document.createElement("span");
    body.className = "text";
    body.textContent = text;
    row.appendChild(name); row.appendChild(body);
    logEl.appendChild(row);
    while (logEl.children.length > 60) logEl.removeChild(logEl.firstChild);
    logEl.scrollTop = logEl.scrollHeight;
  }

  function setStatus(text, cls) {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.className = "status " + (cls || "");
  }

  // ---------------------------------------------------------------- 그리기 도우미
  function wrap(text, maxWidth, font) {
    g.font = font;
    const lines = [];
    for (const para of String(text).split("\n")) {
      let cur = "";
      for (const ch of para) {
        if (g.measureText(cur + ch).width > maxWidth && cur) { lines.push(cur); cur = ch.trim() === "" ? "" : ch; }
        else cur += ch;
      }
      lines.push(cur);
    }
    return lines;
  }

  function panel(x, y, w, h, fill, stroke) {
    g.fillStyle = fill || "rgba(8,12,28,0.78)";
    g.fillRect(x, y, w, h);
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = 1; g.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1); }
  }

  function text(str, x, y, color, size, align, bold) {
    g.font = (bold ? "bold " : "") + (size || 14) + "px 'Malgun Gothic','Apple SD Gothic Neo',sans-serif";
    g.fillStyle = color || C.text;
    g.textAlign = align || "left";
    g.fillText(str, x, y);
  }

  function bar(x, y, w, h, frac, color, back) {
    g.fillStyle = back || "rgba(255,255,255,0.12)";
    g.fillRect(x, y, w, h);
    g.fillStyle = color;
    g.fillRect(x, y, Math.max(0, Math.min(1, frac)) * w, h);
  }

  function bubble(str, cx, bottomY, color, maxW) {
    const font = "14px 'Malgun Gothic','Apple SD Gothic Neo',sans-serif";
    const lines = wrap(str, maxW || 260, font);
    const w = Math.min(maxW || 260, Math.max(...lines.map((l) => g.measureText(l).width))) + 16;
    const h = lines.length * 18 + 12;
    const x = Math.max(8, Math.min(K.W - w - 8, cx - w / 2));
    const y = bottomY - h;
    panel(x, y, w, h, "rgba(10,16,36,0.92)", color);
    lines.forEach((l, i) => text(l, x + 8, y + 20 + i * 18, C.text, 14));
    return { x, y, w, h };
  }

  // ---------------------------------------------------------------- 월드
  function drawBackground(G, cam) {
    const grad = g.createLinearGradient(0, 0, 0, K.H);
    grad.addColorStop(0, C.bg1); grad.addColorStop(1, C.bg2);
    g.fillStyle = grad; g.fillRect(0, 0, K.W, K.H);
    // 먼 건물(시차)
    for (let i = 0; i < 30; i++) {
      const bx = i * 210 - ((cam * 0.3) % 210) - 100;
      const bh = 160 + ((i * 97) % 220);
      g.fillStyle = "rgba(60,70,130,0.35)";
      g.fillRect(bx, K.GROUND_Y - bh, 120, bh);
      g.fillStyle = "rgba(45,226,255,0.25)";
      for (let wy = K.GROUND_Y - bh + 14; wy < K.GROUND_Y - 20; wy += 34) g.fillRect(bx + 14, wy, 8, 10);
    }
  }

  function drawPlayer(p, color, label, G) {
    if (!p.alive) { // 쓰러짐
      g.fillStyle = C.stun;
      g.fillRect(p.x - 24, p.y - 14, 48, 14);
      text(`${label} 쓰러짐 ${Math.ceil(p.respawn)}s`, p.x, p.y - 22, C.red, 12, "center");
      return;
    }
    g.fillStyle = p.hurt > 0 ? "#ffffff" : color;
    g.fillRect(p.x - p.w / 2, p.y - p.h, p.w, p.h);
    g.fillStyle = "#0b0f1d";
    g.fillRect(p.x + p.facing * 5 - 3, p.y - p.h + 10, 6, 6); // 눈
    bar(p.x - 20, p.y - p.h - 12, 40, 5, p.hp / p.hpMax, p.hp / p.hpMax > 0.4 ? C.green : C.red);
    text(label, p.x, p.y - p.h - 16, C.dim, 11, "center");
    if (p.shield > 0) {
      g.strokeStyle = C.cyan; g.lineWidth = 3;
      g.beginPath(); g.arc(p.x, p.y - p.h / 2, 36, 0, Math.PI * 2); g.stroke();
    }
    if (p.boost > 0) { g.strokeStyle = C.gold; g.lineWidth = 2; g.strokeRect(p.x - p.w / 2 - 3, p.y - p.h - 3, p.w + 6, p.h + 6); }
    if (p.healFx) p.healFx = 0;
  }

  function drawEnemy(e) {
    const body = e.dead ? "rgba(120,120,120,0.5)" : e.stun > 0 ? C.stun : e.hit > 0 ? "#ffffff" : e.kind === "elite" ? C.elite : C.spider;
    g.fillStyle = body;
    g.fillRect(e.x - e.w / 2, e.y - e.h, e.w, e.h);
    g.strokeStyle = body; g.lineWidth = 3;
    for (let i = -1; i <= 1; i += 2) { // 다리
      g.beginPath(); g.moveTo(e.x + i * e.w / 3, e.y - e.h / 2); g.lineTo(e.x + i * (e.w / 2 + 8), e.y); g.stroke();
    }
    if (e.dead) return;
    g.fillStyle = "#0b0f1d";
    g.fillRect(e.x + e.dir * 6 - 3, e.y - e.h + 5, 6, 5);
    bar(e.x - e.w / 2, e.y - e.h - 8, e.w, 4, e.hp / e.hpMax, C.red);
    if (e.stun > 0) text("zzz", e.x, e.y - e.h - 12, C.cyan, 12, "center");
    if (e.decoy > 0) text("!?", e.x, e.y - e.h - 12, C.gold, 12, "center");
  }

  function drawWorld(G, cam, ctx) {
    g.save();
    g.translate(-cam, 0);
    // 바닥·발판
    g.fillStyle = C.ground; g.fillRect(0, K.GROUND_Y, K.WORLD_W, K.H - K.GROUND_Y);
    g.fillStyle = C.groundLine; g.fillRect(0, K.GROUND_Y, K.WORLD_W, 2);
    for (const p of G.platforms) { g.fillStyle = C.platform; g.fillRect(p.x, p.y, p.w, 10); g.fillStyle = C.groundLine; g.fillRect(p.x, p.y, p.w, 2); }

    // 충전 패드
    const pad = G.pad;
    g.fillStyle = pad.cooldown > 0 ? "#3a4060" : C.green;
    g.fillRect(pad.x - 30, K.GROUND_Y - 6, 60, 6);
    text(pad.cooldown > 0 ? `CHARGE ${Math.ceil(pad.cooldown)}s` : "CHARGE [E]", pad.x, K.GROUND_Y - 14, pad.cooldown > 0 ? C.dim : C.green, 12, "center");

    // 단말기
    for (const t of G.terminals) {
      const col = t.state === "hacked" ? C.green : t.state === "drone" ? C.gold : "#ff6b6b";
      g.fillStyle = "#222a46"; g.fillRect(t.x - 22, K.GROUND_Y - 70, 44, 70);
      g.fillStyle = col; g.fillRect(t.x - 16, K.GROUND_Y - 62, 32, 22);
      text("TERMINAL", t.x, K.GROUND_Y - 78, C.dim, 11, "center");
      if (t.state !== "hacked") bar(t.x - 22, K.GROUND_Y - 34, 44, 5, t.progress, C.cyan);
    }
    // 문(맵 중간)
    drawDoor(G.door.x, G.door.open, "DOOR");
    // 기록 칩
    if (G.chip.present) {
      g.fillStyle = C.gold; g.fillRect(G.chip.x - 10, K.GROUND_Y - 16, 20, 14);
      text("기록 칩 [E]", G.chip.x, K.GROUND_Y - 24, C.gold, 12, "center");
    }
    // WARDEN + 맵 끝 문
    const wx = G.wardenX;
    g.fillStyle = "#2a1620"; g.fillRect(wx - 40, K.GROUND_Y - 150, 80, 150);
    g.fillStyle = ctx.warden.solved ? C.green : C.red;
    g.beginPath(); g.arc(wx, K.GROUND_Y - 100, ctx.warden.requestPending ? 12 + 4 * Math.sin(G.time * 12) : 14, 0, Math.PI * 2); g.fill();
    text("WARDEN", wx, K.GROUND_Y - 160, C.red, 14, "center", true);
    drawDoor(G.gate.x, G.gate.open, "GATE");

    for (const e of G.enemies) drawEnemy(e);
    drawPlayer(G.sister, C.sister, "여동생", G);
    drawPlayer(G.brother, C.brother, "오빠", G);
    g.fillStyle = "#ffe66b";
    for (const b of G.bullets) g.fillRect(b.x - 3, b.y - 1.5, 6, 3);
    drawDrone(G);
    g.restore();
  }

  function drawDoor(x, open, label) {
    g.fillStyle = open ? "rgba(93,255,154,0.25)" : "#5a2a3a";
    g.fillRect(x - 8, K.GROUND_Y - (open ? 20 : 170), 16, open ? 20 : 170);
    text(label + (open ? " OPEN" : " LOCKED"), x, K.GROUND_Y - 178, open ? C.green : C.red, 11, "center");
  }

  function drawDrone(G) {
    const d = G.drone;
    const y = d.y + Math.sin(d.bob * 3) * 3;
    const col = d.action && (d.action.name === "DecoyActive") ? (Math.floor(G.time * 12) % 2 ? "#ff1a0d" : "#ffffff") : K.EMOTION_COLORS[d.emotion] || C.cyan;
    // 빔: 단말기 대행 / 재부팅 / 펄스
    g.strokeStyle = col; g.lineWidth = 2;
    const a = d.action;
    if (a && a.name === "TerminalHacking") { g.beginPath(); g.moveTo(d.x, y); g.lineTo(a.target.x, K.GROUND_Y - 50); g.stroke(); }
    if (a && a.name === "HackAssist" && a.partner) { g.beginPath(); g.moveTo(d.x, y); g.lineTo(a.partner.x, a.partner.y - 30); g.stroke(); }
    if (d.beamFx) { g.beginPath(); g.moveTo(d.x, y); g.lineTo(d.beamFx.target.x, d.beamFx.target.y - 20); g.stroke(); }
    if (d.pulseFx) {
      g.strokeStyle = "#ff3322"; g.lineWidth = 4;
      g.beginPath(); g.arc(d.pulseFx.x, d.pulseFx.y - 20, (0.6 - d.pulseFx.t) * 140, 0, Math.PI * 2); g.stroke();
    }
    g.fillStyle = col;
    g.beginPath(); g.arc(d.x, y, 13, 0, Math.PI * 2); g.fill();
    g.fillStyle = "#0b0f1d";
    g.fillRect(d.x - 7, y - 3, 14, 5);
    text("제미니", d.x, y - 20, col, 11, "center");
  }

  // ---------------------------------------------------------------- HUD
  function drawHud(G, ctx, view) {
    const d = G.drone, b = G.brother, s = G.sister;
    // 좌상단: 체력·탄약·배터리
    panel(10, 10, 290, 118, null, "#2b3566");
    text("오빠", 20, 30, C.brother, 13, "left", true);
    bar(60, 20, 150, 10, b.hp / b.hpMax, b.alive ? C.green : C.red);
    text(`${Math.round(b.hp)}/${b.hpMax}`, 216, 30, C.text, 12);
    text("여동생", 20, 50, C.sister, 13, "left", true);
    bar(60, 40, 150, 10, s.hp / s.hpMax, s.alive ? C.green : C.red);
    text(`${Math.round(s.hp)}/${s.hpMax}`, 216, 50, C.text, 12);
    text(b.reloading ? "재장전 중…" : `탄약 ${b.ammo}/${b.ammoMax}`, 20, 72, b.ammo <= 8 ? C.red : C.text, 13);
    if (b.boost > 0) text(`공격력 x2 ${Math.ceil(b.boost)}s`, 130, 72, C.gold, 13);
    text("배터리", 20, 94, C.cyan, 13, "left", true);
    bar(70, 84, 140, 12, d.battery / 100, d.battery > 30 ? C.cyan : C.red);
    text(`${d.battery}%`, 216, 94, C.text, 12);
    const tag = d.serverMock ? "[MOCK]" : view.serverOnline === false ? "[OFFLINE]" : view.serverOnline ? "[AI]" : "[연결 중…]";
    text(tag, 20, 118, d.serverMock || view.serverOnline === false ? C.gold : C.green, 13, "left", true);
    if (d.action) text("제미니 작업 중: " + d.action.name, 90, 118, C.dim, 12);

    // 우상단: 증거창
    const ev = K.WARDEN.EVIDENCE;
    const ew = ctx.warden;
    const px = K.W - 400, rowH = 56, ph = 40 + ev.length * rowH + 22;
    panel(px, 10, 390, ph, null, "#6a5a2a");
    text(`EVIDENCE // ${ew.evidenceNum()}      CONFIRMED ${ew.creditedNum()}/${ev.length}`, px + 10, 30, C.gold, 14, "left", true);
    ev.forEach((e, i) => {
      const have = G.evidence[i];
      const y = 54 + i * rowH;
      text(`${e.id} ${have ? e.title : "???"}${have && ew.credited[i] ? "  [인정]" : ""}`, px + 10, y, have ? C.text : C.dim, 13, "left", true);
      if (have) { // 한 화면이라 "찾은 사람만 보임" 규칙은 빼고 전문을 보여 준다
        wrap(e.body, 370, "11px 'Malgun Gothic',sans-serif").forEach((l, j) => text(l, px + 10, y + 15 + j * 14, C.dim, 11));
      }
    });
    const n = ew.evidenceNum();
    const next = ew.solved ? "" : n < ev.length ? `다음 ▶ 증거 ${n}/${ev.length}` : "다음 ▶ WARDEN과 대화";
    if (next) text(next, px + 10, 10 + ph - 8, C.cyan, 13, "left", true);

    // 좌측: 규칙·작전 제안
    let y = 140;
    for (const r of ctx.rules.rules) {
      text(`RULE#${r.id}: ${r.label}${r.held ? "  (보류)" : ""}`, 14, y, r.held ? C.gold : C.green, 12);
      y += 16;
    }
    for (const r of ctx.rules.proposal) {
      text(`PLAN?: ${r.label}  — "응"/"아니" 또는 R/T`, 14, y, C.cyan, 12);
      y += 16;
    }

    // 퍼즐 표시(여동생 해킹 중)
    const info = CX.follower.puzzleInfo(s);
    if (info) {
      const pw = 360, x0 = (K.W - pw) / 2;
      panel(x0, 10, pw, 74, null, "#2de2ff");
      text(`여동생 해킹 중 — ${info.type === "terminal" ? "단말기" : "적"}  ${Math.round(info.progress * 100)}%`, x0 + 10, 30, C.cyan, 13, "left", true);
      const seq = view.puzzleSeq(info.type);
      const filled = H.filledCount(info.progress, seq.length);
      seq.forEach((a, i) => {
        text(a, x0 + 22 + i * 40, 66, i < filled ? C.green : C.dim, 28, "center", true);
      });
    }

    // 말풍선(제미니)
    if (d.dialogue && G.time - d.dialogueTime < 7) {
      const col = d.dialogueStatus === "rejected" ? C.red : d.dialogueStatus === "advice" ? C.gold : d.dialogueStatus === "rule" ? C.green : C.cyan;
      const sx = d.x - view.cam;
      bubble(d.dialogue, sx, Math.max(60, d.y - 28), col, 280);
    }
    // WARDEN 말풍선
    const w = ctx.warden;
    if (w.line && w.anyoneWithin(K.WARDEN.SHOW_RANGE)) {
      const sx = G.wardenX - view.cam;
      bubble("WARDEN: " + w.line, sx, K.GROUND_Y - 190, C.red, 300);
    }

    // 선제 제안 박스
    const pro = ctx.proactive;
    if (pro.hasOffer()) {
      const o = K.OFFER[pro.offerType];
      const bw = 520, bx = (K.W - bw) / 2, by = K.H - 96;
      panel(bx, by, bw, 78, "rgba(8,24,40,0.92)", C.cyan);
      text(`${o.label}   ${Math.ceil(pro.offerRemaining())}s`, bx + 12, by + 22, C.cyan, 14, "left", true);
      text(o.line.replace(" [응/아니]", ""), bx + 12, by + 46, C.text, 14);
      text("[R] 응      [T] 아니", bx + 12, by + 68, C.gold, 14, "left", true);
    }

    // 조작 안내
    text("A/D 이동  W/Space 점프  J/클릭 사격  Q 장전  E 상호작용  Enter 채팅  R/T 제안 응/아니", K.W - 12, K.H - 8, C.dim, 11, "right");
  }

  function drawOverlay(G, ctx) {
    if (!G.over && !G.cleared) return;
    g.fillStyle = "rgba(0,0,0,0.7)"; g.fillRect(0, 0, K.W, K.H);
    if (G.over) {
      text("GAME OVER", K.W / 2, K.H / 2 - 10, C.red, 56, "center", true);
      text("두 사람 모두 쓰러졌습니다.", K.W / 2, K.H / 2 + 30, C.text, 18, "center");
    } else {
      text("CLEAR", K.W / 2, K.H / 2 - 10, C.green, 64, "center", true);
      text("WARDEN을 설득해 잠금을 풀었습니다.", K.W / 2, K.H / 2 + 30, C.text, 18, "center");
    }
    text("아래 [다시 시작] 버튼 또는 F5", K.W / 2, K.H / 2 + 64, C.dim, 14, "center");
  }

  /** view: { cam, serverOnline, puzzleSeq(type) } */
  function render(G, ctx, view) {
    drawBackground(G, view.cam);
    drawWorld(G, view.cam, ctx);
    drawHud(G, ctx, view);
    drawOverlay(G, ctx);
  }

  CX.ui = { init, addLog, setStatus, render, colors: C };
})(typeof window !== "undefined" ? window : globalThis);
