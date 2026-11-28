// WARDEN 설득: /persuade 호출, 응답의 credited 보관->다음 요청에 전송, 3초 쿨다운, 200자 제한, 분석 중 접수(대기열).
// 원본: Warden/CXWardenTerminal.cpp (SubmitText, QueueText, HandleResponse, Interact, CheckApproach), CXWardenClient.cpp
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const WK = K.WARDEN;
  const SESSION = "Map1:WARDEN_1"; // 서버 session 규칙: [A-Za-z0-9_.:-]

  /**
   * deps: api (sendPersuade), onLine(wardenText, playerText, status) (UI 표시), coach() (또는 null), onSolved()
   */
  function create(G, deps) {
    const w = {
      solved: false, requestPending: false, lastReason: "", mock: false,
      credited: [false, false, false], lastAttempt: -1e9, queuedText: "", flushAt: 0,
      lineSeq: 0, line: "", playerLine: "", lineStatus: "", introduced: false, pendingPlayerText: "",
    };

    w.evidenceIds = () => WK.EVIDENCE.filter((_, i) => G.evidence[i]).map((e) => e.id);
    w.evidenceNum = () => G.evidence.filter(Boolean).length;
    w.creditedIds = () => WK.EVIDENCE.filter((_, i) => w.credited[i] && G.evidence[i]).map((e) => e.id);
    w.creditedNum = () => w.creditedIds().length;
    w.cooldownRemaining = () => Math.max(0, WK.ATTEMPT_COOLDOWN - (G.time - w.lastAttempt));

    const livingDx = () => [G.brother, G.sister].filter((p) => p.alive).map((p) => Math.abs(p.x - G.wardenX));
    /** 살아 있는 사람이 range(px) 안에 있나. */
    w.anyoneWithin = (range) => livingDx().some((dx) => dx <= range);
    w.isNear = () => w.anyoneWithin(WK.SHOW_RANGE);          // 상태(state.warden.near)용: 대사가 보이는 거리
    w.anyoneNear = () => w.anyoneWithin(WK.TALK_RANGE);        // 코치용
    /** 채팅(Enter)이 WARDEN으로 가는가: 오빠가 5.5m 안에 있고 아직 안 풀렸을 때. */
    w.canTalk = () => !w.solved && G.brother.alive && Math.abs(G.brother.x - G.wardenX) <= WK.TALK_RANGE;

    w.setLine = function (wardenText, playerText, status) {
      w.line = wardenText; w.playerLine = playerText; w.lineStatus = status; w.lineSeq++;
      deps.onLine(wardenText, playerText, status);
    };

    /** E키: 상황 다시 듣기. 증거가 하나도 없으면 봉쇄 안내, 있으면 근거 요구. */
    w.interact = function () {
      if (w.requestPending || w.solved) return;
      w.setLine(w.evidenceNum() === 0 ? WK.LINES.Intro : WK.LINES.Hint, "", "intro");
    };

    function queueText(text, why) {
      w.queuedText = text;
      if (w.requestPending) {
        w.setLine(WK.LINES.AnalyzingQueued, text, "busy");
      } else {
        const wait = w.cooldownRemaining();
        w.setLine(`${WK.LINES.Cooldown} ${Math.ceil(wait)}초 뒤 자동으로 전달합니다.`, text, "cooldown");
        scheduleFlush(0);
      }
    }

    function scheduleFlush(minDelay) {
      if (!w.queuedText || w.solved) return;
      w.flushAt = G.time + Math.max(0.05, minDelay, w.cooldownRemaining() + 0.05);
    }

    /** 채팅 한 줄을 WARDEN에게. 접수(전송 또는 대기열)했으면 true. */
    w.submitText = function (text) {
      const clean = String(text || "").trim().slice(0, WK.TEXT_MAX);
      if (!clean || w.solved) return false;
      if (w.requestPending || w.cooldownRemaining() > 0) { queueText(clean, w.requestPending ? "analyzing" : "cooldown"); return true; }
      w.lastAttempt = G.time;
      w.introduced = true;
      w.requestPending = true;
      w.queuedText = "";
      w.pendingPlayerText = clean;
      w.playerLine = clean;
      deps.api.sendPersuade(clean, w.evidenceIds(), w.creditedIds(), SESSION).then((resp) => w.handleResponse(resp));
      return true;
    };

    w.handleResponse = function (resp) {
      w.requestPending = false;
      w.mock = resp.mock;
      if (resp.credited) { // 누적 인정: 응답의 credited로 교체(획득한 증거만)
        w.credited = WK.EVIDENCE.map((e, i) => resp.credited.includes(e.id) && !!G.evidence[i]);
      }
      w.setLine(resp.reply, w.pendingPlayerText, resp.status);
      // 제미니 조언 힌트용. 시도 간격·연결 실패는 판정이 아니므로 이전 사유 유지.
      if (resp.status === "rejected") w.lastReason = resp.reason;
      const coach = deps.coach && deps.coach();
      if (coach) coach.notifyResponse(resp.status === "rejected");
      if (resp.solved) { w.queuedText = ""; w.solve(); }
      else scheduleFlush(WK.QUEUE_READ_DELAY); // 분석 중에 받아 둔 텍스트는 답을 읽을 시간 뒤에 이어서 보낸다
    };

    w.solve = function () {
      if (w.solved) return;
      w.solved = true;
      w.requestPending = false;
      G.gate.open = true;
      G.clearT = 2.5;
      if (deps.onSolved) deps.onSolved();
    };

    w.update = function () {
      if (w.solved) return;
      // 처음 가까이 오면 한 번 인사
      if (!w.introduced && w.anyoneWithin(WK.TALK_RANGE * WK.INTRO_RANGE_MULT)) {
        w.introduced = true;
        w.setLine(WK.LINES.Intro, "", "intro");
      }
      if (w.queuedText && !w.requestPending && G.time >= w.flushAt) {
        const text = w.queuedText;
        w.queuedText = "";
        w.submitText(text);
      }
    };

    return w;
  }

  const api = { create, SESSION };
  CX.warden = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
