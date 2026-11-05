// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoopHUD.h"
#include "BrotherCombatComponent.h"
#include "SisterCharacter.h"
#include "SisterHackComponent.h"
#include "HackTerminal.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXHealthComponent.h"
#include "CXHackTarget.h"
#include "CombatDamageable.h"
#include "Sound/SoundBase.h"
#include "CXGameInstance.h"
#include "CXSavePoint.h"
#include "CXLevelExit.h"
#include "CXSfx.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace CoopHUDLayout
{
	// 분할 한 칸 기준 해상도. 실제 칸 크기에 맞춰 S()로 스케일한다.
	constexpr float RefWidth = 1920.0f;
	constexpr float RefHeight = 540.0f;
	constexpr float Margin = 28.0f;
	// 엔진 기본 Canvas 폰트가 작아서 분할 화면에서 읽히도록 키우는 배율
	constexpr float TextScale = 1.45f;
}

ACoopHUD::ACoopHUD()
{
	ZoneTitles.Add(TEXT("Map1"), TEXT("SCRAP STREET"));
	ZoneTitles.Add(TEXT("Map1_SoulCity"), TEXT("SCRAP STREET"));
	// 2026-11-02 맵 이름 변경(Map1_SoulCity1 / Map2_SoulCity2) — 예전 이름도 유지
	ZoneTitles.Add(TEXT("Map1_SoulCity1"), TEXT("SCRAP STREET"));
	ZoneTitles.Add(TEXT("Map2"), TEXT("SQUATTER STACKS"));
	ZoneTitles.Add(TEXT("Map2_SoulCity2"), TEXT("SQUATTER STACKS"));
	ZoneTitles.Add(TEXT("Map3"), TEXT("WASTE PLANT"));
	// MVP 흐름(세이브포인트·맵 이동 기획 1장): 세이브 복도 → 마지막 방
	ZoneTitles.Add(TEXT("Map_SaveCorridor"), TEXT("SERVICE CORRIDOR"));
	ZoneTitles.Add(TEXT("Map3_Final"), TEXT("THE PIT"));
	ZoneTitles.Add(TEXT("Map5"), TEXT("THE PIT"));

	MemoryFlashSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_DigitTick.SW_Hack_DigitTick"));
}

void ACoopHUD::BeginPlay()
{
	Super::BeginPlay();

	StartTime = GetWorld()->GetTimeSeconds();
	const FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);
	const FString* Found = ZoneTitles.Find(MapName);
	ZoneTitle = Found ? *Found : DefaultZoneTitle;

	RefreshHackTerminals();
	GetWorldTimerManager().SetTimer(HackRefreshTimer, this, &ACoopHUD::RefreshHackTerminals, 1.0f, true);
}

void ACoopHUD::RefreshHackTerminals()
{
	HackTerminals.Reset();
	EnemyActors.Reset();
	HackEnemyTargets.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		bool bHackable = Actor->IsA<AHackTerminal>() || Actor->Tags.Contains(HackableTag);
		if (!bHackable)
		{
			const FString ClassName = Actor->GetClass()->GetName();
			for (const FString& Prefix : HackableClassPrefixes)
			{
				if (ClassName.StartsWith(Prefix))
				{
					bHackable = true;
					break;
				}
			}
		}
		if (bHackable)
		{
			HackTerminals.Add(Actor);
		}
		// 적(체력 바) / 적 해킹 대상(여동생 마커) — 플레이어는 ICombatDamageable이 아니라 빠진다
		if (Actor->Implements<UCombatDamageable>() && UCXHealthComponent::FindOn(Actor))
		{
			EnemyActors.Add(Actor);
		}
		if (Actor->Implements<UCXHackTarget>())
		{
			HackEnemyTargets.Add(Actor);
		}
	}
}

void ACoopHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	UIScale = FMath::Clamp(FMath::Min(Canvas->ClipX / CoopHUDLayout::RefWidth, Canvas->ClipY / CoopHUDLayout::RefHeight), 0.5f, 2.0f);

	APawn* MyPawn = GetOwningPawn();
	const bool bIsSister = Cast<ASisterCharacter>(MyPawn) != nullptr;
	const FLinearColor MyColor = bIsSister ? SisterColor : BrotherColor;

	// 사망(부활 대기/게임 오버) 중에는 무기·조준·해킹·상호작용 표시를 끄고 대기 화면을 그린다(게임 오버화면 기획 3장)
	const UCXPlayerLifeComponent* MyLife = MyPawn ? MyPawn->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
	const bool bDead = MyLife && !MyLife->IsAlive();

	if (bDead)
	{
		DrawRespawnOverlay(*MyLife, MyColor);
	}
	else
	{
		LastTickSecond = INDEX_NONE;
	}

	DrawSplitEdge(MyColor);

	const UCXHealthComponent* MyHealth = UCXHealthComponent::FindOn(MyPawn);
	const UBrotherCombatComponent* MyCombat = MyPawn ? MyPawn->FindComponentByClass<UBrotherCombatComponent>() : nullptr;

	// 적 체력 바는 월드 마커라 패널보다 먼저(아래에) 그린다
	if (!bDead)
	{
		DrawEnemyHealthBars(bIsSister ? nullptr : MyCombat);
		// 그로기 중인 적: 두 칸 모두(여동생이 건 그로기를 오빠가 보고 마무리)
		DrawEnemyStunMarkers();
	}

	if (bIsSister)
	{
		DrawPlayerTag(MyColor, TEXT("P2"), TEXT("SISTER"), TEXT("INFILTRATION  //  HACK"));
		if (!bDead)
		{
			DrawEnemyHackMarkers();
			DrawSisterHackLink();
		}
	}
	else
	{
		DrawPlayerTag(MyColor, TEXT("P1"), TEXT("BROTHER"), TEXT("COMBAT  //  RAILGUN"));
		if (MyCombat && !bDead)
		{
			DrawTargetMarker(*MyCombat);
			DrawBrotherWeapon(*MyCombat);
		}
		// 메모리 패턴 중계(해킹 시스템 기획 11장): 패턴 점멸은 오빠 칸에만. 사망 중이면 그리지 않는다(서버도 재생을 멈춘다).
		if (MyPawn && !bDead)
		{
			DrawMemoryRelay(*MyPawn);
		}
	}

	// 체력 바: 플레이어 태그 바로 아래(HUD 기획 — 체력 연결 지점)
	if (MyHealth)
	{
		DrawPlayerHealth(*MyHealth, MyColor);
	}

	if (MyPawn && !bDead)
	{
		DrawInteractPrompt(*MyPawn, MyColor);
	}

	const APawn* Partner = bDead ? nullptr : FindPartnerPawn();
	if (Partner)
	{
		const bool bPartnerIsSister = Cast<ASisterCharacter>(Partner) != nullptr;
		FString PartnerName = bPartnerIsSister ? TEXT("SISTER") : TEXT("BROTHER");
		FLinearColor PartnerColor = bPartnerIsSister ? SisterColor : BrotherColor;
		// 파트너 부활 대기 중: 머리 위 표시가 SIGNAL LOST + 남은 초로 바뀐다
		const UCXPlayerLifeComponent* PartnerLife = Partner->FindComponentByClass<UCXPlayerLifeComponent>();
		if (PartnerLife && !PartnerLife->IsAlive())
		{
			PartnerName = PartnerLife->GetLifeState() == ECXLifeState::AwaitingRespawn
				? FString::Printf(TEXT("%s  SIGNAL LOST  %d"), *PartnerName, FMath::CeilToInt(PartnerLife->GetRespawnRemaining()))
				: FString::Printf(TEXT("%s  SIGNAL LOST"), *PartnerName);
			PartnerColor = PartnerColor.CopyWithNewOpacity(0.55f + 0.45f * Pulse(6.0f));
		}
		const bool bPartnerAlive = !PartnerLife || PartnerLife->IsAlive();
		DrawPartnerIndicator(*Partner, PartnerColor, PartnerName, bPartnerAlive ? UCXHealthComponent::FindOn(Partner) : nullptr);
	}

	if (MyLife && MyLife->IsAlive())
	{
		DrawRebooted(*MyLife, MyColor);
	}

	// 피격 플래시(자기 칸만 — 위아래 분할에서 맞은 사람 화면만 붉어진다)
	if (MyHealth && !bDead)
	{
		DrawHitFlash(*MyHealth);
	}

	DrawFlowNotices(MyPawn, bDead);
	DrawZoneBanner();
	DrawTravelFade();
}

// ---------------------------------------------------------------- 부활 대기(게임 오버화면 기획 3장)

void ACoopHUD::DrawRespawnOverlay(const UCXPlayerLifeComponent& Life, const FLinearColor& Color)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	// 어둡게(채도 0 대신 — 포스트프로세스/카메라는 건드리지 않는다) + 가장자리 비네팅
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), 0.0f, 0.0f, W, H);
	for (int32 i = 1; i <= 8; ++i)
	{
		const float T = i * 16.0f * S();
		const FLinearColor V(0.0f, 0.0f, 0.0f, 0.08f);
		DrawRect(V, 0.0f, 0.0f, W, T);
		DrawRect(V, 0.0f, H - T, W, T);
		DrawRect(V, 0.0f, T, T, H - 2.0f * T);
		DrawRect(V, W - T, T, T, H - 2.0f * T);
	}
	// 약한 신호 노이즈(가로 줄)
	for (int32 i = 0; i < 14; ++i)
	{
		const float Y = FMath::FRand() * H;
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.02f + 0.05f * FMath::FRand()), 0.0f, Y, W, (1.0f + 2.0f * FMath::FRand()) * S());
	}

	const float CX = W * 0.5f;
	const float CY = H * 0.46f;
	Label(TEXT("SIGNAL LOST"), CX, CY - 150.0f * S(), WarningColor.CopyWithNewOpacity(0.75f + 0.25f * Pulse(5.0f)), GEngine->GetLargeFont(), 1.3f, true);

	if (Life.GetLifeState() != ECXLifeState::AwaitingRespawn)
	{
		// 게임 오버 확정 — 카운트다운 없음
		return;
	}

	const float Remaining = Life.GetRespawnRemaining();
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Remaining));

	// 남은 초(플레이어 색). 마지막 3초는 펄스 + 틱
	float NumScale = 4.2f;
	float NumAlpha = 1.0f;
	if (Seconds <= 3)
	{
		const float Frac = Remaining - FMath::FloorToFloat(Remaining);
		NumScale *= 1.0f + 0.18f * Frac;
		NumAlpha = 0.6f + 0.4f * Frac;
		if (Seconds > 0 && Seconds != LastTickSecond)
		{
			if (RespawnTickSound)
			{
				UGameplayStatics::PlaySound2D(this, RespawnTickSound);
			}
		}
	}
	LastTickSecond = Seconds;
	const FString Num = FString::Printf(TEXT("%02d"), Seconds);
	float TW = 0.0f, TH = 0.0f;
	GetTextSize(Num, TW, TH, GEngine->GetLargeFont(), NumScale * S() * 1.45f);
	Label(Num, CX, CY - 10.0f * S() - TH * 0.5f, Color.CopyWithNewOpacity(NumAlpha), GEngine->GetLargeFont(), NumScale, true);

	// 진행 바(채워진 쪽 = 지난 시간)
	const float BarW = 520.0f * S();
	const float BarH = 6.0f * S();
	const float BarX = CX - BarW * 0.5f;
	const float BarY = CY + 52.0f * S();
	const float Duration = FMath::Max(0.01f, Life.GetRespawnDuration());
	const float Progress = FMath::Clamp(1.0f - Remaining / Duration, 0.0f, 1.0f);
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f), BarX, BarY, BarW, BarH);
	DrawRect(Color.CopyWithNewOpacity(0.9f), BarX, BarY, BarW * Progress, BarH);

	Label(FString::Printf(TEXT("REBOOTING IN %ds"), Seconds), CX, BarY + 16.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), GEngine->GetSmallFont(), 1.0f, true);
}

void ACoopHUD::DrawRebooted(const UCXPlayerLifeComponent& Life, const FLinearColor& Color)
{
	const float Since = Life.GetTimeSinceRevive();
	if (Since < 0.0f || Since > RebootedShowTime)
	{
		return;
	}
	const float Alpha = 1.0f - FMath::Clamp((Since - RebootedShowTime * 0.6f) / (RebootedShowTime * 0.4f), 0.0f, 1.0f);
	Label(TEXT("REBOOTED"), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f - 130.0f * S(), Color.CopyWithNewOpacity(Alpha), GEngine->GetLargeFont(), 1.3f, true);
}

// ---------------------------------------------------------------- 요소

void ACoopHUD::DrawPlayerTag(const FLinearColor& Color, const FString& SlotText, const FString& DisplayName, const FString& RoleText)
{
	const float X = CoopHUDLayout::Margin * S();
	const float Y = CoopHUDLayout::Margin * S();

	// 세로 네온 바 + 슬롯 번호 + 이름 + 역할
	DrawRect(Color, X, Y, 4.0f * S(), 66.0f * S());
	Label(SlotText, X + 14.0f * S(), Y - 4.0f * S(), Color, GEngine->GetSmallFont(), 1.0f);
	Label(DisplayName, X + 14.0f * S(), Y + 13.0f * S(), FLinearColor::White, GEngine->GetLargeFont(), 1.05f);
	Label(RoleText, X + 14.0f * S(), Y + 50.0f * S(), Color.CopyWithNewOpacity(0.75f), GEngine->GetTinyFont(), 1.0f);
}

void ACoopHUD::DrawBrotherWeapon(const UBrotherCombatComponent& Combat)
{
	const int32 MaxAmmo = FMath::Max(1, Combat.GetMaxAmmo());
	const int32 Ammo = FMath::Clamp(Combat.CurrentAmmo, 0, MaxAmmo);
	const bool bLow = !Combat.bIsReloading && Ammo <= FMath::CeilToInt(MaxAmmo * LowAmmoRatio);
	const FLinearColor Accent = bLow ? WarningColor : BrotherColor;

	const float W = 400.0f * S();
	const float H = 96.0f * S();
	const float X = CoopHUDLayout::Margin * S();
	const float Y = Canvas->ClipY - CoopHUDLayout::Margin * S() - H;
	Panel(X, Y, W, H, Accent);

	const float Pad = 16.0f * S();
	DrawPistolIcon(X + W - Pad - 46.0f * S(), Y + 10.0f * S(), Accent.CopyWithNewOpacity(0.9f));
	Label(TEXT("RAIL PISTOL"), X + Pad, Y + 8.0f * S(), Accent.CopyWithNewOpacity(0.8f), GEngine->GetTinyFont(), 1.0f);

	// 큰 탄약 숫자 + 최대치
	const FString AmmoText = FString::Printf(TEXT("%02d"), Ammo);
	Label(AmmoText, X + Pad, Y + 22.0f * S(), Combat.bIsReloading ? Accent.CopyWithNewOpacity(0.35f) : FLinearColor::White, GEngine->GetLargeFont(), 1.9f);
	Label(FString::Printf(TEXT("/ %d"), MaxAmmo), X + Pad + 70.0f * S(), Y + 50.0f * S(), Accent.CopyWithNewOpacity(0.7f), GEngine->GetSmallFont(), 1.0f);

	// 오른쪽: 탄약 눈금 또는 장전 게이지
	const float BarX = X + 140.0f * S();
	const float BarY = Y + 42.0f * S();
	const float BarW = W - 140.0f * S() - Pad;
	if (Combat.bIsReloading)
	{
		const float Progress = Combat.GetReloadProgress();
		DrawRect(Accent.CopyWithNewOpacity(0.15f), BarX, BarY, BarW, 14.0f * S());
		DrawRect(Accent, BarX, BarY, BarW * Progress, 14.0f * S());
		const float Blink = 0.45f + 0.55f * Pulse(6.0f);
		Label(FString::Printf(TEXT("RECHARGING  %3d%%"), FMath::RoundToInt(Progress * 100.0f)), BarX, BarY + 20.0f * S(),
		      Accent.CopyWithNewOpacity(Blink), GEngine->GetSmallFont(), 1.0f);
	}
	else
	{
		// 탄 1발 = 눈금 1칸. 남은 탄은 채움, 쓴 탄은 흐리게.
		const float Gap = 2.0f * S();
		const float PipW = (BarW - Gap * (MaxAmmo - 1)) / MaxAmmo;
		for (int32 i = 0; i < MaxAmmo; ++i)
		{
			const bool bFilled = i < Ammo;
			DrawRect(bFilled ? Accent : Accent.CopyWithNewOpacity(0.12f), BarX + i * (PipW + Gap), BarY, PipW, 22.0f * S());
		}
		if (bLow)
		{
			Label(TEXT("LOW CHARGE"), BarX, BarY + 28.0f * S(), WarningColor.CopyWithNewOpacity(0.5f + 0.5f * Pulse(4.0f)), GEngine->GetTinyFont(), 1.0f);
		}
	}
}

void ACoopHUD::DrawSisterHackLink()
{
	// 가장 가까운 해킹 단말기
	const APawn* MyPawn = GetOwningPawn();
	const AActor* Nearest = nullptr;
	float NearestDist = TNumericLimits<float>::Max();
	if (MyPawn)
	{
		for (const TWeakObjectPtr<AActor>& Weak : HackTerminals)
		{
			const AActor* Terminal = Weak.Get();
			if (Terminal && IsTerminalAvailable(Terminal))
			{
				const float D = FVector::Dist(MyPawn->GetActorLocation(), Terminal->GetActorLocation());
				if (D < NearestDist)
				{
					NearestDist = D;
					Nearest = Terminal;
				}
			}
		}
	}
	const bool bInSignal = Nearest && NearestDist <= HackSignalRange;
	const USisterHackComponent* Hack = MyPawn ? MyPawn->FindComponentByClass<USisterHackComponent>() : nullptr;
	const bool bEnemyLink = Hack && Hack->IsEnemyHacking();
	const bool bActive = !bEnemyLink && (Hack ? (Hack->GetFocusTerminal() != nullptr) : (Nearest && NearestDist <= HackActivateRange));

	// 신호 범위 안 단말기에는 월드 마커(가장 가까운 것만 활성)
	if (MyPawn)
	{
		for (const TWeakObjectPtr<AActor>& Weak : HackTerminals)
		{
			const AActor* Terminal = Weak.Get();
			if (Terminal && IsTerminalAvailable(Terminal))
			{
				const float D = FVector::Dist(MyPawn->GetActorLocation(), Terminal->GetActorLocation());
				if (D <= HackSignalRange)
				{
					DrawHackTerminalMarker(*Terminal, Terminal == Nearest && bActive, D / 100.0f);
				}
			}
		}
	}

	const float W = 400.0f * S();
	const float H = 96.0f * S();
	const float X = CoopHUDLayout::Margin * S();
	const float Y = Canvas->ClipY - CoopHUDLayout::Margin * S() - H;
	const FLinearColor Accent = bActive ? SisterColor : SisterColor.CopyWithNewOpacity(bInSignal ? 0.85f : 0.55f);
	Panel(X, Y, W, H, Accent);

	const float Pad = 16.0f * S();
	Label(TEXT("HACK LINK"), X + Pad, Y + 8.0f * S(), SisterColor.CopyWithNewOpacity(0.8f), GEngine->GetTinyFont(), 1.0f);

	if (bEnemyLink)
	{
		// 적 해킹 중: 패널은 상태만(퍼즐은 칸 위쪽 가운데 패널)
		const ICXHackTarget* EnemyTarget = Cast<ICXHackTarget>(Hack->GetActiveEnemy());
		Label(TEXT("OVERRIDE"), X + Pad, Y + 24.0f * S(), FLinearColor::White, GEngine->GetLargeFont(), 1.1f);
		Label(FString::Printf(TEXT("BYPASS SEQUENCE  //  %s"), EnemyTarget ? *EnemyTarget->GetHackTargetName() : TEXT("TARGET")),
			X + Pad, Y + 62.0f * S(), SisterColor.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);
	}
	else if (bActive)
	{
		// 단말기 활성화: 큰 상태 문구 + 입력 안내, 패널 전체가 맥박처럼 밝아져 시선을 끈다
		const float Blink = 0.5f + 0.5f * Pulse(7.0f);
		DrawRect(SisterColor.CopyWithNewOpacity(0.22f * Blink), X, Y, W, H);
		Label(TEXT("TERMINAL ONLINE"), X + Pad, Y + 24.0f * S(), FLinearColor::White, GEngine->GetLargeFont(), 1.1f);
		Label(FString::Printf(TEXT("%s  BREACH"), *HackKeyHint), X + Pad, Y + 62.0f * S(), SisterColor.CopyWithNewOpacity(Blink), GEngine->GetSmallFont(), 1.0f);
	}
	else if (bInSignal)
	{
		Label(TEXT("SIGNAL DETECTED"), X + Pad, Y + 24.0f * S(), FLinearColor::White, GEngine->GetLargeFont(), 1.0f);
		Label(FString::Printf(TEXT("TERMINAL  %.1fm"), NearestDist / 100.0f), X + Pad, Y + 62.0f * S(), SisterColor.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);
	}
	else
	{
		Label(TEXT("SCANNING"), X + Pad, Y + 30.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), GEngine->GetLargeFont(), 1.1f);
	}

	// 신호 막대: 단말기에 가까울수록 많이 켜진다. 신호가 없으면 스캔 애니메이션.
	const int32 Bars = 5;
	const float BarBase = Y + H - 18.0f * S();
	const int32 Lit = bInSignal
		? FMath::Clamp(FMath::CeilToInt((1.0f - NearestDist / HackSignalRange) * Bars), 1, Bars)
		: static_cast<int32>(FMath::Fmod(GetWorld()->GetTimeSeconds() * 2.0f, static_cast<float>(Bars + 1)));
	for (int32 i = 0; i < Bars; ++i)
	{
		const float BarH = (10.0f + i * 9.0f) * S();
		const float Alpha = (i < Lit) ? 0.9f : 0.18f;
		DrawRect(SisterColor.CopyWithNewOpacity(Alpha), X + W - Pad - (Bars - i) * 14.0f * S(), BarBase - BarH, 9.0f * S(), BarH);
	}

	// 적 해킹 쿨타임: 패널 윗줄 오른쪽 RELINK n초 + 아랫변 게이지(HUD 기획 — 해킹 쿨타임 연결 지점)
	if (Hack && !bEnemyLink && Hack->GetEnemyHackCooldownRemaining() > 0.0f)
	{
		const float Remain = Hack->GetEnemyHackCooldownRemaining();
		const FString Text = FString::Printf(TEXT("ENEMY RELINK  %.1fs"), Remain);
		float TW = 0.0f, TH = 0.0f;
		GetTextSize(Text, TW, TH, GEngine->GetTinyFont(), S() * CoopHUDLayout::TextScale);
		Label(Text, X + W - Pad - TW, Y + 8.0f * S(), SisterColor.CopyWithNewOpacity(0.7f), GEngine->GetTinyFont(), 1.0f);
		const float Frac = Hack->GetEnemyHackCooldown() > 0.0f ? FMath::Clamp(Remain / Hack->GetEnemyHackCooldown(), 0.0f, 1.0f) : 0.0f;
		DrawRect(SisterColor.CopyWithNewOpacity(0.6f), X, Y + H - 3.0f * S(), W * Frac, 3.0f * S());
	}

	if (bEnemyLink)
	{
		DrawEnemyHackSession(*Hack);
	}
	else if (Hack && Hack->IsHacking())
	{
		DrawHackSession(*Hack);
	}
}

void ACoopHUD::DrawHackTerminalMarker(const AActor& Terminal, bool bActive, float DistM, float HeightOffset, const FString& InactiveText)
{
	if (!PlayerOwner)
	{
		return;
	}
	FVector Origin, Extent;
	Terminal.GetActorBounds(true, Origin, Extent);

	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
	if (FVector::DotProduct(Origin - ViewLoc, ViewRot.Vector()) <= 0.0f)
	{
		return;
	}
	const FVector P = Project(Origin + FVector(0.0f, 0.0f, Extent.Z + HeightOffset), false);
	if (P.X < 0.0f || P.X > Canvas->ClipX || P.Y < 0.0f || P.Y > Canvas->ClipY)
	{
		return;
	}

	// 마름모 아이콘: 활성이면 굵게 + 펄스 + 가운데 점, 아니면 얇은 외곽선과 거리
	const float R = (bActive ? 12.0f + 3.0f * Pulse(6.0f) : 9.0f) * S();
	const FLinearColor C = bActive ? SisterColor : SisterColor.CopyWithNewOpacity(0.6f);
	const float Th = bActive ? 2.5f : 1.5f;
	DrawLine(P.X, P.Y - R, P.X + R, P.Y, C, Th);
	DrawLine(P.X + R, P.Y, P.X, P.Y + R, C, Th);
	DrawLine(P.X, P.Y + R, P.X - R, P.Y, C, Th);
	DrawLine(P.X - R, P.Y, P.X, P.Y - R, C, Th);
	if (bActive)
	{
		DrawRect(C, P.X - 3.0f * S(), P.Y - 3.0f * S(), 6.0f * S(), 6.0f * S());
		Label(FString::Printf(TEXT("%s HACK"), *HackKeyHint), P.X, P.Y - R - 20.0f * S(), C, GEngine->GetSmallFont(), 1.0f, true);
	}
	else
	{
		Label(InactiveText.IsEmpty() ? FString::Printf(TEXT("%.0fm"), DistM) : InactiveText, P.X, P.Y - R - 16.0f * S(), C, GEngine->GetTinyFont(), 1.0f, true);
	}
}

void ACoopHUD::DrawInteractPrompt(const APawn& MyPawn, const FLinearColor& Color)
{
	const UCoopInteractComponent* Interact = MyPawn.FindComponentByClass<UCoopInteractComponent>();
	FVector Location;
	FString Prompt;
	if (!PlayerOwner || !Interact || !Interact->GetFocusPrompt(Location, Prompt))
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
	if (FVector::DotProduct(Location - ViewLoc, ViewRot.Vector()) <= 0.0f)
	{
		return;
	}
	const FVector P = Project(Location, false);
	if (P.X < 0.0f || P.X > Canvas->ClipX || P.Y < 0.0f || P.Y > Canvas->ClipY)
	{
		return;
	}

	// 키 표기: 이 캐릭터의 조작 장치 설정(슬롯 0 오빠 / 1 여동생) 기준 — 키보드 F / 게임패드 X
	const int32 Slot = UCoopInteractComponent::GetPawnRole(&MyPawn) == ECoopCharacterRole::Sister ? 1 : 0;
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	const bool bKeyboard = GI ? GI->GetSlotDeviceType(Slot) == ECXInputDeviceType::KeyboardMouse : Slot == 0;
	const FString Text = FString::Printf(TEXT("%s %s"), bKeyboard ? TEXT("[F]") : TEXT("[X]"), *Prompt);

	// 버튼을 감싸는 펄스 브래킷 + 위쪽 안내 문구
	const float Half = (14.0f + 3.0f * Pulse(6.0f)) * S();
	Bracket(FVector2D(P.X, P.Y), Half, 6.0f * S(), Color, 2.0f);
	Label(Text, P.X, P.Y - Half - 22.0f * S(), Color, GEngine->GetSmallFont(), 1.0f, true);
}

void ACoopHUD::DrawTargetMarker(const UBrotherCombatComponent& Combat)
{
	const AActor* Target = Combat.CurrentTarget;
	if (!IsValid(Target) || !PlayerOwner)
	{
		return;
	}

	FVector Origin, Extent;
	Target->GetActorBounds(true, Origin, Extent);

	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
	if (FVector::DotProduct(Origin - ViewLoc, ViewRot.Vector()) <= 0.0f)
	{
		return;
	}

	const FVector Screen = Project(Origin, false);
	const FVector Top = Project(Origin + FVector(0.0f, 0.0f, Extent.Z), false);
	const float HalfSize = FMath::Clamp(FMath::Abs(Screen.Y - Top.Y) * 1.15f, 18.0f * S(), 120.0f * S());

	const bool bLocked = Combat.bIsAiming;
	const FLinearColor Color = bLocked ? BrotherColor : BrotherColor.CopyWithNewOpacity(0.35f + 0.45f * Pulse(5.0f));
	Bracket(FVector2D(Screen.X, Screen.Y), HalfSize, HalfSize * 0.4f, Color, bLocked ? 2.5f : 1.5f);

	if (bLocked)
	{
		// 조준 중: 중앙 십자 점
		DrawRect(Color, Screen.X - 2.0f * S(), Screen.Y - 2.0f * S(), 4.0f * S(), 4.0f * S());
	}

	const APawn* MyPawn = GetOwningPawn();
	const float DistM = MyPawn ? FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation()) / 100.0f : 0.0f;
	Label(FString::Printf(TEXT("%s  %.1fm"), bLocked ? TEXT("LOCK") : TEXT("AUTO"), DistM),
	      Screen.X + HalfSize + 6.0f * S(), Screen.Y - HalfSize, Color, GEngine->GetTinyFont(), 1.0f);
}

void ACoopHUD::DrawPartnerIndicator(const APawn& Partner, const FLinearColor& Color, const FString& PartnerName, const UCXHealthComponent* PartnerHealth)
{
	if (!PlayerOwner)
	{
		return;
	}

	float HeadOffset = 100.0f;
	if (const ACharacter* PartnerChar = Cast<ACharacter>(&Partner))
	{
		HeadOffset = PartnerChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.0f;
	}
	const FVector Head = Partner.GetActorLocation() + FVector(0.0f, 0.0f, HeadOffset);

	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
	const bool bInFront = FVector::DotProduct(Head - ViewLoc, ViewRot.Vector()) > 0.0f;

	const APawn* MyPawn = GetOwningPawn();
	const float DistM = MyPawn ? FVector::Dist(MyPawn->GetActorLocation(), Partner.GetActorLocation()) / 100.0f : 0.0f;
	const FString Text = FString::Printf(TEXT("%s  %dm"), *PartnerName, FMath::RoundToInt(DistM));

	const FVector P = Project(Head, false);
	const float Edge = 40.0f * S();
	const bool bOnScreen = bInFront && P.X >= Edge && P.X <= Canvas->ClipX - Edge && P.Y >= Edge && P.Y <= Canvas->ClipY - Edge;

	if (bOnScreen)
	{
		// 머리 위 작은 역삼각형 + 이름
		const float Size = 7.0f * S();
		DrawLine(P.X - Size, P.Y - Size, P.X + Size, P.Y - Size, Color, 2.0f);
		DrawLine(P.X - Size, P.Y - Size, P.X, P.Y, Color, 2.0f);
		DrawLine(P.X + Size, P.Y - Size, P.X, P.Y, Color, 2.0f);
		Label(PartnerName, P.X, P.Y - Size - 18.0f * S(), Color, GEngine->GetTinyFont(), 1.0f, true);
		if (PartnerHealth)
		{
			// 이름 위 작은 체력 바(분할 화면에서 서로 상태 파악 — 전투 시스템 기획 9.2)
			const float BarW = 56.0f * S();
			const float Pct = PartnerHealth->GetHealthPercent();
			const FLinearColor BarColor = Pct <= LowHealthRatio ? DangerColor.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Color;
			DrawSegmentBar(P.X - BarW * 0.5f, P.Y - Size - 26.0f * S(), BarW, 4.0f * S(), PlayerHealthSegments, Pct, BarColor, 1.0f * S());
		}
		return;
	}

	// 화면 밖: 화면 중심에서 파트너 방향으로 가장자리에 화살표
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	FVector2D Dir(P.X - Center.X, P.Y - Center.Y);
	if (!bInFront)
	{
		Dir = -Dir;
	}
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(1.0f, 0.0f);
	}
	Dir.Normalize();

	const float HalfW = Canvas->ClipX * 0.5f - Edge;
	const float HalfH = Canvas->ClipY * 0.5f - Edge;
	const float T = FMath::Min(HalfW / FMath::Max(FMath::Abs(Dir.X), KINDA_SMALL_NUMBER), HalfH / FMath::Max(FMath::Abs(Dir.Y), KINDA_SMALL_NUMBER));
	const FVector2D Tip = Center + Dir * T;
	const FVector2D Side(-Dir.Y, Dir.X);
	const float Len = 16.0f * S();
	const float Wid = 9.0f * S();
	const FVector2D Base = Tip - Dir * Len;
	const FVector2D A = Base + Side * Wid;
	const FVector2D B = Base - Side * Wid;
	DrawLine(Tip.X, Tip.Y, A.X, A.Y, Color, 2.5f);
	DrawLine(Tip.X, Tip.Y, B.X, B.Y, Color, 2.5f);
	DrawLine(A.X, A.Y, B.X, B.Y, Color, 2.5f);

	// 글자는 화살표 안쪽(화면 중심 쪽)에 둔다
	const FVector2D TextPos = Base - Dir * (26.0f * S());
	Label(Text, TextPos.X, TextPos.Y - 8.0f * S(), Color, GEngine->GetTinyFont(), 1.0f, true);
	if (PartnerHealth)
	{
		const float BarW = 56.0f * S();
		const float Pct = PartnerHealth->GetHealthPercent();
		const FLinearColor BarColor = Pct <= LowHealthRatio ? DangerColor.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Color;
		DrawSegmentBar(TextPos.X - BarW * 0.5f, TextPos.Y + 12.0f * S(), BarW, 4.0f * S(), PlayerHealthSegments, Pct, BarColor, 1.0f * S());
	}
}

// ---------------------------------------------------------------- 체력(전투 시스템 기획 9.2)

void ACoopHUD::DrawPlayerHealth(const UCXHealthComponent& Health, const FLinearColor& Color)
{
	// 플레이어 태그(Y 28~94) 바로 아래, 태그 글자와 같은 들여쓰기
	const float X = CoopHUDLayout::Margin * S() + 14.0f * S();
	const float Y = CoopHUDLayout::Margin * S() + 76.0f * S();
	const float BarW = 240.0f * S();
	const float BarH = 10.0f * S();

	const float Pct = Health.GetHealthPercent();
	const bool bLow = Pct <= LowHealthRatio;
	const FLinearColor BarColor = bLow ? DangerColor.CopyWithNewOpacity(0.55f + 0.45f * Pulse(8.0f)) : Color;

	// 어두운 받침 + 칸 바(1칸 = 10)
	DrawRect(PanelColor, X - 4.0f * S(), Y - 4.0f * S(), BarW + 8.0f * S(), BarH + 8.0f * S());
	DrawSegmentBar(X, Y, BarW, BarH, PlayerHealthSegments, Pct, BarColor, 3.0f * S());

	const FString Text = FString::Printf(TEXT("HP %d/%d"), FMath::CeilToInt(Health.GetHealth()), FMath::RoundToInt(Health.GetMaxHealth()));
	Label(Text, X + BarW + 12.0f * S(), Y - 5.0f * S(), bLow ? BarColor : FLinearColor::White, GEngine->GetSmallFont(), 1.0f);
}

void ACoopHUD::DrawSegmentBar(float X, float Y, float W, float H, int32 Segments, float Fill01, const FLinearColor& Color, float Gap)
{
	Segments = FMath::Max(1, Segments);
	const float SegW = (W - Gap * (Segments - 1)) / Segments;
	const float Filled = FMath::Clamp(Fill01, 0.0f, 1.0f) * Segments;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float SX = X + i * (SegW + Gap);
		DrawRect(Color.CopyWithNewOpacity(Color.A * 0.15f), SX, Y, SegW, H);
		const float Part = FMath::Clamp(Filled - i, 0.0f, 1.0f);
		if (Part > 0.0f)
		{
			DrawRect(Color, SX, Y, SegW * Part, H);
		}
	}
}

void ACoopHUD::DrawHitFlash(const UCXHealthComponent& Health)
{
	const float Since = Health.GetTimeSinceLastDamage();
	if (Since < 0.0f || Since > HitFlashTime)
	{
		return;
	}
	// 가장자리만 붉게(중앙 시야는 가리지 않는다), 빠르게 사라짐
	const float Alpha = 1.0f - Since / FMath::Max(0.01f, HitFlashTime);
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	for (int32 i = 1; i <= 6; ++i)
	{
		const float T = i * 10.0f * S();
		const FLinearColor C = DangerColor.CopyWithNewOpacity(0.09f * Alpha);
		DrawRect(C, 0.0f, 0.0f, W, T);
		DrawRect(C, 0.0f, H - T, W, T);
		DrawRect(C, 0.0f, T, T, H - 2.0f * T);
		DrawRect(C, W - T, T, T, H - 2.0f * T);
	}
}

void ACoopHUD::DrawEnemyHealthBars(const UBrotherCombatComponent* Combat)
{
	if (!PlayerOwner)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
	const AActor* LockedTarget = Combat ? Combat->CurrentTarget.Get() : nullptr;

	for (const TWeakObjectPtr<AActor>& Weak : EnemyActors)
	{
		const AActor* Enemy = Weak.Get();
		const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Enemy);
		if (!Enemy || !Health || Health->IsDead())
		{
			continue;
		}
		// 피격 후 3초, 오빠가 조준 중인 대상, 그로기 중인 대상만(화면이 체력 바로 덮이지 않게)
		const ICXHackTarget* HackTarget = Cast<ICXHackTarget>(Enemy);
		const bool bStunned = HackTarget && HackTarget->GetHackStunRemaining() > 0.0f;
		if (Health->GetTimeSinceLastDamage() > EnemyHealthShowTime && Enemy != LockedTarget && !bStunned)
		{
			continue;
		}
		FVector Origin, Extent;
		Enemy->GetActorBounds(true, Origin, Extent);
		const FVector Head = Origin + FVector(0.0f, 0.0f, Extent.Z + 15.0f);
		if (FVector::DotProduct(Head - ViewLoc, ViewRot.Vector()) <= 0.0f)
		{
			continue;
		}
		const FVector P = Project(Head, false);
		if (P.X < 0.0f || P.X > Canvas->ClipX || P.Y < 0.0f || P.Y > Canvas->ClipY)
		{
			continue;
		}
		// 1칸 = 총알 1발(10) → 거미로봇 50 = 5칸
		const int32 Segments = FMath::Clamp(FMath::RoundToInt(Health->GetMaxHealth() / FMath::Max(1.0f, EnemyHealthSegmentValue)), 1, 20);
		const float BarW = 74.0f * S();
		const float BarH = 7.0f * S();
		const float Fade = (Enemy == LockedTarget || bStunned) ? 1.0f : FMath::Clamp((EnemyHealthShowTime - Health->GetTimeSinceLastDamage()) / 0.5f, 0.0f, 1.0f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Fade), P.X - BarW * 0.5f - 3.0f * S(), P.Y - 3.0f * S(), BarW + 6.0f * S(), BarH + 6.0f * S());
		DrawSegmentBar(P.X - BarW * 0.5f, P.Y, BarW, BarH, Segments, Health->GetHealthPercent(), DangerColor.CopyWithNewOpacity(Fade), 2.0f * S());
	}
}

void ACoopHUD::DrawEnemyHackMarkers()
{
	const APawn* MyPawn = GetOwningPawn();
	if (!MyPawn)
	{
		return;
	}
	const USisterHackComponent* Hack = MyPawn->FindComponentByClass<USisterHackComponent>();
	const float Cooldown = Hack ? Hack->GetEnemyHackCooldownRemaining() : 0.0f;
	const bool bBusy = Hack && Hack->IsHacking();
	for (const TWeakObjectPtr<AActor>& Weak : HackEnemyTargets)
	{
		const AActor* Actor = Weak.Get();
		const ICXHackTarget* Target = Cast<ICXHackTarget>(Actor);
		// 사망·그로기(→ SYSTEM DOWN 마커가 대신 뜸)·지금 해킹 중인 대상은 표시 안 함
		if (!Target || !Target->IsHackTargetAvailable() || (Hack && Hack->GetActiveEnemy() == Actor))
		{
			continue;
		}
		const float D = FVector::Dist(MyPawn->GetActorLocation(), Actor->GetActorLocation());
		if (D <= HackSignalRange)
		{
			// 단말기와 같은 마커 문법. 적 체력 바(머리 위 15cm)와 겹치지 않게 조금 더 위에.
			// `[X] HACK`은 실제로 해킹할 수 있을 때만(범위 안 + 쿨타임 아님 + 다른 해킹 중 아님). 쿨타임이면 RELINK 남은 초.
			const bool bInRange = D <= Target->GetHackInteractRange();
			const bool bHackable = bInRange && Cooldown <= 0.0f && !bBusy;
			const FString Inactive = (bInRange && Cooldown > 0.0f) ? FString::Printf(TEXT("RELINK %.1fs"), Cooldown) : FString();
			DrawHackTerminalMarker(*Actor, bHackable, D / 100.0f, 75.0f, Inactive);
		}
	}
}

void ACoopHUD::DrawEnemyStunMarkers()
{
	if (!PlayerOwner)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);

	// 청록/자홍 글리치(0.07초마다 색·흔들림이 바뀜) — 해킹으로 전원이 끊긴 느낌
	const int32 Seed = static_cast<int32>(GetWorld()->GetTimeSeconds() / 0.07f);
	for (const TWeakObjectPtr<AActor>& Weak : HackEnemyTargets)
	{
		const AActor* Actor = Weak.Get();
		const ICXHackTarget* Target = Cast<ICXHackTarget>(Actor);
		const float Remaining = Target ? Target->GetHackStunRemaining() : 0.0f;
		if (Remaining <= 0.0f)
		{
			continue;
		}
		FVector Origin, Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		const FVector Head = Origin + FVector(0.0f, 0.0f, Extent.Z + 75.0f);
		if (FVector::DotProduct(Head - ViewLoc, ViewRot.Vector()) <= 0.0f)
		{
			continue;
		}
		const FVector P = Project(Head, false);
		if (P.X < 0.0f || P.X > Canvas->ClipX || P.Y < 0.0f || P.Y > Canvas->ClipY)
		{
			continue;
		}

		FRandomStream Glitch(Seed + Actor->GetUniqueID());
		const FLinearColor C = Glitch.FRand() < 0.5f ? BrotherColor : SisterColor;
		const float JX = Glitch.FRand() < 0.25f ? Glitch.FRandRange(-4.0f, 4.0f) * S() : 0.0f;
		const float CX = P.X + JX;

		// 배경 + 문구 + 남은 시간 바(전체 시간 대비)
		const float BoxW = 132.0f * S();
		const float BoxH = 40.0f * S();
		const float BoxY = P.Y - BoxH * 0.5f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), CX - BoxW * 0.5f, BoxY, BoxW, BoxH);
		DrawRect(C.CopyWithNewOpacity(0.9f), CX - BoxW * 0.5f, BoxY, 3.0f * S(), BoxH);
		Label(TEXT("SYSTEM DOWN"), CX, BoxY + 3.0f * S(), FLinearColor::White, GEngine->GetSmallFont(), 0.85f, true);

		const float Duration = FMath::Max(0.1f, Target->GetHackStunDuration());
		const float Frac = FMath::Clamp(Remaining / Duration, 0.0f, 1.0f);
		const float BarW = BoxW - 46.0f * S();
		const float BarX = CX - BoxW * 0.5f + 10.0f * S();
		const float BarY = BoxY + BoxH - 11.0f * S();
		DrawRect(C.CopyWithNewOpacity(0.2f), BarX, BarY, BarW, 5.0f * S());
		DrawRect(C, BarX, BarY, BarW * Frac, 5.0f * S());
		Label(FString::Printf(TEXT("%.1fs"), Remaining), BarX + BarW + 18.0f * S(), BarY - 6.0f * S(), C, GEngine->GetTinyFont(), 0.9f, true);

		// 글리치 띠
		if (Glitch.FRand() < 0.4f)
		{
			DrawRect(C.CopyWithNewOpacity(0.35f), CX - BoxW * 0.5f, BoxY + Glitch.FRandRange(0.0f, BoxH), BoxW, 2.0f * S());
		}
	}
}

void ACoopHUD::DrawEnemyHackSession(const USisterHackComponent& Hack)
{
	const AActor* Enemy = Hack.GetActiveEnemy();
	if (!Enemy)
	{
		return;
	}
	const ICXHackTarget* Target = Cast<ICXHackTarget>(Enemy);
	const EHackFeedback Feedback = Hack.GetFeedback();
	const float FeedbackAge = Hack.GetFeedbackAge();
	const bool bMiss = Feedback == EHackFeedback::Denied && FeedbackAge < 0.45f;
	const bool bGranted = Feedback == EHackFeedback::Granted;
	const bool bFailed = Feedback == EHackFeedback::Failed;

	const FLinearColor DeniedColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor GrantedColor(0.25f, 1.0f, 0.45f, 1.0f);
	const FLinearColor Accent = bGranted ? GrantedColor : ((bMiss || bFailed) ? DeniedColor : SisterColor);

	// 빗나가면 좌우 흔들림(단말기 패널과 같은 연출)
	const float Shake = bMiss ? FMath::Sin(FeedbackAge * 70.0f) * (1.0f - FeedbackAge / 0.45f) * 10.0f * S() : 0.0f;

	const float W = 780.0f * S();
	const float H = 196.0f * S();
	const float X = (Canvas->ClipX - W) * 0.5f + Shake;
	// 전투 중이라 화면 위쪽 가운데(캐릭터·거미는 칸 아래쪽에 서 있어 하단 패널이면 가려진다 — 단말기 패널과 다른 점)
	const float Y = CoopHUDLayout::Margin * S() + 12.0f * S();

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), X, Y, W, H);
	Panel(X, Y, W, H, Accent);

	const int32 Seed = static_cast<int32>(GetWorld()->GetTimeSeconds() / 0.08f);
	FRandomStream Glitch(Seed);
	for (int32 i = 0; i < 3; ++i)
	{
		DrawRect(Accent.CopyWithNewOpacity(Glitch.FRandRange(0.03f, 0.09f)), X, Y + Glitch.FRandRange(0.0f, H), W, Glitch.FRandRange(1.0f, 4.0f) * S());
	}

	// 헤더: OVERRIDE // 대상 + 커서, 오른쪽 BYPASS n/5
	const float Pad = 20.0f * S();
	const FString Header = FString::Printf(TEXT("OVERRIDE  //  %s"), Target ? *Target->GetHackTargetName() : TEXT("TARGET"));
	Label(Header, X + Pad, Y + 12.0f * S(), Accent, GEngine->GetSmallFont(), 1.0f);
	float HW = 0.0f, HH = 0.0f;
	GetTextSize(Header, HW, HH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	if (Pulse(9.0f) > 0.5f)
	{
		DrawRect(Accent, X + Pad + HW + 6.0f * S(), Y + 14.0f * S(), 9.0f * S(), HH * 0.8f);
	}
	// 오른쪽: BYPASS n / 5(맞힌 칸 수)
	const TArray<EHackArrow>& Sequence = Hack.GetArrowSequence();
	const int32 Count = Sequence.Num();
	const int32 Done = bGranted ? Count : Hack.GetArrowIndex();
	const FString Right = FString::Printf(TEXT("BYPASS  %d / %d"), Done, Count);
	float RW = 0.0f, RH = 0.0f;
	GetTextSize(Right, RW, RH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	Label(Right, X + W - Pad - RW, Y + 12.0f * S(), Accent.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);

	// 본체: 화살표 칸(단말기 화살표 패널과 같은 문법 — 완료 채움 / 현재 펄스 / 대기 흐림)
	const float BarW = W - Pad * 2.0f;
	DrawArrowCells(Sequence, Done, bGranted, X + W * 0.5f, Y + 56.0f * S(), 74.0f * S(), 14.0f * S(), Accent);

	// 하단: 남은 시간 바(4초, 1초 이하 붉게) + 조작 안내
	const float FootY = Y + H - 34.0f * S();
	const float TimeLimit = Hack.GetEnemyHackTimeLimit();
	const float TimeLeft = Hack.GetEnemyHackTimeRemaining();
	const float TimeFrac = TimeLimit > 0.0f ? FMath::Clamp(TimeLeft / TimeLimit, 0.0f, 1.0f) : 0.0f;
	const FLinearColor TimeColor = (TimeLeft < 1.0f && !bGranted) ? DeniedColor.CopyWithNewOpacity(0.6f + 0.4f * Pulse(10.0f)) : Accent;
	DrawRect(Accent.CopyWithNewOpacity(0.15f), X + Pad, FootY - 10.0f * S(), BarW, 4.0f * S());
	DrawRect(TimeColor, X + Pad, FootY - 10.0f * S(), BarW * TimeFrac, 4.0f * S());
	Label(FString::Printf(TEXT("D-PAD  ENTER SEQUENCE        [B]  ABORT        %.1fs"), TimeLeft),
		X + W * 0.5f, FootY, FLinearColor(1.0f, 1.0f, 1.0f, 0.65f), GEngine->GetTinyFont(), 1.0f, true);

	// 판정 연출(패널 아래) — 칸마다 정답 플래시는 생략(5칸 빠른 입력이라 시끄럽다), 소리만
	if (bMiss || bGranted || bFailed)
	{
		const FString Msg = bGranted ? TEXT("ACCESS GRANTED  //  SYSTEM DOWN") : (bFailed ? TEXT("ACCESS DENIED") : TEXT("SEQUENCE RESET"));
		const FLinearColor MsgColor = (bMiss || bFailed) ? DeniedColor : GrantedColor;
		const float Fade = (bGranted || bFailed) ? 1.0f : 1.0f - FeedbackAge / 0.45f;
		DrawRect(MsgColor.CopyWithNewOpacity(0.16f * Fade), X, Y, W, H);
		Label(Msg, X + W * 0.5f, Y + H + 10.0f * S(), MsgColor.CopyWithNewOpacity(Fade), GEngine->GetLargeFont(), (bGranted || bFailed) ? 1.4f : 1.1f, true);
	}
}

void ACoopHUD::DrawArrowCells(const TArray<EHackArrow>& Sequence, int32 DoneCount, bool bAllDone, float CenterX, float Y, float Cell, float Gap, const FLinearColor& Accent)
{
	const int32 Count = Sequence.Num();
	const float RowW = Count * Cell + FMath::Max(0, Count - 1) * Gap;
	float CX = CenterX - RowW * 0.5f;
	for (int32 i = 0; i < Count; ++i)
	{
		const bool bDone = bAllDone || i < DoneCount;
		const bool bCurrent = !bAllDone && i == DoneCount;
		const FLinearColor CellColor = bDone ? Accent : (bCurrent ? Accent.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Accent.CopyWithNewOpacity(0.25f));
		if (bDone)
		{
			DrawRect(Accent.CopyWithNewOpacity(0.28f), CX, Y, Cell, Cell);
		}
		const float T = bCurrent ? 3.0f : 1.5f;
		DrawLine(CX, Y, CX + Cell, Y, CellColor, T);
		DrawLine(CX + Cell, Y, CX + Cell, Y + Cell, CellColor, T);
		DrawLine(CX + Cell, Y + Cell, CX, Y + Cell, CellColor, T);
		DrawLine(CX, Y + Cell, CX, Y, CellColor, T);
		DrawArrowGlyph(FVector2D(CX + Cell * 0.5f, Y + Cell * 0.5f), Cell * 0.3f, Sequence[i],
		               bDone || bCurrent ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.3f));
		CX += Cell + Gap;
	}
}

void ACoopHUD::DrawFreqLockBar(const USisterHackComponent& Hack, float BarX, float BarY, float BarW, float BarH, bool bGranted, bool bMiss)
{
	const FLinearColor DeniedColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor GrantedColor(0.25f, 1.0f, 0.45f, 1.0f);
	const FLinearColor Accent = bGranted ? GrantedColor : (bMiss ? DeniedColor : SisterColor);
	const float Mid = BarY + BarH * 0.5f;
	DrawRect(Accent.CopyWithNewOpacity(0.07f), BarX, BarY, BarW, BarH);
	DrawLine(BarX, BarY, BarX + BarW, BarY, Accent.CopyWithNewOpacity(0.5f), 1.0f);
	DrawLine(BarX, BarY + BarH, BarX + BarW, BarY + BarH, Accent.CopyWithNewOpacity(0.5f), 1.0f);

	// 파형(배경 장식) — 성능 최적화: 선분 64 → 20, 두께 1(얇은 선은 Canvas가 단순 라인으로 일괄 처리),
	// 느린 흐름으로 바꿔 빠른 커서와 겹쳐 보일 때의 끊김(저더)도 줄인다.
	const float Now = GetWorld()->GetTimeSeconds();
	constexpr int32 Segs = 20;
	const float Amp = BarH * 0.26f;
	const FLinearColor WaveColor = Accent.CopyWithNewOpacity(0.22f);
	FVector2D Prev(BarX, Mid + FMath::Sin(-Now * 2.0f) * Amp);
	for (int32 i = 1; i <= Segs; ++i)
	{
		const float T = static_cast<float>(i) / Segs;
		const FVector2D Cur(BarX + BarW * T, Mid + FMath::Sin(T * 18.0f - Now * 2.0f) * Amp);
		DrawLine(Prev.X, Prev.Y, Cur.X, Cur.Y, WaveColor, 1.0f);
		Prev = Cur;
	}

	// 목표 구간
	const float ZoneW = Hack.GetFreqZoneWidth() * BarW;
	const float ZoneX = BarX + Hack.GetFreqZoneCenter() * BarW - ZoneW * 0.5f;
	const float Cursor = Hack.GetFreqCursor();
	const bool bInside = FMath::Abs(Cursor - Hack.GetFreqZoneCenter()) <= Hack.GetFreqZoneWidth() * 0.5f;
	const FLinearColor ZoneColor = bGranted ? GrantedColor : SisterColor;
	DrawRect(ZoneColor.CopyWithNewOpacity(bInside ? 0.45f : 0.24f), ZoneX, BarY, ZoneW, BarH);
	DrawLine(ZoneX, BarY - 6.0f * S(), ZoneX, BarY + BarH + 6.0f * S(), ZoneColor, 2.5f);
	DrawLine(ZoneX + ZoneW, BarY - 6.0f * S(), ZoneX + ZoneW, BarY + BarH + 6.0f * S(), ZoneColor, 2.5f);
	Label(TEXT("LOCK"), ZoneX + ZoneW * 0.5f, BarY - 22.0f * S(), ZoneColor, GEngine->GetTinyFont(), 1.0f, true);

	// 커서(흰 세로선 + 위아래 삼각) — 구간 안이면 굵게
	const float CursorX = BarX + Cursor * BarW;
	const FLinearColor CursorColor = bMiss ? DeniedColor : FLinearColor::White;
	DrawLine(CursorX, BarY - 8.0f * S(), CursorX, BarY + BarH + 8.0f * S(), CursorColor, bInside ? 4.0f : 2.5f);
	const float Tri = 7.0f * S();
	DrawLine(CursorX - Tri, BarY - 8.0f * S() - Tri, CursorX, BarY - 8.0f * S(), CursorColor, 2.0f);
	DrawLine(CursorX + Tri, BarY - 8.0f * S() - Tri, CursorX, BarY - 8.0f * S(), CursorColor, 2.0f);
	DrawLine(CursorX - Tri, BarY + BarH + 8.0f * S() + Tri, CursorX, BarY + BarH + 8.0f * S(), CursorColor, 2.0f);
	DrawLine(CursorX + Tri, BarY + BarH + 8.0f * S() + Tri, CursorX, BarY + BarH + 8.0f * S(), CursorColor, 2.0f);
}

void ACoopHUD::DrawZoneBanner()
{
	const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
	if (Elapsed > ZoneBannerDuration)
	{
		return;
	}

	// 0.5초 페이드 인, 마지막 1초 페이드 아웃
	const float Alpha = FMath::Clamp(FMath::Min(Elapsed / 0.5f, (ZoneBannerDuration - Elapsed) / 1.0f), 0.0f, 1.0f);
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = 34.0f * S();

	Label(TEXT("DISTRICT 5"), CX, Y, FLinearColor(0.7f, 0.8f, 1.0f, 0.7f * Alpha), GEngine->GetSmallFont(), 1.0f, true);
	Label(ZoneTitle, CX, Y + 22.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, Alpha), GEngine->GetLargeFont(), 1.3f, true);

	float TW = 0.0f, TH = 0.0f;
	GetTextSize(ZoneTitle, TW, TH, GEngine->GetLargeFont(), 1.3f * S() * CoopHUDLayout::TextScale);
	const float LineY = Y + 22.0f * S() + TH * 0.55f;
	const float LineLen = 120.0f * S() * Alpha;
	const FLinearColor LineColor = FLinearColor(0.7f, 0.8f, 1.0f, 0.6f * Alpha);
	DrawLine(CX - TW * 0.5f - 16.0f * S() - LineLen, LineY, CX - TW * 0.5f - 16.0f * S(), LineY, LineColor, 1.5f);
	DrawLine(CX + TW * 0.5f + 16.0f * S(), LineY, CX + TW * 0.5f + 16.0f * S() + LineLen, LineY, LineColor, 1.5f);
}

// ---------------------------------------------------------------- 세이브포인트 / 맵 이동(세이브포인트·맵 이동 기획 5장)

void ACoopHUD::DrawFlowNotices(const APawn* MyPawn, bool bDead)
{
	const float CX = Canvas->ClipX * 0.5f;

	// 1) PROGRESS SAVED — 가장 최근에 저장한 세이브포인트 하나(두 칸 모두, 구역 배너 아래)
	const ACXSavePoint* Latest = nullptr;
	float LatestAge = -1.0f;
	for (TActorIterator<ACXSavePoint> It(GetWorld()); It; ++It)
	{
		const float Age = It->GetTimeSinceSaved();
		if (Age >= 0.0f && Age < SaveBannerDuration && (!Latest || Age < LatestAge))
		{
			Latest = *It;
			LatestAge = Age;
		}
	}
	if (Latest)
	{
		const float Alpha = FMath::Clamp(FMath::Min(LatestAge / 0.3f, (SaveBannerDuration - LatestAge) / 0.6f), 0.0f, 1.0f);
		const float W = 470.0f * S();
		const float H = 70.0f * S();
		const float X = CX - W * 0.5f;
		const float Y = 112.0f * S();
		DrawRect(PanelColor.CopyWithNewOpacity(PanelColor.A * Alpha), X, Y, W, H);
		DrawRect(SaveColor.CopyWithNewOpacity(0.9f * Alpha), X, Y, W, 2.0f * S());
		DrawRect(SaveColor.CopyWithNewOpacity(0.9f * Alpha), X, Y + H - 2.0f * S(), W, 2.0f * S());
		// 저장 직후 0.4초 밝은 띠가 패널을 훑는다
		if (LatestAge < 0.4f)
		{
			const float SweepX = X + W * (LatestAge / 0.4f);
			DrawRect(SaveColor.CopyWithNewOpacity(0.35f * Alpha), SweepX, Y, 18.0f * S(), H);
		}
		Label(FString::Printf(TEXT("SAVE POINT  //  %s"), *Latest->ResolveZoneName()), CX, Y + 6.0f * S(), SaveColor.CopyWithNewOpacity(0.85f * Alpha), GEngine->GetSmallFont(), 1.0f, true);
		Label(TEXT("PROGRESS SAVED"), CX, Y + 24.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, Alpha), GEngine->GetLargeFont(), 1.15f, true);
	}

	// 2) NEXT AREA OFFLINE — 다음 맵이 아직 없는 출구에 전원이 들어왔을 때(두 칸 모두)
	for (TActorIterator<ACXLevelExit> It(GetWorld()); It; ++It)
	{
		const float Age = It->GetOfflineNoticeAge();
		const float Duration = 3.0f;
		if (Age < 0.0f || Age >= Duration)
		{
			continue;
		}
		const float Alpha = FMath::Clamp(FMath::Min(Age / 0.2f, (Duration - Age) / 0.5f), 0.0f, 1.0f);
		const float Y = Canvas->ClipY * 0.20f;	// 출구 표지(화면 중앙 위)를 가리지 않게
		const float W = 560.0f * S();
		DrawRect(PanelColor.CopyWithNewOpacity(PanelColor.A * Alpha), CX - W * 0.5f, Y - 8.0f * S(), W, 74.0f * S());
		DrawRect(WarningColor.CopyWithNewOpacity(0.9f * Alpha), CX - W * 0.5f, Y - 8.0f * S(), 4.0f * S(), 74.0f * S());
		DrawRect(WarningColor.CopyWithNewOpacity(0.9f * Alpha), CX + W * 0.5f - 4.0f * S(), Y - 8.0f * S(), 4.0f * S(), 74.0f * S());
		const float Blink = 0.7f + 0.3f * Pulse(6.0f);
		Label(TEXT("NEXT AREA OFFLINE"), CX, Y, DangerColor.CopyWithNewOpacity(Alpha * Blink), GEngine->GetLargeFont(), 1.2f, true);
		Label(TEXT("ROUTE NOT CONNECTED  //  HOLD POSITION"), CX, Y + 40.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.7f * Alpha), GEngine->GetTinyFont(), 1.0f, true);
		break;
	}

	// 3) WAITING FOR PARTNER — 출구 구역 안에 있는 사람 칸에만
	if (!bDead && MyPawn)
	{
		for (TActorIterator<ACXLevelExit> It(GetWorld()); It; ++It)
		{
			const FString Wait = It->GetWaitText(MyPawn);
			if (Wait.IsEmpty())
			{
				continue;
			}
			const float Y = Canvas->ClipY - 96.0f * S();
			float TW = 0.0f, TH = 0.0f;
			GetTextSize(Wait, TW, TH, GEngine->GetMediumFont(), S() * CoopHUDLayout::TextScale);
			const float W = TW + 60.0f * S();
			DrawRect(PanelColor, CX - W * 0.5f, Y - 6.0f * S(), W, TH + 12.0f * S());
			Label(Wait, CX, Y, WarningColor.CopyWithNewOpacity(0.6f + 0.4f * Pulse(3.0f)), GEngine->GetMediumFont(), 1.0f, true);
			break;
		}
	}
}

void ACoopHUD::DrawTravelFade()
{
	const APlayerCameraManager* Camera = PlayerOwner ? PlayerOwner->PlayerCameraManager.Get() : nullptr;
	if (Camera && Camera->bEnableFading && Camera->FadeAmount > 0.001f)
	{
		DrawRect(Camera->FadeColor.CopyWithNewOpacity(FMath::Clamp(Camera->FadeAmount, 0.0f, 1.0f)), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	}
}

void ACoopHUD::DrawSplitEdge(const FLinearColor& Color)
{
	const ULocalPlayer* LP = PlayerOwner ? PlayerOwner->GetLocalPlayer() : nullptr;
	if (!LP)
	{
		return;
	}

	// 분할선과 맞닿은 쪽 가장자리에만 플레이어 색 라인을 그어 두 화면을 구분한다.
	const FLinearColor EdgeColor = Color.CopyWithNewOpacity(0.7f);
	const float Thickness = 2.0f * S();
	if (LP->Origin.Y > 0.01f)
	{
		DrawRect(EdgeColor, 0.0f, 0.0f, Canvas->ClipX, Thickness);
	}
	else if (LP->Size.Y < 0.99f)
	{
		DrawRect(EdgeColor, 0.0f, Canvas->ClipY - Thickness, Canvas->ClipX, Thickness);
	}
	// 좌우 분할(설정 메뉴 4.3): 세로 분할선 쪽
	else if (LP->Origin.X > 0.01f)
	{
		DrawRect(EdgeColor, 0.0f, 0.0f, Thickness, Canvas->ClipY);
	}
	else if (LP->Size.X < 0.99f)
	{
		DrawRect(EdgeColor, Canvas->ClipX - Thickness, 0.0f, Thickness, Canvas->ClipY);
	}
}

void ACoopHUD::DrawHackSession(const USisterHackComponent& Hack)
{
	const AHackTerminal* Terminal = Hack.GetActiveTerminal();
	if (!Terminal)
	{
		return;
	}

	if (Terminal->PuzzleType == EHackPuzzleType::MemoryPattern)
	{
		DrawMemoryHackSession(Hack, *Terminal);
		return;
	}

	// 단말기 퍼즐: 비밀번호 / 주파수 동조(메모리 패턴은 위에서 따로). 화살표는 적 해킹 전용(DrawEnemyHackSession).
	const bool bFreq = Terminal->PuzzleType == EHackPuzzleType::FrequencyLock;
	const EHackFeedback Feedback = Hack.GetFeedback();
	const float FeedbackAge = Hack.GetFeedbackAge();
	const bool bDenied = Feedback == EHackFeedback::Denied && FeedbackAge < 0.6f;
	const bool bRoundClear = Feedback == EHackFeedback::RoundClear && FeedbackAge < 0.6f;
	const bool bHitFlash = bFreq && Feedback == EHackFeedback::Correct && FeedbackAge < 0.3f;
	const bool bGranted = Feedback == EHackFeedback::Granted;

	const FLinearColor DeniedColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor GrantedColor(0.25f, 1.0f, 0.45f, 1.0f);
	const FLinearColor Accent = bGranted ? GrantedColor : (bDenied ? DeniedColor : SisterColor);

	// 실패 시 좌우 흔들림
	const float Shake = bDenied ? FMath::Sin(FeedbackAge * 70.0f) * (1.0f - FeedbackAge / 0.6f) * 10.0f * S() : 0.0f;

	const float W = 780.0f * S();
	const float H = 230.0f * S();
	const float X = (Canvas->ClipX - W) * 0.5f + Shake;
	const float Y = Canvas->ClipY - CoopHUDLayout::Margin * S() - H;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), X, Y, W, H);
	Panel(X, Y, W, H, Accent);

	// 글리치 스캔 라인: 0.08초마다 위치가 바뀌는 가로 띠 3개
	const int32 Seed = static_cast<int32>(GetWorld()->GetTimeSeconds() / 0.08f);
	FRandomStream Glitch(Seed);
	for (int32 i = 0; i < 3; ++i)
	{
		DrawRect(Accent.CopyWithNewOpacity(Glitch.FRandRange(0.03f, 0.09f)), X, Y + Glitch.FRandRange(0.0f, H), W, Glitch.FRandRange(1.0f, 4.0f) * S());
	}

	// 헤더: BREACHING // 이름 + 깜빡이는 커서, 오른쪽 라운드
	const float Pad = 20.0f * S();
	const FString Header = FString::Printf(TEXT("BREACHING  //  %s"), *Terminal->TerminalName);
	Label(Header, X + Pad, Y + 12.0f * S(), Accent, GEngine->GetSmallFont(), 1.0f);
	float HW = 0.0f, HH = 0.0f;
	GetTextSize(Header, HW, HH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	if (Pulse(9.0f) > 0.5f)
	{
		DrawRect(Accent, X + Pad + HW + 6.0f * S(), Y + 14.0f * S(), 9.0f * S(), HH * 0.8f);
	}
	const int32 FreqSteps = Hack.GetFreqStepCount();
	const int32 FreqDone = bGranted ? FreqSteps : Hack.GetFreqStep();
	const FString Right = bFreq ? FString::Printf(TEXT("FREQ LOCK  %d / %d"), FreqDone, FreqSteps) : FString(TEXT("PASSCODE  4-DIGIT"));
	float RW = 0.0f, RH = 0.0f;
	GetTextSize(Right, RW, RH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	float RightX = X + W - Pad - RW;
	if (bFreq)
	{
		// 오른쪽 위 단계 칸(채움 = 고정 완료) + 글자
		const float Pip = 14.0f * S();
		const float PipGap = 6.0f * S();
		float PipX = X + W - Pad - FreqSteps * Pip - (FreqSteps - 1) * PipGap;
		for (int32 i = 0; i < FreqSteps; ++i)
		{
			const FLinearColor PC = Accent.CopyWithNewOpacity(i < FreqDone ? 1.0f : 0.25f);
			const float PY = Y + 16.0f * S();
			if (i < FreqDone)
			{
				DrawRect(PC, PipX, PY, Pip, Pip);
			}
			else
			{
				DrawLine(PipX, PY, PipX + Pip, PY, PC, 1.5f);
				DrawLine(PipX + Pip, PY, PipX + Pip, PY + Pip, PC, 1.5f);
				DrawLine(PipX + Pip, PY + Pip, PipX, PY + Pip, PC, 1.5f);
				DrawLine(PipX, PY + Pip, PipX, PY, PC, 1.5f);
			}
			PipX += Pip + PipGap;
		}
		RightX -= FreqSteps * (Pip + PipGap) + 10.0f * S();
	}
	Label(Right, RightX, Y + 12.0f * S(), Accent.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);

	const float BodyY = Y + 58.0f * S();
	if (bFreq)
	{
		// 주파수 막대(적 해킹 시절과 같은 그림 — DrawFreqLockBar 공용)
		DrawFreqLockBar(Hack, X + Pad, BodyY + 22.0f * S(), W - Pad * 2.0f, 64.0f * S(), bGranted, bDenied);
	}
	else
	{
		// 비밀번호 4칸: 현재 칸은 위아래 셰브론으로 변경 가능함을 표시
		const TArray<int32>& Digits = Hack.GetPasscodeInput();
		const float BoxW = 86.0f * S();
		const float BoxH = 100.0f * S();
		const float Gap = 22.0f * S();
		const float RowW = 4 * BoxW + 3 * Gap;
		float BX = X + (W - RowW) * 0.5f;
		for (int32 i = 0; i < 4; ++i)
		{
			const bool bCursor = !bGranted && i == Hack.GetPasscodeCursor();
			const FLinearColor BoxColor = bCursor ? Accent.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Accent.CopyWithNewOpacity(0.45f);
			DrawRect(Accent.CopyWithNewOpacity(bCursor ? 0.22f : 0.08f), BX, BodyY + 8.0f * S(), BoxW, BoxH);
			DrawLine(BX, BodyY + 8.0f * S() + BoxH, BX + BoxW, BodyY + 8.0f * S() + BoxH, BoxColor, bCursor ? 3.0f : 1.5f);
			const FString D = Digits.IsValidIndex(i) ? FString::FromInt(Digits[i]) : FString(TEXT("-"));
			Label(D, BX + BoxW * 0.5f, BodyY + 22.0f * S(), FLinearColor::White, GEngine->GetLargeFont(), 1.9f, true);
			if (bCursor)
			{
				DrawArrowGlyph(FVector2D(BX + BoxW * 0.5f, BodyY - 4.0f * S()), 8.0f * S(), EHackArrow::Up, Accent);
				DrawArrowGlyph(FVector2D(BX + BoxW * 0.5f, BodyY + BoxH + 22.0f * S()), 8.0f * S(), EHackArrow::Down, Accent);
			}
			BX += BoxW + Gap;
		}
	}

	// 하단: 진행 바(주파수 동조 — 제한 시간이 있으면 남은 시간, 없으면 단계 진행) + 조작 안내
	const float FootY = Y + H - 34.0f * S();
	FString TimeText;
	if (bFreq)
	{
		const float BarW = W - Pad * 2.0f;
		const float Limit = Hack.GetFreqTimeLimit();
		const float Frac = Limit > 0.0f ? FMath::Clamp(Hack.GetFreqTimeRemaining() / Limit, 0.0f, 1.0f) : Hack.GetProgress();
		DrawRect(Accent.CopyWithNewOpacity(0.15f), X + Pad, FootY - 10.0f * S(), BarW, 4.0f * S());
		DrawRect(Accent, X + Pad, FootY - 10.0f * S(), BarW * Frac, 4.0f * S());
		if (Limit > 0.0f)
		{
			TimeText = FString::Printf(TEXT("        %.1fs"), Hack.GetFreqTimeRemaining());
		}
	}
	const FString Hint = bFreq
		? FString::Printf(TEXT("%s  LOCK SIGNAL        [B]  ABORT%s"), *HackKeyHint, *TimeText)
		: FString::Printf(TEXT("D-PAD UP/DOWN  DIGIT      LEFT/RIGHT  MOVE      %s  SUBMIT      [B]  ABORT"), *HackKeyHint);
	Label(Hint, X + W * 0.5f, FootY, FLinearColor(1.0f, 1.0f, 1.0f, 0.65f), GEngine->GetTinyFont(), 1.0f, true);

	// 판정 연출
	if (bDenied || bRoundClear || bGranted || bHitFlash)
	{
		const FString Msg = bGranted ? TEXT("ACCESS GRANTED") : (bDenied ? (bFreq ? TEXT("DESYNC") : TEXT("ACCESS DENIED")) : (bHitFlash ? TEXT("LOCKED") : TEXT("ROUND CLEAR")));
		const FLinearColor MsgColor = bDenied ? DeniedColor : GrantedColor;
		const float Fade = bGranted ? 1.0f : 1.0f - FeedbackAge / (bHitFlash ? 0.3f : 0.6f);
		DrawRect(MsgColor.CopyWithNewOpacity(0.18f * Fade), X, Y, W, H);
		Label(Msg, X + W * 0.5f, Y - 46.0f * S(), MsgColor.CopyWithNewOpacity(Fade), GEngine->GetLargeFont(), 1.4f, true);
	}
}

void ACoopHUD::DrawArrowGlyph(const FVector2D& Center, float Size, EHackArrow Direction, const FLinearColor& Color)
{
	FVector2D Dir;
	switch (Direction)
	{
	case EHackArrow::Up:    Dir = FVector2D(0.0f, -1.0f); break;
	case EHackArrow::Down:  Dir = FVector2D(0.0f, 1.0f); break;
	case EHackArrow::Left:  Dir = FVector2D(-1.0f, 0.0f); break;
	default:                Dir = FVector2D(1.0f, 0.0f); break;
	}
	const FVector2D Side(-Dir.Y, Dir.X);
	const FVector2D Tip = Center + Dir * Size;
	const FVector2D Tail = Center - Dir * Size;
	const FVector2D HeadBase = Center + Dir * (Size * 0.1f);
	const float Th = FMath::Max(2.0f, Size * 0.18f);
	DrawLine(Tail.X, Tail.Y, Tip.X, Tip.Y, Color, Th);
	const FVector2D A = HeadBase + Side * (Size * 0.75f);
	const FVector2D B = HeadBase - Side * (Size * 0.75f);
	DrawLine(Tip.X, Tip.Y, A.X, A.Y, Color, Th);
	DrawLine(Tip.X, Tip.Y, B.X, B.Y, Color, Th);
}

// ---------------------------------------------------------------- 메모리 패턴(해킹 시스템 기획 11장)

void ACoopHUD::DrawMemoryNode(float X, float Y, float Cell, int32 Node, const FLinearColor& Accent, float Dim, float Flash, bool bCursor, bool bPressed)
{
	// 바탕(어두운 칸) + 테두리 + 숫자(반투명 — 전화 키패드 1~9). 점멸 중이면 칸을 밝게 채우고 숫자는 흰색으로 또렷하게.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Dim), X, Y, Cell, Cell);
	DrawRect(Accent.CopyWithNewOpacity((bCursor ? 0.2f : 0.07f) * Dim), X, Y, Cell, Cell);
	if (Flash > 0.0f)
	{
		const float Glow = 6.0f * S() * Flash;
		DrawRect(Accent.CopyWithNewOpacity(0.25f * Flash), X - Glow, Y - Glow, Cell + Glow * 2.0f, Cell + Glow * 2.0f);
		DrawRect(Accent.CopyWithNewOpacity(0.9f * Flash), X, Y, Cell, Cell);
	}
	if (bPressed)
	{
		DrawRect(Accent.CopyWithNewOpacity(0.45f), X, Y, Cell, Cell);
	}

	const FLinearColor Edge = Flash > 0.0f ? FLinearColor::White
		: (bCursor ? Accent.CopyWithNewOpacity(0.65f + 0.35f * Pulse(8.0f)) : Accent.CopyWithNewOpacity(0.4f * Dim));
	const float T = (Flash > 0.0f || bCursor) ? 3.0f : 1.5f;
	DrawLine(X, Y, X + Cell, Y, Edge, T);
	DrawLine(X + Cell, Y, X + Cell, Y + Cell, Edge, T);
	DrawLine(X + Cell, Y + Cell, X, Y + Cell, Edge, T);
	DrawLine(X, Y + Cell, X, Y, Edge, T);
	if (bCursor)
	{
		Bracket(FVector2D(X + Cell * 0.5f, Y + Cell * 0.5f), Cell * 0.5f + 5.0f * S(), Cell * 0.22f, Accent, 2.0f);
	}

	// 숫자: 평소 반투명(35%), 커서/점멸이면 또렷하게. 칸 크기에 맞춰 글자 배율 조절(기준 칸 76 → 1.9배).
	const float NumAlpha = Flash > 0.0f ? 1.0f : ((bCursor || bPressed) ? 0.9f : 0.35f * Dim);
	const FLinearColor NumColor = Flash > 0.0f ? FLinearColor(0.02f, 0.05f, 0.08f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, NumAlpha);
	const FString Num = FString::FromInt(Node + 1);
	const float Scale = FMath::Clamp(Cell / S() / 40.0f, 0.8f, 2.2f);
	float TW = 0.0f, TH = 0.0f;
	GetTextSize(Num, TW, TH, GEngine->GetLargeFont(), Scale * S() * CoopHUDLayout::TextScale);
	if (Flash > 0.0f)
	{
		// 밝은 칸 위에서는 그림자 없는 어두운 숫자가 더 잘 읽힌다
		DrawText(Num, NumColor, X + (Cell - TW) * 0.5f, Y + (Cell - TH) * 0.5f, GEngine->GetLargeFont(), Scale * S() * CoopHUDLayout::TextScale);
	}
	else
	{
		Label(Num, X + Cell * 0.5f, Y + (Cell - TH) * 0.5f, NumColor, GEngine->GetLargeFont(), Scale, true);
	}
}

void ACoopHUD::DrawMemoryHackSession(const USisterHackComponent& Hack, const AHackTerminal& Terminal)
{
	const EHackFeedback Feedback = Hack.GetFeedback();
	const float FeedbackAge = Hack.GetFeedbackAge();
	const bool bDenied = Feedback == EHackFeedback::Denied && FeedbackAge < 0.6f;
	const bool bRoundClear = Feedback == EHackFeedback::RoundClear && FeedbackAge < 0.6f;
	const bool bGranted = Feedback == EHackFeedback::Granted;

	const FLinearColor DeniedColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor GrantedColor(0.25f, 1.0f, 0.45f, 1.0f);
	const FLinearColor Accent = bGranted ? GrantedColor : (bDenied ? DeniedColor : SisterColor);
	const float Shake = bDenied ? FMath::Sin(FeedbackAge * 70.0f) * (1.0f - FeedbackAge / 0.6f) * 10.0f * S() : 0.0f;

	const float W = 780.0f * S();
	const float H = 262.0f * S();
	const float X = (Canvas->ClipX - W) * 0.5f + Shake;
	const float Y = Canvas->ClipY - CoopHUDLayout::Margin * S() - H;
	const float Pad = 20.0f * S();

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), X, Y, W, H);
	Panel(X, Y, W, H, Accent);
	FRandomStream Glitch(static_cast<int32>(GetWorld()->GetTimeSeconds() / 0.08f));
	for (int32 i = 0; i < 3; ++i)
	{
		DrawRect(Accent.CopyWithNewOpacity(Glitch.FRandRange(0.03f, 0.09f)), X, Y + Glitch.FRandRange(0.0f, H), W, Glitch.FRandRange(1.0f, 4.0f) * S());
	}

	// 헤더
	const FString Header = FString::Printf(TEXT("BREACHING  //  %s"), *Terminal.TerminalName);
	Label(Header, X + Pad, Y + 12.0f * S(), Accent, GEngine->GetSmallFont(), 1.0f);
	float HW = 0.0f, HH = 0.0f;
	GetTextSize(Header, HW, HH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	if (Pulse(9.0f) > 0.5f)
	{
		DrawRect(Accent, X + Pad + HW + 6.0f * S(), Y + 14.0f * S(), 9.0f * S(), HH * 0.8f);
	}
	const FString Right = FString::Printf(TEXT("GHOST ECHO   ROUND %d / %d"), FMath::Min(Terminal.GetMemoryRound() + 1, Terminal.GetMemoryRoundCount()), Terminal.GetMemoryRoundCount());
	float RW = 0.0f, RH = 0.0f;
	GetTextSize(Right, RW, RH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	Label(Right, X + W - Pad - RW, Y + 12.0f * S(), Accent.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);

	// 3x3 숫자판(점멸 없음 — 패턴은 오빠 칸에만). 커서 칸 강조, 방금 보낸 노드는 짧게 채움.
	const float Cell = 54.0f * S();
	const float Gap = 8.0f * S();
	const float GX = X + Pad + 16.0f * S();
	const float GY = Y + 50.0f * S();
	const bool bPressFlash = (Feedback == EHackFeedback::Correct || Feedback == EHackFeedback::RoundClear || bGranted) && FeedbackAge < 0.35f;
	for (int32 Node = 0; Node < 9; ++Node)
	{
		const float CX = GX + (Node % 3) * (Cell + Gap);
		const float CY = GY + (Node / 3) * (Cell + Gap);
		DrawMemoryNode(CX, CY, Cell, Node, Accent, 1.0f, 0.0f, !bGranted && Node == Hack.GetMemoryCursor(), bPressFlash && Node == Hack.GetMemoryLastPressed());
	}

	// 오른쪽: 입력 진행 칸 + 중계 상태
	const float RX = GX + 3.0f * Cell + 2.0f * Gap + 44.0f * S();
	const int32 Count = Terminal.GetMemorySequence().Num();
	const int32 Done = bGranted ? Count : Terminal.GetMemoryInputIndex();
	Label(TEXT("PATTERN INPUT"), RX, Y + 54.0f * S(), Accent.CopyWithNewOpacity(0.75f), GEngine->GetTinyFont(), 1.0f);
	const float Slot = 30.0f * S();
	for (int32 i = 0; i < Count; ++i)
	{
		const float SX = RX + i * (Slot + 8.0f * S());
		const float SY = Y + 78.0f * S();
		const bool bFilled = i < Done;
		const bool bNext = !bGranted && i == Done;
		DrawRect(Accent.CopyWithNewOpacity(bFilled ? 0.85f : 0.1f), SX, SY, Slot, Slot);
		const FLinearColor E = bNext ? Accent.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Accent.CopyWithNewOpacity(0.45f);
		DrawLine(SX, SY + Slot, SX + Slot, SY + Slot, E, bNext ? 3.0f : 1.5f);
	}

	const APawn* Relay = Terminal.FindRelayPawn();
	const UCXPlayerLifeComponent* RelayLife = Relay ? Relay->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
	const float StatusY = Y + 128.0f * S();
	if (bGranted)
	{
		Label(TEXT("PATTERN ACCEPTED"), RX, StatusY, GrantedColor, GEngine->GetSmallFont(), 1.0f);
	}
	else if (Terminal.IsMemoryRelayActive())
	{
		Label(TEXT("PARTNER RELAY  //  LINKED"), RX, StatusY, Accent, GEngine->GetSmallFont(), 1.0f);
		Label(TEXT("BROTHER SEES THE PATTERN - ENTER HIS NUMBERS IN ORDER"), RX, StatusY + 30.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), GEngine->GetTinyFont(), 1.0f);
	}
	else
	{
		Label(TEXT("PARTNER RELAY REQUIRED"), RX, StatusY, WarningColor.CopyWithNewOpacity(0.55f + 0.45f * Pulse(6.0f)), GEngine->GetSmallFont(), 1.0f);
		FString Sub;
		if (!Relay)
		{
			Sub = TEXT("NO PARTNER SIGNAL");
		}
		else if (RelayLife && !RelayLife->IsAlive())
		{
			Sub = TEXT("BROTHER SIGNAL LOST - PATTERN ON HOLD");
		}
		else
		{
			Sub = FString::Printf(TEXT("BROTHER %.1fm  -  RELAY RANGE %.0fm"), FVector::Dist(Relay->GetActorLocation(), Terminal.GetActorLocation()) / 100.0f, Terminal.MemoryRelayRange / 100.0f);
		}
		Label(Sub, RX, StatusY + 30.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), GEngine->GetTinyFont(), 1.0f);
	}

	// 하단: 진행 바 + 조작 안내
	const float FootY = Y + H - 34.0f * S();
	const float BarX = RX;
	const float BarW = X + W - Pad - RX;
	DrawRect(Accent.CopyWithNewOpacity(0.15f), BarX, FootY - 14.0f * S(), BarW, 4.0f * S());
	DrawRect(Accent, BarX, FootY - 14.0f * S(), BarW * Hack.GetProgress(), 4.0f * S());
	Label(FString::Printf(TEXT("D-PAD  MOVE      %s  SELECT NODE      [B]  ABORT"), *HackKeyHint), BarX, FootY, FLinearColor(1.0f, 1.0f, 1.0f, 0.65f), GEngine->GetTinyFont(), 1.0f);

	if (bDenied || bRoundClear || bGranted)
	{
		const FString Msg = bGranted ? TEXT("ACCESS GRANTED") : (bDenied ? TEXT("ACCESS DENIED") : TEXT("ROUND CLEAR"));
		const FLinearColor MsgColor = bDenied ? DeniedColor : GrantedColor;
		const float Fade = bGranted ? 1.0f : 1.0f - FeedbackAge / 0.6f;
		DrawRect(MsgColor.CopyWithNewOpacity(0.18f * Fade), X, Y, W, H);
		Label(Msg, X + W * 0.5f, Y - 46.0f * S(), MsgColor.CopyWithNewOpacity(Fade), GEngine->GetLargeFont(), 1.4f, true);
	}
}

void ACoopHUD::DrawMemoryRelay(const APawn& MyPawn)
{
	// 진행 중인 메모리 패턴 단말기(또는 방금 성공해 결과만 보여 주는 단말기) 중 가장 가까운 것
	const AHackTerminal* Terminal = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AActor>& Weak : HackTerminals)
	{
		const AHackTerminal* T = Cast<AHackTerminal>(Weak.Get());
		if (!T || !T->IsMemoryPattern())
		{
			continue;
		}
		const bool bShowResult = T->GetState() == EHackTerminalState::Hacked && T->GetMemoryLastEvent() == EHackFeedback::Granted && T->GetMemoryEventAge() < 1.5f;
		if (!T->IsMemorySessionActive() && !bShowResult)
		{
			continue;
		}
		const float D = FVector::Dist(MyPawn.GetActorLocation(), T->GetActorLocation());
		if (D < BestDist)
		{
			Terminal = T;
			BestDist = D;
		}
	}
	if (!Terminal)
	{
		LastMemoryFlashKey = -1;
		return;
	}

	const EHackFeedback Event = Terminal->GetMemoryLastEvent();
	const float EventAge = Terminal->GetMemoryEventAge();
	const bool bGranted = Event == EHackFeedback::Granted;
	const bool bDenied = Event == EHackFeedback::Denied && EventAge < 1.0f;
	const bool bRoundClear = Event == EHackFeedback::RoundClear && EventAge < 1.0f;
	const bool bLinked = Terminal->IsMemoryRelayActive();
	const FLinearColor DeniedColor(1.0f, 0.12f, 0.08f, 1.0f);
	const FLinearColor GrantedColor(0.25f, 1.0f, 0.45f, 1.0f);
	const FLinearColor Accent = bGranted ? GrantedColor : (bDenied ? DeniedColor : (bLinked ? BrotherColor : WarningColor));
	const float Shake = bDenied ? FMath::Sin(EventAge * 70.0f) * FMath::Max(0.0f, 1.0f - EventAge / 0.6f) * 8.0f * S() : 0.0f;

	// 패널: 오빠 칸 오른쪽(캐릭터는 칸 가운데 아래에 서 있고, 왼쪽 아래는 탄약 패널).
	// 오른쪽 가장자리에서 70 띄운다 — 파트너가 화면 밖이면 가장자리에 파트너 표시(SISTER ▶)가 붙는다.
	const float Cell = 76.0f * S();
	const float Gap = 10.0f * S();
	const float Grid = 3.0f * Cell + 2.0f * Gap;
	const float W = 330.0f * S();
	const float H = 392.0f * S();
	const float X = Canvas->ClipX - (CoopHUDLayout::Margin + 70.0f) * S() - W + Shake;
	const float Y = FMath::Max(CoopHUDLayout::Margin * S(), (Canvas->ClipY - H) * 0.5f);
	const float Pad = 18.0f * S();

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), X, Y, W, H);
	Panel(X, Y, W, H, Accent);
	FRandomStream Glitch(static_cast<int32>(GetWorld()->GetTimeSeconds() / 0.08f) + 17);
	for (int32 i = 0; i < 2; ++i)
	{
		DrawRect(Accent.CopyWithNewOpacity(Glitch.FRandRange(0.03f, 0.08f)), X, Y + Glitch.FRandRange(0.0f, H), W, Glitch.FRandRange(1.0f, 3.0f) * S());
	}

	Label(TEXT("PATTERN RELAY"), X + Pad, Y + 10.0f * S(), Accent, GEngine->GetSmallFont(), 1.0f);
	const FString Round = FString::Printf(TEXT("R %d/%d"), FMath::Min(Terminal->GetMemoryRound() + 1, Terminal->GetMemoryRoundCount()), Terminal->GetMemoryRoundCount());
	float RW = 0.0f, RH = 0.0f;
	GetTextSize(Round, RW, RH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	Label(Round, X + W - Pad - RW, Y + 10.0f * S(), Accent.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);
	Label(FString::Printf(TEXT("GHOST ECHO  //  %s"), *Terminal->TerminalName), X + Pad, Y + 38.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), GEngine->GetTinyFont(), 1.0f);

	// 점멸(서버 시각 기준 — 여동생 칸에는 그리지 않는다)
	int32 Step = INDEX_NONE;
	float FlashAlpha = 0.0f, Wait = 0.0f;
	const int32 FlashNode = Terminal->GetMemoryFlashNode(Step, FlashAlpha, Wait);
	if (FlashNode != INDEX_NONE)
	{
		if (Step != LastMemoryFlashKey && MemoryFlashSound && MyPawn.IsLocallyControlled())
		{
			// 노드가 켜지는 순간 같은 피치의 짧은 틱(박자 안내만 — 노드별 음 없음)
			UGameplayStatics::PlaySound2D(this, MemoryFlashSound, 0.6f, 1.25f);
		}
		LastMemoryFlashKey = Step;
	}
	else
	{
		LastMemoryFlashKey = -1;
	}

	const float GX = X + (W - Grid) * 0.5f;
	const float GY = Y + 66.0f * S();
	const float Dim = (bLinked || bGranted) ? 1.0f : 0.3f;
	const FLinearColor GridAccent = bGranted ? GrantedColor : MemoryFlashColor;
	for (int32 Node = 0; Node < 9; ++Node)
	{
		const float CX = GX + (Node % 3) * (Cell + Gap);
		const float CY = GY + (Node / 3) * (Cell + Gap);
		DrawMemoryNode(CX, CY, Cell, Node, GridAccent, Dim, Node == FlashNode ? FlashAlpha : 0.0f, false, false);
	}

	const float FootY = GY + Grid + 14.0f * S();
	if (!bLinked && !bGranted)
	{
		// 중계 거리 밖: 패턴은 기다린다(가까이 오면 처음부터 재생)
		const float Blink = 0.55f + 0.45f * Pulse(6.0f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), GX, GY + Grid * 0.5f - 44.0f * S(), Grid, 88.0f * S());
		Label(TEXT("MOVE CLOSER"), X + W * 0.5f, GY + Grid * 0.5f - 38.0f * S(), WarningColor.CopyWithNewOpacity(Blink), GEngine->GetLargeFont(), 1.1f, true);
		Label(TEXT("SIGNAL WEAK"), X + W * 0.5f, GY + Grid * 0.5f + 8.0f * S(), WarningColor.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f, true);
		const float DistM = FVector::Dist(MyPawn.GetActorLocation(), Terminal->GetActorLocation()) / 100.0f;
		Label(FString::Printf(TEXT("TERMINAL %.1fm  -  RELAY RANGE %.0fm"), DistM, Terminal->MemoryRelayRange / 100.0f), X + W * 0.5f, FootY, FLinearColor(1.0f, 1.0f, 1.0f, 0.75f), GEngine->GetTinyFont(), 1.0f, true);
		Label(TEXT("SISTER IS WAITING FOR YOUR RELAY"), X + W * 0.5f, FootY + 26.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), GEngine->GetTinyFont(), 1.0f, true);

		// 월드 마커: 단말기 위 괄호 + 거리(어디로 가야 하는지)
		FVector Origin, Extent;
		Terminal->GetActorBounds(true, Origin, Extent);
		FVector ViewLoc;
		FRotator ViewRot;
		if (PlayerOwner)
		{
			PlayerOwner->GetPlayerViewPoint(ViewLoc, ViewRot);
			if (FVector::DotProduct(Origin - ViewLoc, ViewRot.Vector()) > 0.0f)
			{
				const FVector P = Project(Origin + FVector(0.0f, 0.0f, Extent.Z + 50.0f), false);
				if (P.X >= 0.0f && P.X <= Canvas->ClipX && P.Y >= 0.0f && P.Y <= Canvas->ClipY)
				{
					Bracket(FVector2D(P.X, P.Y), 14.0f * S(), 6.0f * S(), WarningColor.CopyWithNewOpacity(Blink), 2.0f);
					Label(FString::Printf(TEXT("RELAY  %.0fm"), DistM), P.X, P.Y - 36.0f * S(), WarningColor.CopyWithNewOpacity(Blink), GEngine->GetTinyFont(), 1.0f, true);
				}
			}
		}
	}
	else
	{
		// 재생 순번 칸: 지난 순번 / 지금 순번(밝게) / 남은 순번
		const int32 Count = Terminal->GetMemorySequence().Num();
		const float SW = 22.0f * S();
		const float SG = 6.0f * S();
		const float RowW = Count * SW + FMath::Max(0, Count - 1) * SG;
		for (int32 i = 0; i < Count; ++i)
		{
			const float SX = X + (W - RowW) * 0.5f + i * (SW + SG);
			const bool bNow = i == Step && FlashNode != INDEX_NONE;
			const bool bPast = Step != INDEX_NONE && i < Step;
			const FLinearColor C = bGranted ? GrantedColor : (bNow ? MemoryFlashColor : MemoryFlashColor.CopyWithNewOpacity(bPast ? 0.55f : 0.18f));
			DrawRect(C, SX, FootY, SW, 8.0f * S());
		}
		FString Status;
		if (bGranted)
		{
			Status = TEXT("RELAY COMPLETE");
		}
		else if (Step != INDEX_NONE)
		{
			Status = FString::Printf(TEXT("STEP %d / %d  -  CALL OUT THE NUMBERS"), Step + 1, Count);
		}
		else
		{
			Status = Wait > 0.0f ? FString::Printf(TEXT("REPLAY IN %.1fs"), Wait) : FString(TEXT("STAND BY"));
		}
		Label(Status, X + W * 0.5f, FootY + 14.0f * S(), FLinearColor(1.0f, 1.0f, 1.0f, 0.75f), GEngine->GetTinyFont(), 1.0f, true);
		const int32 Input = bGranted ? Count : Terminal->GetMemoryInputIndex();
		Label(FString::Printf(TEXT("SISTER INPUT  %d / %d"), Input, Count), X + W * 0.5f, FootY + 38.0f * S(), SisterColor.CopyWithNewOpacity(0.85f), GEngine->GetTinyFont(), 1.0f, true);
	}

	if (bDenied || bRoundClear || bGranted)
	{
		const FString Msg = bGranted ? TEXT("ACCESS GRANTED") : (bDenied ? TEXT("INPUT DENIED // REPLAY") : TEXT("ROUND CLEAR"));
		const FLinearColor MsgColor = bDenied ? DeniedColor : GrantedColor;
		const float Fade = bGranted ? FMath::Clamp(1.5f - EventAge, 0.0f, 1.0f) : FMath::Clamp(1.0f - EventAge, 0.0f, 1.0f);
		DrawRect(MsgColor.CopyWithNewOpacity(0.14f * Fade), X, Y, W, H);
		Label(Msg, X + W * 0.5f, Y - 34.0f * S(), MsgColor.CopyWithNewOpacity(Fade), GEngine->GetSmallFont(), 1.1f, true);
	}
}

// ---------------------------------------------------------------- 그리기 도우미

bool ACoopHUD::IsTerminalAvailable(const AActor* Terminal)
{
	const AHackTerminal* HackTerminal = Cast<AHackTerminal>(Terminal);
	return !HackTerminal || HackTerminal->GetState() != EHackTerminalState::Hacked;
}

void ACoopHUD::DrawPistolIcon(float X, float Y, const FLinearColor& Color)
{
	// 46x26(기준 해상도) 권총 실루엣: 슬라이드, 총구, 프레임, 손잡이, 방아쇠울, 레일 하이라이트
	const float U = S();
	DrawRect(Color, X, Y + 2.0f * U, 40.0f * U, 8.0f * U);
	DrawRect(Color, X + 40.0f * U, Y + 4.0f * U, 5.0f * U, 4.0f * U);
	DrawRect(Color, X + 6.0f * U, Y + 10.0f * U, 22.0f * U, 3.0f * U);
	DrawRect(Color, X + 6.0f * U, Y + 13.0f * U, 9.0f * U, 13.0f * U);
	DrawLine(X + 15.0f * U, Y + 13.0f * U, X + 15.0f * U, Y + 19.0f * U, Color, 1.5f);
	DrawLine(X + 15.0f * U, Y + 19.0f * U, X + 22.0f * U, Y + 19.0f * U, Color, 1.5f);
	DrawLine(X + 22.0f * U, Y + 19.0f * U, X + 22.0f * U, Y + 13.0f * U, Color, 1.5f);
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, Color.A), X + 4.0f * U, Y + 4.0f * U, 28.0f * U, 1.5f * U);
}

void ACoopHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Accent)
{
	DrawRect(PanelColor, X, Y, W, H);
	// 윗변 네온 라인 + 왼쪽 짧은 강조 바 + 오른쪽 아래 모서리 눈금
	DrawRect(Accent.CopyWithNewOpacity(0.85f), X, Y, W, 2.0f * S());
	DrawRect(Accent, X, Y, 4.0f * S(), H);
	DrawLine(X + W - 14.0f * S(), Y + H, X + W, Y + H - 14.0f * S(), Accent.CopyWithNewOpacity(0.6f), 1.5f);
}

void ACoopHUD::Label(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCenterX)
{
	const float FinalScale = Scale * S() * CoopHUDLayout::TextScale;
	float DrawX = X;
	if (bCenterX)
	{
		float TW = 0.0f, TH = 0.0f;
		GetTextSize(Text, TW, TH, Font, FinalScale);
		DrawX = X - TW * 0.5f;
	}
	// 가독성을 위한 1px 그림자
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.8f), DrawX + 1.0f, Y + 1.0f, Font, FinalScale);
	DrawText(Text, Color, DrawX, Y, Font, FinalScale);
}

void ACoopHUD::Bracket(const FVector2D& C, float Half, float Arm, const FLinearColor& Color, float Thickness)
{
	const float L = C.X - Half, R = C.X + Half, T = C.Y - Half, B = C.Y + Half;
	DrawLine(L, T, L + Arm, T, Color, Thickness); DrawLine(L, T, L, T + Arm, Color, Thickness);
	DrawLine(R, T, R - Arm, T, Color, Thickness); DrawLine(R, T, R, T + Arm, Color, Thickness);
	DrawLine(L, B, L + Arm, B, Color, Thickness); DrawLine(L, B, L, B - Arm, Color, Thickness);
	DrawLine(R, B, R - Arm, B, Color, Thickness); DrawLine(R, B, R, B - Arm, Color, Thickness);
}

APawn* ACoopHUD::FindPartnerPawn() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC != PlayerOwner && PC->GetPawn())
		{
			return PC->GetPawn();
		}
	}
	return nullptr;
}

float ACoopHUD::Pulse(float Speed) const
{
	return 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * Speed);
}
