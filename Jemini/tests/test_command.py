"""명세서 10절 테스트(모의 모드 기준)."""
import json
from dataclasses import replace

import pytest

from app import llm, main
from app.validator import LOW_BATTERY_TAIL, LlmCall
from conftest import make_state, voice

ARROW = {"type": "enemy", "progress": 0.0}
TERMINAL = {"type": "terminal", "progress": 0.4}
ENEMIES = {"enemy_alive_count": 2, "enemy_hackable_count": 2, "enemy_active_count": 2,
           "enemy_nearest_dist": 6.0}


def post(client, text, headers=None, **state):
    return client.post("/command", json={"text": text, "state": make_state(**state)},
                       headers=headers or {})


def _real_mode(client, settings, monkeypatch, fn):
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False))
    monkeypatch.setattr(main, "llm_fn", fn)


def _fake_llm(client, settings, monkeypatch, name, **args):
    """실제 모드 + LLM이 정해진 함수를 고른 것처럼."""
    async def fake(*a, **k):
        return LlmCall(name, {"dialogue": "가요!", **args})
    _real_mode(client, settings, monkeypatch, fake)


def ok(r, name, target, battery_after):
    assert r["status"] == "ok", r
    assert r["command"] == {"name": name, "target": target}
    assert r["battery_after"] == battery_after and r["reason"] is None


def rejected(r, reason, battery):
    assert r["status"] == "rejected", r
    assert r["reason"] == reason and r["command"] is None and r["battery_after"] == battery


# ---------- 1. 기능별 ok + 정확한 차감 ----------

def test_reload_ok(client):
    ok(post(client, "총알 없어! 채워줘", brother_ammo=3, battery=35).json(), "ReloadSupport", "brother", 30)


def test_hack_terminal_on_open_terminal(client):
    ok(post(client, "퍼즐 좀 풀어줘", sister_puzzle=TERMINAL, battery=50).json(), "HackTerminal", "sister", 30)


def test_hack_terminal_next_locked_terminal(client):
    # 여동생이 단말기를 안 열었어도 잠긴 단말기가 있으면 ok
    ok(post(client, "이 단말기 뚫어줘", terminal_locked_count=2, battery=50).json(), "HackTerminal", "sister", 30)


def test_hack_enemy_without_puzzle(client):
    ok(post(client, "적 해킹해줘", battery=50, **ENEMIES).json(), "HackEnemy", "enemy", 30)


def test_hack_enemy_during_arrow_puzzle(client):
    # 화살표 퍼즐 중이면 적 수와 무관하게 통과(그 적을 즉시 성공)
    ok(post(client, "적 해킹해줘", sister_puzzle=ARROW, battery=50).json(), "HackEnemy", "enemy", 30)


def test_pulse_attack_ok_even_if_stunned(client):
    r = post(client, "적 공격해", battery=60, enemy_alive_count=1, enemy_hackable_count=0,
             enemy_active_count=0).json()
    ok(r, "PulseAttack", "enemy", 10)


def test_damage_boost_ok(client):
    ok(post(client, "공격력 올려줘", battery=50).json(), "DamageBoost", "brother", 40)


def test_damage_boost_extend_while_active(client, settings, monkeypatch):
    r = post(client, "공격력 올려줘", battery=50, damage_boost_remaining=12.0).json()
    ok(r, "DamageBoost", "brother", 40)
    # LLM 대사가 비면 연장 대사
    _fake_llm(client, settings, monkeypatch, "DamageBoost", dialogue="")
    r = post(client, "데미지 더", battery=50, damage_boost_remaining=12.0).json()
    assert r["dialogue"] == "30초 더 늘려 드릴게요!"


def test_heal_ok(client):
    ok(post(client, "힐 좀 해줘", battery=50, brother_hp=70, sister_hp=40).json(), "HealSupport", "both", 20)


def test_heal_only_one_hurt(client):
    ok(post(client, "체력 채워줘", battery=50, sister_hp=40).json(), "HealSupport", "both", 20)


def test_decoy_ok(client):
    ok(post(client, "어그로 끌어줘", battery=50, **ENEMIES).json(), "Decoy", "enemies", 35)


@pytest.mark.parametrize("who_down,target", [("brother", "brother"), ("sister", "sister")])
def test_quick_reboot_ok(client, who_down, target):
    r = post(client, "빨리 살려줘", battery=50, **{f"{who_down}_alive": False,
                                                  f"{who_down}_respawn_remaining": 7.0}).json()
    ok(r, "QuickReboot", target, 20)


def test_quick_reboot_downed_speaker_can_ask(client):
    # 쓰러진 사람이 직접 말해도 된다
    r = post(client, "나 살려줘", battery=50, speaker="sister", sister_alive=False).json()
    ok(r, "QuickReboot", "sister", 20)


def test_shield_named_target(client):
    ok(post(client, "여동생 지켜줘", battery=50, speaker="brother").json(), "Shield", "sister", 25)
    ok(post(client, "오빠 보호해줘", battery=50, speaker="sister").json(), "Shield", "brother", 25)


def test_shield_defaults_to_speaker(client):
    ok(post(client, "보호막 쳐줘", battery=50, speaker="sister").json(), "Shield", "sister", 25)
    ok(post(client, "보호막 쳐줘", battery=50, speaker="brother").json(), "Shield", "brother", 25)


def test_shield_bad_who_falls_back_to_speaker(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "Shield", who="enemy")
    ok(post(client, "보호막", battery=50, speaker="sister").json(), "Shield", "sister", 25)


def test_shield_refresh_while_active(client):
    ok(post(client, "보호막 쳐줘", battery=50, brother_shield_remaining=2.0).json(), "Shield", "brother", 25)


# ---------- 2. 거절 사유 ----------

def test_ammo_full(client):
    rejected(post(client, "재장전", brother_ammo=30).json(), "AMMO_FULL", 100)


def test_already_reloading(client):
    rejected(post(client, "재장전", brother_reloading=True).json(), "ALREADY_RELOADING", 100)


def test_no_terminal(client):
    r = post(client, "단말기 뚫어줘", terminal_locked_count=0).json()
    rejected(r, "NO_TERMINAL", 100)
    assert r["dialogue"] == "이 구역 단말기는 전부 열려 있어요!"


def test_no_terminal_even_during_arrow_puzzle(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "HackTerminal")
    rejected(post(client, "퍼즐", sister_puzzle=ARROW, terminal_locked_count=0).json(), "NO_TERMINAL", 100)


def test_hack_enemy_no_enemy(client):
    r = post(client, "적 해킹해줘").json()
    rejected(r, "NO_ENEMY", 100)
    assert r["dialogue"] == "근처에 해킹할 적이 없어요."


def test_hack_enemy_all_stunned_not_upgraded_to_pulse(client):
    r = post(client, "적 해킹해줘", enemy_alive_count=2, enemy_hackable_count=0, enemy_active_count=0).json()
    rejected(r, "ENEMY_NOT_HACKABLE", 100)
    assert r["dialogue"].startswith("이미 기절해 있어요!")


def test_pulse_no_enemy(client):
    r = post(client, "적 처치해줘").json()
    rejected(r, "NO_ENEMY", 100)
    assert r["dialogue"] == "공격할 적이 안 보여요."


def test_damage_boost_brother_down(client):
    rejected(post(client, "공격력 올려줘", brother_alive=False).json(), "BROTHER_DOWN", 100)


def test_heal_hp_full(client):
    rejected(post(client, "힐 해줘").json(), "HP_FULL", 100)


def test_heal_ignores_downed_player(client):
    # 쓰러진 사람은 회복 대상이 아니다 → 살아 있는 사람이 가득이면 HP_FULL
    rejected(post(client, "힐 해줘", sister_alive=False, sister_hp=0).json(), "HP_FULL", 100)


def test_decoy_no_enemy(client):
    r = post(client, "적 좀 유인해줘").json()
    rejected(r, "NO_ENEMY", 100)
    assert r["dialogue"] == "근처에 유인할 적이 없어요."


def test_decoy_all_stunned(client):
    r = post(client, "어그로 끌어줘", enemy_alive_count=2, enemy_hackable_count=0, enemy_active_count=0).json()
    rejected(r, "NO_ENEMY", 100)
    assert r["dialogue"] == "다들 멈춰 있어요! 지금 지나가세요."


def test_quick_reboot_no_downed(client):
    r = post(client, "부활시켜줘").json()
    rejected(r, "NO_DOWNED_PLAYER", 100)
    assert r["dialogue"] == "두 분 다 멀쩡해요!"


def test_shield_target_down(client):
    rejected(post(client, "여동생 지켜줘", sister_alive=False).json(), "TARGET_DOWN", 100)
    # 쓰러진 사람이 자기 보호막을 부탁해도 TARGET_DOWN
    rejected(post(client, "보호막 쳐줘", speaker="brother", brother_alive=False).json(), "TARGET_DOWN", 100)


# 각 기능 LOW_BATTERY(비용-1), 비용과 같으면 ok(0 남음)
LOW_BATTERY_CASES = [
    ("재장전", 5, {}),
    ("단말기 뚫어줘", 20, {"terminal_locked_count": 1}),
    ("적 해킹해줘", 20, ENEMIES),
    ("적 공격해", 50, ENEMIES),
    ("공격력 올려줘", 10, {}),
    ("힐 해줘", 30, {"brother_hp": 50}),
    ("어그로 끌어줘", 15, ENEMIES),
    ("살려줘", 30, {"sister_alive": False}),
    ("보호막 쳐줘", 25, {}),
]


@pytest.mark.parametrize("text,cost,extra", LOW_BATTERY_CASES)
def test_low_battery_each_function(client, text, cost, extra):
    r = post(client, text, battery=cost - 1, **extra).json()
    rejected(r, "LOW_BATTERY", cost - 1)
    r = post(client, text, battery=cost, **extra).json()
    assert r["status"] == "ok" and r["battery_after"] == 0


def test_pulse_low_battery_line(client):
    r = post(client, "펄스 쏴", battery=49, **ENEMIES).json()
    assert r["reason"] == "LOW_BATTERY" and r["dialogue"] == "펄스를 쏠 전력이 부족해요..."


def test_condition_checked_before_battery(client):
    # 조건 거절이 배터리 거절보다 먼저
    rejected(post(client, "재장전", brother_ammo=30, battery=0).json(), "AMMO_FULL", 0)


def test_reject_ignores_llm_dialogue(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "HealSupport", dialogue="치료해 드렸어요!")
    r = post(client, "힐").json()
    assert r["reason"] == "HP_FULL" and r["dialogue"] != "치료해 드렸어요!"


# ---------- 3. BUSY ----------

def test_busy_rejects_actions(client):
    r = post(client, "재장전", drone_busy=True, battery=50).json()
    rejected(r, "BUSY", 50)


def test_busy_still_allows_chat(client):
    r = post(client, "안녕 제미니", drone_busy=True).json()
    assert r["status"] == "chat" and r["reason"] is None


# ---------- 4. 배터리 20 이하 꼬리말 ----------

def test_low_battery_tail_added(client):
    r = post(client, "재장전", battery=25).json()
    assert r["status"] == "ok" and r["battery_after"] == 20
    assert LOW_BATTERY_TAIL in r["dialogue"]


def test_no_tail_above_threshold(client):
    r = post(client, "재장전", battery=26).json()
    assert r["battery_after"] == 21 and LOW_BATTERY_TAIL not in r["dialogue"]


def test_no_tail_if_llm_mentions_battery(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "ReloadSupport", dialogue="재장전! 배터리가 간당간당해요.")
    r = post(client, "재장전", battery=10).json()
    assert r["dialogue"] == "재장전! 배터리가 간당간당해요."


def test_tail_added_even_if_line_says_full_power(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "PulseAttack", dialogue="전력 최대, 날려 버릴게요!")
    r = post(client, "적 공격해", battery=60, **ENEMIES).json()
    assert r["dialogue"] == f"전력 최대, 날려 버릴게요! {LOW_BATTERY_TAIL}"


def test_long_dialogue_tail_on_new_line(client, settings, monkeypatch):
    long_line = "가" * 50
    _fake_llm(client, settings, monkeypatch, "ReloadSupport", dialogue=long_line)
    r = post(client, "재장전", battery=10).json()
    assert r["dialogue"] == f"{long_line}\n{LOW_BATTERY_TAIL}"


# ---------- 5. 모의 모드 판별 ----------

@pytest.mark.parametrize("text,expected,extra", [
    ("공격력 올려줘", "DamageBoost", {}),
    ("적 공격해", "PulseAttack", ENEMIES),
    ("적 어떻게 해봐", "HackEnemy", ENEMIES),
    ("막아줘", "HackEnemy", ENEMIES),
    ("어그로 끌어줘", "Decoy", ENEMIES),
    ("지켜줘", "Shield", {}),
    ("살려줘", "QuickReboot", {"sister_alive": False}),
    ("힐 해줘", "HealSupport", {"brother_hp": 50}),
    ("퍼즐 풀어줘", "HackTerminal", {"sister_puzzle": TERMINAL}),
    ("퍼즐 풀어줘", "HackEnemy", {"sister_puzzle": ARROW}),
    ("해킹해줘", "HackEnemy", ENEMIES),
    ("해킹해줘", "HackTerminal", {"terminal_locked_count": 1}),
])
def test_mock_routing(client, text, expected, extra):
    r = post(client, text, **extra).json()
    assert r["status"] == "ok" and r["command"]["name"] == expected, r


# ---------- 6. 잡담·음성·스키마 ----------

def test_chat_at_zero_battery(client):
    r = post(client, "안녕 제미니", battery=0).json()
    assert r["status"] == "chat" and r["command"] is None and r["battery_after"] == 0
    assert r["reason"] is None


def test_battery_charge_request_is_chat(client):
    assert post(client, "배터리 채워줘").json()["status"] == "chat"


def test_old_state_without_new_fields_still_accepted(client):
    old = {"brother_ammo": 0, "brother_ammo_max": 30, "brother_reloading": False,
           "brother_hp": 100, "sister_puzzle": None, "battery": 100}
    r = client.post("/command", json={"text": "재장전", "state": old}).json()
    assert r["status"] == "ok"


def test_voice_same_as_text(client):
    r = voice(client, mock_text="총알 채워줘", state=make_state(battery=30)).json()
    assert r["status"] == "ok" and r["battery_after"] == 25
    assert r["heard"] == "총알 채워줘"


def test_voice_uses_state_for_routing(client):
    r = voice(client, mock_text="해킹해줘", state=make_state(sister_puzzle=ARROW)).json()
    assert r["command"]["name"] == "HackEnemy"


def test_voice_without_mock_text_is_chat(client):
    assert voice(client).json()["status"] == "chat"


def test_voice_too_long(client):
    assert voice(client, seconds=9.0).status_code == 422


def test_voice_not_wav(client):
    assert voice(client, audio=b"not a wav file at all").status_code == 422


def test_voice_bad_state(client):
    files = {"audio": ("v.wav", b"x", "audio/wav")}
    assert client.post("/command/voice", files=files, data={"state": "{bad"}).status_code == 422


def test_text_schema_error(client):
    assert client.post("/command", json={"text": "hi"}).status_code == 422


def test_bad_speaker_is_422(client):
    assert post(client, "보호막", speaker="drone").status_code == 422


# ---------- 7. 허용되지 않은 함수명 / 이상한 인자 ----------

@pytest.mark.parametrize("name", ["SelfDestruct", "HackAssist"])
def test_unknown_function_not_executed(client, settings, monkeypatch, name):
    _fake_llm(client, settings, monkeypatch, name, target="brother")
    r = post(client, "해킹", sister_puzzle=TERMINAL).json()
    assert r["status"] == "chat" and r["command"] is None and r["battery_after"] == 100


def test_extra_target_arg_ignored(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "ReloadSupport", target="sister")
    # 대상은 서버가 정한다
    assert post(client, "장전").json()["command"]["target"] == "brother"


# ---------- 8. Gemini 예외 → fallback ----------

def test_llm_error_fallback(client, settings, monkeypatch):
    async def boom(*a, **k):
        raise llm.LlmError("timeout")
    _real_mode(client, settings, monkeypatch, boom)
    res = post(client, "장전", battery=50)
    assert res.status_code == 200
    r = res.json()
    assert r["status"] == "fallback" and r["battery_after"] == 50 and r["command"] is None
    assert r["reason"] == "LLM_ERROR"


async def _rate_limited(*a, **k):
    raise llm.RateLimitError("rate limited on m2")


def test_rate_limit_text_uses_mock(client, settings, monkeypatch):
    # 모든 모델 한도 초과 → 텍스트는 mock 키워드 판별로 계속 동작, mock=true
    _real_mode(client, settings, monkeypatch, _rate_limited)
    r = post(client, "총알 없어! 채워줘", brother_ammo=3, battery=35).json()
    ok(r, "ReloadSupport", "brother", 30)
    assert r["mock"] is True


def test_rate_limit_voice_without_text_falls_back(client, settings, monkeypatch):
    # 음성은 mock이 받아 적지 못하므로 fallback
    _real_mode(client, settings, monkeypatch, _rate_limited)
    r = voice(client).json()
    assert r["status"] == "fallback" and r["reason"] == "LLM_ERROR" and r["mock"] is False


def test_rate_limit_mock_disabled(client, settings, monkeypatch):
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False, mock_on_rate_limit=False))
    monkeypatch.setattr(main, "llm_fn", _rate_limited)
    assert post(client, "장전", brother_ammo=3).json()["status"] == "fallback"


def test_real_llm_response_not_mock(client, settings, monkeypatch):
    _fake_llm(client, settings, monkeypatch, "Chat")
    assert post(client, "안녕").json()["mock"] is False


def test_real_mode_without_key_falls_back(client, settings, monkeypatch):
    monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=False, gemini_api_key=""))
    assert post(client, "장전").json()["status"] == "fallback"


# ---------- 9. API_TOKEN ----------

def test_api_token(client, settings, monkeypatch):
    monkeypatch.setattr(main, "_settings", replace(settings, api_token="secret"))
    assert post(client, "안녕").status_code == 401
    assert post(client, "안녕", headers={"X-API-Key": "wrong"}).status_code == 401
    assert post(client, "안녕", headers={"X-API-Key": "secret"}).status_code == 200
    assert voice(client, headers={"X-API-Key": "secret"}).status_code == 200
    assert voice(client).status_code == 401


# ---------- 10. 로그 ----------

def test_log_line_written(client, settings):
    post(client, "장전", brother_ammo=1)
    voice(client, mock_text="해킹")
    lines = settings.log_path.read_text(encoding="utf-8").strip().splitlines()
    assert len(lines) == 2
    first, second = json.loads(lines[0]), json.loads(lines[1])
    assert first["kind"] == "text" and first["status"] == "ok" and "latency_ms" in first
    assert second["kind"] == "voice" and second["audio_sec"] == 1.0
    assert "GEMINI" not in settings.log_path.read_text(encoding="utf-8")


def test_health(client):
    r = client.get("/health").json()
    assert r["status"] == "ok" and r["mock"] is True
