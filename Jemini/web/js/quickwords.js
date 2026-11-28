// 한 단어 명령 + "응/아니" 짧은 대답 분류. 서버 없이 즉시 처리한다.
// 원본: CXJeminiProactiveComponent.cpp (NormalizeUtterance, ParseShortAnswer, HandleLocalText, TryKeywordCommand)
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);

  /** 공백·문장부호 제거 + 소문자. */
  function normalize(text) {
    let out = "";
    for (const ch of String(text || "")) {
      if (/\s/.test(ch) || K.PUNCT.includes(ch)) continue;
      out += ch.toLowerCase();
    }
    return out;
  }

  /** 1 = 동의, -1 = 거절, 0 = 짧은 대답 아님. */
  function parseShortAnswer(text) {
    const n = normalize(text);
    if (!n || n.length > K.PRO.MAX_SHORT_CHARS) return 0;
    if (K.YES_WORDS.includes(n)) return 1;
    if (K.NO_WORDS.includes(n)) return -1;
    return 0;
  }

  /** 정리된 문장에서 한 단어 명령 찾기. 공손한 끝말("해줘", "좀"...)은 한 번 떼어 본다. */
  function matchKeyword(normalized) {
    const candidates = [normalized];
    for (const suffix of K.SUFFIXES) {
      if (normalized.length > suffix.length && normalized.endsWith(suffix)) {
        candidates.push(normalized.slice(0, normalized.length - suffix.length));
      }
    }
    for (const candidate of candidates) {
      for (const [word, cmd, target, ack] of K.KEYWORDS) {
        if (candidate === word) return { word, cmd, target, ack };
      }
    }
    return null;
  }

  /**
   * 채팅 입력 분류. null이면 서버로 보낸다.
   * ctx.hasOffer: 선제 제안 대기 중, ctx.resolveSmartHack(): "HackTerminal" | "HackEnemy" | ""
   * 반환: { type: "answer", accept } | { type: "command", cmd, target, ack }
   */
  function classify(text, ctx) {
    const n = normalize(text);
    if (!n || n.length > K.PRO.MAX_SHORT_CHARS) return null;
    if (ctx && ctx.hasOffer) {
      const answer = parseShortAnswer(n);
      if (answer !== 0) return { type: "answer", accept: answer > 0 };
    }
    const hit = matchKeyword(n);
    if (!hit) return null;
    let cmd = hit.cmd;
    if (cmd === "HackSmart") {
      cmd = (ctx && ctx.resolveSmartHack && ctx.resolveSmartHack()) || "HackTerminal"; // 대상이 없으면 거절 대사가 나오게
    }
    const target = cmd === "HackEnemy" ? "enemy" : cmd === "HackTerminal" ? "sister" : hit.target;
    return { type: "command", cmd, target, ack: hit.ack };
  }

  const api = { normalize, parseShortAnswer, matchKeyword, classify };
  CX.quickwords = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
