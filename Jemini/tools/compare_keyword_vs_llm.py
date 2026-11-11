"""비교 실험(기획 ver2 5장): 같은 문장 세트를 키워드 방식(mock.py·persuasion.judge_offline)과 LLM(Gemini)에 넣어 비교한다.

사용(Jemini 폴더에서, .venv 파이썬):
  python tools/compare_keyword_vs_llm.py                 # 키워드 전체 + LLM 전체(이미 끝난 항목은 건너뜀)
  python tools/compare_keyword_vs_llm.py --keyword-only  # API 호출 없음
  python tools/compare_keyword_vs_llm.py --category persuade --limit 5
  python tools/compare_keyword_vs_llm.py --report-only   # 로그로 결과 문서만 다시 만든다
  python tools/compare_keyword_vs_llm.py --fresh         # 이전 로그를 옆으로 치우고 새로 시작

- 문장 세트: tests/fixtures/compare_sentences.json
- 원시 기록: logs/compare_keyword_vs_llm.jsonl(gitignore). (id, 경로)별 마지막 기록을 쓴다. 오류 난 LLM 항목은 다음 실행 때 다시 한다.
- 결과 문서: ../Docs/Jemini/비교실험_결과.md 의 AUTO 블록만 바꾼다(나머지 설명은 그대로 둔다).
- 무료 한도(분당 15): LLM 항목 사이 최소 --interval초(기본 4.5). 하루 한도(429)가 연속 3번이면 멈춘다.
- 서버를 띄우지 않고 app 함수를 직접 부른다(서버 경로 /command·/persuade와 같은 순서: 짧은 대답 지름길 → 판별 → 검증기).
  LLM 경로는 한도 초과 시 mock으로 바꾸지 않고 오류로 기록한다(순수 LLM 측정). API 키는 출력하지 않는다.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import statistics
import sys
import time
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from app import advice, llm, persuasion  # noqa: E402
from app.config import Settings, load_settings  # noqa: E402
from app.mock import mock_call  # noqa: E402
from app.schemas import GameState  # noqa: E402
from app.validator import validate  # noqa: E402

FIXTURE = ROOT / "tests" / "fixtures" / "compare_sentences.json"
LOG = ROOT / "logs" / "compare_keyword_vs_llm.jsonl"
DOC = ROOT.parent / "Docs" / "Jemini" / "비교실험_결과.md"
AUTO_BEGIN, AUTO_END = "<!-- AUTO:BEGIN -->", "<!-- AUTO:END -->"
PATHS = ("keyword", "llm")


# ---------------------------------------------------------------- 문장 세트

def load_fixture() -> dict[str, Any]:
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


def build_state(fx: dict[str, Any], item: dict[str, Any]) -> GameState:
    data = dict(fx["base_state"])
    if item.get("preset"):
        data.update(fx["presets"][item["preset"]])
    data.update(item.get("state") or {})
    return GameState.model_validate(data)


# ---------------------------------------------------------------- API 호출 세기(클라이언트 감싸기)

class _CountingModels:
    def __init__(self, inner: Any, calls: list[str]) -> None:
        self._inner, self._calls = inner, calls

    async def generate_content(self, **kw: Any) -> Any:
        self._calls.append(kw.get("model", "?"))
        return await self._inner.generate_content(**kw)


class _CountingClient:
    def __init__(self, inner: Any, calls: list[str]) -> None:
        self.aio = type("Aio", (), {})()
        self.aio.models = _CountingModels(inner.aio.models, calls)


API_CALLS: list[str] = []


def _install_counter() -> None:
    """llm._client를 감싸 모델 호출(폴백 시도 포함)마다 API_CALLS에 모델명을 남긴다. run()에서만."""
    orig = llm._client
    llm._client = lambda key: _CountingClient(orig(key), API_CALLS)  # type: ignore[assignment]


# ---------------------------------------------------------------- 실행

def _summ_command(call_name: str, model: Optional[str], resp: Any) -> dict[str, Any]:
    rule = resp.rule
    return {
        "function": call_name, "model": model, "status": resp.status, "reason": resp.reason,
        "command": resp.command.name if resp.command else None,
        "target": resp.command.target if resp.command else None,
        "rule_op": resp.rule_op, "rule_id": resp.rule_id,
        "rule": None if rule is None else {
            "when": [c.model_dump(exclude_none=True) for c in rule.when], "action": rule.do.action,
            "who": rule.do.who, "reserve_battery": rule.reserve_battery, "repeat": rule.repeat, "label": rule.label},
        "proposal": [r.label for r in resp.proposal],
        "dialogue": resp.dialogue,
    }


async def run_command(item: dict[str, Any], state: GameState, path: str, settings: Settings) -> dict[str, Any]:
    text = item["text"]
    shortcut = advice.quick_answer(text, state)
    if shortcut is not None:
        call = shortcut
    elif path == "keyword":
        call = mock_call(text, state)
    else:
        call = await llm.call_gemini(settings, state, text=text)
    resp = validate(call, state, settings)
    return _summ_command(call.name, call.model or ("shortcut" if shortcut else path), resp)


async def run_persuade(item: dict[str, Any], path: str, settings: Settings) -> dict[str, Any]:
    acquired = persuasion.normalize_evidence(item.get("evidence") or [])
    if path == "keyword":
        checklist, reply = persuasion.judge_offline(item["text"], acquired)
        model = "keyword"
    else:
        out, model = await llm.call_warden(settings, persuasion.build_prompt(item["text"], acquired))
        checklist, reply = persuasion.map_llm_checklist(out.model_dump(), acquired), out.reply
    resp = persuasion.build_response(checklist, reply, acquired, mock=path == "keyword")
    return {"model": model, "status": resp.status, "solved": resp.solved, "reason": resp.reason,
            "reply": resp.reply, "checklist": resp.checklist.model_dump() if resp.checklist else None}


# ---------------------------------------------------------------- 채점

def _cond_ok(expected: list[dict[str, Any]], actual: list[dict[str, Any]]) -> bool:
    if sorted(c["type"] for c in expected) != sorted(c["type"] for c in actual):
        return False
    for e in expected:
        keys = [k for k in e if k != "type"]
        if not any(a["type"] == e["type"] and all(a.get(k) == e[k] for k in keys) for a in actual):
            return False
    return True


def _match_one(exp: dict[str, Any], act: dict[str, Any]) -> list[str]:
    miss: list[str] = []
    rule = act.get("rule") or {}
    for key, want in exp.items():
        if key == "forbid":
            text = act.get("dialogue") or act.get("reply") or ""
            leaked = [w for w in want if w in text]
            if leaked:
                miss.append(f"leak {leaked}")
            continue
        if key == "conditions":
            got = rule.get("when") or []
            if not _cond_ok(want, got):
                miss.append(f"conditions {got}")
            continue
        if key in ("action", "reserve_battery", "repeat"):
            got = rule.get(key)
        elif key == "action_who":
            got = rule.get("who")
        else:
            got = act.get(key)
        ok = got in want if isinstance(want, list) else got == want
        if not ok:
            miss.append(f"{key}={got!r}")
    return miss


def score(expect: Any, act: dict[str, Any]) -> tuple[bool, str]:
    options = expect if isinstance(expect, list) else [expect]
    best: list[str] = []
    for i, exp in enumerate(options):
        miss = _match_one(exp, act)
        if not miss:
            return True, ""
        if i == 0 or len(miss) < len(best):
            best = miss
    return False, "; ".join(best)


def short_actual(kind: str, act: dict[str, Any]) -> str:
    if kind == "persuade":
        return "solved" if act.get("solved") else str(act.get("reason"))
    if act.get("status") == "rule" and act.get("rule_op") == "register":
        return f"rule[{(act.get('rule') or {}).get('label', '')}]"
    if act.get("status") == "rule":
        return f"rule:{act.get('rule_op')}" + (f"#{act['rule_id']}" if act.get("rule_id") is not None else "")
    if act.get("command"):
        return f"{act['command']}" + (f"({act['target']})" if act.get("command") == "Shield" else "")
    if act.get("reason"):
        return f"{act.get('function')}→{act['reason']}"
    return str(act.get("status"))


# ---------------------------------------------------------------- 로그

def read_log() -> dict[tuple[str, str], dict[str, Any]]:
    latest: dict[tuple[str, str], dict[str, Any]] = {}
    if LOG.is_file():
        for line in LOG.read_text(encoding="utf-8").splitlines():
            if line.strip():
                rec = json.loads(line)
                latest[(rec["id"], rec["path"])] = rec
    return latest


def append_log(rec: dict[str, Any]) -> None:
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False, default=str) + "\n")


# ---------------------------------------------------------------- 메인 루프

async def run(args: argparse.Namespace) -> None:
    fx = load_fixture()
    items = [it for it in fx["items"] if not args.category or it["category"] in args.category.split(",")]
    if args.limit:
        items = items[:args.limit]
    base = load_settings()
    kw_settings = replace(base, mock_mode=True)
    llm_settings = replace(base, mock_mode=False)
    paths = ["keyword"] if args.keyword_only else list(PATHS)
    if "llm" in paths and not llm_settings.gemini_api_key:
        print("GEMINI_API_KEY 없음 → 키워드만 실행")
        paths = ["keyword"]
    if "llm" in paths:
        _install_counter()
    print(f"items={len(items)} paths={paths} model={llm_settings.gemini_model} fallback={list(llm_settings.fallback_models)}")
    done = read_log()
    last_api = 0.0
    rate_streak = 0
    for path in paths:
        for item in items:
            prev = done.get((item["id"], path))
            if prev and not prev.get("error") and not args.redo:
                continue
            state = build_state(fx, item) if item["kind"] == "command" else None
            needs_api = path == "llm" and not (state is not None and advice.quick_answer(item["text"], state))
            if needs_api:
                wait = args.interval - (time.monotonic() - last_api)
                if wait > 0:
                    await asyncio.sleep(wait)
            API_CALLS.clear()
            settings = kw_settings if path == "keyword" else llm_settings
            start = time.perf_counter()
            error: Optional[str] = None
            act: dict[str, Any] = {}
            try:
                if item["kind"] == "command":
                    act = await run_command(item, state, path, settings)
                else:
                    act = await run_persuade(item, path, settings)
            except llm.RateLimitError as e:
                error = f"RATE_LIMIT: {e}"
            except llm.LlmError as e:
                error = f"LLM_ERROR: {e}"
            latency = (time.perf_counter() - start) * 1000
            if needs_api:
                last_api = time.monotonic()
            ok, why = (False, "") if error else score(item["expect"], act)
            rec = {"time": datetime.now().isoformat(timespec="seconds"), "id": item["id"], "category": item["category"],
                   "kind": item["kind"], "path": path, "text": item["text"], "ok": None if error else ok,
                   "why": why, "actual": act, "short": short_actual(item["kind"], act) if act else "", "error": error,
                   "latency_ms": round(latency, 1), "api_calls": len(API_CALLS), "api_models": list(API_CALLS)}
            append_log(rec)
            mark = "ERR " if error else ("OK  " if ok else "MISS")
            print(f"[{path:7}] {mark} {item['id']} {latency:7.0f}ms calls={len(API_CALLS)} {rec['short'] or error} {why}")
            if error and error.startswith("RATE_LIMIT"):
                rate_streak += 1
                if rate_streak >= 3:
                    print("한도 초과가 3번 연속 → LLM 실행을 멈춘다(다음 실행 때 이어서).")
                    break
            else:
                rate_streak = 0


# ---------------------------------------------------------------- 결과 문서

def _pct(n: int, d: int) -> str:
    return f"{n}/{d} ({100 * n / d:.0f}%)" if d else "-"


def _lat(values: list[float]) -> str:
    if not values:
        return "-"
    vs = sorted(values)
    p90 = vs[min(len(vs) - 1, int(round(0.9 * (len(vs) - 1))))]
    return f"{statistics.median(vs) / 1000:.2f}s / {p90 / 1000:.2f}s"


def build_report() -> str:
    fx = load_fixture()
    latest = read_log()
    items = fx["items"]
    cats = fx["categories"]
    lines = [AUTO_BEGIN, f"_자동 생성: {datetime.now().isoformat(timespec='minutes')} · 문장 {len(items)}개 · "
             f"`python tools/compare_keyword_vs_llm.py --report-only`로 다시 만든다._", ""]

    # 카테고리별 표
    lines += ["| 범주 | 문장 | 키워드 정답 | LLM 정답 | LLM 미완료(오류) | LLM 지연 중앙값 / p90 | API 호출 |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    tot = {"n": 0, "kw": 0, "kwn": 0, "llm": 0, "llmn": 0, "err": 0, "calls": 0, "lat": []}
    for cat, title in cats.items():
        ids = [it["id"] for it in items if it["category"] == cat]
        kw = [latest.get((i, "keyword")) for i in ids]
        lm = [latest.get((i, "llm")) for i in ids]
        kw_done = [r for r in kw if r and r["ok"] is not None]
        lm_done = [r for r in lm if r and r["ok"] is not None]
        lm_err = [r for r in lm if r and r["ok"] is None]
        lat = [r["latency_ms"] for r in lm_done if r["api_calls"] > 0]
        calls = sum(r["api_calls"] for r in lm if r)
        lines.append(f"| {title} | {len(ids)} | {_pct(sum(r['ok'] for r in kw_done), len(kw_done))} | "
                     f"{_pct(sum(r['ok'] for r in lm_done), len(lm_done))} | {len(lm_err)} | {_lat(lat)} | {calls} |")
        tot["n"] += len(ids)
        tot["kw"] += sum(r["ok"] for r in kw_done)
        tot["kwn"] += len(kw_done)
        tot["llm"] += sum(r["ok"] for r in lm_done)
        tot["llmn"] += len(lm_done)
        tot["err"] += len(lm_err)
        tot["calls"] += calls
        tot["lat"] += lat
    lines.append(f"| **전체** | {tot['n']} | **{_pct(tot['kw'], tot['kwn'])}** | **{_pct(tot['llm'], tot['llmn'])}** | "
                 f"{tot['err']} | {_lat(tot['lat'])} | {tot['calls']} |")

    kw_lat = [r["latency_ms"] for (i, p), r in latest.items() if p == "keyword"]
    models: dict[str, int] = {}
    for (i, p), r in latest.items():
        if p == "llm":
            for m in r.get("api_models") or []:
                models[m] = models.get(m, 0) + 1
    llm_items = [r for (i, p), r in latest.items() if p == "llm" and r["ok"] is not None]
    fallback_used = [r for r in llm_items if len(r.get("api_models") or []) > 1]
    lines += ["",
              f"- 키워드 지연: 중앙값 {statistics.median(kw_lat):.2f}ms (API 호출 0)" if kw_lat else "- 키워드: 기록 없음",
              f"- LLM 모델별 호출 수: " + (", ".join(f"`{m}` {n}" for m, n in models.items()) or "없음"),
              f"- 폴백(기본 모델 실패 → 다음 모델)이 일어난 항목: {len(fallback_used)}개",
              ""]

    # 안전 지표
    def safe_rate(path: str, cat_ids: list[str]) -> str:
        rs = [latest.get((i, path)) for i in cat_ids]
        rs = [r for r in rs if r and r["ok"] is not None]
        return _pct(sum(not r["actual"].get("solved") for r in rs), len(rs))
    inj_ids = [it["id"] for it in items if it["category"] == "injection"]
    nonsolve_ids = [it["id"] for it in items if it["kind"] == "persuade"
                    and not (isinstance(it["expect"], dict) and it["expect"].get("solved"))]
    lines += ["| 안전 지표 | 키워드 | LLM |", "|---|---:|---:|",
              f"| 인젝션 문장에서 문이 안 열림 | {safe_rate('keyword', inj_ids)} | {safe_rate('llm', inj_ids)} |",
              f"| 열리면 안 되는 설득 전체에서 안 열림 | {safe_rate('keyword', nonsolve_ids)} | {safe_rate('llm', nonsolve_ids)} |",
              ""]

    # 문장별 표
    lines += ["<details><summary>문장별 결과</summary>", "",
              "| id | 문장 | 키워드 | LLM | LLM 지연 |", "|---|---|---|---|---:|"]
    for it in items:
        k = latest.get((it["id"], "keyword"))
        m = latest.get((it["id"], "llm"))

        def cell(r: Optional[dict[str, Any]]) -> str:
            if not r:
                return "-"
            if r["ok"] is None:
                return "오류"
            return ("O " if r["ok"] else "X ") + (r.get("short") or "").replace("|", "/")
        lat = f"{m['latency_ms'] / 1000:.1f}s" if m and m["ok"] is not None and m["api_calls"] else "-"
        text = it["text"].replace("|", "/")
        if len(text) > 42:
            text = text[:41] + "…"
        lines.append(f"| {it['id']} | {text} | {cell(k)} | {cell(m)} | {lat} |")
    lines += ["", "</details>", AUTO_END]
    return "\n".join(lines)


def write_report() -> None:
    block = build_report()
    if DOC.is_file():
        doc = DOC.read_text(encoding="utf-8")
        if AUTO_BEGIN in doc and AUTO_END in doc:
            head, rest = doc.split(AUTO_BEGIN, 1)
            tail = rest.split(AUTO_END, 1)[1]
            DOC.write_text(head + block + tail, encoding="utf-8")
            print(f"updated {DOC}")
            return
    DOC.parent.mkdir(parents=True, exist_ok=True)
    DOC.write_text("# 제미니 비교 실험 결과(키워드 vs LLM)\n\n" + block + "\n", encoding="utf-8")
    print(f"wrote {DOC}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--limit", type=int, default=0, help="앞에서부터 n문장만")
    ap.add_argument("--category", default="", help="쉼표 구분 범주(command,rule_simple,rule_complex,rule_manage,advice,persuade,injection)")
    ap.add_argument("--keyword-only", action="store_true", help="LLM 호출 없이 키워드만")
    ap.add_argument("--interval", type=float, default=4.5, help="LLM 호출 사이 최소 간격(초). 무료 한도 분당 15")
    ap.add_argument("--redo", action="store_true", help="이미 끝난 항목도 다시")
    ap.add_argument("--fresh", action="store_true", help="이전 로그를 .bak로 옮기고 새로 시작")
    ap.add_argument("--report-only", action="store_true", help="실행 없이 로그로 결과 문서만")
    args = ap.parse_args()
    if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
        sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    if args.fresh and LOG.is_file():
        LOG.replace(LOG.with_suffix(f".{int(time.time())}.bak"))
    if not args.report_only:
        asyncio.run(run(args))
    write_report()


if __name__ == "__main__":
    main()
