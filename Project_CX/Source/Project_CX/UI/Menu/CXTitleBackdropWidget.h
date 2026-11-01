// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXTitleBackdropWidget.generated.h"

/**
 *  시작화면 배경(게임 시작화면 기획 4장 A안 변형).
 *
 *  Map1 하이레졸 스크린샷 에셋이 아직 없어서, 텍스처 없이 Slate 그리기만으로 슬럼 야경을 만든다.
 *    - 남색→검정 하늘 그라디언트 + 원경/근경 2겹 빌딩 실루엣(고정 시드 절차 생성), 창문 불빛·네온 간판 깜박임
 *    - 느린 좌우 패닝(40초 루프, 레이어별 시차) + 아주 느린 줌(켄 번스 1.0→1.04)
 *    - 비 줄기, 바닥 안개
 *    - 메뉴 가독성: 하단 그라디언트(검정 0→70%) + 왼쪽 어둡게 + 비네팅 + 스캔라인
 *  나중에 배경 이미지가 생기면 이 위젯 아래에 UImage를 깔고 실루엣 레이어만 끄면 된다.
 */
UCLASS()
class UCXTitleBackdropWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

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

	TArray<FBuilding> FarLayer;
	TArray<FBuilding> NearLayer;
	TArray<FRainDrop> Rain;
	float Time = 0.0f;
};
