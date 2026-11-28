// fetch 래퍼: /command, /validate, /persuade, /health. 토큰·타임아웃·실패 시 폴백 응답.
// 응답 해석은 원본 CXJeminiClient.cpp(ParseResponse, MakeFallback), CXWardenClient.cpp(ParseResponse)와 같다.
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});
  const K = CX.K || (typeof require !== "undefined" ? require("./constants.js") : null);
  const RULES = CX.rules || (typeof require !== "undefined" ? require("./rules.js") : null);

  const config = () => (root.JEMINI_CONFIG || { serverUrl: "http://127.0.0.1:8000", token: "" });
  const baseUrl = () => String(config().serverUrl || "").replace(/\/+$/, "");

  /** POST/GET -> { ok, json, error }. 네트워크 오류·HTTP 오류·시간 초과는 ok=false. */
  async function request(method, path, body, timeoutSec) {
    const cfg = config();
    const headers = {};
    if (body !== undefined) headers["Content-Type"] = "application/json; charset=utf-8";
    if (cfg.token) headers["X-API-Key"] = cfg.token;
    const ctrl = typeof AbortController !== "undefined" ? new AbortController() : null;
    const timer = ctrl ? setTimeout(() => ctrl.abort(), Math.max(1, timeoutSec) * 1000) : null;
    try {
      const res = await fetch(baseUrl() + path, {
        method, headers, body: body !== undefined ? JSON.stringify(body) : undefined, signal: ctrl ? ctrl.signal : undefined,
      });
      if (!res.ok) return { ok: false, json: null, error: "HTTP_" + res.status };
      return { ok: true, json: await res.json(), error: "" };
    } catch (e) {
      return { ok: false, json: null, error: e && e.name === "AbortError" ? "TIMEOUT" : "NO_CONNECTION" };
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  /** 실패 시 대체 응답. 배터리 차감 없음. */
  function makeFallback(battery, reason, rnd) {
    const lines = K.FALLBACK_LINES;
    return {
      status: "fallback", dialogue: lines[Math.floor((rnd || Math.random)() * lines.length)], batteryAfter: battery, reason,
      heard: "", mock: false, commandName: "", commandTarget: "", ruleOp: "", rule: null, ruleId: 0, proposal: [],
    };
  }

  /** /command, /validate 응답 JSON -> 내부 형식. status(문자열)·battery_after(숫자)가 없으면 null. */
  function parseCommandResponse(j) {
    if (!j || typeof j.status !== "string" || typeof j.battery_after !== "number") return null;
    const out = {
      status: j.status,
      dialogue: typeof j.dialogue === "string" ? j.dialogue : "",
      reason: typeof j.reason === "string" ? j.reason : "",
      heard: typeof j.heard === "string" ? j.heard : "",
      mock: j.mock === true,
      batteryAfter: Math.min(100, Math.max(0, Math.round(j.battery_after))),
      commandName: "", commandTarget: "",
      ruleOp: typeof j.rule_op === "string" ? j.rule_op : "",
      rule: null, ruleId: 0, proposal: [],
    };
    if (j.command && typeof j.command === "object") {
      out.commandName = typeof j.command.name === "string" ? j.command.name : "";
      out.commandTarget = typeof j.command.target === "string" ? j.command.target : "";
    }
    if (j.rule) out.rule = RULES.parseRule(j.rule);
    if (typeof j.rule_id === "number") out.ruleId = Math.round(j.rule_id);
    if (Array.isArray(j.proposal)) {
      for (const p of j.proposal) {
        const r = RULES.parseRule(p);
        if (r && out.proposal.length < 2) out.proposal.push(r);
      }
    }
    return out;
  }

  /** /persuade 응답 -> { status, solved, reply, reason, mock, credited(null이면 없음) }. reply가 비면 null. */
  function parseWardenResponse(j) {
    if (!j || typeof j.status !== "string") return null;
    const out = {
      status: j.status,
      solved: j.solved === true && j.status === "solved", // solved는 status와 같이 온다
      reply: (typeof j.reply === "string" ? j.reply : "").slice(0, 120),
      reason: typeof j.reason === "string" ? j.reason : "",
      mock: j.mock === true,
      credited: Array.isArray(j.credited) ? j.credited.filter((x) => typeof x === "string") : null,
    };
    return out.reply ? out : null;
  }

  const timeouts = { command: K.REQUEST_TIMEOUT, validate: 3, persuade: 17 };

  async function sendCommand(text, state) {
    const r = await request("POST", "/command", { text: String(text).slice(0, 300), state }, timeouts.command);
    const parsed = r.ok ? parseCommandResponse(r.json) : null;
    return parsed || makeFallback(state.battery, r.ok ? "BAD_RESPONSE" : r.error);
  }

  async function sendValidate(action, who, reserveBattery, state) {
    const body = { action, reserve_battery: Math.min(100, Math.max(0, reserveBattery | 0)), state };
    if (who === "brother" || who === "sister") body.who = who;
    const r = await request("POST", "/validate", body, timeouts.validate); // LLM 없는 검증: 짧게
    const parsed = r.ok ? parseCommandResponse(r.json) : null;
    return parsed || makeFallback(state.battery, r.ok ? "BAD_RESPONSE" : r.error);
  }

  async function sendPersuade(text, evidence, credited, session) {
    const body = { text: String(text).slice(0, K.WARDEN.TEXT_MAX), evidence, credited, session: session || "default" };
    const r = await request("POST", "/persuade", body, timeouts.persuade);
    const parsed = r.ok ? parseWardenResponse(r.json) : null;
    return parsed || { status: "fallback", solved: false, reply: K.WARDEN.LINES.Fallback, reason: r.ok ? "BAD_RESPONSE" : r.error, mock: false, credited: null };
  }

  /** 서버 깨우기·연결 확인. { ok, mock } */
  async function health() {
    const r = await request("GET", "/health", undefined, 60);
    return r.ok ? { ok: true, mock: !!(r.json && r.json.mock) } : { ok: false, mock: false };
  }

  const api = { request, makeFallback, parseCommandResponse, parseWardenResponse, sendCommand, sendValidate, sendPersuade, health, baseUrl };
  CX.api = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
