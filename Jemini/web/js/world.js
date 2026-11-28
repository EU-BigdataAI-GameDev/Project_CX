// 맵, 적, 총알, 충돌, 단말기·증거·충전 패드(사각형만). 월드 상태 G를 만들고 매 프레임 갱신한다.
// 좌표: x = 가운데, y = 발바닥. 모든 길이는 px (1m = 40px).
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);

  const PLATFORMS = [
    { x: 520, y: 530, w: 170 }, { x: 1260, y: 525, w: 200 }, { x: 2720, y: 530, w: 190 }, { x: 3300, y: 525, w: 200 },
  ];
  // 시작 -> 거미 구간 -> 충전 패드 -> 단말기(+문) -> 엘리트 거미 -> 기록 칩 -> WARDEN 문
  const LAYOUT = {
    brotherStart: 120, sisterStart: 60,
    spiders: [820, 980, 1130, 1330, 1500, 1640, 2080, 2190, 2700, 2860],
    elite: 3050,
    pad: 1760, terminal: 1960, door: 2350, warden: 3720, gate: 3900,
  };

  function makePlayer(x, role) {
    return {
      role, x, y: K.GROUND_Y, w: 28, h: 48, vx: 0, vy: 0, onGround: true, facing: 1,
      hp: K.PLAYER_HP, hpMax: K.PLAYER_HP, alive: true, respawn: 0, shield: 0, hurt: 0,
      ammo: K.MAX_AMMO, ammoMax: K.MAX_AMMO, reloading: false, reloadT: 0, fireT: 0, boost: 0,
      hack: { kind: null, target: null, progress: 0, assist: 0, cooldown: 0 }, // 여동생 전용
    };
  }

  function makeEnemy(id, kind, x) {
    const d = kind === "elite" ? K.ELITE : K.SPIDER;
    return {
      id, kind, x, y: K.GROUND_Y, w: d.w, h: d.h, hp: d.hp, hpMax: d.hp, dmg: d.dmg, speed: d.speed, aggro: d.aggro,
      interval: d.interval, dir: 1, home: x, stun: 0, atkT: 0.5, decoy: 0, dead: false, deadT: 0, hit: 0,
    };
  }

  function create() {
    const enemies = LAYOUT.spiders.map((x, i) => makeEnemy(i + 1, "spider", x));
    enemies.push(makeEnemy(100, "elite", LAYOUT.elite));
    return {
      time: 0,
      brother: makePlayer(LAYOUT.brotherStart, "brother"),
      sister: makePlayer(LAYOUT.sisterStart, "sister"),
      enemies,
      bullets: [],
      platforms: PLATFORMS,
      terminals: [{ id: "TERMINAL_01", x: LAYOUT.terminal, state: "locked", progress: 0 }], // locked | drone | hacked
      door: { x: LAYOUT.door, open: false },
      gate: { x: LAYOUT.gate, open: false },
      pad: { x: LAYOUT.pad, cooldown: 0 },
      chip: { x: 0, y: K.GROUND_Y, present: false },
      wardenX: LAYOUT.warden,
      evidence: [false, false, false], // E1 E2 E3
      over: false, cleared: false, clearT: 0,
      hooks: {}, // main.js가 채운다: onTerminalHacked, onEnemyKilled, onPad, onEvidence, onSolved
    };
  }

  // ---------------------------------------------------------------- 물리
  function walls(G) {
    const list = [];
    if (!G.door.open) list.push(G.door.x);
    if (!G.gate.open) list.push(G.gate.x);
    return list;
  }

  function stepBody(b, dt, G, ignorePlatforms) {
    const prevY = b.y;
    b.vy += K.GRAVITY * dt;
    b.y += b.vy * dt;
    b.onGround = false;
    if (b.y >= K.GROUND_Y) { b.y = K.GROUND_Y; b.vy = 0; b.onGround = true; }
    else if (b.vy >= 0 && !ignorePlatforms) {
      for (const p of G.platforms) {
        if (prevY <= p.y + 1 && b.y >= p.y && b.x > p.x - b.w / 2 && b.x < p.x + p.w + b.w / 2) {
          b.y = p.y; b.vy = 0; b.onGround = true; break;
        }
      }
    }
    const before = b.x;
    b.x += b.vx * dt;
    for (const wx of walls(G)) { // 닫힌 문: 어느 쪽에서든 통과 못 함
      if (before < wx && b.x > wx - 12) b.x = wx - 12;
      else if (before > wx && b.x < wx + 12) b.x = wx + 12;
    }
    b.x = Math.max(b.w / 2, Math.min(K.WORLD_W - b.w / 2, b.x));
  }

  const dist = (a, b) => Math.hypot(a.x - b.x, (a.y - a.h / 2) - (b.y - b.h / 2));

  function livingPlayers(G) { return [G.brother, G.sister].filter((p) => p.alive); }

  // ---------------------------------------------------------------- 적 조회(드론·상태·여동생 AI가 쓴다)
  /** 살아 있는 플레이어에게서 range 안에 있는 적 요약. 원본 QueryEnemies. */
  function queryEnemies(G, range) {
    const lim = range == null ? K.ENEMY_SEARCH_RANGE : range;
    const q = { alive: 0, hackable: 0, active: 0, nearest: null, nearestDist: -1, nearestHackable: null, activeList: [] };
    const players = livingPlayers(G);
    if (players.length === 0) return q;
    let bestAlive = Infinity, bestHack = Infinity;
    for (const e of G.enemies) {
      if (e.dead) continue;
      let d = Infinity;
      for (const p of players) d = Math.min(d, dist(p, e));
      if (d > lim) continue;
      q.alive++;
      if (d < bestAlive) { bestAlive = d; q.nearest = e; }
      if (e.stun <= 0) { q.hackable++; q.active++; q.activeList.push(e); if (d < bestHack) { bestHack = d; q.nearestHackable = e; } }
    }
    q.nearestDist = q.nearest ? bestAlive : -1;
    return q;
  }

  /** 한 사람 기준 가장 가까운 적까지 거리(px), 범위 제한 없음. activeOnly면 그로기 제외. 없으면 -1. */
  function nearestEnemyDist(G, p, activeOnly) {
    let best = Infinity;
    for (const e of G.enemies) {
      if (e.dead || (activeOnly && e.stun > 0)) continue;
      best = Math.min(best, dist(p, e));
    }
    return best === Infinity ? -1 : best;
  }

  // ---------------------------------------------------------------- 피해·사망
  function damagePlayer(G, p, amount) {
    if (!p.alive || p.shield > 0) return;
    p.hp = Math.max(0, p.hp - amount);
    p.hurt = 0.2;
    if (p.hp <= 0) downPlayer(G, p);
  }

  function downPlayer(G, p) {
    p.alive = false;
    p.hp = 0;
    p.respawn = K.RESPAWN_SEC;
    p.vx = 0;
    if (p.role === "sister") cancelHack(G.sister);
    if (!G.brother.alive && !G.sister.alive) G.over = true;
  }

  function revive(G, p, hp) {
    const partner = p.role === "brother" ? G.sister : G.brother;
    p.alive = true;
    p.hp = Math.min(p.hpMax, hp);
    p.respawn = 0;
    p.shield = 0;
    p.vy = 0;
    if (partner.alive) { p.x = partner.x - 30; p.y = partner.y; }
  }

  function killEnemy(G, e) {
    if (e.dead) return;
    e.dead = true; e.hp = 0; e.deadT = 0.6;
    if (G.hooks.onEnemyKilled) G.hooks.onEnemyKilled(e);
    if (e.kind === "elite") { G.chip = { x: e.x, y: K.GROUND_Y, present: true }; }
  }

  function damageEnemy(G, e, amount) {
    if (e.dead) return;
    e.hp -= amount;
    e.hit = 0.12;
    if (e.hp <= 0) killEnemy(G, e);
  }

  // ---------------------------------------------------------------- 오빠 조작·사격
  function startReload(b) {
    if (b.reloading || b.ammo >= b.ammoMax) return false;
    b.reloading = true;
    b.reloadT = K.RELOAD_SEC;
    return true;
  }

  function fire(G) {
    const b = G.brother;
    if (!b.alive || b.reloading || b.fireT > 0) return;
    if (b.ammo <= 0) { startReload(b); return; }
    // 자동 조준: 사거리 안에서 가장 가까운 적. 없으면 보는 방향으로 직진
    let target = null, best = K.AIM_RANGE;
    for (const e of G.enemies) {
      if (e.dead) continue;
      const d = dist(b, e);
      if (d < best) { best = d; target = e; }
    }
    const sx = b.x, sy = b.y - b.h * 0.65;
    let dx = b.facing, dy = 0;
    if (target) {
      dx = target.x - sx; dy = (target.y - target.h / 2) - sy;
      const len = Math.hypot(dx, dy) || 1;
      dx /= len; dy /= len;
      b.facing = dx >= 0 ? 1 : -1;
    }
    G.bullets.push({ x: sx + b.facing * 14, y: sy, vx: dx * K.BULLET_SPEED, vy: dy * K.BULLET_SPEED, life: 0.9 });
    b.ammo--;
    b.fireT = K.FIRE_INTERVAL;
    if (b.ammo <= 0) startReload(b);
  }

  function updateBrother(G, dt, input) {
    const b = G.brother;
    b.fireT = Math.max(0, b.fireT - dt);
    b.boost = Math.max(0, b.boost - dt);
    if (b.reloading) {
      b.reloadT -= dt;
      if (b.reloadT <= 0) { b.reloading = false; b.ammo = b.ammoMax; }
    }
    if (!b.alive) return;
    let move = 0;
    if (input.left) move -= 1;
    if (input.right) move += 1;
    b.vx = move * 230;
    if (move !== 0) b.facing = move;
    if (input.jump && b.onGround) { b.vy = -650; b.onGround = false; }
    if (input.fire) fire(G);
    if (input.reload) startReload(b);
  }

  /** 총알 이동·명중. 공격력 강화 중이면 피해 x2. */
  function updateBullets(G, dt) {
    const mult = G.brother.boost > 0 ? K.DAMAGE_BOOST_MULT : 1;
    for (const bl of G.bullets) {
      bl.x += bl.vx * dt; bl.y += bl.vy * dt; bl.life -= dt;
      for (const e of G.enemies) {
        if (e.dead) continue;
        if (Math.abs(bl.x - e.x) < e.w / 2 + 4 && bl.y > e.y - e.h - 4 && bl.y < e.y + 4) {
          damageEnemy(G, e, K.BULLET_DMG * mult);
          bl.life = 0;
          break;
        }
      }
    }
    G.bullets = G.bullets.filter((bl) => bl.life > 0 && bl.x > 0 && bl.x < K.WORLD_W);
  }

  // ---------------------------------------------------------------- 적 AI
  function updateEnemy(G, e, dt) {
    if (e.dead) { e.deadT -= dt; return; }
    e.hit = Math.max(0, e.hit - dt);
    e.atkT = Math.max(0, e.atkT - dt);
    if (e.stun > 0) { e.stun -= dt; return; } // 해킹 그로기·펄스: 움직이지도 공격하지도 못한다
    let moveTo = null, attack = null;
    if (e.decoy > 0) { // 미끼 드론 쪽으로 몰린다
      e.decoy -= dt;
      moveTo = G.drone.decoyX != null ? G.drone.decoyX : G.drone.x;
    } else {
      let near = null, best = e.aggro;
      for (const p of livingPlayers(G)) {
        const d = Math.abs(p.x - e.x);
        if (d < best) { best = d; near = p; }
      }
      if (near) {
        moveTo = near.x;
        if (Math.abs(near.x - e.x) < e.w / 2 + 16 && Math.abs((near.y) - e.y) < 46) attack = near;
      }
    }
    let vx = 0;
    if (moveTo != null) {
      if (Math.abs(moveTo - e.x) > e.w / 2 + 6) vx = Math.sign(moveTo - e.x) * e.speed;
      if (vx !== 0) e.dir = Math.sign(vx);
    } else { // 순찰
      if (Math.abs(e.x - e.home) > 90) e.dir = Math.sign(e.home - e.x) || 1;
      vx = e.dir * e.speed * 0.35;
    }
    if (attack && e.atkT <= 0) { damagePlayer(G, attack, e.dmg); e.atkT = e.interval; }
    e.vx = attack ? 0 : vx;
    const before = e.x;
    e.x += e.vx * dt;
    for (const wx of walls(G)) {
      if (before < wx && e.x > wx - e.w / 2) e.x = wx - e.w / 2;
      else if (before > wx && e.x < wx + e.w / 2) e.x = wx + e.w / 2;
    }
  }

  // ---------------------------------------------------------------- 상호작용(E)
  /** E키: 충전 패드 / 기록 칩 / WARDEN. 처리했으면 이유 문자열 반환. */
  function interact(G) {
    const b = G.brother;
    if (!b.alive) return null;
    if (G.chip.present && Math.abs(b.x - G.chip.x) < 60) {
      G.chip.present = false;
      giveEvidence(G, [0]);
      return "chip";
    }
    if (Math.abs(b.x - G.pad.x) < 70) {
      if (G.pad.cooldown > 0) return "pad-wait";
      G.pad.cooldown = K.PAD_COOLDOWN;
      if (G.hooks.onPad) G.hooks.onPad();
      return "pad";
    }
    if (Math.abs(b.x - G.wardenX) < K.WARDEN.TALK_RANGE * K.WARDEN.INTRO_RANGE_MULT) return "warden";
    return null;
  }

  function giveEvidence(G, indexes) {
    const added = [];
    for (const i of indexes) if (!G.evidence[i]) { G.evidence[i] = true; added.push(i); }
    if (added.length && G.hooks.onEvidence) G.hooks.onEvidence(added);
  }

  /** 단말기 해킹 완료(여동생 퍼즐·드론 대행 공통): 문 개방 + 증거 E2·E3 동시 지급. */
  function markTerminalHacked(G, term) {
    if (term.state === "hacked") return;
    term.state = "hacked"; term.progress = 1;
    G.door.open = true;
    giveEvidence(G, [1, 2]);
    if (G.hooks.onTerminalHacked) G.hooks.onTerminalHacked(term);
  }

  function cancelHack(sister) {
    sister.hack.kind = null; sister.hack.target = null; sister.hack.assist = 0;
  }

  function update(G, dt, input) {
    if (G.over || G.cleared) return;
    G.time += dt;
    updateBrother(G, dt, input);
    for (const p of [G.brother, G.sister]) {
      p.hurt = Math.max(0, p.hurt - dt);
      p.shield = Math.max(0, p.shield - dt);
      if (p.alive) stepBody(p, dt, G); // 쓰러진 사람은 제자리
      else {
        p.respawn -= dt;
        const partner = p.role === "brother" ? G.sister : G.brother;
        if (p.respawn <= 0 && partner.alive) revive(G, p, K.REVIVE_HP);
      }
    }
    updateBullets(G, dt);
    for (const e of G.enemies) updateEnemy(G, e, dt);
    G.enemies = G.enemies.filter((e) => !e.dead || e.deadT > 0);
    G.pad.cooldown = Math.max(0, G.pad.cooldown - dt);
    if (G.gate.open && G.clearT > 0) { G.clearT -= dt; if (G.clearT <= 0) G.cleared = true; }
  }

  const api = {
    LAYOUT, create, update, interact, queryEnemies, nearestEnemyDist, livingPlayers, dist, stepBody,
    damagePlayer, downPlayer, revive, killEnemy, damageEnemy, startReload, giveEvidence, markTerminalHacked, cancelHack,
  };
  CX.world = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
