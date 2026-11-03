// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoopHUD.h"
#include "BrotherCombatComponent.h"
#include "SisterCharacter.h"
#include "SisterHackComponent.h"
#include "HackTerminal.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "Sound/SoundBase.h"
#include "CXGameInstance.h"
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
	ZoneTitles.Add(TEXT("Map5"), TEXT("THE PIT"));
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

	if (bIsSister)
	{
		DrawPlayerTag(MyColor, TEXT("P2"), TEXT("SISTER"), TEXT("INFILTRATION  //  HACK"));
		if (!bDead)
		{
			DrawSisterHackLink();
		}
	}
	else
	{
		DrawPlayerTag(MyColor, TEXT("P1"), TEXT("BROTHER"), TEXT("COMBAT  //  RAILGUN"));
		if (MyPawn && !bDead)
		{
			if (const UBrotherCombatComponent* Combat = MyPawn->FindComponentByClass<UBrotherCombatComponent>())
			{
				DrawTargetMarker(*Combat);
				DrawBrotherWeapon(*Combat);
			}
		}
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
		DrawPartnerIndicator(*Partner, PartnerColor, PartnerName);
	}

	if (MyLife && MyLife->IsAlive())
	{
		DrawRebooted(*MyLife, MyColor);
	}

	DrawZoneBanner();
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
	const bool bActive = Hack ? (Hack->GetFocusTerminal() != nullptr) : (Nearest && NearestDist <= HackActivateRange);

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

	if (bActive)
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

	if (Hack && Hack->IsHacking())
	{
		DrawHackSession(*Hack);
	}
}

void ACoopHUD::DrawHackTerminalMarker(const AActor& Terminal, bool bActive, float DistM)
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
	const FVector P = Project(Origin + FVector(0.0f, 0.0f, Extent.Z + 40.0f), false);
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
		Label(FString::Printf(TEXT("%.0fm"), DistM), P.X, P.Y - R - 16.0f * S(), C, GEngine->GetTinyFont(), 1.0f, true);
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

void ACoopHUD::DrawPartnerIndicator(const APawn& Partner, const FLinearColor& Color, const FString& PartnerName)
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

	const bool bArrow = Terminal->PuzzleType == EHackPuzzleType::ArrowSequence;
	const EHackFeedback Feedback = Hack.GetFeedback();
	const float FeedbackAge = Hack.GetFeedbackAge();
	const bool bDenied = Feedback == EHackFeedback::Denied && FeedbackAge < 0.6f;
	const bool bRoundClear = Feedback == EHackFeedback::RoundClear && FeedbackAge < 0.6f;
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
	const FString Right = bArrow ? FString::Printf(TEXT("ROUND %d / %d"), Hack.GetRoundIndex() + 1, Hack.GetRoundCount()) : FString(TEXT("PASSCODE  4-DIGIT"));
	float RW = 0.0f, RH = 0.0f;
	GetTextSize(Right, RW, RH, GEngine->GetSmallFont(), S() * CoopHUDLayout::TextScale);
	Label(Right, X + W - Pad - RW, Y + 12.0f * S(), Accent.CopyWithNewOpacity(0.85f), GEngine->GetSmallFont(), 1.0f);

	const float BodyY = Y + 58.0f * S();
	if (bArrow)
	{
		// 화살표 칸: 완료(채움) / 현재(펄스 테두리) / 대기(흐림)
		const TArray<EHackArrow>& Sequence = Hack.GetArrowSequence();
		const int32 Count = Sequence.Num();
		const float Cell = 78.0f * S();
		const float Gap = 14.0f * S();
		const float RowW = Count * Cell + FMath::Max(0, Count - 1) * Gap;
		float CX = X + (W - RowW) * 0.5f;
		for (int32 i = 0; i < Count; ++i)
		{
			const bool bDone = bGranted || i < Hack.GetArrowIndex();
			const bool bCurrent = !bGranted && i == Hack.GetArrowIndex();
			FLinearColor CellColor = bDone ? Accent : (bCurrent ? Accent.CopyWithNewOpacity(0.6f + 0.4f * Pulse(8.0f)) : Accent.CopyWithNewOpacity(0.25f));
			if (bDone)
			{
				DrawRect(Accent.CopyWithNewOpacity(0.28f), CX, BodyY, Cell, Cell);
			}
			const float T = bCurrent ? 3.0f : 1.5f;
			DrawLine(CX, BodyY, CX + Cell, BodyY, CellColor, T);
			DrawLine(CX + Cell, BodyY, CX + Cell, BodyY + Cell, CellColor, T);
			DrawLine(CX + Cell, BodyY + Cell, CX, BodyY + Cell, CellColor, T);
			DrawLine(CX, BodyY + Cell, CX, BodyY, CellColor, T);
			DrawArrowGlyph(FVector2D(CX + Cell * 0.5f, BodyY + Cell * 0.5f), Cell * 0.3f, Sequence[i],
			               bDone || bCurrent ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.3f));
			CX += Cell + Gap;
		}
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

	// 하단: 진행 바(화살표) + 조작 안내
	const float FootY = Y + H - 34.0f * S();
	if (bArrow)
	{
		const float BarW = W - Pad * 2.0f;
		DrawRect(Accent.CopyWithNewOpacity(0.15f), X + Pad, FootY - 10.0f * S(), BarW, 4.0f * S());
		DrawRect(Accent, X + Pad, FootY - 10.0f * S(), BarW * Hack.GetProgress(), 4.0f * S());
	}
	const FString Hint = bArrow
		? FString::Printf(TEXT("D-PAD  ENTER SEQUENCE        [B]  ABORT"))
		: FString::Printf(TEXT("D-PAD UP/DOWN  DIGIT      LEFT/RIGHT  MOVE      %s  SUBMIT      [B]  ABORT"), *HackKeyHint);
	Label(Hint, X + W * 0.5f, FootY, FLinearColor(1.0f, 1.0f, 1.0f, 0.65f), GEngine->GetTinyFont(), 1.0f, true);

	// 판정 연출
	if (bDenied || bRoundClear || bGranted)
	{
		const FString Msg = bGranted ? TEXT("ACCESS GRANTED") : (bDenied ? TEXT("ACCESS DENIED") : TEXT("ROUND CLEAR"));
		const FLinearColor MsgColor = bDenied ? DeniedColor : GrantedColor;
		const float Fade = bGranted ? 1.0f : 1.0f - FeedbackAge / 0.6f;
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
