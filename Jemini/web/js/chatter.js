// 평소 코멘트(15초 간격 고정 문구 14개)와 이벤트 문구(도착·증거·배터리 30% 이하).
// 원본: CXJeminiProactiveComponent.cpp (DetectEvents, TryChatter)
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const P = K.PRO;

  /**
   * deps: blockReason() -> "" 이면 말해도 됨, sayCoach(line), lastSpeechTime() (마지막 드론 대사 시각),
   *       evidenceNum(), wardenSolved(), random() (시험용 교체)
   */
  function create(deps, startTime) {
    const rnd = deps.random || Math.random;
    const self = {
      beginTime: startTime || 0,
      bag: [],
      pendingLine: "", pendingReady: 0, pendingExpire: 0,
      arrivalQueued: false, lastEvidenceNum: 0, allEvidenceSaid: false, lowBatteryArmed: true,
    };

    function queue(line, delay, lifetime, t) {
      self.pendingLine = line;
      self.pendingReady = t + delay;
      self.pendingExpire = t + delay + lifetime;
    }

    /** 도착·증거·배터리 이벤트를 문구 대기열에 넣는다(한 번에 하나, 새 것이 이전 것을 덮는다). */
    self.detectEvents = function (inputs, t) {
      if (!self.arrivalQueued && t - self.beginTime >= P.ARRIVAL_DELAY) {
        self.arrivalQueued = true;
        queue(K.ARRIVAL_LINE, 0, 40, t);
      }
      const num = deps.evidenceNum();
      if (num > self.lastEvidenceNum && !deps.wardenSolved()) {
        if (num >= K.WARDEN.EVIDENCE.length && !self.allEvidenceSaid) {
          self.allEvidenceSaid = true;
          queue(K.EVIDENCE_ALL_LINE, 1.5, 25, t);
        } else if (self.lastEvidenceNum === 0) {
          queue(K.EVIDENCE_FIRST_LINE, 1.5, 25, t);
        }
      }
      self.lastEvidenceNum = num;
      if (inputs.battery <= P.LOW_BATTERY_PERCENT && self.lowBatteryArmed) {
        self.lowBatteryArmed = false;
        queue(K.LOW_BATTERY_LINE, 0, 20, t);
      } else if (inputs.battery > P.LOW_BATTERY_PERCENT + 20) {
        self.lowBatteryArmed = true;
      }
    };

    /** 제안할 게 없을 때: 이벤트 문구 먼저, 없으면 15초 간격 평소 코멘트. 말했으면 true. */
    self.tryChatter = function (inputs, t) {
      if (deps.blockReason()) return false;
      const sinceSpeech = t - deps.lastSpeechTime();
      if (self.pendingLine) {
        if (t >= self.pendingExpire) {
          self.pendingLine = "";
        } else if (t >= self.pendingReady && sinceSpeech >= P.EVENT_MIN_GAP) {
          deps.sayCoach(self.pendingLine);
          self.pendingLine = "";
          return true;
        }
        return false;
      }
      if (P.CHATTER_INTERVAL <= 0 || sinceSpeech < P.CHATTER_INTERVAL || t - self.beginTime < P.CHATTER_INTERVAL) return false;
      // 조용할 때만: 가까이 적이 없고 해킹 중이 아님
      const near = (inputs.brotherEnemyDist >= 0 && inputs.brotherEnemyDist <= P.CHATTER_ENEMY_RANGE)
        || (inputs.sisterEnemyDist >= 0 && inputs.sisterEnemyDist <= P.CHATTER_ENEMY_RANGE);
      if (near || inputs.sisterHackingTerminal) return false;
      const pair = inputs.brotherAlive && inputs.sisterAlive;
      if (self.bag.length === 0) self.bag = K.CHATTER_LINES.map((_, i) => i);
      for (let tries = 0; tries < self.bag.length; tries++) {
        const pick = Math.floor(rnd() * self.bag.length);
        const index = self.bag[pick];
        if (index === K.PAIR_ONLY_LINE && !pair) continue;
        self.bag.splice(pick, 1); // 한 판에서 다 돌기 전에는 같은 줄을 다시 말하지 않는다
        deps.sayCoach(K.CHATTER_LINES[index]);
        return true;
      }
      return false;
    };

    return self;
  }

  const api = { create };
  CX.chatter = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
