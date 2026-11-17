// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuStyle.h"
#include "CXUIFonts.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Layout/Geometry.h"

namespace CXMenuStyle
{
	FLinearColor PlayerColor(int32 PlayerIndex)
	{
		return PlayerIndex == 1 ? Sister : Brother;
	}

	FSlateFontInfo Font(int32 Size, FName Typeface, int32 LetterSpacing)
	{
		return UCXUIFonts::Slate(ECXFontRole::Game, Size, Typeface, LetterSpacing);
	}

	FSlateFontInfo MonoFont(int32 Size, FName Typeface, int32 LetterSpacing)
	{
		return UCXUIFonts::Slate(ECXFontRole::System, Size, Typeface, LetterSpacing);
	}

	const FSlateBrush* WhiteBrush()
	{
		return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	}

	void DrawRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Color)
	{
		if (Size.X <= 0.0f || Size.Y <= 0.0f || Color.A <= 0.0f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), WhiteBrush(), ESlateDrawEffect::None, Color);
	}

	void DrawNeonFrame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FLinearColor& Color, float LineAlpha, float Bracket, float Thickness)
	{
		const FVector2f S = FVector2f(Geometry.GetLocalSize());
		const float T = Thickness;

		// 얇은 테두리(반투명)
		const FLinearColor Line = Color.CopyWithNewOpacity(Color.A * LineAlpha);
		DrawRect(Out, Layer, Geometry, FVector2f(0, 0), FVector2f(S.X, 1), Line);
		DrawRect(Out, Layer, Geometry, FVector2f(0, S.Y - 1), FVector2f(S.X, 1), Line);
		DrawRect(Out, Layer, Geometry, FVector2f(0, 0), FVector2f(1, S.Y), Line);
		DrawRect(Out, Layer, Geometry, FVector2f(S.X - 1, 0), FVector2f(1, S.Y), Line);

		// 모서리 브래킷(진하게)
		const float B = FMath::Min(Bracket, FMath::Min(S.X, S.Y) * 0.5f);
		DrawRect(Out, Layer, Geometry, FVector2f(0, 0), FVector2f(B, T), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(0, 0), FVector2f(T, B), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(S.X - B, 0), FVector2f(B, T), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(S.X - T, 0), FVector2f(T, B), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(0, S.Y - T), FVector2f(B, T), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(0, S.Y - B), FVector2f(T, B), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(S.X - B, S.Y - T), FVector2f(B, T), Color);
		DrawRect(Out, Layer, Geometry, FVector2f(S.X - T, S.Y - B), FVector2f(T, B), Color);
	}
}
