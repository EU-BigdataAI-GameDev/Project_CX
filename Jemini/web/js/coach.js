// WARDEN 코치: 단말기 근처에서 막혔을 때 제미니가 단계별 힌트(도착·증거 없음·3/3인데 막힘·요구만·예시).
// 원본: CXWardenCoachComponent.cpp
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const C = K.WARDEN.COACH;
  const EVIDENCE_COUNT = K.WARDEN.EVIDENCE.length;

  const STEPS = { ARRIVAL: 1, NO_EVIDENCE: 2, ALL_CREDITED: 3, DEMAND_ONLY: 4, EXAMPLE: 5 };

  function lineFor(step, allEvidence) {
    const L = K.WARDEN.COACH_LINES;
    switch (step) {
      case STEPS.ARRIVAL: return L.arrival;
      case STEPS.NO_EVIDENCE: return L.noEvidence;
      case STEPS.ALL_CREDITED: return L.allCredited;
      case STEPS.DEMAND_ONLY: return L.demandOnly;
      // 증거 3개를 다 얻었을 때만 구체 예시. 아니면 내용을 밝히지 않는 형식 안내
      case STEPS.EXAMPLE: return allEvidence ? L.exampleAll : L.exampleSome;
      default: return "";
    }
  }

  /**
   * deps: now(), warden() -> { evidenceNum(), creditedNum(), lastReason, solved, requestPending, lineSeq },
   *       anyoneNear() (WARDEN TalkRange 안에 살아 있는 사람), canAccept(), dialogueSeq(), sayCoach(line),
   *       isChatOpen(), paused()
   */
  function create(deps) {
    const self = {
      rejectCount: 0, nearSeconds: 0, shownAtReject: [-1, -1, -1, -1, -1, -1],
      lastHint: -1e9, lastDroneSpeech: -1e9, lastWardenLine: -1e9, seenDroneSeq: deps.dialogueSeq(), seenWardenSeq: -1,
      lastCheck: -1e9, lastShownStep: 0,
    };

    self.notifyResponse = function (rejected) { if (rejected) self.rejectCount++; };

    function canRepeat(step) {
      const shown = self.shownAtReject[step];
      if (shown < 0) return true;
      if (step === STEPS.ALL_CREDITED || step === STEPS.DEMAND_ONLY) {
        return self.rejectCount - shown >= Math.max(1, C.REPEAT_AFTER_REJECTIONS);
      }
      return false; // 도착·증거 없음·예시는 한 번만
    }

    self.evaluateStep = function () {
      const w = deps.warden();
      if (!w || w.solved) return 0;
      const evidence = w.evidenceNum(), credited = w.creditedNum(), reason = w.lastReason || "";
      if (self.rejectCount >= C.EXAMPLE_REJECT_COUNT && canRepeat(STEPS.EXAMPLE) && (credited >= 1 || evidence >= EVIDENCE_COUNT)) return STEPS.EXAMPLE;
      if (self.rejectCount >= 1 && reason === "DEMAND_ONLY" && canRepeat(STEPS.DEMAND_ONLY)) return STEPS.DEMAND_ONLY;
      if (self.rejectCount >= 1 && credited >= EVIDENCE_COUNT && reason !== "DEMAND_ONLY" && canRepeat(STEPS.ALL_CREDITED)) return STEPS.ALL_CREDITED;
      if (evidence >= 1 && self.nearSeconds >= C.ARRIVAL_DELAY && canRepeat(STEPS.ARRIVAL)) return STEPS.ARRIVAL;
      if (evidence === 0 && self.nearSeconds >= C.NO_EVIDENCE_SECONDS && canRepeat(STEPS.NO_EVIDENCE)) return STEPS.NO_EVIDENCE;
      return 0;
    };

    self.blockReason = function () {
      const w = deps.warden();
      const t = deps.now();
      if (!w) return "no terminal";
      if (w.solved) return "solved";
      if (self.nearSeconds <= 0) return "nobody near";
      if (w.requestPending) return "WARDEN request pending";
      if (!deps.canAccept()) return "drone busy / game state";
      if (t - self.lastDroneSpeech < C.DRONE_SPEECH_HOLD) return "drone speaking";
      if (t - self.lastWardenLine < C.READ_DELAY_AFTER_WARDEN) return "reading WARDEN line";
      if (t - self.lastHint < C.HINT_COOLDOWN) return "cooldown";
      if (deps.paused()) return "paused";
      if (deps.isChatOpen()) return "player typing";
      return "";
    };

    /** 매 프레임 호출. dt는 초. */
    self.update = function (dt) {
      const w = deps.warden();
      if (!w || w.solved) return;
      const t = deps.now();
      self.nearSeconds = deps.anyoneNear() ? self.nearSeconds + dt : 0;
      if (t - self.lastCheck < C.CHECK_INTERVAL) return;
      self.lastCheck = t;

      // 다른 대사가 나온 시점 추적(처음 한 번은 기준만 잡는다)
      const seq = deps.dialogueSeq();
      if (self.seenDroneSeq >= 0 && seq !== self.seenDroneSeq) self.lastDroneSpeech = t;
      self.seenDroneSeq = seq;
      if (self.seenWardenSeq >= 0 && w.lineSeq !== self.seenWardenSeq) self.lastWardenLine = t;
      self.seenWardenSeq = w.lineSeq;

      if (self.nearSeconds <= 0) return;
      const step = self.evaluateStep();
      if (step === 0 || self.blockReason()) return;
      deps.sayCoach(lineFor(step, w.evidenceNum() >= EVIDENCE_COUNT));
      self.seenDroneSeq = deps.dialogueSeq(); // 내 대사는 "다른 대사"로 세지 않는다
      self.lastHint = t;
      self.lastShownStep = step;
      self.shownAtReject[step] = self.rejectCount;
    };

    return self;
  }

  const api = { STEPS, lineFor, create };
  CX.coach = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
