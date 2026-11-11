# 크레딧 (Credits)

이 프로젝트(**CAELUM:UNCHAINED** — 포트폴리오, 비상업)에서 쓰는 외부 폰트·오디오 출처와, 각 언리얼 에셋이 어떤 원본 파일에서 왔는지 기록한다.

## 0. 폰트

| 폰트 | 용도 | 언리얼 에셋 | 라이선스 |
|---|---|---|---|
| **Oxanium** (© 2019 The Oxanium Project Authors, Google Fonts) | 게임 UI: HUD 수치·레이블, 메뉴, 알림, 시작화면 게임명 | `UI/Fonts/F_Oxanium` (면 `FF_Oxanium_Light~Bold`) | SIL Open Font License 1.1 — 원문 `Art/Fonts/Oxanium/OFL.txt` |
| **IBM Plex Mono** (© 2017 IBM Corp., Reserved Font Name "Plex") | 시스템 UI: 해킹 퍼즐·단말기, 시스템 메시지, 제미니 상태·대사·채팅 | `UI/Fonts/F_IBMPlexMono` (면 `FF_IBMPlexMono_Light~Bold`) | SIL Open Font License 1.1 — 원문 `Art/Fonts/IBM_Plex_Mono/OFL.txt` |
| **Droid Sans Fallback** (엔진 기본 `/Engine/EngineFonts/Faces/DroidSansFallback`) | 위 두 폰트의 한글·CJK·기호 서브 폰트 | 엔진 에셋 참조 | Apache License 2.0(엔진 동봉) |

- OFL: 게임에 넣어 배포 가능, 폰트 파일만 따로 판매 금지. 패키징 배포 시 OFL 원문을 같이 둔다.
(효과음 적용 기획 3.2: 받은 즉시 기록 — 파일 ID, 원본 파일명, 출처, 라이선스, 가공 여부)

## 1. 출처 / 라이선스

| 출처 | 내용 | 라이선스 |
|---|---|---|
| **Kenney — UI Audio** (kenney.nl, `kenney_ui-audio`) | 클릭·롤오버·스위치 UI 효과음(OGG) | CC0 1.0 (퍼블릭 도메인, 크레딧 의무 없음 — 감사 표기로 기재) |
| **Kenney — Sci-Fi Sounds** (kenney.nl, `kenney_sci-fi-sounds`) | 레이저·임팩트·엔진·포스필드·컴퓨터 노이즈(OGG) | CC0 1.0 |
| **SoulCity** 에셋팩(프로젝트 `Content/SoulCity/`) | 발소리 `Footstep_Dirt01~08` / `Footstep_Dirt_Cue` | 프로젝트에 포함된 에셋팩 라이선스를 따름. 원본은 옮기지 않고 그대로 참조 |
| **Suno AI** (무료 요금제, Instrumental) | 배경음 `District 5 - Signal`, `District 5 - No Way Out` | Suno 이용 약관(무료 요금제 — 비상업 용도). 제작 과정: `Docs/배경음_제작기록.md` |

- AI 효과음 생성 도구는 쓰지 않았다(효과음 적용 기획 v1.1). Suno는 배경음 전용.
- Kenney 원본 OGG는 언리얼로 가져온 뒤 `Sounds/` 전달 폴더에서 삭제했다. 원본이 다시 필요하면 kenney.nl에서 같은 팩을 받으면 된다.

## 2. 배경음 (BGM)

| 에셋 | 원본 | 출처 |
|---|---|---|
| `Audio/BGM/SW_BGM_Title` | `District 5 - Signal` | Suno AI |
| `Audio/BGM/SW_BGM_Map1` | `District 5 - No Way Out` | Suno AI |

## 3. 효과음 (SFX) — 에셋별 원본

경로는 `Content/Project_CX/Audio/SFX/` 기준. **LY** = 여러 원본을 겹쳐 직접 가공(44.1kHz 모노 16bit WAV로 만든 뒤 가져옴).

### Player (오빠·여동생 공용)

| 용도 | 에셋 | 원본 |
|---|---|---|
| 발소리 | `/Game/SoulCity/Sound/Cue/Footstep_Dirt_Cue` (참조) | SoulCity `Footstep_Dirt01~08` |
| 점프 | 같은 큐(피치 1.2, 음량 0.6) | SoulCity `Footstep_Dirt01~08` |
| 착지 | 같은 큐(피치 0.85, 강 1.0 / 약 0.6) | SoulCity `Footstep_Dirt01~08` |

### Brother (오빠)

| 용도 | 에셋 | 원본 |
|---|---|---|
| 레일건 발사(랜덤 3) | `Brother/SW_Brother_Railgun_Fire_01~03` | **LY**: Kenney Sci-Fi `laserLarge_000`, `laserLarge_001`, `laserLarge_003` + `lowFrequency_explosion_001` + `impactMetal_002` |
| 장전 충전 루프 | `Brother/SW_Brother_Reload_Charge` | Kenney Sci-Fi `spaceEngineSmall_000` |
| 장전 완료 | `Brother/SW_Brother_Reload_Done` | Kenney UI `switch18` |
| 헛방 | `Brother/SW_Brother_DryFire` | Kenney UI `click4` |
| 탄약 부족 | `Brother/SW_Brother_LowAmmo` | Kenney Sci-Fi `laserSmall_004` |
| 탄 벽 충돌(랜덤 3) | `Brother/SW_Brother_Impact_Wall_01~03` | Kenney Sci-Fi `impactMetal_000`, `impactMetal_002`, `impactMetal_004` |
| 조준 고정(LOCK) | `Brother/SW_Brother_AimLock` | Kenney UI `switch12` |

### Sister (해킹)

| 용도 | 에셋 | 원본 |
|---|---|---|
| 해킹 중 루프 | `Sister/SW_Hack_Loop` | Kenney Sci-Fi `computerNoise_002` |
| 해킹 성공 | `Sister/SW_Hack_Granted` | **LY**: Kenney Sci-Fi `forceField_003` + Kenney UI `switch11` ×3(피치를 달리해 상승 화음) |
| 해킹 실패 | `Sister/SW_Hack_Denied` | **LY**: Kenney Sci-Fi `laserRetro_001` ×2(피치 ×0.6) + `lowFrequency_explosion_001` |
| 접속 시작 | `Sister/SW_Hack_Start` | Kenney Sci-Fi `forceField_002` |
| 접속 취소 | `Sister/SW_Hack_Cancel` | Kenney Sci-Fi `laserSmall_000` |
| 화살표 정답 입력 | `Sister/SW_Hack_Input` | Kenney UI `rollover5` |
| 라운드 통과 | `Sister/SW_Hack_RoundClear` | Kenney UI `switch11` |
| 비밀번호 숫자/커서 틱 | `Sister/SW_Hack_DigitTick` | Kenney UI `switch14` |
| 단말기 감지 | `Sister/SW_Hack_Detect` | Kenney UI `rollover1` |

### World

| 용도 | 에셋 | 원본 |
|---|---|---|
| 단말기 대기 웅웅거림 루프 | `World/SW_Terminal_Hum` | Kenney Sci-Fi `spaceEngineSmall_001` |
| 레이저 게이트 꺼짐 | `World/SW_Gate_Off` | Kenney Sci-Fi `laserSmall_001` |
| 출구 문 버튼 | `World/SW_Door_Button` | Kenney UI `switch16` |
| 출구 문 열림 | `World/SW_Door_Open` | Kenney Sci-Fi `doorOpen_001` |

### UI

| 용도 | 에셋 | 원본 |
|---|---|---|
| 포커스 이동 | `UI/SW_UI_Move` | Kenney UI `rollover2` |
| 선택/버튼 클릭 | `UI/SW_UI_Confirm` | Kenney UI `click2` |
| 뒤로/닫기 | `UI/SW_UI_Back` | Kenney UI `switch26` |
| 메뉴 열기 | `UI/SW_UI_Open` | Kenney UI `switch15` |
| 선택 불가 | `UI/SW_UI_Denied` | Kenney UI `switch21` |
| 음량 바 한 칸 | `UI/SW_UI_Slider` | Kenney UI `switch13` |
| 타이틀 네온 지직 | `UI/SW_UI_NeonBuzz` | Kenney Sci-Fi `forceField_000` |
