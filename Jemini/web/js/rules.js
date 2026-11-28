// 조건부 규칙(기능 A) + 작전 제안 보관(기능 C).
// 원본: CXJeminiRuleComponent.cpp / CXJeminiClient.cpp(ParseRule)
// 서버가 준 규칙을 보관(최대 2) -> 0.2초마다 조건 평가 -> 충족 시 POST /validate -> ok면 실행.
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const R = K.RULE;

  /** 서버 rule JSON -> 내부 규칙. when이 비거나 action이 없으면 null. (ParseRule) */
  function parseRule(obj) {
    if (!obj || typeof obj !== "object") return null;
    const when = [];
    for (const c of Array.isArray(obj.when) ? obj.when : []) {
      if (!c || typeof c.type !== "string" || !c.type) continue;
      let value = 0;
      for (const f of ["cm", "pct", "n"]) {
        if (typeof c[f] === "number") value = Math.round(c[f]);
      }
      when.push({ type: c.type, who: typeof c.who === "string" ? c.who : "", value });
    }
    const act = obj.do && typeof obj.do === "object" ? obj.do : {};
    const rule = {
      when,
      action: typeof act.action === "string" ? act.action : "",
      actionWho: typeof act.who === "string" ? act.who : "",
      reserveBattery: Math.min(100, Math.max(0, Math.round(Number(obj.reserve_battery) || 0))),
      repeat: obj.repeat === true,
      label: typeof obj.label === "string" ? obj.label : "",
    };
    return rule.when.length > 0 && rule.action ? rule : null;
  }

  /** 조건 1개 평가. state는 서버로 보내는 GameState JSON과 같은 모양(거리는 m). */
  function evaluateCondition(cond, state) {
    const hpBelow = (alive, hp, hpMax) => alive && hpMax > 0 && hp * 100 < cond.value * hpMax;
    switch (cond.type) {
      case "enemy_within":
        return state.enemy_nearest_dist != null && state.enemy_nearest_dist * 100 <= cond.value;
      case "sister_hacking":
        return state.sister_puzzle != null;
      case "hp_below": {
        const b = hpBelow(state.brother_alive, state.brother_hp, state.brother_hp_max);
        const s = hpBelow(state.sister_alive, state.sister_hp, state.sister_hp_max);
        return cond.who === "brother" ? b : cond.who === "sister" ? s : b || s;
      }
      case "ammo_below":
        return state.brother_alive && state.brother_ammo < cond.value;
      case "player_downed":
        return cond.who === "brother" ? !state.brother_alive
          : cond.who === "sister" ? !state.sister_alive
          : !state.brother_alive || !state.sister_alive;
      case "battery_above":
        return state.battery > cond.value;
      default:
        return false;
    }
  }

  function conditionsMet(rule, state) {
    return rule.when.length > 0 && rule.when.every((c) => evaluateCondition(c, state));
  }

  /**
   * deps: now(), gatherState(), canAccept(), validate(action, who, reserve, state) -> Promise<resp>,
   *       execute(resp, reserve) -> {ok, reason}, say(text, status)
   */
  function create(deps) {
    const self = {
      rules: [],
      proposal: [],
      nextId: 1,
      validatingId: 0,
      lastFire: -1e9,
      proposalExpire: 0,
      checkAccum: 0,
      lastCheck: -1e9,
      lastDebug: "",
    };

    function findRule(id) { return self.rules.find((r) => r.id === id); }

    self.addRule = function (rule) {
      if (self.rules.length >= R.MAX_RULES) return { id: 0, reason: "RULE_LIMIT" };
      const valid = rule.when.length >= 1 && rule.when.length <= 3 && K.COST[rule.action] !== undefined
        && rule.when.every((c) => K.CONDITION_TYPES.includes(c.type));
      if (!valid) return { id: 0, reason: "INVALID_RULE" };
      const added = Object.assign({}, rule, {
        id: self.nextId++, held: false, heldReason: "", failSince: -1, nextTry: 0,
      });
      if (!added.label) added.label = added.action;
      self.rules.push(added);
      return { id: added.id, reason: "" };
    };

    self.removeRule = function (id) {
      if (id === 0) { const n = self.rules.length; self.rules = []; return n; }
      const before = self.rules.length;
      self.rules = self.rules.filter((r) => r.id !== id);
      return before - self.rules.length;
    };

    /** 서버 status "rule" 응답 처리. { accepted, line } (line이 있으면 서버 대사 대신 쓴다) */
    self.handleRuleResponse = function (resp) {
      const L = K.RULE_LINES;
      switch (resp.ruleOp) {
        case "register": {
          let reason = "INVALID_RULE";
          if (resp.rule) {
            const r = self.addRule(resp.rule);
            if (r.id > 0) return { accepted: true, line: "" };
            reason = r.reason;
          }
          return { accepted: false, line: reason === "RULE_LIMIT" ? L.Limit : L.Invalid };
        }
        case "cancel":
          return self.removeRule(resp.ruleId) > 0 ? { accepted: true, line: "" } : { accepted: false, line: L.NoRule };
        case "accept":
          if (self.proposal.length === 0) return { accepted: false, line: L.NoProposal };
          return self.acceptProposal() > 0 ? { accepted: true, line: "" } : { accepted: false, line: L.ProposalFull };
        case "decline":
          self.clearProposal();
          return { accepted: true, line: "" };
        default: // list: 서버가 요청 시점 목록으로 대사를 만들었다
          return { accepted: true, line: "" };
      }
    };

    self.fillState = function (state) {
      state.active_rules = self.rules.map((r) => ({ id: r.id, label: r.label, action: r.action, held: !!r.held }));
      state.pending_proposal = self.proposal.slice(0, 2).map((r) => ({ id: 0, label: r.label, action: r.action }));
    };

    // ---- 작전 제안(기능 C)
    self.setProposal = function (list) {
      self.proposal = [];
      for (const r of list || []) {
        if (self.proposal.length < R.MAX_RULES && r.when.length > 0 && r.action) self.proposal.push(r);
      }
      self.proposalExpire = deps.now() + Math.max(1, R.PROPOSAL_TIMEOUT);
    };
    self.acceptProposal = function () {
      let added = 0;
      for (const r of self.proposal) if (self.addRule(r).id > 0) added++;
      self.clearProposal();
      return added;
    };
    self.clearProposal = function () { self.proposal = []; };

    // ---- 감시·발동
    function noteFailure(rule, reason) {
      const t = deps.now();
      rule.nextTry = Math.max(rule.nextTry, t + R.RETRY_COOLDOWN);
      if (rule.failSince < 0) rule.failSince = t;
      rule.heldReason = reason;
      self.lastDebug = `#${rule.id} failed: ${reason}`;
      if (!rule.held && t - rule.failSince >= R.HOLD_AFTER) {
        rule.held = true;
        // 드론이 바쁜 것은 금방 풀리므로 말하지 않는다(HUD만). 배터리·그 외 거절은 한 번 말한다.
        if (reason !== "BUSY") {
          const battery = reason === "LOW_BATTERY" || reason === "RESERVE_BATTERY";
          deps.say(battery ? K.RULE_LINES.HeldBattery : K.RULE_LINES.HeldOther, "rejected");
        }
      }
    }

    function onValidated(ruleId, resp) {
      self.validatingId = 0;
      const rule = findRule(ruleId);
      if (!rule) return; // 그 사이 취소됨
      if (resp.status !== "ok") { noteFailure(rule, resp.reason || resp.status); return; }
      const result = deps.execute(resp, rule.reserveBattery);
      if (!result.ok) { noteFailure(rule, result.reason); return; }
      self.lastFire = deps.now();
      self.lastDebug = `fired #${ruleId} ${rule.action}`;
      if (rule.repeat) {
        rule.nextTry = self.lastFire + R.REPEAT_INTERVAL;
        rule.failSince = -1; rule.held = false; rule.heldReason = "";
      } else {
        self.removeRule(ruleId);
      }
    }

    /** CheckRules: 제안 시간 초과 + 규칙 조건 평가. */
    self.check = function () {
      const t = deps.now();
      if (self.proposal.length > 0 && t >= self.proposalExpire) self.clearProposal();
      if (self.rules.length === 0 || self.validatingId !== 0) return;
      const state = deps.gatherState();
      for (const rule of self.rules) {
        if (!conditionsMet(rule, state)) {
          if (rule.failSince >= 0 || rule.held) { rule.failSince = -1; rule.held = false; rule.heldReason = ""; }
          continue;
        }
        if (t < rule.nextTry || t - self.lastFire < R.RETRY_COOLDOWN) continue;
        if (!deps.canAccept()) { noteFailure(rule, "BUSY"); continue; }
        self.validatingId = rule.id;
        rule.nextTry = t + R.RETRY_COOLDOWN;
        self.lastDebug = `validating #${rule.id} ${rule.action}`;
        const id = rule.id;
        Promise.resolve(deps.validate(rule.action, rule.actionWho, rule.reserveBattery, state))
          .then((resp) => onValidated(id, resp))
          .catch(() => onValidated(id, { status: "fallback", reason: "NO_CONNECTION" }));
        return; // 한 번에 하나만
      }
    };

    /** 매 프레임 호출. 0.2초 간격으로 check(). */
    self.update = function () {
      const t = deps.now();
      if (t - self.lastCheck >= R.CHECK_INTERVAL) { self.lastCheck = t; self.check(); }
    };

    return self;
  }

  const api = { parseRule, evaluateCondition, conditionsMet, create };
  CX.rules = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
