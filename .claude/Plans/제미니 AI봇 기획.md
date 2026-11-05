# 제미니 AI봇 기획

드론형 AI 동료 '제미니'. 플레이어가 채팅(키보드) 또는 음성(게임패드)으로 부탁하면 Gemini가 명령을 고르고, FastAPI 서버가 검증한 뒤, UE가 드론을 움직여 재장전 지원과 해킹 대행을 실행한다.

- 서버 상세 명세: `Docs/Jemini/02_요구사항_명세서.md` (API, 검증 규칙, 시스템 지시문)
- 목적·일정: `Docs/Jemini/01_프로젝트_개요.md` (제출 2026-12-02, 발표 2026-12-09)

## 확정된 결정

| 항목 | 결정 |
|---|---|
| 탄약 | 30발 유지. **자동 재장전 없음**, 제미니 `ReloadSupport`로만 재장전 |
| 배터리 비용 | 재장전 5 / 해킹 대행 단말기 20, 적 10 / Chat 0(검사 제외) |
| 배터리 충전 | 세이브 복도에서 처리 |
| 배터리 0 | 비상 장전 없음(의도된 페널티). 적은 처치하지 않아도 지나갈 수 있다(오빠가 어그로를 끄는 동안 여동생이 문을 연다). 막히면 RETRY |
| 해킹 대행 | 단말기·적 해킹 모두 가능 |
| 퍼즐 상태 | 퍼즐 중이 아니면 UE가 `sister_puzzle: null` |
| 동시 요청 | 요청 처리 중엔 두 플레이어 모두 새 입력 막기 |
| 키보드 입력 | Enter → 채팅 입력 → Enter(Esc 취소) |
| 패드 입력 | Y 버튼을 누르는 동안 녹음(푸시투토크) → WAV를 서버로 → Gemini가 직접 이해 |
| 함수 호출 | 강제 모드(ANY), 함수 3개(`ReloadSupport`, `HackAssist`, `Chat`), `target` 인자 없음, `heard` 인자로 인식 문장 반환 |
| 드론 모델 | 사용자가 Blender/Hunyuan으로 제작. 그 전까지 임시 메시 |

## 1단계 A — 서버 MVP (Python, 단독 실행)

UE 없이 돌아가야 과제 제출 조건(정상 실행 가능한 Python 소스)을 만족한다. 먼저 끝낸다.

1. `jemini-server/` 골격: `main.py`, `config.py`, `schemas.py`, `llm.py`, `validator.py`, `logger.py`, `mock.py`, `prompts/system_instruction.txt`, `.env.example`, `requirements.txt`, `README.md`
2. `GET /health`, `POST /command`(텍스트)
3. `POST /command/voice`(multipart: `audio` WAV + `state` JSON, 최대 8초)
4. 검증기: 비용표, `ALREADY_RELOADING` / `AMMO_FULL` / `NO_PUZZLE` / `LOW_BATTERY`, Chat 검사 제외
5. MOCK_MODE: 키워드 규칙. 음성은 `mock_text` 필드로 대체
6. fallback: 예외, 타임아웃, 함수 호출 없음 → 대체 대사, 배터리 차감 없음, HTTP 200
7. JSON Lines 로그(오디오는 저장하지 않고 길이와 `heard`만)
8. pytest: 명세서 10절 전부

**완료 기준**: `uvicorn`으로 실행, 모의 모드 테스트 전부 통과, 사용자 노트북에서 실제 Gemini로 텍스트·음성 각각 재장전/해킹/잡담/지시문 노출 요구 확인.

## 1단계 B — UE 연동 (C++)

### B1. 통신
- `UCXJeminiSubsystem`(GameInstance 서브시스템): 서버 주소·토큰은 ini(`DefaultGame.ini` `[Jemini]`).
- `FHttpModule` 비동기 호출. Build.cs에 `HTTP`, `Json`, `JsonUtilities` 추가.
- UE 요청 타임아웃은 서버보다 길게(임시 12초, 허깅페이스 슬립 대비).
- `bRequestPending` 동안 채팅·음성 입력 막고 HUD에 "제미니 응답 중…".
- 응답 `command.name` → C++ 함수 매핑 표, 모르는 이름은 무시.
- 네트워크: 요청은 서버(호스트)가 보낸다. 배터리와 명령 실행은 서버 권위, 결과는 복제.

### B2. 게임 상태와 배터리
- 배터리(0~100)는 GameState에 복제 변수로 둔다. 시작 100.
- `battery_after`는 `status == ok`일 때만 반영한다.
- 세이브 복도 세이브포인트 사용 시 100으로 충전. `CX_Slot0` 세이브에 배터리 포함.
- 상태 수집: 오빠 탄약/최대/재장전 중/HP, 여동생 퍼즐(`type` terminal|enemy, `progress`) 또는 null, 배터리.

### B3. 오빠 재장전 변경
- `BrotherCombatComponent`: 탄약 0일 때 `StartReload()` 자동 호출 제거.
- 탄약 0이면 사격 불가 + HUD "제미니에게 재장전 요청" 안내.
- `ReloadSupport` 수신 → 드론이 오빠에게 이동(약 2초) → `StartReload()`(2초).

### B4. 해킹 대행
- `HackAssist` 수신 → 드론이 여동생에게 이동 → 대행 시간 후 성공 처리.
- 단말기: 기존 `CXSolveHack`과 같은 경로(`CancelHack` → `MarkHacked`). 대행 시간 임시 6초.
- 적: 화살표 해킹 성공과 같은 그로기 처리. 대행 시간 임시 3초.
- 대행 중 여동생이 직접 먼저 풀면 대행은 취소한다(배터리는 이미 차감됨).

### B5. 입력
- 키보드(1P 기본): Enter → C++ Slate 채팅 입력창 → Enter 전송, Esc 취소. 입력창이 열린 동안 그 플레이어의 게임 입력은 멈추지만 게임은 계속 진행된다.
- 게임패드(2P 기본): **Y 버튼**(`Gamepad_FaceButton_Top`, 기존 C++·입력 에셋 어디에도 바인딩 없음)을 누르는 동안 녹음, 떼면 전송.
- 조작 장치를 설정에서 바꾸면 입력 방식도 장치를 따라간다(키보드 = 채팅, 패드 = 음성).
- 녹음: `AudioCapture` 플러그인(`Audio::FAudioCapture`)으로 마이크 버퍼를 받아 16kHz 모노 16bit WAV로 인코딩. 최대 8초에서 자동 전송.

### B6. 드론과 연출
- `ACXJeminiDrone`: 기본은 플레이어 근처를 떠다니며 따라다님, 명령 시 대상에게 이동.
- 메시는 임시(구체 등) → 사용자 모델 도착 시 교체.
- HUD: 배터리 게이지, 제미니 대사 말풍선(수 초 표시), 음성 "인식: …", 녹음 중 표시.

**완료 기준**: 게임에서 채팅과 음성으로 각각 재장전·단말기 대행·적 대행이 실제로 실행되고, 배터리가 비용대로 줄고, 복도에서 충전된다.

## 2단계 — 허깅페이스 Space 배포

- Docker Space, `app_port` 7860, 키는 Secrets, `API_TOKEN` 사용.
- UE ini의 서버 주소만 바꿔 같은 동작 확인. 발표 직전 Space 깨워 두기.

## 3단계 (여유 시) — Streamlit 시연 화면

- UE 없이 채팅/음성 업로드 → 함수 호출 JSON → 검증 결과 → 배터리 변화 표시.

## 미결 사항

- 한 PC에 마이크가 하나라는 전제(2P만 음성 사용).
- 수치(대행 시간, 대사 표시 시간, 타임아웃)는 임시값이며 플레이 테스트로 조정.

## 구현 결과 (as-built)

(구현 후 작성)
