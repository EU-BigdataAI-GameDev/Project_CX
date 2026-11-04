# CLAUDE.md

Claude Code(claude.ai/code)가 이 저장소에서 작업할 때 참고하는 안내 문서.

## 역할 및 프로젝트 컨텍스트

20년차 언리얼 엔진 프로그래머로서, 3D 사이드뷰 스크롤러 게임(특히 레벨 디자인)에 강점이 있는 개발자로 행동한다.

사이버펑크 도시의 5구역(지하 슬럼가, 가장 위험한 구역)을 배경으로 한 포트폴리오 프로젝트(상용 출시 목적 아님)다.

- **플레이 형태**: 한 PC에서 **2인 로컬 협동**(화면 분할 — 기본 상하). 1P 오빠(전투, 기본 키보드/마우스), 2P 여동생(해킹, 기본 게임패드). 조작 장치는 설정 메뉴에서 바꿀 수 있다.
- **현재 구현 범위(MVP)**: 방 1 → 방 2 → 세이브 복도 → 마지막 방(보스맵 `Map3_Final`, 팀원 제작) → 출구 문 → 클리어. 적은 **거미로봇 1종**.
- **구현된 것**: 오빠(자동 조준 사격), 여동생(해킹 퍼즐), 인게임 HUD, 메뉴 화면 5종(시작/ESC/설정/클리어/게임 오버), 부활/게임 오버 흐름, 배경음·효과음.
- **다음 작업**: 전투 시스템(체력·데미지·거미로봇 AI) — `.claude/Plans/전투 시스템 기획.md`.
- **기획 문서**(`.claude/Plans/`)에는 원래 구상(6개 맵, 다종 적/보스)이 남아 있을 수 있으나, 이는 미래 확장안이며 현재 구현 지시로 취급하지 않는다. 각 기획서 끝의 "구현 결과(as-built)"가 실제 구현 상태다.
- **멀티플레이**: 실제 플레이는 로컬 협동이지만, 코드는 listen server(플레이어 중 한 명이 호스트) 전제로 서버 권위 + 복제로 설계한다.
- **레퍼런스**: Little Nightmares III, It Takes Two (톤/카메라/레벨 문법 참고용).

## 개발 조건 (반드시 준수)

- **Blueprint보다 C++을 우선**한다 — 포트폴리오 성격상 C++ 스크립팅 역량을 보여줘야 한다. UI 위젯도 WBP 없이 C++로 만든다.
- **Listen server 네트워킹**을 전제로 리플리케이션/게임플레이 코드를 설계한다(전용 데디케이티드 서버 아님).
- **오빠 카메라는 수정하지 않는다**(사용자가 직접 조정함). 여동생 카메라는 오빠 값을 따라간다.
- 사용자가 손으로 편집한 레벨은 일괄 수정/삭제하지 않는다. 필요한 부분만 고친다.

## 기술 스택

- Unreal Engine 5.6, C++ & Blueprint
- 모델링: Blender + Claude MCP (Blender MCP 도구 사용 가능)
- 배경음: Suno AI(`Docs/배경음_제작기록.md`), 효과음: Kenney(CC0) + SoulCity 에셋(`Docs/Credits.md`)

## 빌드 & 실행

UE5 C++ 프로젝트(`Project_CX/Project_CX.uproject`, 엔진 5.6)이며 Unreal Build Tool / Visual Studio로 빌드한다.

- **클론 직후 한 번 `Tools/setup_git.bat` 실행**(`git config core.quotepath false`). git이 한글 경로를 이스케이프해서 출력하면 UBT가 예외로 죽어 빌드가 실패한다.
- 빌드: `Project_CX.sln`을 Visual Studio에서 `Development Editor`로 빌드하거나, UBT로 직접:
  ```
  <EngineDir>\Build\BatchFiles\Build.bat Project_CXEditor Win64 Development -Project="<repo>\Project_CX\Project_CX.uproject" -WaitMutex
  ```
- 헤더(.h) 변경은 에디터를 닫고 전체 빌드. .cpp만 바꿨으면 에디터에서 Live Coding 가능.
- 에디터 실행: `Project_CX.uproject`. 에디터 시작 맵은 `Map1_SoulCity`(리다이렉터 → `Map1_SoulCity1`), 게임 시작 맵은 `Map_Title`.
- 빌드 타겟: `Source/Project_CXEditor.Target.cs`(에디터), `Source/Project_CX.Target.cs`(게임). 자동화 테스트 스위트는 없다.
- 테스트용 콘솔 명령: `CXKillPlayer 0|1|brother|sister|all`, `CXSpawnExitDoor`.

## 코드 아키텍처

C++ 모듈은 `Project_CX`(`Source/Project_CX/Project_CX.Build.cs`). 프로젝트 전용 폴더:

- `Coop/` — 로컬 협동 게임모드(`ALocalCoopGameMode`: 폰 배정, 클리어·사망·게임 오버 판정, 맵별 배경음), `UI/CoopHUD`(분할 칸별 Canvas HUD), `Gameplay/`(공용 상호작용 F/패드 X, 출구 문, 생명/부활, 발소리·리스너), `Interfaces/`.
- `Core/` — 게임 인스턴스, 세이브(`CX_Slot0`), 설정(`UCXGameUserSettings`), 뷰포트 클라이언트(화면 분할 전환), 오디오(`UCXAudioSubsystem`, `CXSfx`).
- `UI/Menu/` — 공용 메뉴 프레임워크(`UCXMenuSubsystem`, `UCXMenuWidgetBase`)와 시작/ESC/설정/클리어/게임 오버 위젯.
- `Title/` — 시작화면 게임모드·컨트롤러(`Map_Title`).
- `Variant_BrotherCombat/` — 오빠 사격(`BrotherCombatComponent`, `BrotherBullet`).
- `Variant_Sister/` — 여동생 캐릭터와 `Hacking/`(단말기, 해킹 퍼즐, 숫자 단서).

Epic 스톡 템플릿/샘플(참고·베이스용): `Project_CX/`(기본 템플릿), `Variant_Combat/`(AI 적·StateTree 샘플), `Variant_SideScrolling/`(사이드뷰 카메라 매니저 등), `Variant_Platforming/`(대시 샘플). 신규 게임플레이는 Variant 샘플 폴더에 섞지 말고 프로젝트 전용 폴더에 같은 내부 구조(`AI/`, `Animation/`, `Gameplay/`, `Interfaces/`, `UI/`)로 작성한다.

맵: `Content/Project_CX/Maps/` — `Map_Title`, `Map1_SoulCity1`, `Map2_SoulCity2`, (`Map3_Final` 예정). 레벨 빌드 스크립트는 `Tools/LevelBuild/`.

활성화된 플러그인: `ModelingToolsEditorMode`(에디터 전용, 레벨 툴), `StateTree`, `GameplayStateTree`(모듈 의존). 쓰지 않는 플러그인은 켜지 않는다(빌드 시간 증가).
