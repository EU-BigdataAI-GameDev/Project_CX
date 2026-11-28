// 선제 제안 4종(탄약·체력·퍼즐 정체·적 접근) + R/T 응답. 우선순위·쿨다운·"말하지 않는 때" 규칙 포함.
// 원본: CXJeminiProactiveComponent.cpp (Meets, GetBlockReason, TickComponent, ShowOffer, RespondToOffer)
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const P = K.PRO;

  /**
   * 제안 조건. inputs(거리는 px, 없으면 -1):
   *  battery, brotherAlive, ammo, ammoMax, reloading, brotherHp, brotherHpMax, brotherEnemyDist,
   *  sisterAlive, sisterHackingTerminal, sisterEnemyDist, stuckSeconds
   */
  function meets(type, i) {
    switch (type) {
      case "reload":
        return i.brotherAlive && !i.reloading && i.ammoMax > 0 && i.ammo <= i.ammoMax * P.AMMO_FRACTION
          && i.brotherEnemyDist >= 0 && i.brotherEnemyDist <= P.AMMO_ENEMY_RANGE;
      case "heal":
        return i.brotherAlive && i.brotherHpMax > 0 && i.brotherHp > 0 && i.brotherHp < i.brotherHpMax * P.HEAL_HP_FRACTION;
      case "stuck":
        return i.sisterAlive && i.sisterHackingTerminal && i.stuckSeconds >= P.STUCK_SECONDS;
      case "decoy":
        return i.sisterAlive && i.sisterHackingTerminal && i.sisterEnemyDist >= 0 && i.sisterEnemyDist <= P.DECOY_ENEMY_RANGE;
      default:
        return false;
    }
  }

  function canAfford(type, i) { return i.battery >= K.COST[K.OFFER[type].cmd]; }

  /**
   * deps: now(), inputs(), terminalId() (여동생이 해킹 중인 단말기 id 또는 null),
   *       canAccept() (드론 작업·요청 중 아님 + 게임 진행 중), droneBusy(), hasProposal(), isChatOpen(), isNearWarden(),
   *       dialogueSeq(), sayCoach(line), executeLocal(cmd, target, ack) -> bool,
   *       chatter (create 결과, 없어도 됨), random
   */
  function create(deps) {
    const self = {
      offerType: null, offerExpire: 0,
      lastOffer: { reload: -1e9, heal: -1e9, stuck: -1e9, decoy: -1e9 },
      lastAnyOffer: -1e9, lastSpeech: -1e9, seenSeq: deps.dialogueSeq(),
      stuckAccum: 0, stuckTerminal: null, lastCheck: -1e9, lastDebug: "",
    };

    self.hasOffer = () => self.offerType !== null;
    self.offerRemaining = () => (self.hasOffer() ? Math.max(0, self.offerExpire - deps.now()) : 0);
    self.cooldownRemaining = (type) => Math.max(0, P.TYPE_COOLDOWN - (deps.now() - self.lastOffer[type]));

    /** 빈 문자열이면 지금 제안·코멘트를 해도 된다. */
    self.blockReason = function () {
      const t = deps.now();
      if (self.hasOffer()) return "offer pending";
      if (deps.hasProposal()) return "LLM proposal pending";
      if (!deps.canAccept()) return "drone busy / request pending / game state";
      if (t - self.lastSpeech < P.DRONE_SPEECH_HOLD) return "drone speaking";
      if (t - self.lastAnyOffer < P.GLOBAL_COOLDOWN) return "global cooldown";
      if (deps.isChatOpen()) return "player typing";
      if (deps.isNearWarden()) return "near WARDEN (coach's turn)";
      return "";
    };

    self.showOffer = function (type) {
      const t = deps.now();
      self.offerType = type;
      self.offerExpire = t + Math.max(1, P.OFFER_TIMEOUT);
      self.lastAnyOffer = t;
      self.lastOffer[type] = t;
      deps.sayCoach(K.OFFER[type].line);
      self.seenSeq = deps.dialogueSeq(); // 내 대사는 "다른 대사"로 세지 않는다
      self.lastDebug = `offered ${type}`;
    };

    self.clearOffer = function (why) {
      if (self.hasOffer()) self.lastDebug = `${self.offerType} ${why}`;
      self.offerType = null;
    };

    /** R(accept) / T(decline) 또는 채팅의 "응/아니". 제안이 없으면 false. */
    self.respond = function (accept) {
      if (!self.hasOffer()) return false;
      const type = self.offerType;
      self.clearOffer(accept ? "accepted" : "declined");
      const t = deps.now();
      self.lastAnyOffer = t;
      self.lastOffer[type] = t;
      if (!accept) {
        deps.sayCoach(K.DECLINE_LINE);
        self.seenSeq = deps.dialogueSeq();
        return true;
      }
      const o = K.OFFER[type];
      // 기존 실행 경로(배터리·대상 검증·BUSY) — 못 시작하면(드론이 그 사이 바빠짐) 알려 준다
      if (!deps.executeLocal(o.cmd, o.target, o.ack)) deps.sayCoach(K.GONE_LINE);
      self.seenSeq = deps.dialogueSeq();
      return true;
    };

    /** 매 프레임 호출(내부에서 0.25초 간격으로 판단). dt는 초. */
    self.update = function (dt) {
      const t = deps.now();
      // 단말기 정체 시간: 같은 단말기를 계속 해킹한 시간(매 프레임 누적)
      const term = deps.terminalId();
      if (term && term === self.stuckTerminal) self.stuckAccum += dt;
      else { self.stuckTerminal = term || null; self.stuckAccum = 0; }

      if (t - self.lastCheck < P.CHECK_INTERVAL) return;
      self.lastCheck = t;

      // 다른 대사가 나온 시점 추적(처음 한 번은 기준만 잡는다)
      const seq = deps.dialogueSeq();
      if (self.seenSeq >= 0 && seq !== self.seenSeq) self.lastSpeech = t;
      self.seenSeq = seq;

      const inputs = deps.inputs();
      inputs.stuckSeconds = self.stuckAccum;
      if (deps.chatter) deps.chatter.detectEvents(inputs, t);

      // 대기 중인 제안: 시간 초과 / 드론이 일을 시작함 / 조건이 사라짐
      if (self.hasOffer()) {
        if (t >= self.offerExpire) self.clearOffer("timeout");
        else if (deps.droneBusy()) self.clearOffer("drone busy");
        else if (!meets(self.offerType, inputs)) self.clearOffer("condition gone");
        return;
      }
      if (self.blockReason()) return;
      for (const type of K.OFFER_ORDER) {
        if (meets(type, inputs) && canAfford(type, inputs) && self.cooldownRemaining(type) <= 0) {
          self.showOffer(type);
          return;
        }
      }
      if (deps.chatter) deps.chatter.tryChatter(inputs, t);
    };

    return self;
  }

  const api = { meets, canAfford, create };
  CX.offers = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
