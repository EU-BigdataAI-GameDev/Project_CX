// 브라우저 진입점: 루프, 입력, 화면 전환, 서버 연결 확인.
(function () {
  "use strict";
  const CX = window.CX;
  const K = CX.K;

  const canvas = document.getElementById("game");
  const logEl = document.getElementById("log");
  const input = document.getElementById("chatInput");
  const statusEl = document.getElementById("status");
  const form = document.getElementById("chatForm");
  const modeEl = document.getElementById("chatMode");
  CX.ui.init({ canvas, log: logEl, input, status: statusEl });

  const game = CX.game.create({ log: (who, text, status) => CX.ui.addLog(who, text, status) });
  const G = game.G;
  const view = { cam: 0, serverOnline: null, puzzleSeq: puzzleSeq };

  // 여동생 해킹 퍼즐에 보여 줄 화살표(단말기/적 각각 한 번 만들어 둔다)
  const seqs = { terminal: CX.hack.makePuzzle(8), enemy: CX.hack.makePuzzle(5) };
  let lastEnemyTarget = null;
  function puzzleSeq(type) {
    if (type === "enemy" && G.sister.hack.target !== lastEnemyTarget) { lastEnemyTarget = G.sister.hack.target; seqs.enemy = CX.hack.makePuzzle(5); }
    return seqs[type];
  }

  // ---------------------------------------------------------------- 입력
  const keys = {};
  let mouseDown = false;
  function typing() { return document.activeElement === input; }

  window.addEventListener("keydown", (e) => {
    if (typing()) {
      if (e.key === "Escape") input.blur();
      return;
    }
    const k = e.key.toLowerCase();
    if (e.key === "Enter") { e.preventDefault(); if (!G.over && !G.cleared) input.focus(); return; }
    if (["a", "d", "w", " ", "j", "q", "arrowleft", "arrowright", "arrowup"].includes(k)) e.preventDefault();
    if (e.repeat) { keys[k] = true; return; }
    keys[k] = true;
    if (k === "e") game.interact();
    else if (k === "r") game.answer(true);
    else if (k === "t") game.answer(false);
  });
  window.addEventListener("keyup", (e) => { keys[e.key.toLowerCase()] = false; });
  canvas.addEventListener("mousedown", () => { mouseDown = true; });
  window.addEventListener("mouseup", () => { mouseDown = false; });
  window.addEventListener("blur", () => { for (const k in keys) keys[k] = false; mouseDown = false; });

  input.addEventListener("focus", () => { game.typing = true; keys.a = keys.d = keys.w = keys[" "] = keys.j = false; });
  input.addEventListener("blur", () => { game.typing = false; });
  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = input.value;
    input.value = "";
    input.blur();
    if (text.trim()) game.submitChat(text);
  });
  document.getElementById("restart").addEventListener("click", () => window.location.reload());

  function readInput() {
    return {
      left: !!(keys.a || keys.arrowleft), right: !!(keys.d || keys.arrowright),
      jump: !!(keys.w || keys[" "] || keys.arrowup), fire: !!(keys.j || mouseDown), reload: !!keys.q,
    };
  }

  // ---------------------------------------------------------------- 서버 연결 확인(Render 무료 서버는 잠들어 있을 수 있다)
  function refreshStatus() {
    const d = G.drone;
    if (d.serverMock) CX.ui.setStatus("제미니 서버: 모의(MOCK) 모드 - 키워드로 판별", "mock");
    else if (view.serverOnline === false) CX.ui.setStatus("제미니 서버에 연결할 수 없어요 - config.js의 주소·토큰 확인 또는 로컬 서버(python run_server.py)", "off");
    else if (view.serverOnline) CX.ui.setStatus("제미니 서버: 연결됨(AI)", "on");
    else CX.ui.setStatus("제미니 연결 중… (서버가 잠들어 있으면 최대 1분)", "wait");
  }
  function wake() {
    CX.api.health().then((h) => {
      view.serverOnline = h.ok;
      if (h.ok) G.drone.serverMock = h.mock;
      CX.ui.addLog("system", h.ok ? (h.mock ? "서버 연결됨(모의 모드)." : "서버 연결됨.") : "서버에 연결하지 못했어요. 제미니는 연결 실패 대사만 말합니다.", "info");
      refreshStatus();
    });
  }
  CX.ui.addLog("system", "Enter로 제미니에게 말하세요. 예: 장전 / 미끼 던져 / 적이 5m 안에 오면 보호막 씌워줘 / 어떡하지?", "info");
  CX.ui.addLog("system", "WARDEN 문 앞(5.5m)에서는 Enter 채팅이 WARDEN에게 갑니다.", "info");
  wake();

  // ---------------------------------------------------------------- 루프
  let last = performance.now();
  function frame(now) {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    game.step(dt, readInput());
    // 응답으로 연결 상태 갱신
    const d = G.drone;
    if (d.serverOnline !== null) view.serverOnline = d.serverOnline;
    view.cam = Math.max(0, Math.min(K.WORLD_W - K.W, G.brother.x - K.W * 0.35));
    modeEl.textContent = game.warden.canTalk() ? "WARDEN에게 말하기 (200자)" : "제미니에게 말하기";
    input.maxLength = game.warden.canTalk() ? K.WARDEN.TEXT_MAX : 300;
    CX.ui.render(G, game.ctx, view);
    refreshStatus();
    document.getElementById("restart").hidden = !(G.over || G.cleared);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  window.CXGAME = game; // 디버그·시험용
})();
