// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoopHUD.h"
#include "BrotherCombatComponent.h"
#include "SisterCharacter.h"
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
	ZoneTitles.Add(TEXT("Map2"), TEXT("SQUATTER STACKS"));
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
		bool bHackable = Actor->Tags.Contains(HackableTag);
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

	DrawSplitEdge(MyColor);

	if (bIsSister)
	{
		DrawPlayerTag(MyColor, TEXT("P2"), TEXT("SISTER"), TEXT("INFILTRATION  //  HACK"));
		DrawSisterHackLink();
	}
	else
	{
		DrawPlayerTag(MyColor, TEXT("P1"), TEXT("BROTHER"), TEXT("COMBAT  //  RAILGUN"));
		if (MyPawn)
		{
			if (const UBrotherCombatComponent* Combat = MyPawn->FindComponentByClass<UBrotherCombatComponent>())
			{
				DrawTargetMarker(*Combat);
				DrawBrotherWeapon(*Combat);
			}
		}
	}

	if (const APawn* Partner = FindPartnerPawn())
	{
		const bool bPartnerIsSister = Cast<ASisterCharacter>(Partner) != nullptr;
		DrawPartnerIndicator(*Partner, bPartnerIsSister ? SisterColor : BrotherColor, bPartnerIsSister ? TEXT("SISTER") : TEXT("BROTHER"));
	}

	DrawZoneBanner();
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
			if (const AActor* Terminal = Weak.Get())
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
	const bool bActive = Nearest && NearestDist <= HackActivateRange;

	// 신호 범위 안 단말기에는 월드 마커(가장 가까운 것만 활성)
	if (MyPawn)
	{
		for (const TWeakObjectPtr<AActor>& Weak : HackTerminals)
		{
			if (const AActor* Terminal = Weak.Get())
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
}

// ---------------------------------------------------------------- 그리기 도우미

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
