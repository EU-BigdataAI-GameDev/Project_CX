// 여동생 AI: 오빠를 따라다니며 근처 단말기·적을 자동으로 해킹한다(솔로 모드).
// 원본: Solo/CXSoloFollowerComponent.cpp (TickFollow, TickAutoHack)
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const W = CX.world || (typeof require !== "undefined" ? require("./world.js") : null);

  /** 해킹 세션 시작. kind: "terminal" | "enemy" */
  function startHack(s, kind, target) {
    s.hack.kind = kind;
    s.hack.target = target;
    s.hack.assist = 0;
    s.hack.stuck = 0;
    if (kind === "enemy") s.hack.progress = 0;
    else s.hack.progress = target.progress;
  }

  /** 여동생이 지금 하고 있는 해킹이 단말기인지. */
  const isHackingTerminal = (s) => s.alive && s.hack.kind === "terminal";

  function tickHack(G, s, dt) {
    const h = s.hack;
    if (h.kind === "terminal") {
      const t = h.target;
      if (t.state === "hacked") { W.cancelHack(s); return; }
      const rate = 1 / K.TERMINAL_HACK_SEC;
      if (h.assist > 0) { // 제미니 대행: 남은 시간 안에 끝나도록 가속
        const remain = h.assist;
        h.assist = Math.max(0, h.assist - dt);
        t.progress = Math.min(1, t.progress + (1 - t.progress) * dt / Math.max(remain, dt));
      } else {
        t.progress = Math.min(1, t.progress + rate * dt);
      }
      h.progress = t.progress;
      if (t.progress >= 1) { W.cancelHack(s); W.markTerminalHacked(G, t); }
    } else if (h.kind === "enemy") {
      const e = h.target;
      if (!e || e.dead) { W.cancelHack(s); return; }
      if (h.assist > 0) {
        const remain = h.assist;
        h.assist = Math.max(0, h.assist - dt);
        h.progress = Math.min(1, h.progress + (1 - h.progress) * dt / Math.max(remain, dt));
      } else {
        h.progress = Math.min(1, h.progress + dt / K.ENEMY_HACK_SEC);
      }
      if (h.progress >= 1) { // 성공: 3초 그로기
        e.stun = K.ENEMY_STUN_SEC;
        h.cooldown = K.ENEMY_HACK_COOLDOWN;
        W.cancelHack(s);
      }
    }
  }

  function autoHack(G, s) {
    // 단말기 우선, 없으면 범위 안 적
    for (const t of G.terminals) {
      if (t.state === "locked" && Math.abs(t.x - s.x) <= K.HACK_RANGE_TERMINAL && s.onGround) { startHack(s, "terminal", t); return; }
    }
    if (s.hack.cooldown <= 0) {
      let best = null, bd = K.HACK_RANGE_ENEMY;
      for (const e of G.enemies) {
        if (e.dead || e.stun > 0) continue;
        const d = W.dist(s, e);
        if (d < bd) { bd = d; best = e; }
      }
      if (best) startHack(s, "enemy", best);
    }
  }

  function follow(G, s, dt) {
    const leader = G.brother;
    if (!leader.alive) { s.vx = 0; return; }
    const dx = leader.x - s.x;
    const d = Math.abs(dx);
    if (d > K.TELEPORT_DISTANCE) { // 너무 멀어졌다 -> 옆으로 순간이동
      s.x = leader.x - leader.facing * K.FOLLOW_DISTANCE; s.y = leader.y; s.vy = 0; s.vx = 0;
      return;
    }
    if (!s.moving && d > K.FOLLOW_DISTANCE + K.START_MOVE_MARGIN) s.moving = true;
    else if (s.moving && d <= K.FOLLOW_DISTANCE) s.moving = false;
    s.vx = s.moving ? Math.sign(dx) * Math.min(250, 90 + (d - K.FOLLOW_DISTANCE) * 3) : 0;
    if (s.vx !== 0) s.facing = Math.sign(s.vx);
    // 목표가 위에 있고 가까우면 점프
    if (s.onGround && leader.y < s.y - 60 && d < 200) { s.vy = -650; s.onGround = false; }
  }

  /** 매 프레임: 해킹 진행/자동 시작 + 따라다니기. 오빠 업데이트 후, 물리 전에 부른다. */
  function update(G, dt) {
    const s = G.sister;
    s.hack.cooldown = Math.max(0, s.hack.cooldown - dt);
    if (!s.alive) { if (s.hack.kind) W.cancelHack(s); return; }
    if (s.hack.kind) {
      s.vx = 0;
      tickHack(G, s, dt);
      return;
    }
    autoHack(G, s);
    if (s.hack.kind) { s.vx = 0; return; }
    follow(G, s, dt);
  }

  /** UI·상태용: 현재 해킹 세션 요약. 없으면 null. */
  function puzzleInfo(s) {
    if (!s.alive || !s.hack.kind) return null;
    return { type: s.hack.kind === "terminal" ? "terminal" : "enemy", progress: s.hack.progress };
  }

  const api = { update, startHack, isHackingTerminal, puzzleInfo };
  CX.follower = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
