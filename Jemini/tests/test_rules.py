"""기능 A. 조건부 지시(RegisterRule / ManageRules / POST /validate)."""
from dataclasses import replace

import pytest

from app import main
from app.validator import LlmCall
from conftest import make_state

ENEMIES = {"enemy_alive_count": 2, "enemy_hackable_count": 2, "enemy_active_count": 2,
           "enemy_nearest_dist": 6.0}
RULE_A = {"id": 1, "label": "해킹 중 + 적 5m 안 → 미끼", "action": "Decoy"}
RULE_B = {"id": 7, "label": "오빠 체력<30% → 회복", "action": "HealSupport", "held": True}


def post(client, text, **state):
    return client.post("/command", json={"text": text, "state": make_state(**state)}).json()


def fake_llm(settings, monkeypatch, name, **args):
    async def fake(*a, **k):
        return LlmCall(name, {"dialogue": "", **args})
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", fake)


def rule_of(r):
    assert r["status"] == "rule" and r["rule_op"] == "register", r
    assert r["command"] is None and r["reason"] is None
    return r["rule"]


# ---------- RegisterRule (LLM 인자 → 서버 검증·정규화) ----------

def test_register_basic(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy",
             conditions=[{"type": "sister_hacking"}, {"type": "enemy_within", "cm": 500}])
    r = post(client, "동생이 해킹하는 동안 적이 5m 안으로 오면 미끼 써 줘", battery=30)
    rule = rule_of(r)
    assert rule["when"] == [
        {"type": "sister_hacking", "cm": None, "pct": None, "n": None, "who": None},
        {"type": "enemy_within", "cm": 500, "pct": None, "n": None, "who": None}]
    assert rule["do"] == {"action": "Decoy", "who": None}
    assert rule["reserve_battery"] == 0 and rule["repeat"] is False
    assert rule["label"] == "해킹 중 + 적 5m 안 → 미끼"
    # 등록은 배터리를 쓰지 않는다
    assert r["battery_after"] == 30 and r["dialogue"]


def test_register_allowed_while_busy_and_no_battery(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="HealSupport",
             conditions=[{"type": "hp_below", "pct": 30}])
    r = post(client, "체력 30 아래면 회복", drone_busy=True, battery=0)
    assert rule_of(r)["when"][0]["who"] == "brother"  # 지목 없음 → 말한 사람


def test_register_reserve_and_repeat(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="HealSupport", reserve_battery=20, repeat=True,
             conditions=[{"type": "hp_below", "who": "sister", "pct": 30}])
    rule = rule_of(post(client, "동생 체력 30 아래면 매번 회복, 배터리는 20 남겨"))
    assert rule["reserve_battery"] == 20 and rule["repeat"] is True
    assert rule["label"] == "동생 체력<30% → 회복 (예비 20) [반복]"


def test_register_meters_converted_and_clamped(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="HackEnemy",
             conditions=[{"type": "enemy_within", "cm": 5}])
    assert rule_of(post(client, "x"))["when"][0]["cm"] == 500
    fake_llm(settings, monkeypatch, "RegisterRule", action="HackEnemy",
             conditions=[{"type": "enemy_within", "cm": 99999.0}])
    assert rule_of(post(client, "x"))["when"][0]["cm"] == 1500


def test_register_defaults_fill_missing_values(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="ReloadSupport",
             conditions=[{"type": "ammo_below"}, {"type": "battery_above"}])
    when = rule_of(post(client, "x"))["when"]
    assert when[0]["n"] == 5 and when[1]["n"] == 50


def test_register_duplicate_conditions_merged(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy",
             conditions=[{"type": "sister_hacking"}] * 4)
    assert len(rule_of(post(client, "x"))["when"]) == 1


def test_register_quick_reboot_target_from_condition(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="QuickReboot",
             conditions=[{"type": "player_downed", "who": "brother"}])
    assert rule_of(post(client, "내가 쓰러지면 바로 부활"))["do"] == {"action": "QuickReboot", "who": "brother"}


def test_register_shield_defaults_to_speaker(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Shield",
             conditions=[{"type": "enemy_within", "cm": 300}])
    assert rule_of(post(client, "x", speaker="sister"))["do"] == {"action": "Shield", "who": "sister"}


def test_register_who_ignored_for_other_actions(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy", who="sister",
             conditions=[{"type": "sister_hacking"}])
    assert rule_of(post(client, "x"))["do"]["who"] is None


@pytest.mark.parametrize("conditions", [
    [{"type": "unsupported"}],
    [{"type": "boss_appears"}],
    [{"type": "sister_hacking"}, {"type": "unsupported"}],
    ["not a dict"],
])
def test_unsupported_condition(client, settings, monkeypatch, conditions):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy", conditions=conditions)
    r = post(client, "보스가 나오면 미끼", battery=40)
    assert r["status"] == "rejected" and r["reason"] == "UNSUPPORTED_CONDITION"
    assert r["rule"] is None and r["battery_after"] == 40
    assert "감시" in r["dialogue"] or "센서" in r["dialogue"]


def test_too_many_conditions(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy", conditions=[
        {"type": "sister_hacking"}, {"type": "enemy_within", "cm": 500},
        {"type": "battery_above", "n": 30}, {"type": "ammo_below", "n": 3}])
    assert post(client, "x")["reason"] == "TOO_MANY_CONDITIONS"


@pytest.mark.parametrize("args", [
    {"action": "Decoy", "conditions": []},
    {"action": "Decoy"},
    {"action": "Chat", "conditions": [{"type": "sister_hacking"}]},
    {"action": "RegisterRule", "conditions": [{"type": "sister_hacking"}]},
    {"action": "SelfDestruct", "conditions": [{"type": "sister_hacking"}]},
])
def test_invalid_rule(client, settings, monkeypatch, args):
    fake_llm(settings, monkeypatch, "RegisterRule", **args)
    r = post(client, "x")
    assert r["status"] == "rejected" and r["reason"] == "INVALID_RULE" and r["rule"] is None


def test_rule_limit_two(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "RegisterRule", action="Decoy", conditions=[{"type": "sister_hacking"}])
    assert post(client, "x", active_rules=[RULE_A])["status"] == "rule"
    r = post(client, "x", active_rules=[RULE_A, RULE_B])
    assert r["status"] == "rejected" and r["reason"] == "RULE_LIMIT"


# ---------- ManageRules ----------

def test_cancel_all(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "ManageRules", op="cancel", index=0)
    r = post(client, "규칙 다 취소해", active_rules=[RULE_A, RULE_B])
    assert r["status"] == "rule" and r["rule_op"] == "cancel" and r["rule_id"] == 0


def test_cancel_by_index_returns_id(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "ManageRules", op="cancel", index=2)
    r = post(client, "2번 규칙 취소", active_rules=[RULE_A, RULE_B])
    assert r["rule_id"] == 7 and "2번" in r["dialogue"]


@pytest.mark.parametrize("rules,index", [([], 0), ([RULE_A], 2), ([RULE_A], -1)])
def test_cancel_no_rule(client, settings, monkeypatch, rules, index):
    fake_llm(settings, monkeypatch, "ManageRules", op="cancel", index=index)
    r = post(client, "규칙 취소", active_rules=rules)
    assert r["status"] == "rejected" and r["reason"] == "NO_RULE"


def test_list_rules_server_builds_dialogue(client, settings, monkeypatch):
    fake_llm(settings, monkeypatch, "ManageRules", op="list", dialogue="규칙 3개 있어요!")
    r = post(client, "규칙 뭐 있어?", active_rules=[RULE_A, RULE_B])
    assert r["status"] == "rule" and r["rule_op"] == "list"
    # LLM 대사(틀린 개수) 대신 서버가 실제 목록으로
    assert r["dialogue"] == "지금 규칙은 이거예요.\n1) 해킹 중 + 적 5m 안 → 미끼\n2) 오빠 체력<30% → 회복 — 보류 중"
    assert "없어요" in post(client, "규칙 뭐 있어?")["dialogue"]


# ---------- 모의 모드 키워드 판별 ----------

def test_mock_register_example_1(client):
    rule = rule_of(post(client, "동생이 해킹하는 동안 적이 5m 안으로 오면 미끼 써 줘"))
    assert [c["type"] for c in rule["when"]] == ["sister_hacking", "enemy_within"]
    assert rule["when"][1]["cm"] == 500 and rule["do"]["action"] == "Decoy"


def test_mock_register_example_2(client):
    rule = rule_of(post(client, "체력 30 아래면 회복해 줘. 단 배터리는 20 남겨", speaker="sister"))
    assert rule["when"] == [{"type": "hp_below", "pct": 30, "who": "sister", "cm": None, "n": None}]
    assert rule["do"]["action"] == "HealSupport" and rule["reserve_battery"] == 20


def test_mock_register_example_3(client):
    rule = rule_of(post(client, "내가 쓰러지면 가능할 때 바로 부활시켜 줘", speaker="brother"))
    assert rule["when"][0] == {"type": "player_downed", "who": "brother", "cm": None, "pct": None, "n": None}
    assert rule["do"] == {"action": "QuickReboot", "who": "brother"}


def test_mock_register_ammo_and_repeat(client):
    rule = rule_of(post(client, "총알 5발 아래로 떨어지면 매번 재장전해 줘"))
    assert rule["when"][0]["type"] == "ammo_below" and rule["when"][0]["n"] == 5
    assert rule["do"]["action"] == "ReloadSupport" and rule["repeat"] is True


def test_mock_rule_word_but_unknown_condition_refuses(client):
    r = post(client, "보스가 나오면 규칙으로 펄스 쏴 줘")
    assert r["status"] == "rejected" and r["reason"] == "UNSUPPORTED_CONDITION"


def test_mock_conditional_without_known_condition_falls_through(client):
    # 규칙 단어 없이 모르는 조건이면 기존 키워드 판별(즉시 실행)
    r = post(client, "공격력 올려주면 좋겠어", battery=50)
    assert r["status"] == "ok" and r["command"]["name"] == "DamageBoost"


def test_mock_cancel_and_list(client):
    r = post(client, "규칙 취소해 줘", active_rules=[RULE_A])
    assert r["status"] == "rule" and r["rule_op"] == "cancel" and r["rule_id"] == 0
    r = post(client, "1번 규칙 지워", active_rules=[RULE_A, RULE_B])
    assert r["rule_id"] == 1
    assert post(client, "규칙 목록 보여 줘", active_rules=[RULE_A])["rule_op"] == "list"


def test_mock_decoy_word_now(client):
    # "미끼"가 미끼 키워드에 추가됨(즉시 실행)
    r = post(client, "미끼 써 줘", battery=50, **ENEMIES)
    assert r["status"] == "ok" and r["command"]["name"] == "Decoy"


def test_old_state_without_rules_still_accepted(client):
    assert post(client, "재장전")["status"] == "ok"


# ---------- POST /validate ----------

def validate(client, action, who=None, reserve=0, **state):
    body = {"action": action, "reserve_battery": reserve, "state": make_state(**state)}
    if who:
        body["who"] = who
    return client.post("/validate", json=body)


def test_validate_ok(client):
    r = validate(client, "Decoy", battery=50, **ENEMIES).json()
    assert r["status"] == "ok" and r["command"] == {"name": "Decoy", "target": "enemies"}
    assert r["battery_after"] == 35 and r["cost"] == 15 and r["dialogue"].startswith("약속대로")


@pytest.mark.parametrize("action,state,reason", [
    ("Decoy", {"battery": 50}, "NO_ENEMY"),
    ("Decoy", {"battery": 10, **ENEMIES}, "LOW_BATTERY"),
    ("Decoy", {"battery": 50, "drone_busy": True, **ENEMIES}, "BUSY"),
    ("HealSupport", {"battery": 50}, "HP_FULL"),
])
def test_validate_rejections(client, action, state, reason):
    r = validate(client, action, **state).json()
    assert r["status"] == "rejected" and r["reason"] == reason and r["command"] is None
    assert r["battery_after"] == state["battery"]


def test_validate_reserve_battery(client):
    # 35 - 30 = 5 < 예비 20 → 보류
    r = validate(client, "HealSupport", reserve=20, battery=35, brother_hp=20).json()
    assert r["status"] == "rejected" and r["reason"] == "RESERVE_BATTERY" and r["battery_after"] == 35
    r = validate(client, "HealSupport", reserve=20, battery=50, brother_hp=20).json()
    assert r["status"] == "ok" and r["battery_after"] == 20


def test_validate_quick_reboot_prefers_who(client):
    r = validate(client, "QuickReboot", who="sister", battery=50, brother_alive=False, sister_alive=False).json()
    assert r["command"]["target"] == "sister"
    # 지정한 사람이 멀쩡하면 쓰러진 쪽
    r = validate(client, "QuickReboot", who="sister", battery=50, brother_alive=False).json()
    assert r["command"]["target"] == "brother"


def test_validate_shield_who(client):
    assert validate(client, "Shield", who="sister", battery=50).json()["command"]["target"] == "sister"


def test_validate_rejects_non_action(client):
    assert validate(client, "Chat", battery=50).status_code == 422
    assert validate(client, "RegisterRule", battery=50).status_code == 422


def test_validate_works_in_real_mode_without_llm(client, settings, monkeypatch):
    async def boom(*a, **k):
        raise AssertionError("LLM must not be called")
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", boom)
    assert validate(client, "ReloadSupport", battery=50).json()["status"] == "ok"


def test_validate_token_and_log(client, settings, monkeypatch):
    monkeypatch.setattr(main, "_settings", replace(settings, api_token="secret"))
    assert validate(client, "ReloadSupport", battery=50).status_code == 401
    monkeypatch.setattr(main, "_settings", settings)
    validate(client, "ReloadSupport", battery=50)
    assert '"kind": "validate"' in settings.log_path.read_text(encoding="utf-8")
