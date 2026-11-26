// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CXTutorialTypes.h"
#include "CXTutorialSubsystem.generated.h"

class APawn;
class UGameInstance;
struct FKey;

/**
 *  첫 플레이 튜토리얼(코치 마크) — Docs/Guide/튜토리얼_가이드.md.
 *
 *  - 월드마다 하나(인게임 맵에서만 동작 — ALocalCoopGameMode가 있을 때). 한 번에 한 단계만 진행한다.
 *  - 단계 표(문구·시간)는 CXTutorialTypes.cpp, 발동 조건(ShouldTrigger)과 완료 판정(IsCardComplete)은 이 클래스.
 *  - 화면: ACoopHUD::DrawTutorial이 GetView로 자기 칸에 그릴 카드를 받아 Canvas로 그린다(플레이어 칸별, 조작 장치별 키 표기).
 *  - 입력: FCXMenuInputProcessor(Slate 단계 — 월드 일시정지 중에도 동작)가 메뉴가 없을 때 HandleKeyDown을 먼저 부른다.
 *      Info 카드   확인 = Enter / Space / 패드 A (대상 플레이어 각자), 건너뛰기 = Esc / 패드 Start (누가 눌러도 단계 전체)
 *      Action 카드 건너뛰기 = Esc / 패드 Start. 나머지 키는 게임으로 흘려보낸다.
 *      Hint 카드   입력을 가로채지 않는다(Esc = 평소처럼 일시정지 메뉴).
 *  - 일시정지·슬로모션은 "내가 걸었을 때만" 되돌린다. 슬로모션은 풀 때 딜레이션이 내가 건 값 그대로일 때만 원래 값으로 복원한다
 *    (게임 오버 슬로모션 등 다른 시스템이 바꿨으면 손대지 않는다). 둘 다 BlockTimeout이 있어 멈춘 채로 남지 않는다.
 *  - "봤음" 기록·전체 on/off는 UCXGameUserSettings(GameUserSettings.ini). 시작화면 NEW GAME이 기록을 지운다.
 *  - 로컬 협동 전용: 네트워크 클라이언트(NM_Client)에서는 아무것도 하지 않는다(게임 흐름을 막지 않음).
 *  - 콘솔(ALocalCoopGameMode): CXTutorialReset / CXTutorialShow <Id|번호> / CXTutorialEnable 0|1 / CXTutorialState.
 */
UCLASS()
class UCXTutorialSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCXTutorialSubsystem* Get(const UObject* WorldContextObject);
	/** 입력 프리프로세서용: 게임 인스턴스의 현재 월드에서 찾는다. */
	static UCXTutorialSubsystem* GetForGameInstance(const UGameInstance* GameInstance);

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject — 월드 일시정지 중에도 돈다(Info 카드 타임아웃·확인 처리)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

	/** HUD: 이 플레이어(오빠/여동생) 칸에 그릴 내용. 없으면 false. */
	bool GetView(bool bSister, FCXTutorialView& OutView) const;

	/** 메뉴가 없을 때 키 입력. 소비했으면 true(게임·일시정지 메뉴로 넘기지 않음). */
	bool HandleKeyDown(const FKey& Key, bool bFromGamepad, FInputDeviceId DeviceId, bool bRepeat);

	bool IsStepActive() const { return ActiveIndex != INDEX_NONE; }
	/** 튜토리얼이 월드를 멈추거나 느리게 하고 있는 중. */
	bool IsBlocking() const { return bOwnsPause || bOwnsDilation; }

	// --- 콘솔 명령(ALocalCoopGameMode CXTutorial*) ---
	/** "봤음" 기록 전부 지우기(진행 중인 단계는 닫는다). */
	void ResetSeen();
	/** 조건·"봤음"·on/off와 상관없이 그 단계를 바로 띄운다. 시작했으면 true. */
	bool ForceShow(const FString& StepId);
	void SetEnabled(bool bEnabled);
	FString GetDebugState() const;
	/** 이 맵에서만 끈다(설정·"봤음" 기록은 그대로) — 자동 자체 확인(CX*SelfTest)이 일시정지·슬로모션에 걸리지 않게. */
	void SuppressForThisMap(const TCHAR* Why);

private:
	/** 역할 인덱스: 0 = 오빠, 1 = 여동생(UCXGameInstance 슬롯과 같다). */
	static constexpr int32 NumRoles = 2;

	APawn* FindRolePawn(int32 Role) const;
	bool IsRoleGamepad(int32 Role) const;
	/** 살아 있고(부활 대기 아님) 그 장치로 확인 키를 누를 수 있는 플레이어인지. */
	bool CanRoleConfirm(int32 Role) const;
	/** 지금 월드를 멈춰도 되는지(채팅 입력창·음성 녹음 중이면 미룬다). */
	bool CanPauseNow() const;
	/** 근처에 쫓아오는 거미가 없는 조용한 때인지. */
	bool IsCalm() const;
	bool IsSeen(int32 StepIndex) const;

	void TryStartNext();
	bool ShouldTrigger(const FCXTutorialStepDef& Def, AActor*& OutSpotActor) const;
	bool BeginStep(int32 StepIndex, AActor* InSpotActor, bool bInForced);
	void TickActive(float RealDelta);
	bool IsCardComplete(const FCXTutorialStepDef& Def, int32 Role);
	/** 단계 닫기. bMarkSeen이면 "봤음"으로 저장(중단은 false — 다음에 다시 뜬다). */
	void FinishStep(bool bMarkSeen);
	void ReleasePause();
	void ReleaseDilation();
	const FCXTutorialCardDef& GetCard(const FCXTutorialStepDef& Def, int32 Role) const { return Role == 1 ? Def.Sister : Def.Brother; }

	bool bWorldBegun = false;
	/** 이 맵에서 꺼짐(-CXNoTutorial 실행 인자 또는 SuppressForThisMap). */
	bool bSuppressed = false;
	double LastRealTime = 0.0;
	float PollTimer = 0.0f;
	/** 앞 단계가 끝난 뒤(또는 맵 시작 뒤) 흐른 시간 — 메뉴·일시정지 중에는 멈춘다. */
	float IdleClock = 0.0f;
	/** 조용한 상태(IsCalm)가 이어진 시간(실시간 초) — 폴링마다 갱신, 카드가 끝나면 0. 5단계(규칙)를 전투 한가운데 띄우지 않기 위해. */
	float CalmClock = 0.0f;

	int32 ActiveIndex = INDEX_NONE;
	bool bForced = false;
	/** 단계 시작 뒤 흐른 시간(실시간 초) — 메뉴가 떠 있거나 남이 건 일시정지 중에는 멈춘다. */
	float ActiveClock = 0.0f;
	bool bCardActive[NumRoles] = { false, false };
	bool bCardDone[NumRoles] = { false, false };
	/** Info: 이 플레이어의 확인이 있어야 닫힌다. */
	bool bRequired[NumRoles] = { false, false };
	/** Info인데 확인할 수 있는 대상이 없다(패드 미연결 등) → 누가 확인 키를 눌러도 닫힌다. */
	bool bAnyoneCanConfirm = false;
	bool bOwnsPause = false;
	bool bOwnsDilation = false;
	float SavedDilation = 1.0f;
	TWeakObjectPtr<AActor> SpotActor;
	/** Info 카드에서 고른 예시 문장 — 카드가 닫히면 키보드 플레이어 채팅창에 넣는다. */
	FString PendingChatText;
	float ChatPrefillTimer = 0.0f;

	// 완료 판정 기준값(단계 시작 시점)
	double BaseShotTime = 0.0;
	int32 BaseDialogueSeq = 0;
	FVector StartLocation[NumRoles] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bMoved[NumRoles] = { false, false };
	bool bJumped[NumRoles] = { false, false };

	/** 이번 맵에서 JeminiHello(4단계)가 끝났는지와 그 단계 시작 때의 제미니 대사 번호 — 번호가 바뀌면 대답이 온 것. */
	bool bHelloDoneThisMap = false;
	int32 HelloBaseDialogueSeq = 0;
	/** 대답이 온 시점의 IdleClock(아직이면 음수) — 읽을 시간을 준 뒤 규칙 안내(5단계)를 띄운다. */
	float HelloAnswerIdleClock = -1.0f;
};
