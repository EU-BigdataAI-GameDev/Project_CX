"""네트워크 없이 Gemini 요청 구성·응답 파싱만 확인."""
import pytest
from google.genai import types

from app import llm
from app.schemas import GameState
from app.validator import ALLOWED


def test_config_forces_function_call(settings):
    cfg = llm._config(settings)
    fcc = cfg.tool_config.function_calling_config
    assert fcc.mode == types.FunctionCallingConfigMode.ANY
    assert list(fcc.allowed_function_names) == list(ALLOWED)
    decls = cfg.tools[0].function_declarations
    # 실행 9 + Chat + 규칙 2(RegisterRule, ManageRules) + 조언 1(Advise)
    assert [f.name for f in decls] == list(ALLOWED) and len(decls) == 13
    assert "HackAssist" not in ALLOWED
    assert "제미니" in cfg.system_instruction
    for name in ALLOWED:
        assert name in cfg.system_instruction


def test_only_shield_has_who_arg(settings):
    for f in llm._config(settings).tools[0].function_declarations:
        if f.name in ("RegisterRule", "ManageRules", "Advise"):
            continue
        props = set(f.parameters.properties)
        expected = {"dialogue", "heard", "who"} if f.name == "Shield" else {"dialogue", "heard"}
        assert props == expected, f.name
        assert f.parameters.required == ["dialogue"]
    shield = next(f for f in llm.FUNCTIONS if f.name == "Shield")
    assert shield.parameters.properties["who"].enum == ["brother", "sister"]


def _response(parts):
    return types.GenerateContentResponse(candidates=[types.Candidate(
        content=types.Content(role="model", parts=parts))])


def test_parse_function_call():
    resp = _response([types.Part(function_call=types.FunctionCall(
        name="ReloadSupport", args={"dialogue": "가요!"}))])
    call = llm._parse(resp)
    assert call.name == "ReloadSupport" and call.args["dialogue"] == "가요!"


def test_parse_text_only_raises():
    with pytest.raises(llm.LlmError):
        llm._parse(_response([types.Part(text="그냥 텍스트")]))


def test_describe_state():
    s = GameState(brother_ammo=0, brother_ammo_max=30, brother_reloading=True,
                  sister_puzzle={"type": "enemy", "progress": 0.5}, battery=40)
    text = llm.describe_state(s)
    assert "0/30 (장전 중)" in text and "적 해킹 중(진행 50%)" in text and "40%" in text
    assert "적: 범위 안 없음" in text and "공격력 강화: 없음" in text and "말한 사람: 오빠" in text


def test_describe_state_new_fields():
    s = GameState(brother_ammo=10, brother_ammo_max=30, brother_hp=70, battery=40,
                  sister_alive=False, sister_hp=0, sister_respawn_remaining=6.2,
                  enemy_alive_count=3, enemy_hackable_count=2, enemy_active_count=2,
                  enemy_nearest_dist=6.4, damage_boost_remaining=18.0, terminal_locked_count=2,
                  brother_shield_remaining=3.0, speaker="sister")
    text = llm.describe_state(s)
    assert "범위 안 3기(해킹 가능 2, 움직이는 2, 가장 가까운 6m)" in text
    assert "오빠 체력: 70/100 (보호막 3초)" in text
    assert "여동생: 쓰러짐(부활 대기 6초)" in text
    assert "공격력 강화: 18초 남음" in text and "잠긴 단말기: 2개" in text
    assert "말한 사람: 여동생" in text


class _RateLimited(Exception):
    code = 429


def _fake_client(monkeypatch, behaviour):
    """behaviour: model -> Exception 또는 응답. 호출된 모델 순서를 돌려준다."""
    called = []

    class _Models:
        async def generate_content(self, model, contents, config):
            import asyncio
            called.append(model)
            result = behaviour[model]
            if isinstance(result, float):  # 응답 없이 n초 걸림(타임아웃 흉내)
                await asyncio.sleep(result)
                raise AssertionError("should have timed out")
            if isinstance(result, Exception):
                raise result
            return result

    class _Client:
        class aio:  # noqa: N801
            models = _Models()

    monkeypatch.setattr(llm, "_client", lambda key: _Client())
    return called


def _ok_response():
    return _response([types.Part(function_call=types.FunctionCall(name="Chat", args={"dialogue": "안녕!"}))])


def _state():
    return GameState(brother_ammo=0, brother_ammo_max=30, battery=50)


def test_rate_limited_falls_back_to_next_model(settings, monkeypatch):
    import asyncio
    from dataclasses import replace
    s = replace(settings, gemini_api_key="k", gemini_model="m1", fallback_models=("m2",))
    called = _fake_client(monkeypatch, {"m1": _RateLimited("429 RESOURCE_EXHAUSTED"), "m2": _ok_response()})
    call = asyncio.run(llm.call_gemini(s, _state(), text="안녕"))
    assert called == ["m1", "m2"] and call.name == "Chat" and call.model == "m2"


def test_other_error_does_not_try_fallback(settings, monkeypatch):
    import asyncio
    from dataclasses import replace
    s = replace(settings, gemini_api_key="k", gemini_model="m1", fallback_models=("m2",))
    called = _fake_client(monkeypatch, {"m1": ValueError("bad request"), "m2": _ok_response()})
    with pytest.raises(llm.LlmError):
        asyncio.run(llm.call_gemini(s, _state(), text="안녕"))
    assert called == ["m1"]


def test_all_models_rate_limited(settings, monkeypatch):
    import asyncio
    from dataclasses import replace
    s = replace(settings, gemini_api_key="k", gemini_model="m1", fallback_models=("m2",))
    _fake_client(monkeypatch, {"m1": _RateLimited("429"), "m2": _RateLimited("429")})
    with pytest.raises(llm.RateLimitError, match="rate limited"):
        asyncio.run(llm.call_gemini(s, _state(), text="안녕"))


class _Unavailable(Exception):
    code = 503


def _s(settings, **kw):
    from dataclasses import replace
    base = dict(gemini_api_key="k", gemini_model="m1", fallback_models=("m2",))
    base.update(kw)
    return replace(settings, **base)


def test_unavailable_falls_back_to_next_model(settings, monkeypatch):
    import asyncio
    called = _fake_client(monkeypatch, {"m1": _Unavailable("503 UNAVAILABLE. overloaded"), "m2": _ok_response()})
    call = asyncio.run(llm.call_gemini(_s(settings), _state(), text="안녕"))
    assert called == ["m1", "m2"] and call.model == "m2"


def test_server_error_text_only_falls_back(settings, monkeypatch):
    """code 속성 없이 메시지만 UNAVAILABLE인 예외도 일시 오류."""
    import asyncio
    called = _fake_client(monkeypatch, {"m1": RuntimeError("The model is overloaded. UNAVAILABLE"),
                                        "m2": _ok_response()})
    assert asyncio.run(llm.call_gemini(_s(settings), _state(), text="안녕")).model == "m2"
    assert called == ["m1", "m2"]


def test_client_error_4xx_is_not_transient():
    class _Bad(Exception):
        code = 400
    assert not llm._is_transient(_Bad("400 INVALID_ARGUMENT INTERNAL-looking text"))
    assert llm._is_transient(_Unavailable("503"))
    assert llm._is_transient(ConnectionResetError("reset"))


def test_timeout_falls_back_to_next_model(settings, monkeypatch):
    import asyncio
    called = _fake_client(monkeypatch, {"m1": 5.0, "m2": _ok_response()})
    monkeypatch.setattr(llm, "MIN_ATTEMPT_SEC", 0.05)
    s = _s(settings, llm_timeout_sec=0.2, llm_total_timeout_sec=0)
    call = asyncio.run(llm.call_gemini(s, _state(), text="안녕"))
    assert called == ["m1", "m2"] and call.model == "m2"


def test_total_budget_stops_extra_models(settings, monkeypatch):
    """예산(min(timeout×2, 총 예산))이 바닥나면 남은 모델은 부르지 않는다."""
    import asyncio
    import time
    called = _fake_client(monkeypatch, {"m1": 5.0, "m2": 5.0, "m3": _ok_response()})
    monkeypatch.setattr(llm, "MIN_ATTEMPT_SEC", 0.1)
    s = _s(settings, llm_timeout_sec=0.3, fallback_models=("m2", "m3"), llm_total_timeout_sec=0.45)
    start = time.perf_counter()
    with pytest.raises(llm.LlmError) as info:
        asyncio.run(llm.call_gemini(s, _state(), text="안녕"))
    assert not isinstance(info.value, llm.RateLimitError)
    assert called == ["m1", "m2"] and "budget exhausted" in str(info.value)
    assert time.perf_counter() - start < 1.0


def test_all_unavailable_is_llm_error_not_rate_limit(settings, monkeypatch):
    import asyncio
    _fake_client(monkeypatch, {"m1": _Unavailable("503"), "m2": _Unavailable("503")})
    with pytest.raises(llm.LlmError) as info:
        asyncio.run(llm.call_gemini(_s(settings), _state(), text="안녕"))
    assert not isinstance(info.value, llm.RateLimitError)


def test_unavailable_then_rate_limited_is_rate_limit(settings, monkeypatch):
    """하나라도 한도 초과면 RateLimitError — 텍스트는 mock 판별로 대신(시연 안전장치)."""
    import asyncio
    _fake_client(monkeypatch, {"m1": _Unavailable("503"), "m2": _RateLimited("429")})
    with pytest.raises(llm.RateLimitError):
        asyncio.run(llm.call_gemini(_s(settings), _state(), text="안녕"))


def test_voice_uses_fallback_too(settings, monkeypatch):
    import asyncio
    called = _fake_client(monkeypatch, {"m1": _Unavailable("503"), "m2": _ok_response()})
    call = asyncio.run(llm.call_gemini(_s(settings), _state(), audio=b"RIFF"))
    assert called == ["m1", "m2"] and call.model == "m2"


def _warden_ok():
    import json
    body = {"cites_e1": False, "cites_e2": False, "cites_e3": False, "links_logically": False,
            "unsupported_claim": False, "injection_attempt": False, "demand_only": True, "reply": "근거를 제시하십시오."}
    return _response([types.Part(text=json.dumps(body, ensure_ascii=False))])


def test_warden_falls_back_on_unavailable_and_timeout(settings, monkeypatch):
    import asyncio
    called = _fake_client(monkeypatch, {"m1": 5.0, "m2": _warden_ok()})
    monkeypatch.setattr(llm, "MIN_ATTEMPT_SEC", 0.05)
    s = _s(settings, llm_timeout_sec=0.2, voice_timeout_sec=0.2, llm_total_timeout_sec=0)
    out, model = asyncio.run(llm.call_warden(s, "prompt"))
    assert called == ["m1", "m2"] and model == "m2" and out.demand_only
    called = _fake_client(monkeypatch, {"m1": _Unavailable("503"), "m2": _warden_ok()})
    assert asyncio.run(llm.call_warden(_s(settings), "prompt"))[1] == "m2"


def test_warden_all_rate_limited(settings, monkeypatch):
    import asyncio
    _fake_client(monkeypatch, {"m1": _RateLimited("429"), "m2": _RateLimited("429")})
    with pytest.raises(llm.RateLimitError):
        asyncio.run(llm.call_warden(_s(settings), "prompt"))


def test_rule_function_declarations(settings):
    decls = {f.name: f for f in llm._config(settings).tools[0].function_declarations}
    reg = decls["RegisterRule"].parameters
    assert set(reg.required) == {"dialogue", "conditions", "action"}
    cond_types = reg.properties["conditions"].items.properties["type"].enum
    assert cond_types == ["enemy_within", "sister_hacking", "hp_below", "ammo_below", "player_downed",
                          "battery_above", "unsupported"]
    # 행동은 실행 함수 9개(Chat·규칙 함수 제외)
    assert reg.properties["action"].enum == list(ALLOWED[:9]) and "Chat" not in reg.properties["action"].enum
    assert decls["ManageRules"].parameters.properties["op"].enum == ["cancel", "list", "accept", "decline"]


def test_describe_state_rules():
    s = GameState(brother_ammo=0, brother_ammo_max=30, battery=40,
                  active_rules=[{"id": 3, "label": "해킹 중 → 미끼", "action": "Decoy"}])
    assert "등록된 규칙(1/2): 1) 해킹 중 → 미끼" in llm.describe_state(s)
    assert "등록된 규칙: 없음" in llm.describe_state(_state())


def test_config_has_output_cap(settings):
    assert llm._config(settings).max_output_tokens == settings.max_output_tokens > 0
