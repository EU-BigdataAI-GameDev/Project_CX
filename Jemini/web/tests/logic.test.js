// 순수 로직 단위 시험(개발용): node --test web/tests
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("path");
const js = (f) => require(path.join(__dirname, "..", "js", f));
const K = js("constants.js");
const Q = js("quickwords.js");
const R = js("rules.js");
const API = js("api.js");
const OFFERS = js("offers.js");
const CHATTER = js("chatter.js");
const COACH = js("coach.js");

// ---------------------------------------------------------------- 한 단어 명령
test("한 단어 명령 분류", () => {
  const c = (t, ctx) => Q.classify(t, ctx || {});
  assert.deepEqual(c("장전"), { type: "command", cmd: "ReloadSupport", target: "brother", ack: "장전해 드릴게요!" });
  assert.equal(c("엄호!").cmd, "Decoy");
  assert.equal(c("미끼 좀 던져줘"), null, "8자 넘는 문장은 서버로");
  assert.equal(c("미끼해줘").cmd, "Decoy", "공손한 끝말 제거");
  assert.equal(c("회복해요").cmd, "HealSupport");
  assert.equal(c("보호막").cmd, "Shield");
  assert.equal(c("재 부 팅").cmd, "QuickReboot", "공백 무시");
  assert.equal(c("펄스").target, "enemy");
  assert.equal(c("안녕"), null);
  assert.equal(c("힐힐"), null);
});

test("해킹 한 단어는 상황에 맞게 단말기/적", () => {
  assert.deepEqual(Q.classify("해킹", { resolveSmartHack: () => "HackEnemy" }).cmd, "HackEnemy");
  assert.equal(Q.classify("해킹", { resolveSmartHack: () => "HackEnemy" }).target, "enemy");
  const t = Q.classify("해킹", { resolveSmartHack: () => "" });
  assert.equal(t.cmd, "HackTerminal");
  assert.equal(t.target, "sister");
});

test("응/아니 짧은 대답은 제안이 있을 때만", () => {
  assert.deepEqual(Q.classify("응", { hasOffer: true }), { type: "answer", accept: true });
  assert.deepEqual(Q.classify("ㄴㄴ", { hasOffer: true }), { type: "answer", accept: false });
  assert.deepEqual(Q.classify("그래요!", { hasOffer: true }), { type: "answer", accept: true });
  assert.equal(Q.classify("응", { hasOffer: false }), null, "제안 없으면 서버(작전 제안 수락)로");
  assert.equal(Q.parseShortAnswer("좋아요."), 1);
  assert.equal(Q.parseShortAnswer("안돼"), 0);
});

// ---------------------------------------------------------------- 규칙
const baseState = () => ({
  brother_ammo: 30, brother_ammo_max: 30, brother_hp: 100, brother_hp_max: 100, brother_alive: true,
  sister_hp: 100, sister_hp_max: 100, sister_alive: true, sister_puzzle: null, enemy_nearest_dist: null, battery: 100,
});

test("규칙 조건 6종 평가", () => {
  const ev = (cond, patch) => R.evaluateCondition(cond, Object.assign(baseState(), patch));
  assert.equal(ev({ type: "enemy_within", value: 500 }, { enemy_nearest_dist: 4.9 }), true);
  assert.equal(ev({ type: "enemy_within", value: 500 }, { enemy_nearest_dist: 5.1 }), false);
  assert.equal(ev({ type: "enemy_within", value: 500 }, { enemy_nearest_dist: null }), false);
  assert.equal(ev({ type: "sister_hacking" }, { sister_puzzle: { type: "terminal", progress: 0 } }), true);
  assert.equal(ev({ type: "sister_hacking" }, {}), false);
  assert.equal(ev({ type: "hp_below", value: 30, who: "brother" }, { brother_hp: 29 }), true);
  assert.equal(ev({ type: "hp_below", value: 30, who: "brother" }, { brother_hp: 30 }), false);
  assert.equal(ev({ type: "hp_below", value: 30, who: "any" }, { sister_hp: 10 }), true);
  assert.equal(ev({ type: "hp_below", value: 30, who: "brother" }, { brother_hp: 0, brother_alive: false }), false, "쓰러진 사람은 체력 조건 아님");
  assert.equal(ev({ type: "ammo_below", value: 5 }, { brother_ammo: 4 }), true);
  assert.equal(ev({ type: "ammo_below", value: 5 }, { brother_ammo: 5 }), false);
  assert.equal(ev({ type: "player_downed", who: "sister" }, { sister_alive: false }), true);
  assert.equal(ev({ type: "player_downed", who: "any" }, {}), false);
  assert.equal(ev({ type: "battery_above", value: 50 }, { battery: 51 }), true);
  assert.equal(ev({ type: "battery_above", value: 50 }, { battery: 50 }), false);
  assert.equal(ev({ type: "nope" }, {}), false);
});

test("서버 rule JSON 해석", () => {
  const rule = R.parseRule({
    when: [{ type: "enemy_within", cm: 500, pct: null, n: null, who: null }, { type: "sister_hacking" }],
    do: { action: "Decoy", who: null }, reserve_battery: 20, repeat: true, label: "해킹 중 적 5m -> 미끼",
  });
  assert.equal(rule.when.length, 2);
  assert.equal(rule.when[0].value, 500);
  assert.equal(rule.action, "Decoy");
  assert.equal(rule.reserveBattery, 20);
  assert.equal(rule.repeat, true);
  assert.equal(R.parseRule({ when: [], do: { action: "Decoy" } }), null);
  assert.equal(R.parseRule({ when: [{ type: "ammo_below", n: 3 }], do: {} }), null);
});

function makeRules(over) {
  const clock = { t: 0 };
  const log = { validate: [], exec: [], say: [] };
  const deps = Object.assign({
    now: () => clock.t,
    gatherState: () => baseState(),
    canAccept: () => true,
    validate: (a, who, reserve) => { log.validate.push([a, who, reserve]); return Promise.resolve({ status: "ok", commandName: a, commandTarget: "", reason: "" }); },
    execute: (resp, reserve) => { log.exec.push(resp.commandName); return { ok: true, reason: "" }; },
    say: (t, s) => log.say.push([t, s]),
  }, over || {});
  return { rules: R.create(deps), clock, log, deps };
}
const flush = () => new Promise((r) => setTimeout(r, 0));
const addAmmoRule = (rules, extra) => rules.addRule(Object.assign({ when: [{ type: "ammo_below", who: "", value: 5 }], action: "ReloadSupport", actionWho: "", reserveBattery: 0, repeat: false, label: "탄약 5 미만 -> 장전" }, extra));

test("규칙: 최대 2개, 조건 충족 시 /validate -> 실행, 1회성은 삭제", async () => {
  const { rules, clock, log } = makeRules({ gatherState: () => Object.assign(baseState(), { brother_ammo: 2 }) });
  assert.ok(addAmmoRule(rules).id > 0);
  assert.ok(addAmmoRule(rules).id > 0);
  assert.equal(addAmmoRule(rules).reason, "RULE_LIMIT");
  clock.t = 1; rules.update(); await flush();
  assert.equal(log.validate.length, 1, "한 번에 하나만");
  assert.deepEqual(log.exec, ["ReloadSupport"]);
  assert.equal(rules.rules.length, 1, "1회성 삭제");
  clock.t = 1.2; rules.update(); await flush();
  assert.equal(log.validate.length, 1, "발동 간 쿨다운 2초");
  clock.t = 3.5; rules.update(); await flush();
  assert.equal(rules.rules.length, 0);
});

test("규칙: 반복 규칙은 6초 뒤 다시, 조건이 안 맞으면 호출 없음", async () => {
  let ammo = 2;
  const { rules, clock, log } = makeRules({ gatherState: () => Object.assign(baseState(), { brother_ammo: ammo }) });
  addAmmoRule(rules, { repeat: true });
  clock.t = 1; rules.update(); await flush();
  clock.t = 4; rules.update(); await flush();
  assert.equal(log.exec.length, 1, "반복 간격 6초 전엔 안 터짐");
  clock.t = 7.5; rules.update(); await flush();
  assert.equal(log.exec.length, 2);
  ammo = 30; clock.t = 20; rules.update(); await flush();
  assert.equal(log.validate.length, 2, "조건 불충족이면 /validate 안 부름");
});

test("규칙: 계속 실패하면 6초 뒤 보류 + 대사 1회(BUSY는 말 없음)", async () => {
  const { rules, clock, log } = makeRules({
    gatherState: () => Object.assign(baseState(), { brother_ammo: 2 }),
    validate: () => Promise.resolve({ status: "rejected", reason: "LOW_BATTERY" }),
  });
  addAmmoRule(rules);
  for (let t = 0; t <= 9; t += 0.2) { clock.t = t; rules.update(); await flush(); }
  assert.equal(rules.rules[0].held, true);
  assert.equal(log.say.length, 1);
  assert.equal(log.say[0][0], K.RULE_LINES.HeldBattery);

  const busy = makeRules({ gatherState: () => Object.assign(baseState(), { brother_ammo: 2 }), canAccept: () => false });
  addAmmoRule(busy.rules);
  for (let t = 0; t <= 9; t += 0.2) { busy.clock.t = t; busy.rules.update(); await flush(); }
  assert.equal(busy.rules.rules[0].held, true);
  assert.equal(busy.log.say.length, 0, "드론이 바쁜 것은 말하지 않는다");
  assert.equal(busy.log.validate.length, 0, "BUSY면 HTTP 없음");
});

test("작전 제안: 20초 지나면 조용히 버림, 수락하면 빈 자리만큼 등록", () => {
  const { rules, clock } = makeRules();
  const rule = { when: [{ type: "ammo_below", who: "", value: 5 }], action: "ReloadSupport", actionWho: "", reserveBattery: 0, repeat: false, label: "x" };
  rules.setProposal([rule, rule]);
  assert.equal(rules.proposal.length, 2);
  clock.t = 21; rules.check();
  assert.equal(rules.proposal.length, 0);
  rules.setProposal([rule, rule]);
  addAmmoRule(rules);
  assert.equal(rules.acceptProposal(), 1, "자리가 1개뿐");
  const r = rules.handleRuleResponse({ ruleOp: "accept" });
  assert.equal(r.accepted, false);
  assert.equal(r.line, K.RULE_LINES.NoProposal);
});

test("handleRuleResponse: 등록·취소·목록", () => {
  const { rules } = makeRules();
  const rule = { when: [{ type: "ammo_below", who: "", value: 5 }], action: "ReloadSupport", actionWho: "", reserveBattery: 0, repeat: false, label: "x" };
  assert.equal(rules.handleRuleResponse({ ruleOp: "register", rule }).accepted, true);
  assert.equal(rules.handleRuleResponse({ ruleOp: "register", rule: null }).line, K.RULE_LINES.Invalid);
  assert.equal(rules.handleRuleResponse({ ruleOp: "cancel", ruleId: 99 }).line, K.RULE_LINES.NoRule);
  assert.equal(rules.handleRuleResponse({ ruleOp: "cancel", ruleId: 0 }).accepted, true);
  assert.equal(rules.rules.length, 0);
  assert.equal(rules.handleRuleResponse({ ruleOp: "list" }).accepted, true);
});

// ---------------------------------------------------------------- 서버 응답 해석
test("응답 해석: 필수 필드 없으면 null, rule/proposal/command 해석", () => {
  assert.equal(API.parseCommandResponse({ status: "ok" }), null);
  assert.equal(API.parseCommandResponse(null), null);
  const ok = API.parseCommandResponse({ status: "ok", dialogue: "d", battery_after: 75, command: { name: "Shield", target: "brother" }, mock: true, reason: null });
  assert.equal(ok.commandName, "Shield");
  assert.equal(ok.batteryAfter, 75);
  assert.equal(ok.mock, true);
  assert.equal(ok.reason, "");
  const rule = API.parseCommandResponse({
    status: "rule", dialogue: "ok", battery_after: 100, rule_op: "register",
    rule: { when: [{ type: "ammo_below", n: 3 }], do: { action: "ReloadSupport" }, reserve_battery: 0, repeat: false, label: "L" },
  });
  assert.equal(rule.ruleOp, "register");
  assert.equal(rule.rule.when[0].value, 3);
  const adv = API.parseCommandResponse({ status: "advice", dialogue: "a", battery_after: 90, proposal: [
    { when: [{ type: "enemy_within", cm: 500 }], do: { action: "Decoy" }, label: "p1" }, { when: [], do: { action: "Decoy" } },
    { when: [{ type: "hp_below", pct: 40, who: "any" }], do: { action: "HealSupport" }, label: "p2" }] });
  assert.equal(adv.proposal.length, 2, "빈 규칙은 버림");
  const fb = API.makeFallback(60, "NO_CONNECTION", () => 0);
  assert.equal(fb.status, "fallback");
  assert.equal(fb.batteryAfter, 60);
  assert.equal(fb.dialogue, K.FALLBACK_LINES[0]);
  const w = API.parseWardenResponse({ status: "solved", solved: true, reply: "r", credited: ["E1", "E2"], mock: true });
  assert.equal(w.solved, true);
  assert.deepEqual(w.credited, ["E1", "E2"]);
  assert.equal(API.parseWardenResponse({ status: "solved", solved: true, reply: "" }), null);
  assert.equal(API.parseWardenResponse({ status: "rejected", solved: true, reply: "x" }).solved, false, "status와 solved가 어긋나면 solved 아님");
});

// ---------------------------------------------------------------- 선제 제안
const inputs = (patch) => Object.assign({
  battery: 100, brotherAlive: true, ammo: 30, ammoMax: 30, reloading: false, brotherHp: 100, brotherHpMax: 100,
  brotherEnemyDist: -1, sisterAlive: true, sisterHackingTerminal: false, sisterEnemyDist: -1, stuckSeconds: 0,
}, patch);

test("제안 조건 4종", () => {
  assert.equal(OFFERS.meets("reload", inputs({ ammo: 10, brotherEnemyDist: K.m(10) })), true);
  assert.equal(OFFERS.meets("reload", inputs({ ammo: 11, brotherEnemyDist: K.m(10) })), false, "30*0.3334=10.0 초과");
  assert.equal(OFFERS.meets("reload", inputs({ ammo: 5, brotherEnemyDist: K.m(16) })), false, "적이 15m 밖");
  assert.equal(OFFERS.meets("reload", inputs({ ammo: 5, brotherEnemyDist: -1 })), false, "적 없음");
  assert.equal(OFFERS.meets("heal", inputs({ brotherHp: 39 })), true);
  assert.equal(OFFERS.meets("heal", inputs({ brotherHp: 40 })), false);
  assert.equal(OFFERS.meets("heal", inputs({ brotherHp: 0 })), false);
  assert.equal(OFFERS.meets("stuck", inputs({ sisterHackingTerminal: true, stuckSeconds: 20 })), true);
  assert.equal(OFFERS.meets("stuck", inputs({ sisterHackingTerminal: true, stuckSeconds: 19 })), false);
  assert.equal(OFFERS.meets("decoy", inputs({ sisterHackingTerminal: true, sisterEnemyDist: K.m(5) })), true);
  assert.equal(OFFERS.meets("decoy", inputs({ sisterHackingTerminal: true, sisterEnemyDist: K.m(5) + 1 })), false);
  assert.equal(OFFERS.meets("decoy", inputs({ sisterHackingTerminal: false, sisterEnemyDist: 10 })), false);
});

function makeOffers(patchInputs, over) {
  const clock = { t: 100 };
  const said = [];
  let seq = 0, busy = false, proposal = false, chat = false, nearWarden = false, battery = 100;
  const ctl = { set busy(v) { busy = v; }, set proposal(v) { proposal = v; }, set chat(v) { chat = v; }, set nearWarden(v) { nearWarden = v; }, set battery(v) { battery = v; } };
  const executed = [];
  const o = OFFERS.create(Object.assign({
    now: () => clock.t,
    inputs: () => Object.assign(inputs(patchInputs), { battery }),
    terminalId: () => null,
    canAccept: () => !busy,
    droneBusy: () => busy,
    hasProposal: () => proposal,
    isChatOpen: () => chat,
    isNearWarden: () => nearWarden,
    dialogueSeq: () => seq,
    sayCoach: (line) => { said.push(line); seq++; },
    executeLocal: (cmd, target, ack) => { executed.push([cmd, target, ack]); return true; },
    chatter: null,
  }, over || {}));
  return { o, clock, said, ctl, executed, bump: () => { seq++; } };
}
const tick = (h, dt) => { h.clock.t += dt; h.o.update(dt); };

test("제안 우선순위: 미끼 > 회복 > 장전 > 퍼즐 도움", () => {
  const h = makeOffers({ brotherHp: 10, ammo: 1, brotherEnemyDist: K.m(5), sisterHackingTerminal: true, sisterEnemyDist: K.m(3), stuckSeconds: 30 });
  tick(h, 0.3);
  assert.equal(h.o.offerType, "decoy");
  assert.equal(h.said[0], K.OFFER.decoy.line);
  h.o.respond(false);
  assert.equal(h.said[1], K.DECLINE_LINE);
});

test("제안 쿨다운: 같은 종류 30초, 다른 제안 사이 8초", () => {
  const h = makeOffers({ brotherHp: 10, ammo: 1, brotherEnemyDist: K.m(5) });
  tick(h, 0.3);
  assert.equal(h.o.offerType, "heal");
  h.o.respond(false);
  tick(h, 3); assert.equal(h.o.offerType, null, "전역 8초 쿨다운 + 대사 6.5초 유지");
  tick(h, 7); assert.equal(h.o.offerType, "reload", "8초 뒤엔 다른 종류(장전)");
  h.o.respond(false);
  tick(h, 12); assert.equal(h.o.offerType, null, "heal은 30초 쿨다운");
  tick(h, 20); assert.equal(h.o.offerType, "heal");
});

test("제안 말하지 않는 때: 드론 작업·채팅·제안대기·WARDEN 근처·배터리 부족", () => {
  for (const [name, set] of [["busy", (c) => (c.busy = true)], ["proposal", (c) => (c.proposal = true)], ["chat", (c) => (c.chat = true)], ["warden", (c) => (c.nearWarden = true)], ["battery", (c) => (c.battery = 4)]]) {
    const h = makeOffers({ brotherHp: 10 });
    set(h.ctl);
    tick(h, 0.3);
    assert.equal(h.o.offerType, null, name);
  }
  const h = makeOffers({ brotherHp: 10 });
  h.bump(); tick(h, 0.3); // 다른 대사가 방금 나왔다
  assert.equal(h.o.offerType, null, "다른 대사 후 6.5초는 말하지 않음");
  tick(h, 7); assert.equal(h.o.offerType, "heal");
});

test("제안은 15초 지나면 사라지고, 드론이 바빠지거나 조건이 풀리면 지워진다", () => {
  let hp = 10;
  const h = makeOffers({}, { inputs: () => inputs({ brotherHp: hp }) });
  tick(h, 0.3); assert.equal(h.o.offerType, "heal");
  tick(h, 16); assert.equal(h.o.offerType, null, "시간 초과");
  const h2 = makeOffers({}, { inputs: () => inputs({ brotherHp: hp }) });
  tick(h2, 0.3); assert.equal(h2.o.offerType, "heal");
  hp = 100; tick(h2, 0.5); assert.equal(h2.o.offerType, null, "조건 사라짐");
  hp = 10;
  const h3 = makeOffers({}, { inputs: () => inputs({ brotherHp: hp }) });
  tick(h3, 0.3); h3.ctl.busy = true; tick(h3, 0.5);
  assert.equal(h3.o.offerType, null, "드론이 일 시작");
});

test("퍼즐 정체 20초 누적 후 제안, R 수락은 실행 경로로", () => {
  const h = makeOffers({ sisterHackingTerminal: true }, { terminalId: () => "T1" });
  for (let i = 0; i < 19; i++) tick(h, 1);
  assert.equal(h.o.offerType, null);
  for (let i = 0; i < 2; i++) tick(h, 1);
  assert.equal(h.o.offerType, "stuck");
  h.o.respond(true);
  assert.deepEqual(h.executed[0], ["HackTerminal", "sister", "좋아요, 제가 도와드릴게요!"]);
});

test("제안은 배터리가 비용 이상일 때만", () => {
  const h = makeOffers({ ammo: 1, brotherEnemyDist: K.m(5) });
  h.ctl.battery = 4; tick(h, 0.3); assert.equal(h.o.offerType, null);
  h.ctl.battery = 5; tick(h, 0.3); assert.equal(h.o.offerType, "reload");
});

// ---------------------------------------------------------------- 코멘트
test("평소 코멘트: 15초 간격, 한 바퀴 돌기 전엔 중복 없음, 혼자일 땐 둘이라는 문구 제외", () => {
  const said = [];
  let evidence = 0, t = 0;
  const lastSpeech = { v: -1e9 };
  const c = CHATTER.create({
    blockReason: () => "", sayCoach: (l) => { said.push(l); lastSpeech.v = t; }, lastSpeechTime: () => lastSpeech.v,
    evidenceNum: () => evidence, wardenSolved: () => false, random: () => 0,
  }, 0);
  const quiet = inputs({});
  for (t = 0; t < 14; t += 0.5) { c.detectEvents(quiet, t); c.tryChatter(quiet, t); }
  assert.deepEqual(said, [K.ARRIVAL_LINE], "처음엔 도착 문구뿐");
  for (; t < 40; t += 0.5) { c.detectEvents(quiet, t); c.tryChatter(quiet, t); }
  assert.ok(said.length >= 3 && said.length <= 4);
  assert.equal(new Set(said).size, said.length, "중복 없음");
  const solo = inputs({ sisterAlive: false });
  const c2 = CHATTER.create({ blockReason: () => "", sayCoach: (l) => said.push(l), lastSpeechTime: () => -1e9, evidenceNum: () => 0, wardenSolved: () => false, random: () => 0 }, 0);
  said.length = 0;
  for (let i = 0; i < 20; i++) c2.tryChatter(solo, 100 + i);
  assert.ok(!said.includes(K.CHATTER_LINES[K.PAIR_ONLY_LINE]));
  assert.equal(new Set(said).size, said.length);
});

test("이벤트 문구: 증거 첫 획득, 다 모음, 배터리 30% 이하(한 번)", () => {
  const said = [];
  let evidence = 0;
  const c = CHATTER.create({ blockReason: () => "", sayCoach: (l) => said.push(l), lastSpeechTime: () => -1e9, evidenceNum: () => evidence, wardenSolved: () => false, random: () => 0 }, 0);
  const at = (t, patch) => { const i = inputs(patch); c.detectEvents(i, t); c.tryChatter(i, t); };
  at(0, {}); at(3.1, {}); // 도착
  evidence = 2; at(10, {}); at(12, {});
  evidence = 3; at(13, {}); at(15, {});
  at(20, { battery: 25 }); at(21, { battery: 25 });
  at(30, { battery: 25 });
  assert.equal(said.filter((l) => l === K.LOW_BATTERY_LINE).length, 1);
  const events = [K.ARRIVAL_LINE, K.EVIDENCE_FIRST_LINE, K.EVIDENCE_ALL_LINE];
  assert.deepEqual(said.filter((l) => events.includes(l)), events);
});

// ---------------------------------------------------------------- WARDEN 코치
function makeCoach(over) {
  const w = { evidence: 0, credited: 0, lastReason: "", solved: false, requestPending: false, lineSeq: 0, evidenceNum() { return this.evidence; }, creditedNum() { return this.credited; } };
  const clock = { t: 0 };
  const said = [];
  let near = true, seq = 0;
  const c = COACH.create(Object.assign({
    now: () => clock.t, warden: () => w, anyoneNear: () => near, canAccept: () => true,
    dialogueSeq: () => seq, sayCoach: (l) => { said.push(l); seq++; }, isChatOpen: () => false, paused: () => false,
  }, over || {}));
  return { c, w, clock, said, setNear: (v) => (near = v) };
}
const run = (h, sec) => { for (let i = 0; i < sec * 10; i++) { h.clock.t += 0.1; h.c.update(0.1); } };

test("코치: 증거 있고 2.5초 머물면 도착 안내(1회), 증거 없이 25초면 안내", () => {
  const h = makeCoach(); h.w.evidence = 1;
  run(h, 2); assert.equal(h.said.length, 0);
  run(h, 1); assert.deepEqual(h.said, [K.WARDEN.COACH_LINES.arrival]);
  run(h, 60); assert.equal(h.said.length, 1, "도착은 한 번만");
  const h2 = makeCoach();
  run(h2, 24); assert.equal(h2.said.length, 0);
  run(h2, 2); assert.deepEqual(h2.said, [K.WARDEN.COACH_LINES.noEvidence]);
});

test("코치: 요구만(DEMAND_ONLY) -> 3/3 인정인데 막힘 -> 거절 3회면 예시", () => {
  const h = makeCoach(); h.w.evidence = 3; h.w.credited = 0;
  run(h, 3); h.said.length = 0;
  h.w.lastReason = "DEMAND_ONLY"; h.c.notifyResponse(true);
  run(h, 15); assert.equal(h.said[0], K.WARDEN.COACH_LINES.demandOnly);
  h.w.credited = 3; h.w.lastReason = "NEED_RULE"; h.c.notifyResponse(true);
  run(h, 15); assert.equal(h.said[1], K.WARDEN.COACH_LINES.allCredited);
  h.c.notifyResponse(true);
  run(h, 15); assert.equal(h.said[2], K.WARDEN.COACH_LINES.exampleAll, "거절 3회 + 증거 3개면 구체 예시");
  run(h, 60); assert.equal(h.said.length, 3, "예시는 한 번만");
});

test("코치: 증거 일부면 예시는 내용을 밝히지 않는 형식 안내, 말하지 않는 때", () => {
  const h = makeCoach(); h.w.evidence = 1; h.w.credited = 1;
  run(h, 3); h.said.length = 0;
  for (let i = 0; i < 3; i++) h.c.notifyResponse(true);
  run(h, 15);
  assert.equal(h.said[0], K.WARDEN.COACH_LINES.exampleSome);
  const h2 = makeCoach(); h2.w.evidence = 1; h2.w.requestPending = true;
  run(h2, 10); assert.equal(h2.said.length, 0, "WARDEN 요청 중");
  const h3 = makeCoach({ isChatOpen: () => true }); h3.w.evidence = 1;
  run(h3, 10); assert.equal(h3.said.length, 0, "채팅창 열림");
  const h4 = makeCoach(); h4.w.evidence = 1; h4.setNear(false);
  run(h4, 10); assert.equal(h4.said.length, 0, "근처에 아무도 없음");
  const h5 = makeCoach(); h5.w.evidence = 1; h5.w.solved = true;
  run(h5, 10); assert.equal(h5.said.length, 0, "해결됨");
});
