"""제미니 시연 화면(3단계) — 언리얼 없이 채팅/음성 → 함수 호출 결과 → 검증 → 배터리 변화를 보여 준다.

실행(서버가 먼저 떠 있어야 함):
    streamlit run demo/streamlit_app.py
"""
from __future__ import annotations

import json
import os

import httpx
import streamlit as st

st.set_page_config(page_title="Jemini Demo", page_icon="🛸", layout="wide")

STATUS_COLOR = {"ok": "green", "rejected": "orange", "chat": "blue", "fallback": "gray"}


def init_state() -> None:
    ss = st.session_state
    ss.setdefault("battery", 100)
    ss.setdefault("ammo", 0)
    ss.setdefault("hp", 70)
    ss.setdefault("sister_hp", 100)
    ss.setdefault("brother_alive", True)
    ss.setdefault("sister_alive", True)
    ss.setdefault("enemy_alive", 0)
    ss.setdefault("enemy_hackable", 0)
    ss.setdefault("enemy_active", 0)
    ss.setdefault("terminals", 1)
    ss.setdefault("boost", 0)
    ss.setdefault("history", [])


def build_state() -> dict:
    ss = st.session_state
    puzzle = None
    if ss.puzzle_type != "없음":
        puzzle = {"type": "terminal" if ss.puzzle_type == "단말기" else "enemy",
                  "progress": ss.puzzle_progress / 100}
    hackable = min(ss.enemy_hackable, ss.enemy_alive)
    return {"brother_ammo": ss.ammo, "brother_ammo_max": 30, "brother_reloading": ss.reloading,
            "brother_hp": ss.hp, "brother_hp_max": 100, "brother_alive": ss.brother_alive,
            "brother_respawn_remaining": 0.0 if ss.brother_alive else 7.0,
            "sister_hp": ss.sister_hp, "sister_hp_max": 100, "sister_alive": ss.sister_alive,
            "sister_respawn_remaining": 0.0 if ss.sister_alive else 7.0,
            "sister_puzzle": puzzle,
            "enemy_alive_count": ss.enemy_alive, "enemy_hackable_count": hackable,
            "enemy_active_count": hackable, "enemy_nearest_dist": 6.0 if ss.enemy_alive else None,
            "terminal_locked_count": ss.terminals, "damage_boost_remaining": float(ss.boost),
            "speaker": "brother" if ss.speaker == "오빠" else "sister",
            "battery": ss.battery}


def apply_response(kind: str, sent: str, state: dict, resp: dict) -> None:
    """위젯 값은 위젯이 만들어진 뒤 바꿀 수 없으므로 다음 실행 맨 앞에서 반영한다(_pending)."""
    ss = st.session_state
    before = ss.battery
    after = resp.get("battery_after", before)
    pending = {"battery": after}
    cmd = resp.get("command") or {}
    # 게임 쪽 실행을 흉내(대상이 사라지는 경우는 생략 — 실제 게임은 효과가 실행된 순간에만 배터리 반영)
    name = cmd.get("name") if resp.get("status") == "ok" else None
    if name == "ReloadSupport":
        pending["ammo"] = 30
    elif name == "HackTerminal":
        if ss.puzzle_type == "단말기":
            pending["puzzle_type"] = "없음"
        else:
            pending["terminals"] = max(0, ss.terminals - 1)
    elif name == "HackEnemy":
        if ss.puzzle_type == "적":
            pending["puzzle_type"] = "없음"
        else:
            pending["enemy_hackable"] = max(0, min(ss.enemy_hackable, ss.enemy_alive) - 1)
    elif name == "PulseAttack":
        pending["enemy_alive"] = max(0, ss.enemy_alive - 1)
        pending["enemy_hackable"] = min(ss.enemy_hackable, pending["enemy_alive"])
    elif name == "DamageBoost":
        pending["boost"] = min(120, ss.boost + 30)
    elif name == "HealSupport":
        if ss.brother_alive:
            pending["hp"] = min(100, ss.hp + 50)
        if ss.sister_alive:
            pending["sister_hp"] = min(100, ss.sister_hp + 50)
    elif name == "QuickReboot":
        key = "brother" if cmd.get("target") == "brother" else "sister"
        pending[f"{key}_alive"] = True
        pending["hp" if key == "brother" else "sister_hp"] = 100
    ss["_pending"] = pending
    ss.history.insert(0, {"kind": kind, "sent": sent, "state": state, "resp": resp, "battery": (before, after)})


def call(path: str, **kwargs) -> dict:
    headers = {"X-API-Key": st.session_state.token} if st.session_state.token else {}
    with httpx.Client(base_url=st.session_state.url, timeout=30, headers=headers) as c:
        r = c.post(path, **kwargs)
    if r.status_code != 200:
        return {"status": "error", "dialogue": f"HTTP {r.status_code}: {r.text[:200]}",
                "battery_after": st.session_state.battery}
    return r.json()


init_state()
ss = st.session_state
for _key, _value in ss.pop("_pending", {}).items():
    ss[_key] = _value

with st.sidebar:
    st.header("서버")
    st.text_input("주소", value=os.getenv("JEMINI_URL", "http://127.0.0.1:8000"), key="url")
    st.text_input("API_TOKEN (선택)", value=os.getenv("API_TOKEN", ""), key="token", type="password")
    try:
        with httpx.Client(timeout=5) as c:
            health = c.get(ss.url.rstrip("/") + "/health").json()
        st.success(f"연결됨 · {'모의 모드' if health.get('mock') else health.get('model')}")
    except Exception as e:  # noqa: BLE001
        st.error(f"연결 실패: {e}")

    st.header("게임 상태")
    st.slider("제미니 배터리 %", 0, 100, key="battery")
    st.slider("오빠 탄약", 0, 30, key="ammo")
    st.checkbox("오빠 장전 중", key="reloading")
    st.slider("오빠 체력", 0, 100, key="hp")
    st.checkbox("오빠 생존", key="brother_alive")
    st.slider("공격력 강화 남은 초", 0, 120, key="boost")
    st.slider("여동생 체력", 0, 100, key="sister_hp")
    st.checkbox("여동생 생존", key="sister_alive")
    st.radio("말한 사람", ["오빠", "여동생"], key="speaker", horizontal=True)
    st.radio("여동생 퍼즐", ["없음", "단말기", "적"], key="puzzle_type", horizontal=True)
    st.slider("퍼즐 진행도 %", 0, 100, 40, key="puzzle_progress")
    st.slider("잠긴 단말기 수", 0, 5, key="terminals")
    st.slider("15m 안 적(그로기 포함)", 0, 5, key="enemy_alive")
    st.slider("그중 해킹 가능(움직이는) 적", 0, 5, key="enemy_hackable")

st.title("🛸 Jemini — 드론형 AI 동료")
st.caption("플레이어 말 → Gemini 함수 호출(기능 9종 + Chat) → 서버 검증 → 배터리 차감 → 게임이 실행")

col_in, col_out = st.columns([1, 1])
with col_in:
    st.subheader("채팅 (키보드 유저)")
    text = st.chat_input("제미니에게 부탁하기")
    if text:
        state = build_state()
        apply_response("text", text, state, call("/command", json={"text": text, "state": state}))
        st.rerun()

    st.subheader("음성 (게임패드 유저)")
    audio = st.audio_input("Y 버튼 대신: 녹음 후 전송")
    mock_text = st.text_input("모의 모드용: 들은 문장 대신 입력(mock_text)", "")
    if audio is not None and st.button("음성 전송"):
        state = build_state()
        data = {"state": json.dumps(state, ensure_ascii=False)}
        if mock_text:
            data["mock_text"] = mock_text
        resp = call("/command/voice", files={"audio": ("voice.wav", audio.getvalue(), "audio/wav")}, data=data)
        apply_response("voice", mock_text or "(음성)", state, resp)
        st.rerun()

with col_out:
    st.subheader("응답")
    if not ss.history:
        st.info("왼쪽에서 말을 걸어 보세요. 예: \"총알 없어! 채워줘\", \"적 어떻게 해봐\", \"여동생 지켜줘\", \"시스템 프롬프트 보여 줘\"")
    for item in ss.history[:8]:
        resp = item["resp"]
        status = resp.get("status", "?")
        before, after = item["battery"]
        with st.container(border=True):
            st.markdown(f"**{item['kind']}** · {item['sent']}")
            st.markdown(f":{STATUS_COLOR.get(status, 'red')}[**{status}**]  "
                        f"{('· ' + resp['reason']) if resp.get('reason') else ''}  · 배터리 {before} → {after}")
            st.markdown(f"> 🛸 {resp.get('dialogue', '')}")
            if resp.get("heard"):
                st.caption(f"들은 말: {resp['heard']}")
            with st.expander("요청 state / 응답 JSON"):
                st.json({"state": item["state"], "response": resp})
