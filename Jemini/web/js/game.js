// 모듈 조립: 월드 + 여동생 AI + 드론 + 규칙 + 선제 제안 + WARDEN + 코치. DOM 없이 동작하므로 node 통합 시험에서도 쓴다.
// main.js(브라우저 입력·화면)가 이것을 만들고 매 프레임 step()을 부른다.
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const req = (name, file) => CX[name] || (typeof require !== "undefined" ? require(file) : null);
  const K = req("K", "./constants.js");
  const W = req("world", "./world.js");
  const FOLLOWER = req("follower", "./follower.js");
  const DRONE = req("drone", "./drone.js");
  const RULES = req("rules", "./rules.js");
  const OFFERS = req("offers", "./offers.js");
  const CHATTER = req("chatter", "./chatter.js");
  const WARDEN = req("warden", "./warden.js");
  const COACH = req("coach", "./coach.js");
  const STATE = req("state", "./state.js");
  const QUICK = req("quickwords", "./quickwords.js");
  const API = req("api", "./api.js");

  /** opts.log(who, text, status): 대화 로그 출력. who = "jemini" | "warden" | "me" | "system" */
  function create(opts) {
    const log = (opts && opts.log) || (() => {});
    const G = W.create();
    const game = { G, typing: false, chatOpen: () => game.typing };
    const ctx = { world: W, follower: FOLLOWER, drone: null, rules: null, warden: null };

    const gatherState = (speaker) => STATE.gather(G, ctx, speaker);

    const drone = DRONE.create(G, {
      world: W, follower: FOLLOWER, quickwords: QUICK, api: API, gatherState,
      say: (text, status) => log("jemini", text, status),
      getRules: () => rules, getProactive: () => proactive,
    });
    ctx.drone = drone;

    const rules = RULES.create({
      now: () => G.time,
      gatherState: () => gatherState(null),
      canAccept: () => drone.canAccept(),
      validate: (action, who, reserve, state) => API.sendValidate(action, who, reserve, state),
      execute: (resp, reserve) => drone.executeRule(resp, reserve),
      say: (text, status) => drone.say(text, status),
    });
    ctx.rules = rules;

    let coach = null;
    const warden = WARDEN.create(G, {
      api: API,
      onLine: (line, player, status) => log("warden", line, status),
      coach: () => coach,
      onSolved: () => log("system", "WARDEN 설득 성공! 잠금이 해제됩니다.", "solved"),
    });
    ctx.warden = warden;

    coach = COACH.create({
      now: () => G.time,
      warden: () => warden,
      anyoneNear: () => warden.anyoneNear(),
      canAccept: () => drone.canAccept(),
      dialogueSeq: () => drone.dialogueSeq,
      sayCoach: (line) => drone.say(line, "coach"),
      isChatOpen: () => game.typing,
      paused: () => false,
    });

    const inputs = () => {
      const b = G.brother, s = G.sister;
      return {
        battery: drone.battery,
        brotherAlive: b.alive, ammo: b.ammo, ammoMax: b.ammoMax, reloading: b.reloading,
        brotherHp: Math.round(b.hp), brotherHpMax: b.hpMax,
        brotherEnemyDist: b.alive ? W.nearestEnemyDist(G, b, false) : -1,
        sisterAlive: s.alive,
        sisterHackingTerminal: FOLLOWER.isHackingTerminal(s),
        sisterEnemyDist: FOLLOWER.isHackingTerminal(s) ? W.nearestEnemyDist(G, s, true) : -1,
        stuckSeconds: 0,
      };
    };

    const chatter = CHATTER.create({
      blockReason: () => proactive.blockReason(),
      sayCoach: (line) => drone.say(line, "coach"),
      lastSpeechTime: () => proactive.lastSpeech,
      evidenceNum: () => warden.evidenceNum(),
      wardenSolved: () => warden.solved,
    }, 0);

    const proactive = OFFERS.create({
      now: () => G.time,
      inputs,
      terminalId: () => (FOLLOWER.isHackingTerminal(G.sister) ? G.sister.hack.target.id : null),
      canAccept: () => drone.canAccept(),
      droneBusy: () => drone.isBusy(),
      hasProposal: () => rules.proposal.length > 0,
      isChatOpen: () => game.typing,
      isNearWarden: () => !warden.solved && warden.anyoneWithin(K.WARDEN.TALK_RANGE * 1.3),
      dialogueSeq: () => drone.dialogueSeq,
      sayCoach: (line) => drone.say(line, "coach"),
      executeLocal: (cmd, target, ack) => drone.executeLocal(cmd, target, ack),
      chatter,
    });

    // 월드 사건 -> 로그·드론
    G.hooks.onTerminalHacked = () => log("system", "단말기 해킹 완료! 문이 열렸다. 증거 E2·E3 획득.", "event");
    G.hooks.onEvidence = (idx) => { if (idx.includes(0)) log("system", "증거 E1(안전 규정 제7조) 획득.", "event"); };
    G.hooks.onPad = () => { drone.recharge(); log("system", "충전 패드: 제미니 배터리 100%.", "event"); };
    G.hooks.onEnemyKilled = (e) => { if (e.kind === "elite") log("system", "엘리트 거미 처치! 기록 칩이 떨어졌다(E키로 줍기).", "event"); };

    ctx.proactive = proactive; ctx.coach = coach;
    Object.assign(game, { ctx, drone, rules, warden, coach, proactive, chatter, gatherState });

    /** 한 프레임. input = { left, right, jump, fire, reload } */
    game.step = function (dt, input) {
      FOLLOWER.update(G, dt);
      W.update(G, dt, input);
      drone.update(dt);
      if (G.over || G.cleared) return;
      rules.update();
      proactive.update(dt);
      warden.update();
      coach.update(dt);
    };

    /** 채팅 한 줄. WARDEN 근처면 WARDEN, 아니면 제미니. */
    game.submitChat = function (text) {
      const t = String(text || "").trim();
      if (!t) return "empty";
      log("me", t, "");
      if (warden.canTalk()) { warden.submitText(t); return "warden"; }
      const r = drone.submitText(t);
      if (r === "busy") log("system", "제미니가 지금은 말을 들을 수 없어요(작업·응답 대기 중). 잠시 뒤 다시 말해 주세요.", "info");
      return r;
    };

    /** R(accept)/T(decline): 선제 제안이 있으면 그것, 없고 작전 제안이 대기 중이면 "응/아니"로 서버에 전달. */
    game.answer = function (accept) {
      if (proactive.hasOffer()) { proactive.respond(accept); return "offer"; }
      if (rules.proposal.length > 0) { game.submitChat(accept ? "응" : "아니"); return "proposal"; }
      return "none";
    };

    /** E키. */
    game.interact = function () {
      const r = W.interact(G);
      if (r === "warden") warden.interact();
      else if (r === "pad-wait") log("system", `충전 패드 재사용까지 ${Math.ceil(G.pad.cooldown)}초.`, "info");
      return r;
    };

    return game;
  }

  const api = { create };
  CX.game = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
