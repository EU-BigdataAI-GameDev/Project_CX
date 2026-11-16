// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "CXTitleBackdropWidget.generated.h"

class UTexture2D;

/**
 *  시작화면 배경.
 *
 *  키 아트(/Game/Project_CX/Title/Title_Background)를 화면 가득(cover — 비율 유지, 레터박스 없음) 깔고
 *    - 아주 느린 켄 번스(줌 1.00→1.06 + 살짝 패닝, 40초 편도 왕복)
 *    - 메뉴 가독성: 왼쪽/하단 어둡게 + 상단/우측 비네팅
 *    - 스캔라인 + 천천히 내려가는 스윕 라인(텍스처·머티리얼 없이 색 사각형만)
 *    - 설정/확인창이 열리면 추가 딤(SetOverlayDim)
 *  텍스처가 없으면 예전 절차 생성 슬럼 야경(빌딩 실루엣·비)으로 그린다.
 *  숫자는 전부 아래 UPROPERTY에서 조정.
 */
UCLASS()
class UCXTitleBackdropWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCXTitleBackdropWidget(const FObjectInitializer& ObjectInitializer);

	/** 설정 메뉴/확인창이 위에 열렸을 때 배경을 더 어둡게(부드럽게 보간). */
	void SetOverlayDim(bool bDimmed) { bOverlayDim = bDimmed; }

	bool HasKeyArt() const { return BackgroundTexture != nullptr; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** 배경 키 아트(없으면 절차 생성 야경). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop")
	TObjectPtr<UTexture2D> BackgroundTexture;

	/** 배경 밝기/색 보정(곱). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop")
	FLinearColor BackgroundTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** 켄 번스 줌 범위(cover 스케일에 곱). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|KenBurns")
	float KenBurnsZoomMin = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|KenBurns")
	float KenBurnsZoomMax = 1.06f;

	/** 편도 시간(초). 이 시간 동안 Min→Max, 다시 같은 시간 동안 Max→Min(핑퐁, 양끝 감속). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|KenBurns")
	float KenBurnsDuration = 40.0f;

	/** 줌이 끝났을 때 화면이 향하는 쪽(-1~1, +X = 그림 오른쪽, +Y = 그림 아래). 남는 여백 안에서만 움직여 가장자리가 드러나지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|KenBurns")
	FVector2D KenBurnsPan = FVector2D(0.45, -0.35);

	/** 가독성 그라디언트 진하기(검정 알파). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float LeftShadeAlpha = 0.72f;
	/** 왼쪽 그라디언트가 덮는 화면 폭 비율. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float LeftShadeWidth = 0.62f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float BottomShadeAlpha = 0.78f;
	/** 하단 그라디언트가 덮는 화면 높이 비율. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float BottomShadeHeight = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float TopShadeAlpha = 0.4f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float RightShadeAlpha = 0.3f;

	/** 설정/확인창이 열렸을 때 추가 딤(검정 알파)과 보간 속도(1/초). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float OverlayDimAlpha = 0.45f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|Shade")
	float OverlayDimSpeed = 6.0f;

	/** 스캔라인 진하기(0 = 끔). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|FX")
	float ScanlineAlpha = 0.10f;

	/** 위에서 아래로 천천히 내려가는 얇은 스윕 라인(0 = 끔)과 한 번 내려가는 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|FX")
	float SweepAlpha = 0.05f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|FX")
	float SweepPeriod = 9.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Backdrop|FX")
	FLinearColor SweepColor = FLinearColor(1.0f, 0.35f, 0.75f, 1.0f);

private:
	struct FBuilding
	{
		float X, Width, Height;			// 기준 해상도(1920x1080) 좌표, 바닥 기준 높이
		TArray<FVector2f> Windows;		// 건물 내부 상대 좌표(왼쪽 위)
		TArray<uint8> WindowTint;		// 0 = 노랑, 1 = 청록, 2 = 마젠타
		int32 SignIndex = INDEX_NONE;	// 네온 간판 색(0 청록, 1 마젠타, 2 주황), 없으면 NONE
		float SignY = 0.0f, SignPhase = 0.0f;
	};

	struct FRainDrop
	{
		float X, Y, Speed, Length;
	};

	void BuildLayer(TArray<FBuilding>& Out, FRandomStream& Rng, float MinH, float MaxH, float WindowChance, float SignChance);
	void PaintLayer(const TArray<FBuilding>& Layer, FSlateWindowElementList& Out, int32 Layer0, const FGeometry& G, float Scale,
		const FVector2f& Offset, float Pan, const FLinearColor& BodyColor, float WindowAlpha) const;

	/** 키 아트 배경(cover + 켄 번스). 마지막으로 쓴 레이어를 돌려준다. */
	int32 PaintKeyArt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Size) const;
	/** 텍스처가 없을 때의 절차 생성 야경. */
	int32 PaintProcedural(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Size) const;

	/** 키 아트를 그리는 브러시(텍스처 참조 유지). */
	UPROPERTY(Transient)
	FSlateBrush BackgroundBrush;

	TArray<FBuilding> FarLayer;
	TArray<FBuilding> NearLayer;
	TArray<FRainDrop> Rain;
	float Time = 0.0f;
	bool bOverlayDim = false;
	float OverlayDim = 0.0f;
};
