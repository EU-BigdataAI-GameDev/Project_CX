// 서버로 보내는 GameState JSON 수집. 형식은 Jemini/app/schemas.py GameState 와 같다.
// 원본: CXJeminiDrone.cpp GatherState + CXJeminiClient.cpp StateObject
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);

  const nz = (v) => Math.max(0, v);

  /**
   * G: 월드, ctx: { drone, rules, warden(또는 null), world, follower }
   * 거리는 m(소수 1자리), 적은 살아 있는 두 사람 중 가까운 쪽 기준 15m 안.
   */
  function gather(G, ctx, speaker) {
    const W = ctx.world;
    const b = G.brother, s = G.sister;
    const q = W.queryEnemies(G);
    const puzzle = ctx.follower.puzzleInfo(s);
    const state = {
      brother_ammo: nz(b.ammo), brother_ammo_max: Math.max(1, b.ammoMax), brother_reloading: !!b.reloading,
      brother_hp: nz(Math.round(b.hp)), brother_hp_max: Math.max(1, b.hpMax), brother_alive: !!b.alive,
      brother_respawn_remaining: nz(b.alive ? 0 : b.respawn), brother_shield_remaining: nz(b.shield),
      damage_boost_remaining: nz(b.boost),
      sister_hp: nz(Math.round(s.hp)), sister_hp_max: Math.max(1, s.hpMax), sister_alive: !!s.alive,
      sister_respawn_remaining: nz(s.alive ? 0 : s.respawn), sister_shield_remaining: nz(s.shield),
      sister_puzzle: puzzle ? { type: puzzle.type, progress: Math.min(1, nz(puzzle.progress)), kind: "arrow" } : null,
      enemy_alive_count: q.alive, enemy_hackable_count: q.hackable, enemy_active_count: q.active,
      enemy_nearest_dist: q.nearestDist >= 0 ? Math.round((q.nearestDist / K.PX_PER_M) * 10) / 10 : null,
      terminal_locked_count: G.terminals.filter((t) => t.state !== "hacked").length,
      speaker: speaker === "sister" ? "sister" : "brother",
      drone_busy: !!ctx.drone.action,
      battery: Math.min(100, nz(Math.round(ctx.drone.battery))),
      active_rules: [], pending_proposal: [],
    };
    ctx.rules.fillState(state);
    if (ctx.warden && !ctx.warden.solved) { // WARDEN 힌트용: 얻은 증거 id·마지막 거절 사유·근처 여부
      const w = { evidence: ctx.warden.evidenceIds(), near: ctx.warden.isNear(G) };
      if (ctx.warden.lastReason) w.last_reason = ctx.warden.lastReason.slice(0, 32);
      state.warden = w;
    }
    return state;
  }

  const api = { gather };
  CX.state = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
