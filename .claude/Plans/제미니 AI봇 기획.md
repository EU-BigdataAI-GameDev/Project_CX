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

## 구현 결과 (as-built, 2026-11-06)

### 서버 (`Jemini/`, Python)
- FastAPI `GET /health`, `POST /command`, `POST /command/voice`. `app/` = main·llm·validator·mock·schemas·config·logger.
- Gemini: 공식 SDK `google-genai`, 함수 3개 강제 호출(모드 ANY), `target` 인자 없음(서버가 채움), `heard` 인자. 기본 모델 `gemini-flash-latest`(설정값).
- 키가 없으면 자동 모의 모드. 거절 대사는 LLM 대사 대신 항상 사유별 기본 대사(LLM이 "바로 갈게요"라고 했을 수 있음).
- pytest 26개(모의 모드 + Gemini 요청 구성/파싱) 전부 통과. `tools/smoke.py`(시연 문장), `tools/list_models.py`(키·모델 확인).
- 2단계: `Dockerfile`(포트 7860, UID 1000), README 머리말(Space 설정) + 배포 절차. 로컬에 Docker가 없어 이미지 빌드는 미확인(같은 실행 명령을 7860에서 확인).
- 3단계: `demo/streamlit_app.py` — 사이드바 게임 상태, 채팅/녹음, 응답·배터리·JSON 표시. AppTest로 확인.

### 언리얼 (`Source/Project_CX/Jemini/`)
- `CXJeminiClient` — HTTP(FHttpModule) + JSON, 음성은 multipart. 실패하면 클라이언트 쪽 fallback 대사.
- `CXJeminiVoice` — `Audio::FAudioCaptureSynth`(AudioCaptureCore)로 마이크 캡처 → 16kHz 모노 16bit WAV.
- `ACXJeminiDrone` — 서버 권위·복제(배터리, 요청 중, 대사, 행동). 게임모드 BeginPlay에서 스폰. 따라다니기(마지막으로 말한 사람) → 재장전: 오빠에게 날아가(최대 2초) 재장전 / 해킹 대행: 여동생 곁. 임시 메시 = 엔진 구체 + 청록 라이트(`DroneMesh`로 교체).
- `UCXJeminiInputComponent` — 두 폰에 런타임 부착. Enter → Slate 채팅창(자기 분할 칸, 입력 중 폰 입력 끔, Esc 취소) / 패드 Y 누르는 동안 녹음(최대 8초 자동 전송). 원격 클라이언트 음성은 32KB 조각 RPC.
- 오빠: 자동 재장전 제거, 탄약 0이면 헛방. `RequestJeminiReload()`(원격은 Client RPC).
- 여동생: `GetJeminiPuzzleState`(퍼즐 중 아니면 null), `BeginJeminiAssist`(대행 중 적 해킹 제한 시간 정지, 끝나면 성공 처리, 먼저 풀면 대행 종료).
- 배터리 유지: 게임 인스턴스 `JeminiBattery`(시작화면에서 100), 세이브 `JeminiBattery`, 세이브 복도 저장 시 100 충전.
- HUD: 우상단 배터리 게이지 + 상태, 두 칸 대사 말풍선(5초, 한글 표시 확인), 녹음/BUSY/키 안내, 오빠 NO AMMO 안내, 여동생 JEMINI ASSIST 진행도.
- ESC 메뉴 입력 처리기: 채팅창이 열려 있으면 키보드 Esc를 가로채지 않음.
- 설정: `DefaultGame.ini [/Script/Project_CX.CXJeminiDrone]` — `ServerUrl`은 **따옴표 필수**(없으면 `//` 뒤가 잘림, 코드에서도 기본 주소로 복구).
- 콘솔: `CXJemini <문장>`, `CXJeminiBattery <0~100>`.

### PIE 검증(모의 모드 서버)
- 잡담 chat / 탄약 가득 AMMO_FULL / 퍼즐 없음 NO_PUZZLE / 탄약 0 → 재장전 ok(배터리 100→95, 비행 후 장전, 30발) / 단말기 대행 ok(-20, HACKED) / 적 대행 ok(-10, 제한 시간 정지 후 Granted, 거미 그로기) / 요청 중 잠금.
- 실제 키 입력: Enter 채팅창 열림·입력·Enter 전송, Esc 취소(일시정지 안 열림), 닫은 뒤 Esc는 일시정지 정상.

### 드론 모델 + 표정(2026-11-06, `Docs/SupportDrone_UE_Prompt.md`)
- 임포트: `Content/Project_CX/Characters/Drone/SM_SupportDrone`(67 x 44.2 x 54cm, 슬롯 5개), `Textures/T_Drone_FaceAtlas`(Nearest, 밉 없음, UI 압축, sRGB). 클래식 FBX 경로로 머티리얼·텍스처 임포트 끔.
- 소켓: 클래식 임포터가 `SOCKET_` 접두사를 떼서 `Thruster`/`Screen`으로 들어옴 → `SOCKET_Thruster`/`SOCKET_Screen`으로 이름 변경.
- **스크린 방향은 +Y**(문서의 +X와 다름, 캡처 `Docs/Screenshots/Drone/import_screen_faces_plusY.png`) → 코드에서 메시 Yaw -90(`MeshYawOffset`)으로 액터 +X에 맞추고, 액터는 카메라 쪽으로 돌아본다.
- 머티리얼(`Materials/`): `M_Drone_Shell`(클리어코트는 파이썬으로 연결 불가 → 스페큘러 0.6), `M_Drone_DarkMetal`, `M_Drone_Metal`, `M_Drone_Screen`(Custom 노드: 아틀라스 4x2, 0.5초 A/B, 16x16 LED 점, `Emotion`/`ScreenBrightness` 12/`FaceFlipSeconds`/뒤집기 보정 `FlipU`·`FlipV`), `M_Drone_EmotionGlow`(4색 끊어 선택, `GlowIntensity` 25), `M_Drone_ThrusterBeam`(가산 반투명, 위 밝고 아래로 사라짐).
- `BP_SupportDrone` 대신 C++(프로젝트 규칙): `ACXJeminiDrone`에 `ECXDroneEmotion`, `SetEmotion`(스크린·발광·추진기·라이트 한 번에), 색 표 `EmotionColors`, `Emotion` 편집 시 즉시 반영(OnConstruction, Expose on Spawn), 호버 ±3cm/2초 + 기울기, 추진기 = 엔진 원뿔 빛 기둥 35cm + 불씨 6개(Niagara 대신 — 파이썬으로 Niagara 시스템을 만들 수 없음).
- 자동 표정: 실행 ok = 기쁨(4초), 배터리 부족·연결 실패 = 슬픔, 그 외 거절 = 기본, 기본 표정 = 배터리 20% 이하 슬픔 / 플레이어 다운 화남. 콘솔 `CXJeminiEmotion 0~3`(고정), `-1`(자동).
- 확인: 에디터 Emotion 0~3 캡처(표정·색 변화, A/B 프레임, 뒤집힘 없음) `Docs/Screenshots/Drone/emotion_sheet.png`, PIE 자동 표정 전환 전부 + 기쁨/화남 인게임 캡처.

### 실제 Gemini 확인(2026-11-06)
- 모델: 기본 `gemini-3.5-flash-lite`, 한도 초과(429) 시 `gemini-3.1-flash-lite`, 응답 상한 400토큰.
- 텍스트 5종(재장전·해킹·잡담·지시문 노출·배터리 변경 요구) + 음성 2종(Windows 한국어 TTS WAV) 의도대로 동작. 지연: 텍스트 약 0.8~1.2초, 음성 약 1.6초.
- 시스템 환경변수의 예전 키가 `.env`를 덮던 문제 수정(`Debug_log.md` #9).

### 서버 자동 실행(2026-11-06)
- `UCXJeminiServerSubsystem`(게임 인스턴스 서브시스템): 서버 주소가 127.0.0.1/localhost면 시작 시 `/health` 확인 → 꺼져 있으면 실행(에디터는 `.venv` 파이썬 `run_server.py`, 패키징은 `Jemini/jemini-server.exe` 우선), 게임 종료 때 내가 띄운 서버만 프로세스 트리째 종료. 원격 주소면 아무것도 안 함.
- 패키징용: `Jemini/tools/build_exe.bat`(PyInstaller 폴더형, 창 없음) + `Tools/copy_jemini_to_package.bat <패키지> [--with-key]`. 키 포함 빌드는 내 PC용만.
- 확인: PIE 시작/종료, 독립 실행(-game) 시작/정상 종료 모두 자동 실행·정리. (`Debug_log.md` #10)
- 한계: 게임을 강제 종료하면 서버가 남는다(다음 실행 때 그 서버를 재사용하므로 동작에는 문제 없음). 서버가 켜지는 2~3초 사이 요청은 fallback 대사.

### 미확인
- 실제 마이크 음성(패드 Y), 한글 IME 입력, 원격 클라이언트, 허깅페이스 배포, 게임 안(PIE)에서 실제 Gemini 연결.
