// 화살표 퍼즐 표시 모델. 사람이 풀지 않는다 - 여동생 AI가 해킹하는 동안 진행도에 맞춰 한 칸씩 채워지는 모습만 보여 준다.
// 진행도(0~1)는 서버 상태(sister_puzzle.progress)로도 보낸다.
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});

  const ARROWS = ["←", "↑", "→", "↓"];

  /** 퍼즐 하나: 화살표 길이 n짜리 무작위 순서. */
  function makePuzzle(n, rnd) {
    const r = rnd || Math.random;
    const seq = [];
    for (let i = 0; i < n; i++) seq.push(ARROWS[Math.floor(r() * ARROWS.length)]);
    return seq;
  }

  /** 진행도 0~1 -> 채워진 칸 수. */
  function filledCount(progress, n) {
    return Math.min(n, Math.floor(Math.max(0, Math.min(1, progress)) * n + 1e-9));
  }

  const api = { ARROWS, makePuzzle, filledCount };
  CX.hack = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
