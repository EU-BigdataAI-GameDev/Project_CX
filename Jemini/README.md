---
title: Jemini LLM Server
emoji: 🛸
colorFrom: indigo
colorTo: pink
sdk: docker
app_port: 7860
pinned: false
---

# Jemini LLM Server

> 위 머리말은 허깅페이스 Space 설정이다(이 폴더를 그대로 Space에 올릴 때 사용).

Project_CX의 드론형 AI 동료 '제미니'의 두뇌. 플레이어의 채팅/음성을 Gemini Function Calling으로 해석해 게임이 실행할 명령(JSON)을 돌려준다. 언리얼 없이 단독 실행·시연 가능.

```
[UE / 시연 클라이언트] --POST /command, /command/voice--> [FastAPI] --> Gemini
                     <-- {status, dialogue, command, battery_after} --
```

## 실행

```bash
cd Jemini
python -m venv .venv
.venv\Scripts\activate            # macOS/Linux: source .venv/bin/activate
pip install -r requirements.txt
copy .env.example .env            # GEMINI_API_KEY 입력. 키가 없으면 모의 모드로 동작
uvicorn app.main:app --host 127.0.0.1 --port 8000
```

- 확인: `python tools/smoke.py` (기능별 판별 문장 세트 전송, 일치 수 출력), 음성은 `python tools/smoke.py --voice my.wav`
- 테스트: `pytest -q` (모의 모드, 네트워크 불필요)
- 모델 확인: `python tools/list_models.py` → `.env`의 `GEMINI_MODEL`에 지정
- API 문서: http://127.0.0.1:8000/docs

## API

| 경로 | 설명 |
|---|---|
| `GET /health` | `{"status":"ok","mock":bool,"model":str}` |
| `POST /command` | JSON `{text, state}` |
| `POST /command/voice` | multipart: `audio`(WAV, 최대 8초), `state`(JSON 문자열), `mock_text`(모의 모드용, 선택) |

응답: `status`(ok / rejected / chat / fallback), `dialogue`, `command`(`name`, `target`), `battery_after`, `reason`, `heard`.

## 규칙 요약

- Gemini는 10개 함수(`ReloadSupport`, `HackTerminal`, `HackEnemy`, `PulseAttack`, `DamageBoost`, `HealSupport`, `Decoy`, `QuickReboot`, `Shield`, `Chat`) 중 하나를 반드시 호출한다(강제 모드). 실행은 언리얼이 한다.
- 서버가 최종 검증: 비용 재장전 5 / 단말기 해킹 20 / 적 해킹 20 / 펄스 50 / 공격력 강화 10 / 회복 30 / 미끼 15 / 재부팅 30 / 보호막 25 / Chat 0. 거절 사유 `LOW_BATTERY`, `AMMO_FULL`, `ALREADY_RELOADING`, `NO_TERMINAL`, `NO_ENEMY`, `ENEMY_NOT_HACKABLE`, `BROTHER_DOWN`, `HP_FULL`, `NO_DOWNED_PLAYER`, `TARGET_DOWN`, `BUSY`. 상세는 `Docs/Jemini/02_요구사항_명세서.md`.
- Gemini 오류·지연은 `fallback`(대체 대사, 배터리 차감 없음, HTTP 200).
- 서버는 상태를 저장하지 않는다. 로그는 `logs/jemini.jsonl`(오디오·키는 기록 안 함).

## 게임과 같이 켜기(자동 실행)

게임을 켜면 서버도 자동으로 켜지고, 게임을 끄면 같이 꺼진다(`UCXJeminiServerSubsystem`). 서버 주소가 이 PC(127.0.0.1)일 때만 동작하고, 이미 켜진 서버가 있으면 그걸 쓴다.

| 실행 방식 | 게임이 띄우는 서버 |
|---|---|
| 에디터 PIE | `Jemini/.venv/Scripts/python.exe run_server.py` |
| 패키징한 게임 | 게임 폴더의 `Jemini/jemini-server.exe` |

패키징한 게임에 서버 넣기:
1. `Jemini\tools\build_exe.bat` → `Jemini/dist/jemini-server/jemini-server.exe`(파이썬 없이 실행되는 폴더형 exe)
2. `Tools\copy_jemini_to_package.bat <Project_CX.exe가 있는 폴더> --with-key`
   - `--with-key`는 `.env`(API 키)까지 복사한다. **내 PC·발표 노트북용만.** 남에게 주는 빌드에는 키를 넣지 말고 허깅페이스 Space 주소를 쓴다(아래 2단계).
3. 서버 로그: `Jemini/logs/jemini.jsonl`(요청), 창 없이 실행될 때 출력은 `Jemini/logs/server.log`.

끄고 싶으면 `DefaultGame.ini` `[/Script/Project_CX.CXJeminiServerSubsystem] bAutoStartServer=False`.

## 언리얼 연동

- 서버 주소: `Project_CX/Config/DefaultGame.ini` `[/Script/Project_CX.CXJeminiDrone]`의 `ServerUrl` (**따옴표 필수** — 따옴표가 없으면 `//` 뒤가 잘린다), `ApiToken`.
- 게임: 키보드 `Enter` → 채팅 → `Enter`, 게임패드 `Y` 누르는 동안 녹음. 콘솔 `CXJemini 총알 채워줘`, `CXJeminiBattery 3`.

## 허깅페이스 Space 배포 (2단계)

1. huggingface.co → New Space → SDK **Docker** → 이름 입력(무료 CPU).
2. Space Settings → **Secrets**: `GEMINI_API_KEY`, `API_TOKEN`(아무 긴 문자열). 필요하면 Variables에 `GEMINI_MODEL`.
3. 이 `Jemini/` 폴더 내용을 Space 저장소에 push(`.env`, `.venv`는 제외 — `.dockerignore`/`.gitignore`가 막음).
   ```bash
   git clone https://huggingface.co/spaces/<계정>/<스페이스>
   # Jemini/ 안의 app/ prompts/ Dockerfile requirements.txt README.md .dockerignore 복사 후
   git add . && git commit -m "deploy" && git push
   ```
4. 빌드가 끝나면 `https://<계정>-<스페이스>.hf.space/health` 확인 → UE ini `ServerUrl`을 그 주소로, `ApiToken`을 같은 값으로.
5. 무료 Space는 쉬면 잠든다 → 발표 직전에 `/health`로 깨워 둘 것.

## 시연 화면 (3단계, Streamlit)

```bash
pip install -r demo/requirements.txt
streamlit run demo/streamlit_app.py      # 서버 주소는 사이드바에서 변경
```
게임 상태(배터리·탄약·퍼즐)를 사이드바에서 바꾸고, 채팅 또는 녹음(음성)으로 요청 → 응답 status, 대사, 배터리 변화, 원본 JSON 확인.

## 구조

```
app/  main.py(API)  llm.py(Gemini)  validator.py(검증)  mock.py  schemas.py  config.py  logger.py
prompts/system_instruction.txt   tests/   tools/   demo/(Streamlit)   Dockerfile
```

상세 명세: `../Docs/Jemini/02_요구사항_명세서.md`
