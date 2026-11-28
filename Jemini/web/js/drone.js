// 제미니 드론: 요청 보내기, 응답 처리, 명령 9종 실행, 배터리, 작업(비행) 상태 기계, 규칙·조언·한 단어 명령 진입점.
// 원본: CXJeminiDrone.cpp (SubmitText, HandleResponse, ExecuteCommand, TickAction, ExecuteLocalCommand, CheckLocalCommand,
//       ExecuteRuleCommand, ResolveSmartHackCommand)
// 배터리는 효과가 실제로 실행된 순간(chargePending)에 차감한다. 대상이 사라지면 차감 없이 "놓쳤어요".
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const L = K.LINES;
  const px = (cm) => cm * 0.4; // 원본 cm -> px (1m = 40px)

  /**
   * deps: world, follower, quickwords, api, gatherState(speaker) -> state JSON,
   *       say(text, status) (UI 표시), getRules(), getProactive()
   */
  function create(G, deps) {
    const W = deps.world;
    const d = {
      x: G.brother.x, y: G.brother.y - 110, battery: K.START_BATTERY,
      action: null, requestPending: false, pendingBatteryAfter: -1,
      dialogue: "", dialogueStatus: "", dialogueSeq: 0, dialogueTime: -1e9,
      serverMock: false, serverOnline: null, emotion: "neutral", emotionHold: 0, bob: 0, decoyX: null,
      lastReason: "", debug: "",
    };
    G.drone = d;

    // ---------------------------------------------------------------- 기본
    d.isBusy = () => d.requestPending || d.action !== null;
    d.gameBlocked = () => G.over || G.cleared;
    d.canAccept = () => !d.isBusy() && !d.gameBlocked();

    d.say = function (text, status) {
      d.dialogue = text; d.dialogueStatus = status || ""; d.dialogueSeq++; d.dialogueTime = G.time;
      deps.say(text, status || "");
    };

    d.setBattery = (n) => { d.battery = Math.max(0, Math.min(100, Math.round(n))); };

    function chargePending() {
      if (d.pendingBatteryAfter >= 0) { d.setBattery(d.pendingBatteryAfter); d.pendingBatteryAfter = -1; }
    }

    function baseEmotion() {
      if (!G.brother.alive || !G.sister.alive) return "angry";
      return d.battery <= K.SAD_BATTERY ? "sad" : "neutral";
    }
    function react(emotion, hold) { d.emotion = emotion; d.emotionHold = hold == null ? 4 : hold; }

    function setAction(name, extra) {
      d.action = Object.assign({ name, elapsed: 0, phase: 0, target: null, partner: null, location: null, effectDone: false }, extra || {});
    }
    function endAction(ret) {
      d.pendingBatteryAfter = -1;
      d.decoyX = null;
      if (ret) setAction("Returning"); else d.action = null;
    }
    function failTargetLost(line) {
      d.lastReason = "TARGET_LOST";
      d.say(line, "rejected");
      react("neutral");
      const a = d.action;
      const wasAway = a && !["HackAssist", "Heal"].includes(a.name);
      endAction(!!wasAway); // 대상 쪽으로 날아가 있었으면 복귀까지 작업 중
    }

    const hoverGoal = (p) => ({ x: p.x, y: p.y - 110 });
    const brotherAlive = () => G.brother.alive;

    // ---------------------------------------------------------------- 대상 조회
    d.findNextLockedTerminal = () => G.terminals.find((t) => t.state === "locked") || null;
    const sisterSession = (kind) => (G.sister.alive && G.sister.hack.kind === kind ? G.sister.hack : null);

    d.resolveSmartHack = function () {
      const enemy = sisterSession("enemy");
      if (enemy && enemy.target && !enemy.target.dead) return "HackEnemy";
      if (sisterSession("terminal")) return "HackTerminal";
      if (d.findNextLockedTerminal()) return "HackTerminal";
      if (W.queryEnemies(G).nearestHackable) return "HackEnemy";
      return "";
    };

    // ---------------------------------------------------------------- 명령 실행 (ExecuteCommand)
    /** 대상이 있으면 작업을 시작하고 true. 없으면 false(차감 없음). 응답 사이 상황이 바뀐 경우도 여기서 걸린다. */
    function executeCommand(resp) {
      const name = resp.commandName, target = resp.commandTarget;
      d.pendingBatteryAfter = resp.batteryAfter;
      const b = G.brother, s = G.sister;

      switch (name) {
        case "ReloadSupport":
          if (!b.alive) return false;
          setAction("FlyToReload", { target: b });
          return true;

        case "HackTerminal": {
          const session = sisterSession("terminal");
          if (session) { // A: 여동생이 해킹 중인 단말기 - 3초 대행
            session.assist = K.ASSIST_TIME_TERMINAL;
            setAction("HackAssist", { target: session.target, partner: s });
            return true;
          }
          const term = d.findNextLockedTerminal(); // B: 다음 잠긴 단말기로 날아가 3초 해킹
          if (term) { setAction("TerminalHack", { target: term }); return true; }
          return false;
        }

        case "HackEnemy": {
          const session = sisterSession("enemy");
          if (session && session.target && !session.target.dead) { // A: 여동생 퍼즐을 즉시 성공
            session.assist = 0.1;
            setAction("EnemyHack", { target: session.target });
            return true;
          }
          const enemy = W.queryEnemies(G).nearestHackable; // B: 가장 가까운 해킹 가능한 적 즉시 그로기
          if (!enemy) return false;
          enemy.stun = K.ENEMY_STUN_SEC;
          chargePending();
          setAction("EnemyHack", { target: enemy, effectDone: true });
          return true;
        }

        case "PulseAttack": {
          const enemy = W.queryEnemies(G).nearest;
          if (!enemy) return false;
          setAction("PulseAttack", { target: enemy });
          return true;
        }

        case "DamageBoost":
          if (!b.alive) return false;
          setAction("BoostFly", { target: b });
          return true;

        case "HealSupport": { // 응답 즉시: 살아 있는 사람만 +50, 부활 없음
          let healed = 0;
          for (const p of W.livingPlayers(G)) {
            if (p.hp < p.hpMax) { p.hp = Math.min(p.hpMax, p.hp + K.HEAL_AMOUNT); p.healFx = 1; healed++; }
          }
          if (healed === 0) return false;
          chargePending();
          setAction("Heal");
          return true;
        }

        case "Decoy": {
          const active = W.queryEnemies(G).activeList;
          if (active.length === 0) return false;
          let cx = 0;
          for (const e of active) cx += e.x;
          cx /= active.length;
          let nearest = null;
          for (const p of W.livingPlayers(G)) if (!nearest || Math.abs(p.x - cx) < Math.abs(nearest.x - cx)) nearest = p;
          let away = nearest ? Math.sign(cx - nearest.x) : 0;
          if (away === 0) away = 1;
          let lx = cx + away * px(300);
          // 닫힌 문 너머로는 끌지 않는다
          if (nearest) {
            for (const wx of [G.door.open ? null : G.door.x, G.gate.open ? null : G.gate.x]) {
              if (wx != null && nearest.x < wx && lx > wx - 20) lx = wx - 20;
            }
          }
          setAction("Decoy", { location: { x: lx, y: K.GROUND_Y - px(220) } });
          return true;
        }

        case "QuickReboot": {
          const awaiting = (p) => p && !p.alive;
          let downed = target === "sister" ? s : target === "brother" ? b : null;
          if (!awaiting(downed)) downed = awaiting(b) ? b : awaiting(s) ? s : null;
          const partner = downed === b ? s : b;
          if (!downed || !partner || !partner.alive) return false;
          setAction("Reboot", { target: downed, partner });
          return true;
        }

        case "Shield": {
          const who = target === "sister" ? s : target === "brother" ? b : b; // 말한 사람 = 오빠(솔로)
          if (!who || !who.alive) return false;
          setAction("ShieldFly", { target: who });
          return true;
        }

        default: // 모르는 명령은 무시(연동 계약) - 차감 없음
          d.pendingBatteryAfter = -1;
          return false;
      }
    }

    // ---------------------------------------------------------------- 작업 갱신 (TickAction). goal을 돌려준다.
    function tickAction(dt) {
      const a = d.action;
      if (!a) return null;
      a.elapsed += dt;
      const arrived = (goal, tolCm) => Math.hypot(d.x - goal.x, d.y - goal.y) < px(tolCm);
      const out = (goal, dash) => ({ goal, dash: !!dash });

      switch (a.name) {
        case "FlyToReload": {
          if (!a.target.alive) { failTargetLost(L.TargetLost); return null; }
          const goal = hoverGoal(a.target);
          if (arrived(goal, 60) || a.elapsed >= K.MAX_FLY) {
            const b = a.target;
            b.reloading = false; b.reloadT = 0; b.ammo = b.ammoMax; // 재장전 시작 순간 차감
            b.healFx = 1;
            chargePending();
            endAction(false);
            return null;
          }
          return out(goal, true);
        }

        case "HackAssist": {
          const term = a.target, sis = a.partner;
          const doneAt = K.ASSIST_TIME_TERMINAL - 0.2;
          const goal = sis ? hoverGoal(sis) : null;
          if (term && term.state === "hacked") {
            if (a.elapsed >= doneAt) { chargePending(); react("happy", 2); }
            endAction(false); // 여동생이 먼저 풀었으면 차감 없음
          } else if (!term || !sis.alive || (sis.hack.kind !== "terminal" && a.elapsed < doneAt) || a.elapsed > K.ASSIST_TIME_TERMINAL + 2) {
            endAction(false); // 취소·사망 -> 차감 없음
          }
          return goal ? out(goal, false) : null;
        }

        case "TerminalHack":
        case "TerminalHacking": {
          const term = a.target;
          if (!term) { failTargetLost(L.TerminalGone); return null; }
          const goal = { x: term.x, y: K.GROUND_Y - 80 };
          if (a.name === "TerminalHack") {
            if (term.state !== "locked") { failTargetLost(L.TerminalGone); return null; }
            if (arrived(goal, 80) || a.elapsed >= K.MAX_TRAVEL) { term.state = "drone"; a.name = "TerminalHacking"; a.elapsed = 0; }
            return out(goal, true);
          }
          if (term.state !== "drone") { failTargetLost(L.TerminalGone); return null; }
          if (a.elapsed >= K.ASSIST_TIME_TERMINAL) {
            W.markTerminalHacked(G, term);
            chargePending();
            react("happy", 2);
            endAction(true);
            return null;
          }
          return out(goal, true);
        }

        case "EnemyHack": {
          const e = a.target;
          let goal = null;
          if (e && !e.dead) goal = { x: e.x, y: e.y - 72 };
          if (!a.effectDone && e && e.stun > 0 && !e.dead) { a.effectDone = true; chargePending(); }
          else if (!a.effectDone && a.elapsed >= 0.6 && e && !e.dead) {
            // 보험: 여동생 경로가 못 끝냈으면 드론이 직접 건다
            e.stun = K.ENEMY_STUN_SEC; W.cancelHack(G.sister);
            a.effectDone = true; chargePending();
          }
          if (a.elapsed >= 1.0) {
            if (!a.effectDone) failTargetLost(L.TargetLost); else endAction(true);
            return null;
          }
          return goal ? out(goal, a.elapsed < 0.5) : null;
        }

        case "PulseAttack": {
          const e = a.target;
          if (a.phase === 0) {
            if (!e || e.dead) { failTargetLost(L.TargetLost); return null; }
            const goal = { x: e.x, y: e.y - px(260) };
            if (arrived(goal, 70) || a.elapsed >= K.MAX_TRAVEL) {
              d.pulseFx = { x: e.x, y: e.y, t: 0.6 };
              W.damageEnemy(G, e, e.hp + 1000); // 남은 체력 이상 피해(한 방)
              if (e.dead) { chargePending(); react("happy", 2.5); }
              else { failTargetLost(L.TargetLost); return null; }
              a.phase = 1; a.elapsed = 0; a.location = goal;
            }
            return out(goal, true);
          }
          if (a.elapsed >= 0.5) { endAction(true); return null; }
          return out(a.location, false);
        }

        case "BoostFly": {
          if (!a.target.alive) { failTargetLost(L.TargetLost); return null; }
          const goal = hoverGoal(a.target);
          if (arrived(goal, 60) || a.elapsed >= K.MAX_QUICK_FLY) {
            a.target.boost = a.target.boost + K.DAMAGE_BOOST_SEC; // 강화 중이면 남은 시간 + 30초
            a.target.healFx = 1;
            chargePending();
            endAction(false);
            return null;
          }
          return out(goal, true);
        }

        case "Heal": {
          const living = W.livingPlayers(G);
          let goal = null;
          if (living.length) {
            const mx = living.reduce((sum, p) => sum + p.x, 0) / living.length;
            goal = { x: mx, y: K.GROUND_Y - 110 - 16 };
          }
          if (a.elapsed >= 1.0) { endAction(false); return null; }
          return goal ? out(goal, false) : null;
        }

        case "Decoy":
        case "DecoyActive": {
          const goal = a.location;
          if (a.name === "Decoy") {
            if (arrived(goal, 80) || a.elapsed >= K.MAX_TRAVEL) {
              let lured = 0;
              for (const e of W.queryEnemies(G).activeList) { e.decoy = K.DECOY_SEC; lured++; }
              if (lured === 0) { failTargetLost(L.TargetLost); return null; }
              d.decoyX = goal.x;
              chargePending();
              a.name = "DecoyActive"; a.elapsed = 0;
            }
            return out(goal, true);
          }
          if (a.elapsed >= K.DECOY_SEC) { endAction(true); return null; }
          return out(goal, false);
        }

        case "Reboot":
        case "RebootBeam": {
          const downed = a.target, partner = a.partner;
          if (downed.alive) { // 연출 중 자연 부활 -> 차감 없음
            d.lastReason = "TARGET_LOST";
            d.say(L.AlreadyRevived, "chat");
            endAction(true);
            return null;
          }
          if (!partner.alive) { failTargetLost(L.TargetLost); return null; }
          const goal = hoverGoal(partner);
          if (a.name === "Reboot") {
            if (arrived(goal, 70) || a.elapsed >= K.MAX_TRAVEL * 0.5) { a.name = "RebootBeam"; a.elapsed = 0; d.beamFx = { target: downed, t: K.REBOOT_BEAM_SEC }; }
            return out(goal, true);
          }
          if (a.elapsed >= K.REBOOT_BEAM_SEC) {
            W.revive(G, downed, K.REVIVE_HP);
            chargePending();
            react("happy", 2.5);
            endAction(false);
            return null;
          }
          return out(goal, false);
        }

        case "ShieldFly": {
          if (!a.target.alive) { failTargetLost(L.TargetLost); return null; }
          const goal = hoverGoal(a.target);
          if (arrived(goal, 60) || a.elapsed >= K.MAX_QUICK_FLY) {
            a.target.shield = K.SHIELD_SEC; // 이미 보호막이면 5초로 다시 채움
            chargePending();
            endAction(false);
            return null;
          }
          return out(goal, true);
        }

        case "Returning": {
          const follow = brotherAlive() ? G.brother : G.sister.alive ? G.sister : null;
          let goal = null;
          if (follow) {
            goal = hoverGoal(follow);
            if (arrived(goal, 100)) { endAction(false); return null; }
          }
          if (a.elapsed >= K.MAX_TRAVEL) { endAction(false); return null; }
          return goal ? out(goal, true) : null;
        }

        default:
          endAction(false);
          return null;
      }
    }

    d.update = function (dt) {
      d.bob += dt;
      if (d.emotionHold > 0) d.emotionHold -= dt; else d.emotion = baseEmotion();
      if (d.pulseFx) { d.pulseFx.t -= dt; if (d.pulseFx.t <= 0) d.pulseFx = null; }
      if (d.beamFx) { d.beamFx.t -= dt; if (d.beamFx.t <= 0) d.beamFx = null; }
      const res = tickAction(dt);
      let goal, dash = false;
      if (res) { goal = res.goal; dash = res.dash; }
      else {
        const t = brotherAlive() ? G.brother : G.sister.alive ? G.sister : null;
        if (!t) return;
        const gx = t.x - (t.facing || 1) * 30, gy = t.y - 110;
        goal = { x: gx, y: gy };
      }
      if (Math.hypot(goal.x - d.x, goal.y - d.y) > 1200) { d.x = goal.x; d.y = goal.y; return; } // 너무 멀면 붙는다
      if (dash) { // 일정 속도 돌진 (DashSpeed)
        const dx = goal.x - d.x, dy = goal.y - d.y, len = Math.hypot(dx, dy) || 1, step = Math.min(len, 900 * dt);
        d.x += (dx / len) * step; d.y += (dy / len) * step;
      } else { // 부드럽게 따라간다 (FollowSpeed)
        const k = 1 - Math.exp(-5 * dt);
        d.x += (goal.x - d.x) * k; d.y += (goal.y - d.y) * k;
      }
    };

    // ---------------------------------------------------------------- 응답 처리 (HandleResponse)
    d.handleResponse = function (resp) {
      d.requestPending = false;
      d.debug = `${resp.status} ${resp.commandName}(${resp.commandTarget}) after=${resp.batteryAfter} reason=${resp.reason}`;
      if (resp.status !== "fallback") { d.serverMock = resp.mock; d.serverOnline = true; } // fallback은 mock 여부를 알 수 없으니 이전 값 유지
      else if (/^(NO_CONNECTION|TIMEOUT|HTTP_)/.test(resp.reason)) { d.serverOnline = false; d.offlineReason = resp.reason; }
      const rules = deps.getRules();

      // 기능 C: 상황 조언 + 작전 제안. 실행·배터리 없음. 제안은 등록하지 않고 대답 대기로.
      if (resp.status === "advice") {
        rules.setProposal(resp.proposal);
        d.say(resp.dialogue, "advice");
        react("happy", 1);
        return;
      }
      // 기능 A: 규칙 등록·취소·목록(배터리·드론 작업과 무관)
      if (resp.status === "rule") {
        const r = rules.handleRuleResponse(resp);
        d.say(r.line || resp.dialogue, r.accepted ? "rule" : "rejected");
        react(r.accepted ? "happy" : "neutral", r.accepted ? 1.5 : 4);
        return;
      }
      // 작업 중에 받은 요청(규칙 대답용으로 허용)이 실행 응답으로 왔으면 실행하지 않는다
      if (resp.status === "ok" && d.action) {
        d.say(L.Busy, "rejected");
        react("neutral");
        return;
      }
      if (resp.status === "ok" && !d.gameBlocked()) {
        if (executeCommand(resp)) {
          d.say(resp.dialogue, resp.status);
          const pulse = resp.commandName === "PulseAttack";
          react(pulse ? "angry" : "happy", pulse ? 1.5 : 4);
        } else {
          failTargetLost(resp.commandName === "HackTerminal" ? L.TerminalGone : L.TargetLost); // 응답 사이 대상이 사라짐
        }
        return;
      }
      d.say(resp.dialogue, resp.status);
      // 표정: 배터리 부족·연결 실패 = 슬픔, 그 외 거절 = 기본, 잡담 = 유지
      if (resp.status === "fallback" || resp.reason === "LOW_BATTERY") react("sad");
      else if (resp.status === "rejected") react("neutral");
    };

    // ---------------------------------------------------------------- 요청 시작
    /** 드론 작업 중이라도 작전 제안 대기·등록된 규칙이 있으면 발화는 받는다("응/아니"·"규칙 취소"). */
    function beginRequest() {
      if (!d.canAccept()) {
        const rules = deps.getRules();
        const ruleTalk = !d.requestPending && d.action !== null && (rules.proposal.length > 0 || rules.rules.length > 0);
        if (!ruleTalk || d.gameBlocked()) return false;
      }
      d.requestPending = true;
      d.lastReason = "";
      return true;
    }

    /** 채팅 한 줄. 반환: "handled"(서버 없이 처리) | "sent" | "busy". */
    d.submitText = function (text) {
      const trimmed = String(text || "").trim().slice(0, 300);
      if (!trimmed) return "busy";
      const local = deps.quickwords.classify(trimmed, { hasOffer: deps.getProactive().hasOffer(), resolveSmartHack: d.resolveSmartHack });
      if (local) {
        if (local.type === "answer") { deps.getProactive().respond(local.accept); return "handled"; }
        if (d.executeLocal(local.cmd, local.target, local.ack)) return "handled";
      }
      if (!beginRequest()) return "busy";
      const state = deps.gatherState("brother");
      deps.api.sendCommand(trimmed, state).then((resp) => d.handleResponse(resp));
      return "sent";
    };

    // ---------------------------------------------------------------- 서버 없이 실행 (한 단어 명령·선제 제안 수락)
    d.checkLocalCommand = function (name) {
      const b = G.brother;
      switch (name) {
        case "ReloadSupport":
          if (!b.alive) return L.ReloadNo;
          if (b.reloading) return L.ReloadingNow;
          if (b.ammo >= b.ammoMax) return L.AmmoFull;
          return "";
        case "HealSupport":
          return W.livingPlayers(G).some((p) => p.hp < p.hpMax) ? "" : L.HealNone;
        case "Decoy":
          return W.queryEnemies(G).activeList.length ? "" : L.DecoyNone;
        case "PulseAttack":
          return W.queryEnemies(G).nearest ? "" : L.PulseNone;
        case "HackEnemy": {
          const s = sisterSession("enemy");
          return (s && s.target && !s.target.dead) || W.queryEnemies(G).nearestHackable ? "" : L.HackEnemyNone;
        }
        case "HackTerminal":
          return sisterSession("terminal") || d.findNextLockedTerminal() ? "" : L.HackTerminalNone;
        case "DamageBoost":
          return b.alive ? "" : L.BoostNo;
        case "Shield":
          return b.alive ? "" : L.ShieldNone;
        case "QuickReboot":
          return (!G.brother.alive && G.sister.alive) || (!G.sister.alive && G.brother.alive) ? "" : L.RebootNone;
        default:
          return "";
      }
    };

    d.executeLocal = function (name, target, ack) {
      const cost = K.COST[name];
      if (cost === undefined || !beginRequest()) return false;
      const why = d.checkLocalCommand(name);
      let resp;
      if (why) resp = { status: "rejected", reason: "INVALID_TARGET", batteryAfter: d.battery, dialogue: why, mock: d.serverMock };
      else if (d.battery < cost) resp = { status: "rejected", reason: "LOW_BATTERY", batteryAfter: d.battery, dialogue: L.LowBatteryLocal(cost, d.battery), mock: d.serverMock };
      else resp = { status: "ok", commandName: name, commandTarget: target, batteryAfter: d.battery - cost, dialogue: ack, reason: "", mock: d.serverMock };
      d.handleResponse(Object.assign({ commandName: "", commandTarget: "", heard: "", ruleOp: "", rule: null, ruleId: 0, proposal: [] }, resp));
      return true;
    };

    /** 규칙 발동 직전 /validate가 ok였을 때 실행. { ok, reason } (ExecuteRuleCommand) */
    d.executeRule = function (resp, reserveBattery) {
      if (resp.status !== "ok") return { ok: false, reason: resp.reason || "REJECTED" };
      if (!d.canAccept()) return { ok: false, reason: "BUSY" }; // /validate를 기다리는 사이 다른 작업이 시작됨
      const cost = K.COST[resp.commandName];
      if (cost === undefined) return { ok: false, reason: "UNKNOWN_COMMAND" };
      if (d.battery < cost) return { ok: false, reason: "LOW_BATTERY" };
      if (d.battery - cost < reserveBattery) return { ok: false, reason: "RESERVE_BATTERY" };
      const adjusted = Object.assign({}, resp, { batteryAfter: d.battery - cost }); // 지금 배터리로 다시 계산
      d.lastReason = "";
      if (!executeCommand(adjusted)) { endAction(false); return { ok: false, reason: "TARGET_LOST" }; }
      d.say(resp.dialogue, "ok");
      const pulse = resp.commandName === "PulseAttack";
      react(pulse ? "angry" : "happy", pulse ? 1.5 : 4);
      return { ok: true, reason: "" };
    };

    /** 충전 패드. 배터리가 모자랐으면 한마디. */
    d.recharge = function () {
      const wasLow = d.battery < 100;
      d.setBattery(100);
      if (wasLow) d.say(L.Recharged, "chat");
    };

    return d;
  }

  const api = { create };
  CX.drone = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
