# ESC 메뉴(일시정지 메뉴) 기획

- 문서 버전: v1.0 (2026-10-29)
- 원본 요청: `.claude/mds/9. 게임 화면 제작 요청.md`
- 공통 규칙: **`설정 메뉴 기획.md` 0장 "공통 UI 규칙"** (전체 화면 단일 메뉴·누구나 조작, 입력 표, 포커스, 확인창, HUD 톤, 사운드 큐)을 따른다. 이 문서에는 ESC 메뉴 고유 내용만 적는다.
- 범위: 구현 완료(2026-11-01) — 결과와 기획과 다른 점은 **10장 "구현 결과 (as-built)"** 참조.

---

## 1. 목적

플레이 중 두 플레이어 모두를 멈추고 계속하기 / 설정 / 타이틀로 이동을 고르게 한다.

## 2. 진입 / 종료

| 구분 | 조건 |
|---|---|
| 열기 | 플레이 중 누구든 `ESC`(키보드) 또는 `Start`(게임패드) |
| 자동 열기 | 플레이 중 **게임패드 연결이 끊기면** 자동으로 열고 경고 표시(6장) |
| 닫기 | 다시 `ESC`/`Start`, `계속하기` 선택, 최상위에서 `뒤로`(`ESC`/`B`) |
| 열 수 없음 | 게임 오버화면 표시 중, 맵 로딩/전환 중, 페이드 중, 시작화면(자체 메뉴가 있음) |

- 열리면 **월드 전체가 일시정지**된다(로컬 협동이므로 두 플레이어 모두 정지). 부활 대기 카운트다운, 해킹 퍼즐 타이머도 같이 멈춘다.
- 닫으면 0.15초 페이드아웃 후 재개. 재개 직후 0.15초 동안 게임플레이 입력을 무시해 `A`/`Enter`가 점프·사격으로 새지 않게 한다.

## 3. 레이아웃 (1920x1080)

```
┌──────────────────────────────────────────────────────────────────────┐
│  (게임 화면: 상하 분할 그대로 정지, 검정 55% 딤 + 스캔라인)                │
│                                                                      │
│              ┌────────────────────────────────┐                      │
│              │  PAUSED / 일시정지              │                      │
│              │  ───────────────────────────── │                      │
│              │ ▌계속하기                        │                      │
│              │  설정                           │                      │
│              │  타이틀로                        │                      │
│              │  ───────────────────────────── │                      │
│              │  SCRAP STREET · 마지막 저장 03:12 전 │                  │
│              └────────────────────────────────┘                      │
│                                                                      │
│   P1 BROTHER ■ KEYBOARD          P2 SISTER ■ GAMEPAD  CONNECTED      │
│                                         [A] 선택   [B] 뒤로           │
└──────────────────────────────────────────────────────────────────────┘
```

- 중앙 520x400 패널. 분할선 위에 걸쳐 화면 정중앙에 하나만 띄운다.
- 하단 정보줄: 현재 구역명(`ACoopHUD::ZoneTitles` 재사용), 마지막 세이브포인트 저장 후 경과 시간(세이브 시스템 생기면 표시, 없으면 생략). 맨 아래 두 플레이어 장치/연결 상태(청록/마젠타 태그).

## 4. 메뉴 항목

| KR | EN | 동작 | 조건 |
|---|---|---|---|
| 일시정지 (제목) | PAUSED | — | — |
| 계속하기 | RESUME | 메뉴를 닫고 재개 | 항상 |
| 설정 | SETTINGS | 설정 메뉴 열기 — **`설정 메뉴 기획.md` 참조**. ESC에서 열면 `조작 장치`는 **보기만 가능**(시작화면에서는 변경 가능) | 항상 |
| 타이틀로 | QUIT TO TITLE | 확인창 → 맵 종료 → 시작화면 이동. **세이브포인트에서 저장한 내용만 남는다** | 항상 |

- 기본 포커스: `계속하기`. 위/아래 순환.
- 최상위에서 `ESC`/`B` = `계속하기`와 동일(닫기).

### 확인창 — 타이틀로

| | KR | EN |
|---|---|---|
| 제목 | 타이틀로 돌아가시겠습니까? | RETURN TO TITLE? |
| 본문(주황) | 마지막 세이브포인트 이후의 진행은 사라집니다. | PROGRESS SINCE THE LAST SAVE POINT WILL BE LOST. |
| 버튼 | 확인 / **취소**(기본 포커스) | CONFIRM / **CANCEL** |

- 확인 → 화면 검정 페이드(0.4초) → 시작화면 맵 로드. 페이드 중 모든 메뉴 입력 무시.

## 5. 비주얼 / 사운드

- 0장 톤 그대로. 제목 `PAUSED` 옆에 1초 주기로 깜박이는 작은 사각형(녹화 정지 느낌).
- 열기 시 게임 화면이 0.15초간 채도 0으로 빠지며 딤(일시정지가 바로 읽히게).
- BGM은 일시정지 중 **로우패스 + 음량 -6dB**(Sound Mix 추가 1개). 효과음(월드)은 정지.
- 큐: 열기 `UI_Open`, 이동 `UI_Move`, 선택 `UI_Confirm`, 닫기 `UI_Back`.

## 6. 예외 상황

| 상황 | 처리 |
|---|---|
| 두 사람이 동시에 `ESC`/`Start` | 먼저 들어온 입력만 처리(열기). 0.15초 입력 잠금으로 바로 닫히지 않음 |
| 한 명은 열고 다른 한 명이 `계속하기` | 허용(누구나 조작). 포커스 색이 바뀌어 누가 눌렀는지 보임 |
| 게임패드 연결 끊김 | 즉시 일시정지 메뉴 자동 열기 + 상단 경고 "P2 컨트롤러 연결 끊김 — 다시 연결하세요" / `P2 CONTROLLER DISCONNECTED — PLEASE RECONNECT`. 재연결 전에는 `계속하기` 비활성(숨김)하고 경고 유지, 재연결되면 경고가 `RECONNECTED`로 바뀌고 `계속하기` 복귀. `타이틀로`/`설정`은 계속 가능 |
| 해킹 중 메뉴 열기 | 해킹 세션도 일시정지, 닫으면 그대로 이어짐. 퍼즐 오버레이는 딤 아래에 보임 |
| 해킹 중 여동생(키보드)이 `Escape` | 키 충돌 — **확정/적용(2026-11-01)**: 키보드 해킹 취소를 `Backspace`로 바꾸고 `Escape`는 메뉴 전용(`설정 메뉴 기획.md` 4.4). 게임패드는 해킹 중 `B` = 해킹 취소(메뉴가 닫혀 있으므로 겹치지 않음), `Start` = 메뉴 열기 |
| 부활 대기 중 메뉴 열기 | 허용. 카운트다운 정지 |
| 사망 판정 직후 / 게임 오버 연출 중 | 메뉴 열기 불가(입력 무시) |
| 세이브포인트 저장 중 | 저장 완료(보통 1프레임)까지 `타이틀로` 확인 처리를 대기 |

## 7. 필요한 전제 시스템

| 시스템 | 이 문서와의 관계 | 상태 |
|---|---|---|
| 세이브포인트(체크포인트) 저장 | `타이틀로` 시 남는 데이터의 기준, 하단 "마지막 저장" 표시 | 미구현 — 전투 시스템이 갖춰진 뒤 구현 |
| 체력 / 사망 판정(체력 0 또는 낙사) | 사망·게임 오버 중 ESC 비활성 판정 | 미구현 — 전투 시스템 이후 |
| 부활(리스폰) 시스템 | 일시정지 중 부활 카운트다운 정지 | 미구현 — 전투 시스템 이후(`게임 오버화면 기획.md`) |
| 시작화면 맵 | `타이틀로` 이동 대상 | 미구현(`게임 시작화면 기획.md`) |

세이브포인트가 없는 동안은 `타이틀로` 확인창 본문을 "현재 맵의 진행은 저장되지 않습니다" / `PROGRESS ON THIS MAP WILL NOT BE SAVED`로 대체한다.

## 8. 한글 폰트 / 영문 전환

**언리얼에서 한글이 깨져 보이면 메뉴명을 모두 영어로 바꿔도 된다.** 한글을 쓰려면 한글 폰트 에셋(예: Noto Sans KR)을 프로젝트에 추가해야 한다. 영문 라벨은 4장·6장 표에 모두 적어 두었다(`PAUSED`, `RESUME`, `SETTINGS`, `QUIT TO TITLE`, `CONFIRM`, `CANCEL`).

## 9. 구현 메모 (C++ 우선)

- `UCXPauseMenuWidget : UCXMenuWidgetBase`(공통 베이스는 `설정 메뉴 기획.md` 8장). 레이아웃 WBP + `BindWidget`, 로직은 C++.
- 열기/닫기는 `UCXMenuSubsystem`(GameInstanceSubsystem)이 담당: `OpenPauseMenu(APlayerController* Requester)` → `UGameplayStatics::SetGamePaused(this, true)`, 위젯 `AddToViewport`(전체 화면), 두 로컬 플레이어 입력 모드 `FInputModeGameAndUI`/UI 포커스.
- 입력: 두 플레이어 컨트롤러 모두에 `IA_Menu`(ESC / Gamepad_Special_Right) 바인딩, `bTriggerWhenPaused = true`. 게임 오버/전환 상태는 서브시스템의 `bMenuBlocked` 플래그로 차단.
- 컨트롤러 끊김: `IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange()` 구독 → 끊기면 `OpenPauseMenu` + 경고 상태.
- `타이틀로`: 확인 → `UGameplayStatics::OpenLevel(시작화면 맵)`. 세이브는 세이브포인트에서만 쓰므로 여기서는 **아무것도 저장하지 않는다**.
- 일시정지 BGM 처리: `SM_PauseDuck` Sound Mix Push/Pop.

---

## 10. 구현 결과 (as-built, 2026-11-01)

전부 C++(WBP 없음). 시작화면에서 만든 공통 메뉴 기반(`UI/Menu/` — `UCXMenuSubsystem`, `FCXMenuInputProcessor`, `UCXMenuWidgetBase`, `UCXConfirmDialogWidget`, `UCXMenuItemWidget`, `UCXNeonPanelWidget`, `CXMenuStyle`)을 그대로 재사용했다. 라벨은 8장 규칙대로 영문.

### 구현한 것

| 항목 | 내용 |
|---|---|
| 열기/닫기 입력 | `FCXMenuInputProcessor`(Slate 입력 프리프로세서)를 메뉴가 없어도 **항상 등록**해 두고, 메뉴가 없을 때는 `Escape` / `Gamepad_Special_Right`(Start)만 본다 → `UCXMenuSubsystem::HandlePauseKey`. Slate 단계라 월드 일시정지 중에도, 맵의 PlayerController 클래스(현재 BP_GameMode는 기본 `APlayerController`)와 상관없이 두 플레이어 누구의 키보드/패드든 동작한다. 메뉴가 열려 있으면 ESC = 뒤로, Start = 닫기. 시작화면에서는 ESC를 소비하지 않는다(에디터 PIE의 ESC 종료 유지). |
| 일시정지 | `UCXMenuSubsystem::OpenPauseMenu` → `UGameplayStatics::SetGamePaused(true)` + `UCXPauseMenuWidget`을 1P 컨트롤러 소유로 뷰포트 전체(ZOrder 200)에 올림(분할 화면 정중앙 하나). 해킹 세션·월드 타이머도 같이 멈춘다. 닫기: 위젯이 0.15초 페이드아웃 → `FinishClosePauseMenu`(메뉴 내림, 재개, 로컬 컨트롤러 `FlushPressedKeys`, **0.15초 게임플레이 입력 무시** — 프리프로세서가 그동안 키/클릭을 삼킴). |
| 열 수 없음 | 시작화면(`ACXTitleGameMode`), 맵 로딩 중(PreLoadMap~PostLoadMapWithWorld), 타이틀로 페이드 중, 다른 메뉴가 떠 있을 때, `bMenuBlocked`(`SetMenuBlocked` — 게임 오버/사망 시스템이 생기면 켤 훅). 인게임에서 열 수 없는 상태라도 ESC는 소비한다. |
| 위젯 `UCXPauseMenuWidget` | 55% 검정 딤 + 1px/3px 스캔라인(`UCXPauseDimWidget`), 중앙 520x400 네온 패널: `PAUSED` + 1초 주기 깜박이는 주황 사각형 / RESUME(기본 포커스) / SETTINGS / QUIT TO TITLE / 정보줄 `구역명 · DISTRICT 5`(구역명은 `ACoopHUD::GetZoneTitle()` — ZoneTitles 재사용, 협동 HUD가 없으면 맵 이름). 화면 하단 어두운 띠에 `P1 BROTHER KEYBOARD & MOUSE` / `P2 SISTER GAMEPAD CONNECTED|NOT CONNECTED`(청록/마젠타 태그, 0.5초마다 갱신) + `[A / ENTER] SELECT [B / ESC] BACK`. 열기 0.15초 페이드 + 패널 세로 0.96→1.0, 열 때 0.15초 입력 잠금(동시 ESC/Start로 바로 닫히지 않음), 포커스 색 = 연 사람/마지막 입력 플레이어 색. 확인창이 열리면 뒤 패널을 흐리게(글자 겹침 방지). CoopHUD는 숨기지 않고 딤 아래에 보인다(해킹 퍼즐 오버레이 포함). |
| SETTINGS | 설정 메뉴 미구현 → 시작화면과 같은 알림창 `SETTINGS — COMING SOON`. |
| QUIT TO TITLE | 확인창 `RETURN TO TITLE?` / 본문(주황) `PROGRESS ON THIS MAP WILL NOT BE SAVED.`(7장 — 세이브포인트 없음) / CONFIRM·**CANCEL**(기본). 확인 → 0.4초 검정 페이드(입력 전부 무시) → `OpenLevel(/Game/Project_CX/Maps/Map_Title)`. 아무것도 저장하지 않는다. `UCXGameInstance::bTitleIntroPlayed = true`로 두어 시작화면은 항상 0.3초 축약 페이드. |
| 컨트롤러 끊김 | 서브시스템이 `IPlatformInputDeviceMapper::GetOnInputDeviceConnectionChange()` 구독(키보드=기본 장치·시작화면은 제외). 끊긴 패드의 슬롯(시작화면 배정 기준, 없으면 첫 게임패드 슬롯 = P2) → 메뉴 자동 열기 + 상단 주황 펄스 경고 `P2 CONTROLLER DISCONNECTED — PLEASE RECONNECT`, RESUME 숨김, ESC/B/Start로 닫히지 않음(SETTINGS/QUIT TO TITLE은 가능). 패드가 다시 연결되면 그 슬롯에 재배정 → `RECONNECTED`(청록) + RESUME 복귀(포커스 이동). |
| 키 충돌(4.4) | `SisterHackComponent` 키보드 해킹 취소 `Escape` → `BackSpace`(게임패드 B 유지). 해킹 중 ESC는 프리프로세서가 먼저 받아 메뉴를 열고, 해킹은 정지 후 재개 시 그대로 이어진다. |
| BGM 덕킹 훅 | `UCXMenuSubsystem::PauseSoundMix`(`UCLASS(Config=Game)`, `UPROPERTY(Config)`) — 비어 있으면 무시. `SM_PauseDuck` 에셋이 생기면 `DefaultGame.ini`의 `[/Script/Project_CX.CXMenuSubsystem]`에 `PauseSoundMix=/Game/.../SM_PauseDuck.SM_PauseDuck` 지정 → 열 때 Push, 닫기/맵 이동 시 Pop. |
| 테스트용 | `UCXMenuSubsystem::SimulateMenuInput`(메뉴가 없을 때 6=ESC/7=Start → 일시정지 열기 경로), `DebugSimulateGamepadConnection(PlayerIndex, bConnected)`, `GetTopMenuDebugState`(BlueprintCallable). |

파일:
- 신규 `Source/Project_CX/UI/Menu/CXPauseMenuWidget.h/.cpp`(`UCXPauseMenuWidget`, `UCXPauseDimWidget`)
- 수정 `UI/Menu/CXMenuSubsystem.h/.cpp`(일시정지 API·장치 연결·차단 플래그·재개 입력 무시·사운드 믹스 훅, 프리프로세서 상시 등록), `UI/Menu/CXMenuInputProcessor.h/.cpp`(메뉴 없을 때 ESC/Start, 재개 직후 입력 삼킴), `UI/Menu/CXMenuWidgetBase.h/.cpp`(`ConsumesTopLevelBack` — 최상위 ESC/B를 항상 소비할 화면용), `Coop/UI/CoopHUD.h`(`GetZoneTitle()` 게터만), `Variant_Sister/Hacking/SisterHackComponent.cpp`(Backspace)

검증(PIE, Map1_SoulCity): `Docs/Screenshots/PauseMenu/` — 01 일시정지 메뉴, 02 타이틀로 확인창, 03 P2 연결 끊김 경고.
- 실제 키 입력(Windows `keybd_event`로 PIE 창에 전송): ESC → 열림(PIE 종료 안 됨), ESC → 닫힘, ESC → 열림 → Enter(RESUME) → 닫힘·재개.
- 시뮬레이션 입력(`SimulateMenuInput`): 위/아래 순환, SETTINGS 알림창, 확인창 CANCEL 기본·B 취소, 패드 Start 열기/닫기, 포커스 색(패드 입력 = 마젠타).
- 해킹(비밀번호 퍼즐) 중 ESC → 메뉴, 해킹 유지, 재개 후 입력 이어짐. IMC_Hack 매핑에서 Cancel = Gamepad_FaceButton_Right + BackSpace 확인.
- QUIT TO TITLE → Map_Title(로컬 플레이어 1명, 진입 연출 0.3초) → P2 Start + NEW GAME → Map1_SoulCity에서 PlayerController 2개(BP_Brother/BP_Sister). 두 번 반복해도 정상, 새 세션에서 ESC 메뉴 다시 동작.

### 기획과 다른 점

- **입력 경로**: 9장의 `IA_Menu`(Enhanced Input, `bTriggerWhenPaused`) 대신 기존 Slate 입력 프리프로세서로 처리. 맵의 PlayerController가 기본 클래스라 바인딩할 곳이 없고, 프리프로세서는 일시정지·장치 수와 무관하게 동작한다.
- **입력 모드**: 두 컨트롤러 모두 `FInputModeGameAndUI`가 아니라 1P 컨트롤러만 UI 전용(시작화면과 동일). UI 전용 모드가 뷰포트 입력을 막고 월드가 정지돼 있어 2P 쪽으로도 새지 않는다.
- **채도 0 연출(5장)**: 생략. 오빠 카메라/포스트프로세스를 건드리지 않기 위해 딤 + 스캔라인만.
- **하단 "마지막 저장 경과 시간"**: 세이브포인트 시스템이 없어 생략(구역명만).
- **에디터 PIE**: 인게임에서는 ESC를 메뉴가 소비하므로 **ESC로 PIE가 종료되지 않는다**. PIE 종료는 Shift+F1(마우스 해제) 후 툴바 정지 버튼 또는 시작화면에서 ESC. (빌드 게임에는 해당 없음)
- **동시 입력**: 시작화면과 같이 "P1 우선"이 아니라 "먼저 처리된 입력 우선" + 열기/전환 후 0.15초 잠금.

### 보류 / 미검증

- 실제 게임패드를 뽑고 꽂는 테스트는 하지 않았다 — `DebugSimulateGamepadConnection`으로 경고/RESUME 숨김/RECONNECTED 흐름만 확인. 실제 이벤트 경로(`GetOnInputDeviceConnectionChange` → 슬롯 판정)는 미검증.
- 재개 직후 0.15초 입력 무시는 코드로만 확인(패드 A가 여동생 점프로 새지 않는지는 실제 패드로 미검증).
- 해킹 HUD 힌트는 여전히 `[B] ABORT`(게임패드 표기만) — 키보드 Backspace 표기는 넣지 않았다.
- 사운드 큐/BGM 덕킹은 에셋이 없어 무음(슬롯/훅만).
- 부활 카운트다운 정지·게임 오버 중 차단은 해당 시스템이 생기면 `SetGamePaused`/`SetMenuBlocked`로 연결.

