// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiProactiveComponent.h"
#include "CXJeminiDrone.h"
#include "CXJeminiRuleComponent.h"
#include "CXJeminiInputComponent.h"
#include "CXWardenTerminal.h"
#include "CXTutorialSubsystem.h"
#include "BrotherCombatComponent.h"
#include "SisterHackComponent.h"
#include "HackTerminal.h"
#include "CXHealthComponent.h"
#include "CXPlayerLifeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiProactive, Log, All);

namespace CXJeminiProactive
{
	/** 한 단어 명령 표. Cmd "HackSmart" = 상황에 맞게 HackTerminal / HackEnemy. */
	struct FKeyword
	{
		const TCHAR* Word;
		const TCHAR* Cmd;
		const TCHAR* Target;
		const TCHAR* Ack;
	};

	const FKeyword Keywords[] = {
		{ TEXT("엄호"), TEXT("Decoy"), TEXT("enemies"), TEXT("엄호할게요! 미끼 던져요!") },
		{ TEXT("미끼"), TEXT("Decoy"), TEXT("enemies"), TEXT("미끼 던질게요!") },
		{ TEXT("힐"), TEXT("HealSupport"), TEXT("both"), TEXT("회복해 드릴게요!") },
		{ TEXT("회복"), TEXT("HealSupport"), TEXT("both"), TEXT("회복해 드릴게요!") },
		{ TEXT("치료"), TEXT("HealSupport"), TEXT("both"), TEXT("치료해 드릴게요!") },
		{ TEXT("장전"), TEXT("ReloadSupport"), TEXT("brother"), TEXT("장전해 드릴게요!") },
		{ TEXT("보호막"), TEXT("Shield"), TEXT(""), TEXT("보호막 씌울게요!") },
		{ TEXT("방어"), TEXT("Shield"), TEXT(""), TEXT("보호막 씌울게요!") },
		{ TEXT("실드"), TEXT("Shield"), TEXT(""), TEXT("보호막 씌울게요!") },
		{ TEXT("해킹"), TEXT("HackSmart"), TEXT(""), TEXT("해킹 도와드릴게요!") },
		{ TEXT("펄스"), TEXT("PulseAttack"), TEXT("enemy"), TEXT("펄스 발사!") },
		{ TEXT("전기"), TEXT("PulseAttack"), TEXT("enemy"), TEXT("펄스 발사!") },
		{ TEXT("재부팅"), TEXT("QuickReboot"), TEXT(""), TEXT("재부팅 시작할게요!") },
		{ TEXT("강화"), TEXT("DamageBoost"), TEXT("brother"), TEXT("공격력 올려 드릴게요!") },
		{ TEXT("공격력"), TEXT("DamageBoost"), TEXT("brother"), TEXT("공격력 올려 드릴게요!") },
	};

	/** 이 길이(정리 후 글자 수)를 넘는 문장은 한 단어 명령·짧은 대답으로 보지 않는다. */
	constexpr int32 MaxShortChars = 8;

	const TCHAR* DeclineLine = TEXT("알겠어요. 필요하면 말해 줘요!");
	const TCHAR* GoneLine = TEXT("지금은 다른 일 하는 중이에요, 잠깐만요!");
}

UCXJeminiProactiveComponent::UCXJeminiProactiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void UCXJeminiProactiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCXJeminiProactiveComponent, OfferType);
}

void UCXJeminiProactiveComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.TickInterval = FMath::Max(0.05f, CheckInterval);
	// 서버(리슨 호스트)만 판단한다 — 클라이언트에서는 끈다
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(false);
	}
}

ACXJeminiDrone* UCXJeminiProactiveComponent::GetDrone() const
{
	return Cast<ACXJeminiDrone>(GetOwner());
}

float UCXJeminiProactiveComponent::Now() const
{
	return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
}

// ---------------------------------------------------------------- 표 데이터

const TCHAR* UCXJeminiProactiveComponent::GetOfferLabel(ECXJeminiOfferType Type)
{
	switch (Type)
	{
	case ECXJeminiOfferType::Reload: return TEXT("RELOAD");
	case ECXJeminiOfferType::Heal: return TEXT("HEAL");
	case ECXJeminiOfferType::HackAssist: return TEXT("HACK ASSIST");
	case ECXJeminiOfferType::Decoy: return TEXT("DECOY");
	default: return TEXT("");
	}
}

FString UCXJeminiProactiveComponent::GetOfferLine(ECXJeminiOfferType Type)
{
	switch (Type)
	{
	case ECXJeminiOfferType::Reload: return TEXT("탄이 얼마 안 남았어요. 장전해 드릴까요? [응/아니]");
	case ECXJeminiOfferType::Heal: return TEXT("체력이 많이 닳았어요. 회복해 드릴까요? [응/아니]");
	case ECXJeminiOfferType::HackAssist: return TEXT("퍼즐이 오래 걸리네요. 제가 도와드릴까요? [응/아니]");
	case ECXJeminiOfferType::Decoy: return TEXT("여동생이 해킹 중인데 적이 다가와요. 미끼 던질까요? [응/아니]");
	default: return FString();
	}
}

const TCHAR* UCXJeminiProactiveComponent::GetOfferCommand(ECXJeminiOfferType Type)
{
	switch (Type)
	{
	case ECXJeminiOfferType::Reload: return TEXT("ReloadSupport");
	case ECXJeminiOfferType::Heal: return TEXT("HealSupport");
	case ECXJeminiOfferType::HackAssist: return TEXT("HackTerminal");
	case ECXJeminiOfferType::Decoy: return TEXT("Decoy");
	default: return TEXT("");
	}
}

const TCHAR* UCXJeminiProactiveComponent::GetOfferTypeName(ECXJeminiOfferType Type)
{
	switch (Type)
	{
	case ECXJeminiOfferType::Reload: return TEXT("ammo");
	case ECXJeminiOfferType::Heal: return TEXT("hp");
	case ECXJeminiOfferType::HackAssist: return TEXT("stuck");
	case ECXJeminiOfferType::Decoy: return TEXT("decoy");
	default: return TEXT("none");
	}
}

ECXJeminiOfferType UCXJeminiProactiveComponent::ParseOfferName(const FString& Name)
{
	const FString N = Name.TrimStartAndEnd().ToLower();
	if (N == TEXT("ammo") || N == TEXT("reload"))
	{
		return ECXJeminiOfferType::Reload;
	}
	if (N == TEXT("hp") || N == TEXT("heal"))
	{
		return ECXJeminiOfferType::Heal;
	}
	if (N == TEXT("stuck") || N == TEXT("hack"))
	{
		return ECXJeminiOfferType::HackAssist;
	}
	if (N == TEXT("decoy"))
	{
		return ECXJeminiOfferType::Decoy;
	}
	return ECXJeminiOfferType::None;
}

float UCXJeminiProactiveComponent::GetOfferRemaining() const
{
	return HasOffer() ? FMath::Max(0.0f, static_cast<float>(OfferExpireTime) - Now()) : 0.0f;
}

// ---------------------------------------------------------------- 조건

void UCXJeminiProactiveComponent::GatherInputs(FInputs& Out) const
{
	const ACXJeminiDrone* Drone = GetDrone();
	if (!Drone)
	{
		return;
	}
	Out.Battery = Drone->GetBattery();
	const APawn* Brother = Drone->GetBrotherPawn();
	if (Brother)
	{
		Out.bBrotherAlive = UCXPlayerLifeComponent::IsPawnAlive(Brother);
		if (const UBrotherCombatComponent* Combat = Brother->FindComponentByClass<UBrotherCombatComponent>())
		{
			Out.Ammo = Combat->CurrentAmmo;
			Out.AmmoMax = Combat->GetMaxAmmo();
			Out.bReloading = Combat->bIsReloading;
		}
		if (const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Brother))
		{
			Out.BrotherHp = FMath::RoundToInt(Health->GetHealth());
			Out.BrotherHpMax = FMath::RoundToInt(Health->GetMaxHealth());
		}
		float Dist = 0.0f;
		if (Out.bBrotherAlive && Drone->GetNearestEnemyDistance(Brother->GetActorLocation(), false, Dist))
		{
			Out.BrotherEnemyCm = Dist;
		}
	}
	const APawn* Sister = Drone->GetSisterPawn();
	if (Sister)
	{
		Out.bSisterAlive = UCXPlayerLifeComponent::IsPawnAlive(Sister);
		const USisterHackComponent* Hack = Sister->FindComponentByClass<USisterHackComponent>();
		Out.bSisterHackingTerminal = Out.bSisterAlive && Hack && Hack->GetActiveTerminal() != nullptr;
		float Dist = 0.0f;
		if (Out.bSisterHackingTerminal && Drone->GetNearestEnemyDistance(Sister->GetActorLocation(), true, Dist))
		{
			Out.SisterEnemyCm = Dist;
		}
	}
	Out.StuckSeconds = StuckAccum;
}

bool UCXJeminiProactiveComponent::Meets(ECXJeminiOfferType Type, const FInputs& In) const
{
	switch (Type)
	{
	case ECXJeminiOfferType::Reload:
		return In.bBrotherAlive && !In.bReloading && In.AmmoMax > 0 && In.Ammo <= In.AmmoMax * AmmoFraction
			&& In.BrotherEnemyCm >= 0.0f && In.BrotherEnemyCm <= AmmoEnemyRange;
	case ECXJeminiOfferType::Heal:
		return In.bBrotherAlive && In.BrotherHpMax > 0 && In.BrotherHp > 0 && In.BrotherHp < In.BrotherHpMax * HealHpFraction;
	case ECXJeminiOfferType::HackAssist:
		return In.bSisterAlive && In.bSisterHackingTerminal && In.StuckSeconds >= StuckSeconds;
	case ECXJeminiOfferType::Decoy:
		return In.bSisterAlive && In.bSisterHackingTerminal && In.SisterEnemyCm >= 0.0f && In.SisterEnemyCm <= DecoyEnemyRange;
	default:
		return false;
	}
}

bool UCXJeminiProactiveComponent::CanAfford(ECXJeminiOfferType Type, const FInputs& In) const
{
	return In.Battery >= ACXJeminiDrone::GetCommandCost(GetOfferCommand(Type));
}

float UCXJeminiProactiveComponent::CooldownRemaining(ECXJeminiOfferType Type) const
{
	return FMath::Max(0.0f, TypeCooldown - (Now() - static_cast<float>(LastOfferTime[static_cast<int32>(Type)])));
}

bool UCXJeminiProactiveComponent::IsAnyoneTyping() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		const UCXJeminiInputComponent* Input = Pawn ? Pawn->FindComponentByClass<UCXJeminiInputComponent>() : nullptr;
		if (Input && (Input->IsChatOpen() || Input->IsRecording()))
		{
			return true;
		}
	}
	return false;
}

bool UCXJeminiProactiveComponent::IsNearWarden() const
{
	const ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	const UWorld* World = GetWorld();
	if (!Warden || !World || Warden->IsSolved())
	{
		return false;
	}
	const FVector T = Warden->GetActorLocation();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn))
		{
			// 사이드뷰: 깊이(X) 무시. 코치가 말하는 범위(TalkRange)보다 조금 넉넉히 비켜 준다.
			const FVector P = Pawn->GetActorLocation();
			if (FVector2D(P.Y - T.Y, P.Z - T.Z).Size() <= Warden->GetTalkRange() * 1.3f)
			{
				return true;
			}
		}
	}
	return false;
}

FString UCXJeminiProactiveComponent::GetBlockReason() const
{
	const UWorld* World = GetWorld();
	const ACXJeminiDrone* Drone = GetDrone();
	if (!World || !Drone)
	{
		return TEXT("no drone");
	}
	if (!GetOwner()->HasAuthority())
	{
		return TEXT("not server");
	}
	if (HasOffer())
	{
		return TEXT("offer pending");
	}
	if (const UCXJeminiRuleComponent* Rules = Drone->GetRuleComponent(); Rules && Rules->GetProposal().Num() > 0)
	{
		return TEXT("LLM proposal pending");
	}
	if (!Drone->CanAcceptRequest())
	{
		return TEXT("drone busy / request pending / game state");
	}
	const double T = World->GetTimeSeconds();
	if (T - LastDroneSpeechTime < DroneSpeechHold)
	{
		return FString::Printf(TEXT("drone speaking (%.1fs)"), DroneSpeechHold - (T - LastDroneSpeechTime));
	}
	if (T - LastAnyOfferTime < GlobalCooldown)
	{
		return FString::Printf(TEXT("global cooldown (%.1fs)"), GlobalCooldown - (T - LastAnyOfferTime));
	}
	if (UGameplayStatics::IsGamePaused(this))
	{
		return TEXT("paused");
	}
	if (const UCXTutorialSubsystem* Tutorial = UCXTutorialSubsystem::Get(this); Tutorial && (Tutorial->IsStepActive() || Tutorial->IsBlocking()))
	{
		return TEXT("tutorial");
	}
	if (IsAnyoneTyping())
	{
		return TEXT("player typing/recording");
	}
	if (IsNearWarden())
	{
		return TEXT("near WARDEN (coach's turn)");
	}
	return FString();
}

// ---------------------------------------------------------------- 틱

void UCXJeminiProactiveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACXJeminiDrone* Drone = GetDrone();
	UWorld* World = GetWorld();
	if (!Drone || !World || !GetOwner()->HasAuthority())
	{
		return;
	}
	const double T = World->GetTimeSeconds();

	// 다른 대사가 나온 시점 추적(처음 한 번은 기준만 잡는다)
	if (SeenDroneSeq >= 0 && Drone->GetDialogueSeq() != SeenDroneSeq)
	{
		LastDroneSpeechTime = T;
	}
	SeenDroneSeq = Drone->GetDialogueSeq();

	// 같은 단말기를 계속 해킹한 시간
	AActor* Terminal = nullptr;
	if (const APawn* Sister = Drone->GetSisterPawn(); Sister && UCXPlayerLifeComponent::IsPawnAlive(Sister))
	{
		if (const USisterHackComponent* Hack = Sister->FindComponentByClass<USisterHackComponent>())
		{
			Terminal = Cast<AActor>(Hack->GetActiveTerminal());
		}
	}
	if (Terminal && Terminal == StuckTerminal.Get())
	{
		StuckAccum += DeltaTime;
	}
	else
	{
		StuckTerminal = Terminal;
		StuckAccum = 0.0f;
	}

	FInputs In;
	GatherInputs(In);

	// 대기 중인 제안: 시간 초과 / 드론이 일을 시작함 / 조건이 사라짐(탄을 채움·체력 회복 등)
	if (HasOffer())
	{
		if (T >= OfferExpireTime)
		{
			ClearOffer(TEXT("timeout"));
		}
		else if (Drone->IsBusy())
		{
			ClearOffer(TEXT("drone busy"));
		}
		else if (!bOfferForced && !Meets(OfferType, In))
		{
			ClearOffer(TEXT("condition gone"));
		}
		return;
	}

	if (!GetBlockReason().IsEmpty())
	{
		return;
	}
	// 위험한 것부터: 미끼 → 회복 → 장전 → 퍼즐 도움
	static const ECXJeminiOfferType Order[] = { ECXJeminiOfferType::Decoy, ECXJeminiOfferType::Heal, ECXJeminiOfferType::Reload, ECXJeminiOfferType::HackAssist };
	for (const ECXJeminiOfferType Type : Order)
	{
		if (Meets(Type, In) && CanAfford(Type, In) && CooldownRemaining(Type) <= 0.0f)
		{
			ShowOffer(Type, false);
			return;
		}
	}
}

void UCXJeminiProactiveComponent::ShowOffer(ECXJeminiOfferType Type, bool bForced)
{
	ACXJeminiDrone* Drone = GetDrone();
	if (!Drone || Type == ECXJeminiOfferType::None)
	{
		return;
	}
	const double T = GetWorld()->GetTimeSeconds();
	OfferType = Type;
	bOfferForced = bForced;
	OfferExpireTime = T + FMath::Max(1.0f, OfferTimeout);
	LastAnyOfferTime = T;
	LastOfferTime[static_cast<int32>(Type)] = T;
	Drone->SayCoachLine(GetOfferLine(Type));
	SeenDroneSeq = Drone->GetDialogueSeq();	// 내 대사는 "다른 대사"로 세지 않는다
	LastDebug = FString::Printf(TEXT("offered %s%s"), GetOfferTypeName(Type), bForced ? TEXT(" (forced)") : TEXT(""));
	UE_LOG(LogCXJeminiProactive, Log, TEXT("Jemini offer %s%s: %s"), GetOfferTypeName(Type), bForced ? TEXT(" (forced)") : TEXT(""), *GetOfferLine(Type));
}

void UCXJeminiProactiveComponent::ClearOffer(const TCHAR* Why)
{
	if (HasOffer())
	{
		UE_LOG(LogCXJeminiProactive, Log, TEXT("Jemini offer %s cleared (%s)"), GetOfferTypeName(OfferType), Why);
		LastDebug = FString::Printf(TEXT("%s %s"), GetOfferTypeName(OfferType), Why);
	}
	OfferType = ECXJeminiOfferType::None;
	bOfferForced = false;
}

bool UCXJeminiProactiveComponent::ForceOffer(ECXJeminiOfferType Type, FString& OutWhy)
{
	ACXJeminiDrone* Drone = GetDrone();
	if (!Drone || !GetOwner()->HasAuthority())
	{
		OutWhy = TEXT("no drone / not server");
		return false;
	}
	if (Type == ECXJeminiOfferType::None)
	{
		OutWhy = TEXT("unknown type (ammo|hp|stuck|decoy)");
		return false;
	}
	if (HasOffer())
	{
		OutWhy = TEXT("an offer is already pending");
		return false;
	}
	ShowOffer(Type, true);
	return true;
}

// ---------------------------------------------------------------- 대답

bool UCXJeminiProactiveComponent::RespondToOffer(APawn* From, bool bAccept)
{
	ACXJeminiDrone* Drone = GetDrone();
	if (!Drone || !GetOwner()->HasAuthority() || !HasOffer())
	{
		return false;
	}
	const ECXJeminiOfferType Type = OfferType;
	ClearOffer(bAccept ? TEXT("accepted") : TEXT("declined"));
	const double T = GetWorld()->GetTimeSeconds();
	LastAnyOfferTime = T;
	LastOfferTime[static_cast<int32>(Type)] = T;
	if (!bAccept)
	{
		Drone->SayCoachLine(CXJeminiProactive::DeclineLine);
		SeenDroneSeq = Drone->GetDialogueSeq();
		return true;
	}
	// 기존 실행 경로(배터리·대상 검증·BUSY) — 못 시작하면(드론이 그 사이 바빠짐) 알려 준다
	FString Target;
	FString Ack;
	switch (Type)
	{
	case ECXJeminiOfferType::Reload: Target = TEXT("brother"); Ack = TEXT("좋아요, 장전해 드릴게요!"); break;
	case ECXJeminiOfferType::Heal: Target = TEXT("both"); Ack = TEXT("좋아요, 회복해 드릴게요!"); break;
	case ECXJeminiOfferType::HackAssist: Target = TEXT("sister"); Ack = TEXT("좋아요, 제가 도와드릴게요!"); break;
	case ECXJeminiOfferType::Decoy: Target = TEXT("enemies"); Ack = TEXT("좋아요, 미끼 던질게요!"); break;
	default: break;
	}
	if (!Drone->ExecuteLocalCommand(From, GetOfferCommand(Type), Target, Ack))
	{
		Drone->SayCoachLine(CXJeminiProactive::GoneLine);
	}
	SeenDroneSeq = Drone->GetDialogueSeq();
	LastDebug = FString::Printf(TEXT("%s accepted -> %s"), GetOfferTypeName(Type), *Drone->GetDialogue());
	return true;
}

FString UCXJeminiProactiveComponent::NormalizeUtterance(const FString& Text)
{
	static const FString Punct = TEXT(".,!?~…'\"-_/;:()[]^*。！？，");
	FString Out;
	for (const TCHAR C : Text)
	{
		if (FChar::IsWhitespace(C) || Punct.Contains(FString(1, &C)))
		{
			continue;
		}
		Out.AppendChar(FChar::ToLower(C));
	}
	return Out;
}

int32 UCXJeminiProactiveComponent::ParseShortAnswer(const FString& Text)
{
	const FString N = NormalizeUtterance(Text);
	if (N.IsEmpty() || N.Len() > CXJeminiProactive::MaxShortChars)
	{
		return 0;
	}
	static const TCHAR* Yes[] = { TEXT("응"), TEXT("웅"), TEXT("어"), TEXT("네"), TEXT("넹"), TEXT("예"), TEXT("그래"), TEXT("그래요"), TEXT("좋아"), TEXT("좋아요"),
		TEXT("좋지"), TEXT("해줘"), TEXT("해줘요"), TEXT("해주세요"), TEXT("부탁해"), TEXT("부탁해요"), TEXT("부탁"), TEXT("콜"), TEXT("ㅇㅇ"), TEXT("ㅇㅋ"),
		TEXT("오케이"), TEXT("오케"), TEXT("ok"), TEXT("okay"), TEXT("yes"), TEXT("y"), TEXT("yeah") };
	static const TCHAR* No[] = { TEXT("아니"), TEXT("아니요"), TEXT("아니야"), TEXT("아냐"), TEXT("아뇨"), TEXT("싫어"), TEXT("싫어요"), TEXT("괜찮아"), TEXT("괜찮아요"),
		TEXT("됐어"), TEXT("됐어요"), TEXT("됐다"), TEXT("노"), TEXT("no"), TEXT("n"), TEXT("nope"), TEXT("ㄴㄴ"), TEXT("ㄴ") };
	for (const TCHAR* Word : Yes)
	{
		if (N == Word)
		{
			return 1;
		}
	}
	for (const TCHAR* Word : No)
	{
		if (N == Word)
		{
			return -1;
		}
	}
	return 0;
}

bool UCXJeminiProactiveComponent::HandleHeard(APawn* From, const FString& Heard)
{
	if (!HasOffer())
	{
		return false;
	}
	const int32 Answer = ParseShortAnswer(Heard);
	if (Answer == 0)
	{
		return false;
	}
	UE_LOG(LogCXJeminiProactive, Log, TEXT("Jemini offer answered by voice: %s"), Answer > 0 ? TEXT("yes") : TEXT("no"));
	return RespondToOffer(From, Answer > 0);
}

bool UCXJeminiProactiveComponent::HandleLocalText(APawn* From, const FString& Text)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FString N = NormalizeUtterance(Text);
	if (N.IsEmpty() || N.Len() > CXJeminiProactive::MaxShortChars)
	{
		return false;
	}
	if (HasOffer())
	{
		const int32 Answer = ParseShortAnswer(N);
		if (Answer != 0)
		{
			UE_LOG(LogCXJeminiProactive, Log, TEXT("Jemini offer answered by chat: %s"), Answer > 0 ? TEXT("yes") : TEXT("no"));
			return RespondToOffer(From, Answer > 0);
		}
	}
	return TryKeywordCommand(From, N);
}

bool UCXJeminiProactiveComponent::TryKeywordCommand(APawn* From, const FString& Normalized)
{
	ACXJeminiDrone* Drone = GetDrone();
	if (!Drone)
	{
		return false;
	}
	// 공손한 끝말("해줘", "해요", "좀"…)은 한 번 떼어 본다 — 단어 하나로 이루어진 문장만 허용
	static const TCHAR* Suffixes[] = { TEXT("해주세요"), TEXT("해줄래요"), TEXT("해줄래"), TEXT("해줘요"), TEXT("해줘"), TEXT("해요"), TEXT("해"), TEXT("좀"), TEXT("요") };
	TArray<FString, TInlineAllocator<12>> Candidates;
	Candidates.Add(Normalized);
	for (const TCHAR* Suffix : Suffixes)
	{
		const FString S(Suffix);
		if (Normalized.Len() > S.Len() && Normalized.EndsWith(S))
		{
			Candidates.Add(Normalized.Left(Normalized.Len() - S.Len()));
		}
	}
	for (const FString& Candidate : Candidates)
	{
		for (const CXJeminiProactive::FKeyword& Keyword : CXJeminiProactive::Keywords)
		{
			if (Candidate != Keyword.Word)
			{
				continue;
			}
			FString Cmd = Keyword.Cmd;
			if (Cmd == TEXT("HackSmart"))
			{
				Cmd = Drone->ResolveSmartHackCommand();
				if (Cmd.IsEmpty())
				{
					Cmd = TEXT("HackTerminal");	// 대상이 없으면 거절 대사("해킹할 단말기가 없어요")가 나오게
				}
			}
			const FString Target = Cmd == TEXT("HackEnemy") ? TEXT("enemy") : (Cmd == TEXT("HackTerminal") ? TEXT("sister") : FString(Keyword.Target));
			UE_LOG(LogCXJeminiProactive, Log, TEXT("Jemini keyword \"%s\" -> %s %s"), Keyword.Word, *Cmd, *Target);
			return Drone->ExecuteLocalCommand(From, Cmd, Target, Keyword.Ack);
		}
	}
	return false;
}

// ---------------------------------------------------------------- 디버그

FString UCXJeminiProactiveComponent::GetDebugState() const
{
	FInputs In;
	GatherInputs(In);
	const FString Block = GetBlockReason();
	return FString::Printf(
		TEXT("offer=%s remaining=%.1fs | ammo=%d/%d reloading=%d enemy(brother)=%.0fcm | brotherHp=%d/%d | sisterHackTerminal=%d stuck=%.1f/%.0fs enemy(sister)=%.0fcm | battery=%d")
		TEXT(" | meets: ammo=%d hp=%d stuck=%d decoy=%d | cooldown: ammo=%.0fs hp=%.0fs stuck=%.0fs decoy=%.0fs global=%.0fs | blocked=%s | last=\"%s\""),
		GetOfferTypeName(OfferType), GetOfferRemaining(), In.Ammo, In.AmmoMax, In.bReloading ? 1 : 0, In.BrotherEnemyCm, In.BrotherHp, In.BrotherHpMax,
		In.bSisterHackingTerminal ? 1 : 0, In.StuckSeconds, StuckSeconds, In.SisterEnemyCm, In.Battery,
		Meets(ECXJeminiOfferType::Reload, In) ? 1 : 0, Meets(ECXJeminiOfferType::Heal, In) ? 1 : 0, Meets(ECXJeminiOfferType::HackAssist, In) ? 1 : 0,
		Meets(ECXJeminiOfferType::Decoy, In) ? 1 : 0,
		CooldownRemaining(ECXJeminiOfferType::Reload), CooldownRemaining(ECXJeminiOfferType::Heal), CooldownRemaining(ECXJeminiOfferType::HackAssist),
		CooldownRemaining(ECXJeminiOfferType::Decoy), FMath::Max(0.0f, GlobalCooldown - (Now() - static_cast<float>(LastAnyOfferTime))),
		Block.IsEmpty() ? TEXT("no (can offer)") : *Block, *LastDebug);
}
