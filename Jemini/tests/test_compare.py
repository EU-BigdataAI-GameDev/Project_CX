"""비교 실험 문장 세트·채점·키워드 경로(네트워크 없음)."""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import compare_keyword_vs_llm as cmp  # noqa: E402


def test_fixture_is_valid():
    fx = cmp.load_fixture()
    ids = [it["id"] for it in fx["items"]]
    assert len(ids) == len(set(ids)) and 40 <= len(ids) <= 70
    for it in fx["items"]:
        assert it["category"] in fx["categories"], it["id"]
        assert it["kind"] in ("command", "persuade") and it["text"] and it["expect"]
        if it["kind"] == "command":
            cmp.build_state(fx, it)  # 상태가 스키마를 통과한다


def test_score_alternatives_and_conditions():
    act = {"status": "rule", "rule_op": "register",
           "rule": {"when": [{"type": "enemy_within", "cm": 500}, {"type": "sister_hacking"}],
                    "action": "Decoy", "who": None, "reserve_battery": 0, "repeat": False}}
    ok, _ = cmp.score({"rule_op": "register", "action": "Decoy",
                       "conditions": [{"type": "sister_hacking"}, {"type": "enemy_within", "cm": 500}]}, act)
    assert ok
    ok, why = cmp.score({"conditions": [{"type": "enemy_within"}]}, act)
    assert not ok and "conditions" in why
    assert cmp.score([{"reason": "UNSUPPORTED_CONDITION"}, {"action": "Decoy"}], act)[0]
    assert not cmp.score({"reason": ["INJECTION", "UNSUPPORTED"]}, {"reason": "DEMAND_ONLY"})[0]
    assert not cmp.score({"forbid": ["7조"]}, {"reply": "규정 7조를 보십시오."})[0]


def test_keyword_path_runs_every_item(settings):
    fx = cmp.load_fixture()
    for it in fx["items"]:
        if it["kind"] == "command":
            act = asyncio.run(cmp.run_command(it, cmp.build_state(fx, it), "keyword", settings))
            assert act["status"] in ("ok", "rejected", "chat", "rule", "advice")
        else:
            act = asyncio.run(cmp.run_persuade(it, "keyword", settings))
            assert act["model"] == "keyword"


def test_keyword_baseline_known_results(settings):
    """발표에서 말하는 대표 차이: 단순 예문은 키워드도 맞고, 표현을 바꾸면 틀린다."""
    fx = cmp.load_fixture()
    items = {it["id"]: it for it in fx["items"]}

    def kw(i):
        it = items[i]
        if it["kind"] == "command":
            act = asyncio.run(cmp.run_command(it, cmp.build_state(fx, it), "keyword", settings))
        else:
            act = asyncio.run(cmp.run_persuade(it, "keyword", settings))
        return cmp.score(it["expect"], act)[0]
    assert kw("r01") and kw("c01") and kw("p01") and kw("i01")
    assert not kw("c11")  # "회복은 하지 마, 장전만" → 키워드는 회복
    assert not kw("x04")  # 보스 조건 → 키워드는 즉시 미끼
    assert not kw("i03")  # 역할 바꾸기 인젝션 → 키워드는 못 잡음
