// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTitleBackdropWidget.h"
#include "CXMenuStyle.h"
#include "CXSfx.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"

namespace CXBackdrop
{
	constexpr float RefW = 1920.0f;
	constexpr float RefH = 1080.0f;
	constexpr float GroundY = 900.0f;		// 빌딩 바닥선(기준 해상도)
	constexpr float PanPeriod = 40.0f;		// 좌우 패닝 루프(초)
	constexpr float ZoomPeriod = 60.0f;		// 켄 번스 줌 왕복(초)
	constexpr int32 NumRain = 140;

	const FLinearColor SignColors[] = {
		FLinearColor(0.0f, 0.85f, 1.0f, 1.0f),
		FLinearColor(1.0f, 0.22f, 0.62f, 1.0f),
		FLinearColor(1.0f, 0.45f, 0.10f, 1.0f)
	};
	const FLinearColor WindowColors[] = {
		FLinearColor(1.0f, 0.78f, 0.45f, 1.0f),
		FLinearColor(0.35f, 0.9f, 1.0f, 1.0f),
		FLinearColor(1.0f, 0.4f, 0.75f, 1.0f)
	};

	/** 세로(위→아래) 그라디언트. Slate의 Orient_Horizontal = 위에서 아래로 색이 바뀐다. 정지점 위치는 로컬 픽셀. */
	void DrawVGradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		std::initializer_list<TPair<float, FLinearColor>> Stops)
	{
		TArray<FSlateGradientStop> GradientStops;
		for (const TPair<float, FLinearColor>& S : Stops)
		{
			GradientStops.Add(FSlateGradientStop(FVector2D(0.0f, S.Key * Size.Y), S.Value));
		}
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(GradientStops), Orient_Horizontal);
	}

	/** 가로(왼→오) 그라디언트. */
	void DrawHGradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		std::initializer_list<TPair<float, FLinearColor>> Stops)
	{
		TArray<FSlateGradientStop> GradientStops;
		for (const TPair<float, FLinearColor>& S : Stops)
		{
			GradientStops.Add(FSlateGradientStop(FVector2D(S.Key * Size.X, 0.0f), S.Value));
		}
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(GradientStops), Orient_Vertical);
	}
}

UCXTitleBackdropWidget::UCXTitleBackdropWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 없으면 조용히 nullptr → 절차 생성 야경으로 그린다
	BackgroundTexture = CXSfx::Find<UTexture2D>(TEXT("/Game/Project_CX/Title/Title_Background.Title_Background"));
}

void UCXTitleBackdropWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (BackgroundTexture)
	{
		BackgroundBrush.SetResourceObject(BackgroundTexture);
		BackgroundBrush.ImageSize = FVector2D(BackgroundTexture->GetSizeX(), BackgroundTexture->GetSizeY());
		BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;
		BackgroundBrush.Tiling = ESlateBrushTileType::NoTile;
	}
	// 켄 번스로 화면보다 크게 그리는 부분이 뷰포트 밖(에디터 PIE 창 등)으로 새지 않게
	SetClipping(EWidgetClipping::ClipToBounds);

	// 고정 시드 → 매번 같은 스카이라인
	FRandomStream Rng(5);
	BuildLayer(FarLayer, Rng, 260.0f, 560.0f, 0.10f, 0.0f);
	BuildLayer(NearLayer, Rng, 160.0f, 420.0f, 0.22f, 0.35f);

	Rain.SetNum(CXBackdrop::NumRain);
	for (FRainDrop& D : Rain)
	{
		D.X = Rng.FRandRange(-100.0f, CXBackdrop::RefW + 100.0f);
		D.Y = Rng.FRandRange(0.0f, CXBackdrop::RefH);
		D.Speed = Rng.FRandRange(900.0f, 1500.0f);
		D.Length = Rng.FRandRange(18.0f, 42.0f);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCXTitleBackdropWidget::BuildLayer(TArray<FBuilding>& Out, FRandomStream& Rng, float MinH, float MaxH, float WindowChance, float SignChance)
{
	// 패닝 여유를 위해 화면보다 넓게 깐다.
	float X = -260.0f;
	while (X < CXBackdrop::RefW + 260.0f)
	{
		FBuilding B;
		B.X = X;
		B.Width = Rng.FRandRange(70.0f, 190.0f);
		B.Height = Rng.FRandRange(MinH, MaxH);

		// 창문 격자(일부만 켜짐)
		for (float WY = 24.0f; WY < B.Height - 30.0f; WY += 22.0f)
		{
			for (float WX = 12.0f; WX < B.Width - 16.0f; WX += 18.0f)
			{
				if (Rng.FRand() < WindowChance)
				{
					B.Windows.Add(FVector2f(WX, WY));
					const float T = Rng.FRand();
					B.WindowTint.Add(T < 0.7f ? 0 : (T < 0.88f ? 1 : 2));
				}
			}
		}
		if (Rng.FRand() < SignChance)
		{
			B.SignIndex = Rng.RandRange(0, 2);
			B.SignY = Rng.FRandRange(30.0f, FMath::Max(40.0f, B.Height - 140.0f));
			B.SignPhase = Rng.FRandRange(0.0f, 10.0f);
		}
		X += B.Width + Rng.FRandRange(-10.0f, 26.0f);
		Out.Add(MoveTemp(B));
	}
}

void UCXTitleBackdropWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	OverlayDim = FMath::FInterpTo(OverlayDim, bOverlayDim ? 1.0f : 0.0f, InDeltaTime, OverlayDimSpeed);
	if (BackgroundTexture)
	{
		return;
	}
	for (FRainDrop& D : Rain)
	{
		D.Y += D.Speed * InDeltaTime;
		D.X -= D.Speed * 0.18f * InDeltaTime;	// 약간 사선
		if (D.Y > CXBackdrop::RefH + 40.0f)
		{
			D.Y -= CXBackdrop::RefH + 80.0f;
			D.X += 180.0f;
		}
		if (D.X < -120.0f)
		{
			D.X += CXBackdrop::RefW + 240.0f;
		}
	}
}

void UCXTitleBackdropWidget::PaintLayer(const TArray<FBuilding>& Layer, FSlateWindowElementList& Out, int32 Layer0, const FGeometry& G, float Scale,
	const FVector2f& Offset, float Pan, const FLinearColor& BodyColor, float WindowAlpha) const
{
	using namespace CXBackdrop;
	auto ToLocal = [&](float X, float Y) { return Offset + FVector2f(X + Pan, Y) * Scale; };

	for (const FBuilding& B : Layer)
	{
		const float Top = GroundY - B.Height;
		const FVector2f P = ToLocal(B.X, Top);
		const FVector2f S = FVector2f(B.Width, B.Height + (RefH - GroundY)) * Scale;
		CXMenuStyle::DrawRect(Out, Layer0, G, P, S, BodyColor);

		// 옥상 테두리 희미한 림라이트
		CXMenuStyle::DrawRect(Out, Layer0 + 1, G, P, FVector2f(S.X, FMath::Max(1.0f, Scale)), FLinearColor(0.4f, 0.3f, 0.7f, 0.18f));

		for (int32 i = 0; i < B.Windows.Num(); ++i)
		{
			// 몇몇 창문은 천천히 켜졌다 꺼진다
			const float Flick = 0.65f + 0.35f * FMath::Sin(Time * 0.7f + B.X * 0.13f + i * 1.7f);
			const FLinearColor C = WindowColors[B.WindowTint[i]].CopyWithNewOpacity(WindowAlpha * Flick);
			CXMenuStyle::DrawRect(Out, Layer0 + 1, G, ToLocal(B.X + B.Windows[i].X, Top + B.Windows[i].Y), FVector2f(7.0f, 9.0f) * Scale, C);
		}

		if (B.SignIndex != INDEX_NONE)
		{
			// 세로 네온 간판: 글로우(넓고 옅게) + 본체 + 가끔 지직 깜박임
			const float Phase = FMath::Fmod(Time + B.SignPhase, 7.0f);
			const bool bGlitch = (Phase > 5.6f && Phase < 5.7f) || (Phase > 5.8f && Phase < 5.86f);
			const float On = bGlitch ? 0.15f : 1.0f;
			const FLinearColor C = SignColors[B.SignIndex];
			const float SX = B.X + B.Width * 0.5f - 7.0f;
			const float SY = Top + B.SignY;
			CXMenuStyle::DrawRect(Out, Layer0 + 2, G, ToLocal(SX - 18.0f, SY - 18.0f), FVector2f(50.0f, 146.0f) * Scale, C.CopyWithNewOpacity(0.08f * On));
			CXMenuStyle::DrawRect(Out, Layer0 + 2, G, ToLocal(SX - 6.0f, SY - 6.0f), FVector2f(26.0f, 122.0f) * Scale, C.CopyWithNewOpacity(0.16f * On));
			CXMenuStyle::DrawRect(Out, Layer0 + 3, G, ToLocal(SX, SY), FVector2f(14.0f, 110.0f) * Scale, C.CopyWithNewOpacity(0.85f * On));
			CXMenuStyle::DrawRect(Out, Layer0 + 3, G, ToLocal(SX + 4.0f, SY + 6.0f), FVector2f(6.0f, 98.0f) * Scale, FLinearColor(1, 1, 1, 0.5f * On));
		}
	}
}

int32 UCXTitleBackdropWidget::PaintKeyArt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Size) const
{
	const FVector2f ImageSize(FMath::Max(1.0f, (float)BackgroundBrush.ImageSize.X), FMath::Max(1.0f, (float)BackgroundBrush.ImageSize.Y));

	// 핑퐁 진행도 0→1→0(양끝 감속). 한쪽 방향에 KenBurnsDuration초.
	const float Phase = 0.5f - 0.5f * FMath::Cos(Time * UE_PI / FMath::Max(1.0f, KenBurnsDuration));
	const float Zoom = FMath::Max(1.0f, FMath::Lerp(KenBurnsZoomMin, KenBurnsZoomMax, Phase));

	// cover: 비율을 지키면서 화면을 꽉 채우는 최소 배율(남는 쪽은 잘린다)
	const float Scale = FMath::Max(Size.X / ImageSize.X, Size.Y / ImageSize.Y) * Zoom;
	const FVector2f DrawSize = ImageSize * Scale;

	// 남는 여백(Size - DrawSize ≤ 0) 안에서만 패닝 → 가장자리가 드러나지 않는다
	const FVector2f Pan(FMath::Clamp((float)KenBurnsPan.X, -1.0f, 1.0f) * Phase, FMath::Clamp((float)KenBurnsPan.Y, -1.0f, 1.0f) * Phase);
	const FVector2f Pos = (Size - DrawSize) * (FVector2f(0.5f, 0.5f) + Pan * 0.5f);

	++Layer;
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(DrawSize, FSlateLayoutTransform(Pos)), &BackgroundBrush, ESlateDrawEffect::None, BackgroundTint);
	return Layer;
}

int32 UCXTitleBackdropWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace CXBackdrop;
	int32 L = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FGeometry& G = AllottedGeometry;
	const FVector2f Size = FVector2f(G.GetLocalSize());
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return L;
	}

	const bool bKeyArt = BackgroundTexture && BackgroundBrush.GetResourceObject();
	L = bKeyArt ? PaintKeyArt(OutDrawElements, L, G, Size) : PaintProcedural(OutDrawElements, L, G, Size);

	// 가독성: 왼쪽(로고·메뉴 쪽) 어둡게, 하단 그라디언트, 상단/우측 비네팅
	++L;
	DrawHGradient(OutDrawElements, L, G, FVector2f(0, 0), FVector2f(Size.X * LeftShadeWidth, Size.Y), {
		{ 0.0f, FLinearColor(0, 0, 0, LeftShadeAlpha) },
		{ 0.55f, FLinearColor(0, 0, 0, LeftShadeAlpha * 0.45f) },
		{ 1.0f, FLinearColor(0, 0, 0, 0.0f) } });
	DrawVGradient(OutDrawElements, L, G, FVector2f(0, Size.Y * (1.0f - BottomShadeHeight)), FVector2f(Size.X, Size.Y * BottomShadeHeight), {
		{ 0.0f, FLinearColor(0, 0, 0, 0.0f) },
		{ 1.0f, FLinearColor(0, 0, 0, BottomShadeAlpha) } });
	DrawHGradient(OutDrawElements, L, G, FVector2f(Size.X * 0.82f, 0), FVector2f(Size.X * 0.18f, Size.Y), {
		{ 0.0f, FLinearColor(0, 0, 0, 0.0f) },
		{ 1.0f, FLinearColor(0, 0, 0, RightShadeAlpha) } });
	DrawVGradient(OutDrawElements, L, G, FVector2f(0, 0), FVector2f(Size.X, Size.Y * 0.18f), {
		{ 0.0f, FLinearColor(0, 0, 0, TopShadeAlpha) },
		{ 1.0f, FLinearColor(0, 0, 0, 0.0f) } });

	// 스캔라인(3px 간격 1px)
	++L;
	if (ScanlineAlpha > 0.0f)
	{
		const float Step = FMath::Max(3.0f, 3.0f * Size.Y / RefH);
		const FLinearColor ScanColor(0.0f, 0.0f, 0.0f, ScanlineAlpha);
		for (float Y = 0.0f; Y < Size.Y; Y += Step)
		{
			CXMenuStyle::DrawRect(OutDrawElements, L, G, FVector2f(0.0f, Y), FVector2f(Size.X, 1.0f), ScanColor);
		}
	}

	// 스윕 라인: 옅은 띠(위쪽 꼬리) + 얇은 선이 천천히 내려간다
	if (SweepAlpha > 0.0f && SweepPeriod > 0.0f)
	{
		const float Band = Size.Y * 0.08f;
		const float Y = FMath::Fmod(Time / SweepPeriod, 1.0f) * (Size.Y + Band) - Band;
		DrawVGradient(OutDrawElements, L, G, FVector2f(0, Y), FVector2f(Size.X, Band), {
			{ 0.0f, SweepColor.CopyWithNewOpacity(0.0f) },
			{ 1.0f, SweepColor.CopyWithNewOpacity(SweepAlpha * 0.6f) } });
		CXMenuStyle::DrawRect(OutDrawElements, L, G, FVector2f(0.0f, Y + Band), FVector2f(Size.X, FMath::Max(1.0f, Size.Y / RefH)), SweepColor.CopyWithNewOpacity(SweepAlpha));
	}

	// 설정/확인창이 위에 열렸을 때 추가 딤
	if (OverlayDim > 0.001f)
	{
		++L;
		CXMenuStyle::DrawRect(OutDrawElements, L, G, FVector2f::ZeroVector, Size, FLinearColor(0.0f, 0.0f, 0.0f, OverlayDimAlpha * OverlayDim));
	}
	return L;
}

int32 UCXTitleBackdropWidget::PaintProcedural(FSlateWindowElementList& OutDrawElements, int32 L, const FGeometry& G, const FVector2f& Size) const
{
	using namespace CXBackdrop;

	// 화면을 덮도록 기준 해상도에서 스케일(+ 켄 번스 줌), 가운데 정렬
	const float Zoom = 1.0f + 0.02f * (1.0f - FMath::Cos(Time * UE_TWO_PI / ZoomPeriod));
	const float Scale = FMath::Max(Size.X / RefW, Size.Y / RefH) * Zoom;
	const FVector2f Offset = (Size - FVector2f(RefW, RefH) * Scale) * 0.5f;
	const float PanWave = FMath::Sin(Time * UE_TWO_PI / PanPeriod);

	// 1) 하늘: 위 짙은 남색 → 지평선 보랏빛 도시광 → 아래 검정
	DrawVGradient(OutDrawElements, ++L, G, FVector2f(0, 0), Size, {
		{ 0.00f, FLinearColor(0.004f, 0.006f, 0.02f, 1.0f) },
		{ 0.45f, FLinearColor(0.02f, 0.015f, 0.06f, 1.0f) },
		{ 0.78f, FLinearColor(0.10f, 0.025f, 0.12f, 1.0f) },
		{ 1.00f, FLinearColor(0.01f, 0.01f, 0.02f, 1.0f) } });

	// 2) 원경 빌딩(어둡고 느림) → 원경 안개 → 근경 빌딩(조금 더 빠름)
	L += 1;
	PaintLayer(FarLayer, OutDrawElements, L, G, Scale, Offset, PanWave * 30.0f, FLinearColor(0.03f, 0.025f, 0.07f, 1.0f), 0.35f);
	L += 4;
	DrawVGradient(OutDrawElements, L, G, FVector2f(0, Offset.Y + 480.0f * Scale), FVector2f(Size.X, 520.0f * Scale), {
		{ 0.0f, FLinearColor(0.10f, 0.03f, 0.14f, 0.0f) },
		{ 0.7f, FLinearColor(0.10f, 0.03f, 0.14f, 0.35f) },
		{ 1.0f, FLinearColor(0.02f, 0.01f, 0.04f, 0.6f) } });
	L += 1;
	PaintLayer(NearLayer, OutDrawElements, L, G, Scale, Offset, PanWave * 70.0f, FLinearColor(0.008f, 0.008f, 0.02f, 1.0f), 0.6f);
	L += 4;

	// 3) 바닥 안개(청록 기운)
	DrawVGradient(OutDrawElements, L, G, FVector2f(0, Offset.Y + 760.0f * Scale), FVector2f(Size.X, 320.0f * Scale), {
		{ 0.0f, FLinearColor(0.0f, 0.25f, 0.3f, 0.0f) },
		{ 0.5f, FLinearColor(0.0f, 0.2f, 0.25f, 0.10f) },
		{ 1.0f, FLinearColor(0.0f, 0.05f, 0.08f, 0.2f) } });

	// 4) 비
	{
		++L;
		const FLinearColor RainColor(0.6f, 0.8f, 1.0f, 0.13f);
		for (const FRainDrop& D : Rain)
		{
			TArray<FVector2f> Pts;
			Pts.Add(Offset + FVector2f(D.X, D.Y) * Scale);
			Pts.Add(Offset + FVector2f(D.X - D.Length * 0.18f, D.Y - D.Length) * Scale);
			FSlateDrawElement::MakeLines(OutDrawElements, L, G.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, RainColor, true, 1.0f);
		}
	}

	return L;
}
