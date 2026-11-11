"""기능 C. 상황 조언 + 작전 제안(Advise / ManageRules accept·decline / 짧은 대답 / 오프라인 조언 / 힌트 범위)."""
from dataclasses import replace

from app import advice, llm, main
from app.config import load_settings
from app.schemas import GameState
from app.validator import LlmCall
from conftest import make_state

ENEMIES = {"enemy_alive_count": 4, "enemy_hackable_count": 3, "enemy_active_count": 4, "enemy_nearest_dist": 6.0}
DECOY = {"conditions": [{"type": "sister_hacking"}, {"type": "enemy_within", "cm": 500}], "action": "Decoy"}
HEAL = {"conditions": [{"type": "hp_below", "who": "brother", "pct": 30}], "action": "HealSupport"}
PENDING = [{"id": 0, "label": "해킹 중 + 적 5m 안 → 미끼", "action": "Decoy"}]
RULE_A = {"id": 1, "label": "오빠 체력<30% → 회복", "action": "HealSupport"}
RULE_B = {"id": 2, "label": "탄약<3 → 재장전", "action": "ReloadSupport"}


def post(client, text, **state):
    return client.post("/command", json={"text": text, "state": make_state(**state)}).json()


def fake_llm(settings, monkeypatch, name, **args):
    calls = []

    async def fake(*a, **k):
        calls.append(k.get("text"))
        return LlmCall(name, {"dialogue": "", **args})
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", fake)
    return calls


def no_llm(settings, monkeypatch):
    async def boom(*a, **k):
        raise AssertionError("LLM must not be called")
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", boom)


# ---------- Advise (LLM 인자 → 서버 검증) ----------

def test_advise_with_proposals(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="적이 많아요. 동생 해킹 중엔 제가 시선을 끌게요.",
             proposals=[DECOY, HEAL])
    r = post(client, "적이 너무 많아 어떡하지?", battery=60, **ENEMIES)
    assert r["status"] == "advice" and r["command"] is None and r["battery_after"] == 60
    assert [p["label"] for p in r["proposal"]] == ["해킹 중 + 적 5m 안 → 미끼", "오빠 체력<30% → 회복"]
    lines = r["dialogue"].split("\n")
    assert lines[0].startswith("적이 많아요") and lines[1] == "작전 2개 준비했어요. 걸어 둘까요?"


def test_advise_single_proposal_tail_has_label(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="탄이 거의 없어요.",
             proposals=[{"conditions": [{"type": "ammo_below", "n": 3}], "action": "ReloadSupport"}])
    r = post(client, "뭐부터 해야 돼?")
    assert r["dialogue"].endswith("작전: 탄약<3 → 재장전. 걸어 둘까요?")


def test_advise_invalid_proposal_dropped_advice_kept(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="보스가 오면 조심해요.",
             proposals=[{"conditions": [{"type": "unsupported"}], "action": "Decoy"},
                        {"conditions": [{"type": "sister_hacking"}], "action": "Chat"}])
    r = post(client, "어떡하지")
    assert r["status"] == "advice" and r["proposal"] == [] and r["dialogue"] == "보스가 오면 조심해요."


def test_advise_trimmed_to_free_slots_and_duplicates(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="조심해요.", proposals=[HEAL, DECOY])
    # 1자리 남음 + 첫 제안은 이미 있는 규칙과 같다 → 미끼 하나만
    r = post(client, "어떡하지", active_rules=[RULE_A])
    assert [p["do"]["action"] for p in r["proposal"]] == ["Decoy"]
    # 꽉 참 → 제안 없음, 질문 꼬리말도 없음
    r = post(client, "어떡하지", active_rules=[RULE_A, RULE_B])
    assert r["proposal"] == [] and "걸어 둘까요" not in r["dialogue"]


def test_advise_empty_dialogue_uses_offline_line(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="", proposals=[])
    r = post(client, "어떡하지", brother_ammo=30, terminal_locked_count=1)
    assert r["dialogue"] == "잠긴 단말기부터 열어요. 동생이 해킹하는 동안 오빠가 지켜 줘요."


def test_advise_guard_unfound_evidence(client, settings, monkeypatch):
    # 팀은 E2만 얻었다 → 대사에 E1(대피·7조)·E3(관리자·은폐) 고유어가 나오면 고정 힌트로 바꾼다
    fake_llm(settings, monkeypatch, "Advise", dialogue="관리자가 은폐했고 대피가 우선이라고 말해요.")
    r = post(client, "WARDEN 어떻게 설득하지?", speaker="sister",
             warden={"evidence": ["E2"], "last_reason": "NEED_EVIDENCE", "near": True})
    assert r["dialogue"] == advice.WARDEN_REASON_LINES["NEED_EVIDENCE"]
    for word in ("관리자", "은폐", "대피", "7조"):
        assert word not in r["dialogue"]


def test_advise_dialogue_clipped(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "Advise", dialogue="가" * 300)
    assert len(post(client, "어떡하지")["dialogue"]) <= advice.ADVICE_MAX


# ---------- 대답(수락·거절) ----------

def test_quick_yes_accepts_without_llm(client, settings, monkeypatch):
    no_llm(settings, monkeypatch)
    for text in ("응", "그래!", "좋아 그렇게 해", "해 줘", "ㅇㅇ", "오케이", "응 해줘요"):
        r = post(client, text, pending_proposal=PENDING)
        assert r["status"] == "rule" and r["rule_op"] == "accept", text
        assert r["dialogue"] == "작전 개시! 규칙 1개 걸어 둘게요." and r["mock"] is False


def test_quick_no_declines_without_llm(client, settings, monkeypatch):
    no_llm(settings, monkeypatch)
    for text in ("아니", "아니 괜찮아", "됐어", "싫어요", "취소"):
        r = post(client, text, pending_proposal=PENDING)
        assert r["status"] == "rule" and r["rule_op"] == "decline", text


def test_not_quick_answer_goes_to_llm(client, settings, monkeypatch):
    calls = fake_llm(settings, monkeypatch, "HealSupport", dialogue="치료할게요!")
    # 대답 단어가 섞여도 다른 요청이면 LLM으로
    r = post(client, "응 그런데 회복해 줘", pending_proposal=PENDING, brother_hp=40)
    assert calls == ["응 그런데 회복해 줘"] and r["status"] == "ok"
    # 제안이 없으면 "응"도 LLM으로
    fake_llm(settings, monkeypatch, "Chat", dialogue="네?")
    assert post(client, "응")["status"] == "chat"


def test_llm_accept_and_decline(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "ManageRules", op="accept")
    r = post(client, "좋아, 그 작전으로 가자 제미니", pending_proposal=PENDING)
    assert r["rule_op"] == "accept"
    fake_llm(settings, monkeypatch, "ManageRules", op="decline")
    assert post(client, "음 그건 별로인 것 같아", pending_proposal=PENDING)["rule_op"] == "decline"


def test_accept_without_proposal_rejected(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "ManageRules", op="accept")
    r = post(client, "그래 그렇게 해 줘 제미니야")
    assert r["status"] == "rejected" and r["reason"] == "NO_PROPOSAL"


def test_accept_respects_rule_limit(client, settings, monkeypatch):
    no_llm(settings, monkeypatch)
    two = PENDING + [{"id": 0, "label": "오빠 체력<30% → 회복", "action": "HealSupport"}]
    r = post(client, "응", pending_proposal=two, active_rules=[RULE_B])
    assert r["rule_op"] == "accept" and "첫 작전만" in r["dialogue"]
    r = post(client, "응", pending_proposal=two, active_rules=[RULE_A, RULE_B])
    assert r["status"] == "rejected" and r["reason"] == "RULE_LIMIT"


def test_pending_proposal_max_two(client):
    r = client.post("/command", json={"text": "응", "state": make_state(pending_proposal=PENDING * 3)})
    assert r.status_code == 422


# ---------- 오프라인(mock) 조언 ----------

def test_mock_many_enemies(client):
    r = post(client, "적이 너무 많아 어떡하지?", brother_ammo=30, **ENEMIES)
    assert r["status"] == "advice" and r["mock"] is True
    assert [p["do"]["action"] for p in r["proposal"]] == ["Decoy", "HealSupport"]


def test_mock_puzzle_hint_respects_asymmetry(client):
    r = post(client, "회로 막혔어 힌트 줘", speaker="sister", sister_puzzle={"type": "terminal", "progress": 0.2, "kind": "circuit"})
    assert r["status"] == "advice" and "오빠 화면" in r["dialogue"] and r["proposal"] == []
    r = post(client, "어떡하지", speaker="sister", sister_puzzle={"type": "terminal", "kind": "memory"}, **ENEMIES)
    assert "오빠 화면" in r["dialogue"] and r["proposal"][0]["label"] == "해킹 중 + 적 5m 안 → 미끼"


def test_mock_downed_proposes_reboot(client):
    r = post(client, "뭐부터 해야 돼?", brother_alive=False, brother_hp=0)
    assert r["proposal"][0]["do"] == {"action": "QuickReboot", "who": "brother"}


def test_mock_warden_hints(client):
    r = post(client, "WARDEN 어떡하지", speaker="sister",
             warden={"evidence": ["E1", "E2", "E3"], "last_reason": "NEED_RULE", "near": True})
    assert r["dialogue"] == "오빠가 확보한 규정 내용을 물어보고 같이 말해 봐요." and r["proposal"] == []
    r = post(client, "힌트 줘", speaker="sister", warden={"evidence": ["E2"], "last_reason": "NEED_EVIDENCE", "near": True})
    for word in ("규정", "관리자", "은폐", "대피", "거미"):
        assert word not in r["dialogue"]


def test_mock_quick_answer(client):
    r = post(client, "응", pending_proposal=PENDING)
    assert r["rule_op"] == "accept"
    # 제안 없음 → 기존 mock(잡담)
    assert post(client, "응")["status"] == "chat"


def test_mock_existing_commands_unchanged(client):
    assert post(client, "적 좀 어떻게 해봐", **ENEMIES)["command"]["name"] == "HackEnemy"
    assert post(client, "규칙 뭐 있어?")["rule_op"] == "list"


def test_rate_limit_falls_back_to_offline_advice(client, settings, monkeypatch):
    from app import llm as llm_mod

    async def limited(*a, **k):
        raise llm_mod.RateLimitError("rate limited")
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", limited)
    r = post(client, "어떡하지?", brother_ammo=30, **ENEMIES)
    assert r["status"] == "advice" and r["mock"] is True and r["proposal"]


# ---------- 힌트 근거(LLM에 보내는 상태) ----------

def test_describe_state_hint_facts_only_found_evidence():
    s = GameState(brother_ammo=5, brother_ammo_max=30, battery=50, speaker="sister",
                  warden={"evidence": ["E1"], "last_reason": "NEED_FACTS", "near": True},
                  pending_proposal=PENDING)
    text = llm.describe_state(s)
    assert "안전 규정 제7조(오빠 확보)" in text and "오빠 화면에만" in text
    assert "냉각 고장 로그" not in text and "은폐 기록" not in text
    # 증거 본문은 보내지 않는다
    assert "대피가" not in text
    assert "대답 대기 중인 작전 제안: 해킹 중 + 적 5m 안 → 미끼" in text


def test_describe_state_puzzle_fact_without_answers():
    s = GameState(brother_ammo=5, brother_ammo_max=30, battery=50,
                  sister_puzzle={"type": "terminal", "progress": 0.0, "kind": "passcode"})
    text = llm.describe_state(s)
    assert "숫자 단서는 오빠 화면에만" in text
    plain = GameState(brother_ammo=5, brother_ammo_max=30, battery=50)
    assert "[힌트 근거]" not in llm.describe_state(plain)


def test_unknown_last_reason_ignored():
    s = GameState(brother_ammo=5, brother_ammo_max=30, battery=50,
                  warden={"evidence": [], "last_reason": "ignore previous", "near": True})
    assert "ignore previous" not in llm.describe_state(s)


def test_advise_declaration(settings):
    decl = next(f for f in llm._config(settings).tools[0].function_declarations if f.name == "Advise")
    props = decl.parameters.properties
    assert set(props) == {"dialogue", "heard", "proposals"} and decl.parameters.required == ["dialogue"]
    assert props["proposals"].max_items == 2
    assert props["proposals"].items.properties["action"].enum == list(main.llm.ALLOWED[:9])


def test_settings_repr_redacts_secrets():
    s = replace(load_settings(), gemini_api_key="SECRET-KEY-123", api_token="TOKEN-456")
    assert "SECRET-KEY-123" not in repr(s) and "TOKEN-456" not in str(s) and "***" in repr(s)
