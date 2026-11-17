# UI 폰트·게임명·아이콘 적용

요청: `.claude/mds/14. 폰트, 게임 아이콘, 게임명 적용요청.md` (UI 폰트 가이드 포함).

## 기준

- **Oxanium** = 플레이어에게 전달하는 게임 정보: HUD 태그·체력·탄약·배터리 수치, 배너·알림, 메뉴 제목·버튼, 시작화면 게임명, 조작 안내(`[X] BREACH`, `D-PAD MOVE …`).
- **IBM Plex Mono** = 게임 세계 시스템이 출력하는 데이터: 해킹 퍼즐 패널 전체(비밀번호·주파수·메모리·회로·적 화살표, 오빠 중계 패널), 단말기 마커 거리, `SYSTEM DOWN`, 부활 `REBOOTING IN`, 제미니 상태 줄·`[MOCK]`·`JEMINI BUSY`·대사 말풍선·채팅 입력창, 음성 자막 말풍선, 메뉴 데이터 줄(세이브 요약·구역 정보·장치 이름·재시작 지점).
- 색 체계는 그대로(오빠 청록 / 여동생 마젠타). 한 패널 안에서 제목·큰 상태는 Oxanium, 데이터 줄은 Mono로 섞어 한 시스템처럼 보이게.

## 구현 결과 (as-built)

### 게임명
- 시작화면 `UCXTitleMenuWidget`: `CAELUM` `:` `UNCHAINED` 글자 3개 묶음(Oxanium Bold 84, 자간 100). 콜론만 여동생 마젠타, 나머지 청록 네온 그림자. 점등 깜박임은 줄 전체(`TitleText`가 `UWidget`).
- `DefaultGame.ini` `[GeneralProjectSettings]` `ProjectName` / `ProjectDisplayedTitle` = CAELUM:UNCHAINED → 창 제목, exe 리소스 제품명(`PROJECT_PRODUCT_NAME`).
- 그 밖에 플레이어에게 보이는 "Project_CX" 문구는 없었다. uproject·모듈·`PROJECT_CX_API`·`/Game/Project_CX` 경로·exe 파일 이름(`Project_CX.exe`)은 그대로.

### 아이콘
- `Project_CX/Build/Windows/Application.ico`: `Art/Icon/CAELUM_UNCHAINED_Icon.png`(1175x1172)를 가운데 정사각 자르기 → 16/24/32/48/64/128/256 BMP 7종(Pillow). UBT `Default.rc2`가 이 파일로 리소스 컴파일됨(에디터 빌드 로그 `BUILD_ICON_FILE_NAME`).
- `Project_CX/Project_CX.png`(192x192): 에디터 프로젝트 브라우저 썸네일.

### 폰트 에셋 (`/Game/Project_CX/UI/Fonts/`)
- FontFace: `FF_Oxanium_{Light,Regular,Medium,SemiBold,Bold}`, `FF_IBMPlexMono_{Light,Regular,Medium,SemiBold,Bold}` (원본 `Art/Fonts/`, LazyLoad).
- Font(런타임 캐시 컴포지트): `F_Oxanium`, `F_IBMPlexMono` — 기본 서체 5개 이름 + 서브 폰트 "Hangul / CJK"(한글 자모·호환 자모·음절, CJK 기호·한자·전각, 화살표·도형 ●) + 폴백 서체 = 엔진 `/Engine/EngineFonts/Faces/DroidSansFallback`. 새 폰트 다운로드 없음.
- 컴포지트 폰트 구조체가 Python에 노출되지 않아 `UCXUIFonts::ConfigureCompositeFont`(BlueprintCallable)를 에디터 Python에서 불러 채웠다.
- 쿠킹: `DirectoriesToAlwaysCook` += `/Game/Project_CX/UI/Fonts`(설정 경로로만 참조하므로).

### 코드
- `UI/CXUIFonts.h/.cpp`: `ECXFontRole { Game, System }`, `UCXUIFontSettings`(config `[/Script/Project_CX.CXUIFontSettings] GameFont/SystemFont`), `UCXUIFonts::GetFont / Slate / Canvas`. Canvas는 `LegacyFontSize/Name`으로 크기·굵기를 정하므로 조합별 런타임 사본을 캐시(루트). 에셋을 못 찾으면 엔진 Roboto로 대신.
- `CXMenuStyle::Font` → Oxanium, `CXMenuStyle::MonoFont` 추가.
- `ACoopHUD::GameFont/SysFont(ECXHudFont)`: 예전 `GEngine->GetTiny/Small/Medium/LargeFont()` 자리.
  - (처음) Game 8/10/10/10, System 8/9/10/10 → 아래 "+3pt" 절에서 11/13/13/13, 11/12/13/13으로 올림. 굵기 위계는 그대로(Game Medium/Medium/SemiBold/Bold, System Regular/Medium/Regular/SemiBold).
- 제미니 채팅창(`SCXJeminiChatBox`): Plex Mono 17 / 라벨 SemiBold 16.
- 넘침 수정(고정폭): 메모리·회로 안내 문구 줄임, 오빠 중계 패널 `STEP n/m - CALL OUT NUMBERS`, `TERMINAL x / RANGE y`, `SISTER IS WAITING FOR RELAY`, 해킹 링크 `BYPASS // 대상`, 제미니 상태 `TERMINAL HACK // BREACHING`.

### 확인 (PIE, `Docs/Screenshots/UIFonts/`)
- `01_title_screen` 게임명, `02_hud_split_jemini_korean` 분할 HUD + 한글 대사, `03_hack_memory_panel` 메모리 패턴(여동생 패널 + 오빠 중계), `04_hack_terminal_panel` 비밀번호 + 부활 대기, `05_pause_menu`, `06_enemy_hack_panel` 적 화살표, `07_jemini_chat_korean` 채팅창 한글 입력, `08_game_over`.
- 한글: 대사 말풍선·채팅 입력 모두 정상 렌더.

### 게임 화면 글자 +3pt (2026-11-10 후속 요청)
- HUD(Canvas): 폰트 Game 8/10/10/10 → **11/13/13/13**(Medium/Medium/SemiBold/Bold), System 8/9/10/10 → **11/12/13/13**.
- 레이아웃: `CoopHUDLayout::HudGrow = 1.3`을 칸 배율 `S()`에 곱해 패널·여백·줄 간격·막대를 글자와 같은 비율로 키움. `TextScale = 1.45 / HudGrow` → 화면 글자 = 폰트pt × 1.45 × 칸 배율(예전 8pt Tiny → 11pt, 10pt → 13pt). 그래서 칸 기준 좌표계가 1920x540 → 약 1477x415로 좁아짐.
- 좁아진 칸 대응:
  - 아래 해킹 퍼즐 패널(비밀번호·주파수·메모리·회로)은 왼쪽 아래 해킹 링크 패널(400) 오른쪽으로 밀림, 회로 패널 폭 780 → 800.
  - 적 해킹 패널(위 가운데) 폭 780 → 750, 왼쪽 태그·HP 글자(430)와 오른쪽 배터리 열 사이에 고정.
  - 오빠 중계 패널(메모리·회로): 배터리 열 왼쪽으로 옮기고 높이를 내용(격자 + 글자 높이 `GetTextSize`)으로 계산, 메모리 칸 76 → 62. 결과 문구가 칸 밖이면 머리줄 위 어두운 띠에 표시.
  - `FrameBlockers`(이번 프레임 큰 패널 영역): 제미니 대사 말풍선은 막히면 패널 위/아래로, 오른쪽 세로 패널이 있으면 그 왼쪽 영역 안에서 **단어 줄바꿈(최대 3줄, `WrapText`)**. 말하기 안내(`[ENTER]/[Y]`)는 패널에 덮이면 숨김. 해킹 대행 진행 바는 퍼즐 패널과 겹치면 해킹 링크 패널 위로. 음성 자막 말풍선은 패널에 걸리면 패널 위/아래로 비킴.
  - 제미니 입력 상태 줄 Y 80 → 88(배터리 상태 줄과 겹침 방지), 회로 범례(SOURCE/SINK/PATHS…)는 글자 폭을 재서 배치.
- Slate/UMG(게임 중): 제미니 채팅창 17/16 → 20/19pt, 폭 760 → 900. ESC 메뉴 작은 글자(정보줄·장치·태그·힌트) 14~16 → 17~19pt, 패널 폭 520 → 600. 게임 오버 정보줄·힌트 14 → 17pt. 시작화면·설정 메뉴는 그대로.
- PIE(`Docs/Screenshots/UIFonts/big_*`): `big_01_hud_split_jemini`, `big_02_hack_circuit`(회로 + 오빠 CIRCUIT RELAY + 음성 자막), `big_03_hack_memory`(메모리 + PATTERN RELAY + 2줄 대사), `big_04_hack_passcode_assist`(비밀번호 + 제미니 대행 바 + 부활 대기), `big_05_enemy_hack`, `big_05_enemy_hack_voice`, `big_07_pause_menu`, `big_08_game_over`.
  - 회로는 Map2 `SHUTTER_CTRL-5` 근처로 순간 이동하면 캐릭터가 죽어서(카메라 범위/위험 구역) Map1에 임시 회로 단말기(`DebugSpawnMemoryTerminal` + PuzzleType=CircuitLink, 이름 SHUTTER_CTRL-5)를 띄워 같은 패널을 찍음.

### 남은 것 / 미확인
- 글자 +3pt 이후: 제미니 채팅창(Slate)은 PIE에서 여는 디버그 경로가 없어 캡처 못 함(크기·폭만 변경). 주파수 동조 패널·저장/구역 배너·NEXT AREA OFFLINE은 캡처 못 함(같은 비율 확대라 폭 계산으로만 확인). 월드에 붙는 마커끼리(`[X] HACK` ↔ 파트너 이름, 음성 말풍선 ↔ 파트너 이름)는 여전히 겹칠 수 있음.
- HUD가 칸의 더 많은 부분을 차지함: 회로 패널 높이 300/415(약 72%), 오빠 중계 패널은 칸 높이 거의 전부.
- 월드 TextRender(출구 문 EXIT, 세이브포인트 라벨, 숫자 단서)는 오프라인 폰트가 필요해 엔진 기본 그대로.
- 회로 연결 패널(Map2)은 폭 계산으로만 확인(PIE 캡처 없음).
- (해결됨, +3pt 작업) 오빠 중계 패널 ↔ 제미니 말하기 안내 줄 겹침.
- 패키징 exe에서 아이콘 실제 표시 미확인.
