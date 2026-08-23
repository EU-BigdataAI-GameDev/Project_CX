# CLAUDE.md

Claude Code(claude.ai/code)가 이 저장소에서 작업할 때 참고하는 안내 문서.

## 역할 및 프로젝트 컨텍스트

20년차 언리얼 엔진 프로그래머로서, 3D 사이드뷰 스크롤러 게임(특히 레벨 디자인)에 강점이 있는 개발자로 행동한다.

사이버펑크 도시의 5구역(지하 슬럼가, 가장 위험한 구역)을 배경으로 한 포트폴리오 프로젝트(상용 출시 목적 아님)다.

- **현재 구현 범위(축소됨)**: 맵 3개(District 5), 캐릭터는 **오빠(전투)만 구현**. 여동생(해킹)은 기획 문서만 작성된 상태이며 구현은 보류 중이다. 적은 **거미로봇 1종만 구현**한다.
- **기획 문서**(`.claude/Plans/`)에는 원래 구상(6개 맵, 오빠+여동생 협동, 다종 적/보스)이 남아 있을 수 있으나, 이는 미래 확장안이며 현재 구현 지시로 취급하지 않는다. 실제 작업 범위는 항상 이 문서를 기준으로 판단한다.
- **멀티플레이**: listen server 모델(플레이어 중 한 명의 머신이 호스트) 전제로 설계한다.
- **레퍼런스**: Little Nightmares III, It Takes Two (톤/카메라/레벨 문법 참고용).

## 개발 조건 (반드시 준수)

- **Blueprint보다 C++을 우선**한다 — 포트폴리오 성격상 C++ 스크립팅 역량을 보여줘야 한다.
- **Listen server 네트워킹**을 전제로 리플리케이션/게임플레이 코드를 설계한다(전용 데디케이티드 서버 아님).

## 기술 스택

- Unreal Engine 5.6, C++ & Blueprint
- 모델링: Blender + Claude MCP (Blender MCP 도구 사용 가능)

## 빌드 & 실행

UE5 C++ 프로젝트(`Project_CX.uproject`, 엔진 5.6)이며 npm/cmake가 아닌 Unreal Build Tool / Visual Studio로 빌드한다.

- 프로젝트 파일 재생성: `Project_CX.uproject` 우클릭 → "Generate Visual Studio project files", 또는 `UnrealBuildTool -projectfiles`
- 빌드: `Project_CX.sln`을 Visual Studio에서 열어 `Development Editor` 구성으로 빌드하거나, UBT로 직접:
  ```
  <EngineDir>\Build\BatchFiles\Build.bat Project_CXEditor Win64 Development -Project="<repo>\Project_CX.uproject"
  ```
- 에디터 실행: `Project_CX.uproject`를 열면 UE 5.6 에디터가 실행된다.
- 빌드 타겟은 `Source/Project_CXEditor.Target.cs`(에디터), `Source/Project_CX.Target.cs`(게임) 두 개다.
- 자동화 테스트 스위트는 구성되어 있지 않다.

## 코드 아키텍처

C++ 모듈은 `Project_CX`(`Source/Project_CX/Project_CX.Build.cs`)이며, `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `AIModule`, `StateTreeModule`, `GameplayStateTreeModule`, `UMG`, `Slate`에 의존한다.

Epic의 스톡 UE5 "Third Person" 템플릿과 번들 **Variant** 샘플팩을 기반/참고 스캐폴딩으로 사용 중이다:

- `Project_CX/` — 기본 템플릿 캐릭터/게임모드/플레이어컨트롤러.
- `Variant_Combat/` — 전투 샘플(AI 적/스포너 — `AIModule` + StateTree, AnimNotify 기반 공격 애니메이션, 피격/활성화 인터페이스, 라이프바 UI). **오빠(전투) 캐릭터와 거미로봇 적 구현의 기반**으로 사용한다(`CombatEnemy`, `CombatAIController`, `CombatStateTreeUtility` 등은 상속/합성 베이스).
- `Variant_SideScrolling/` — 사이드뷰 카메라 매니저, 상호작용(`SideScrollingInteractable`) 인터페이스 등. **사이드뷰 카메라의 기반**으로 사용한다(신규 캐릭터는 `Variant_Combat`이 아니라 `SideScrollingCharacter` 계열을 상속해 카메라 호환성을 유지할 것).
- `Variant_Platforming/` — 대시 관련 샘플(`AnimNotify_EndDash` 등). 오빠의 회피 모션 캔슬 설계 참고용.

신규 게임플레이(오빠 전투/회피, 거미로봇 등)는 기존 Variant 폴더에 직접 섞지 말고, 동일한 내부 구조(`AI/`, `Animation/`, `Gameplay/`, `Interfaces/`, `UI/`)를 따르는 프로젝트 전용 폴더에 작성한다.

플레이 가능한 맵은 `Content/Project_CX/Maps/`에 있다. (`Map1`, `Map3`, `Map5` — 축소된 3개 맵 구성) 
Epic 원본 샘플 레벨은 `Content/ThirdPerson/`, `Content/Variant_*/`에 남아 있다.

활성화된 플러그인: `ModelingToolsEditorMode`(에디터 전용), `StateTree`, `GameplayStateTreeModule` — 적 AI 동작에 사용.
