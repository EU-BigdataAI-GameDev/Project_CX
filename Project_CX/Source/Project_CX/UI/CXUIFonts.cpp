// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXUIFonts.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"

namespace CXUIFontsPrivate
{
	/** 로드한 Font 에셋(역할별). 에셋은 패키지가 쥐고 있으므로 약한 참조로 캐시. */
	TWeakObjectPtr<UFont> Loaded[2];
	bool bLoadFailed[2] = { false, false };

	/** Canvas용 런타임 사본(역할·크기·굵기). 루트에 올려 GC에서 지킨다(조합 수십 개 이하). */
	TMap<FString, TWeakObjectPtr<UFont>> CanvasVariants;

	/** 한글 자모·호환 자모·음절, CJK 기호·전각, 도형(●) 등 — 두 폰트에 없는 문자를 서브 폰트로 넘긴다. */
	const TPair<int32, int32> CjkRanges[] = {
		{ 0x1100, 0x11FF },	// Hangul Jamo
		{ 0x2190, 0x21FF },	// Arrows
		{ 0x25A0, 0x25FF },	// Geometric Shapes(● ■ ▶)
		{ 0x2600, 0x26FF },	// Misc Symbols
		{ 0x3000, 0x303F },	// CJK Symbols and Punctuation
		{ 0x3130, 0x318F },	// Hangul Compatibility Jamo
		{ 0x4E00, 0x9FFF },	// CJK Unified Ideographs
		{ 0xAC00, 0xD7AF },	// Hangul Syllables
		{ 0xFF00, 0xFFEF },	// Halfwidth and Fullwidth Forms
	};
}

UFont* UCXUIFonts::GetFont(ECXFontRole Role)
{
	using namespace CXUIFontsPrivate;
	const int32 Index = Role == ECXFontRole::System ? 1 : 0;
	if (UFont* Font = Loaded[Index].Get())
	{
		return Font;
	}
	if (bLoadFailed[Index])
	{
		return nullptr;
	}
	const UCXUIFontSettings* Settings = GetDefault<UCXUIFontSettings>();
	const FSoftObjectPath& Path = Index == 1 ? Settings->SystemFont : Settings->GameFont;
	UFont* Font = Path.IsValid() ? Cast<UFont>(Path.TryLoad()) : nullptr;
	if (!Font || Font->FontCacheType != EFontCacheType::Runtime)
	{
		UE_LOG(LogTemp, Warning, TEXT("CXUIFonts: %s 폰트(%s)를 불러오지 못해 엔진 기본 폰트를 씁니다."), Index == 1 ? TEXT("System") : TEXT("Game"), *Path.ToString());
		bLoadFailed[Index] = true;
		return nullptr;
	}
	// 게임 내내 쓰는 작은 에셋 — 순수 Slate 위젯(제미니 채팅창)은 FontObject를 GC에 알리지 않으므로 루트에 올려 둔다.
	Font->AddToRoot();
	Loaded[Index] = Font;
	return Font;
}

FSlateFontInfo UCXUIFonts::Slate(ECXFontRole Role, int32 Size, FName Typeface, int32 LetterSpacing)
{
	FSlateFontInfo Info;
	if (UFont* Font = GetFont(Role))
	{
		Info = FSlateFontInfo(Font, static_cast<float>(Size), Typeface);
	}
	else
	{
		// Roboto에는 Medium/SemiBold 이름이 없으면 기본으로 떨어진다 — 가까운 이름으로 맞춘다
		const FName Fallback = (Typeface == TEXT("SemiBold")) ? FName(TEXT("Bold")) : (Typeface == TEXT("Medium") ? FName(TEXT("Regular")) : Typeface);
		Info = FCoreStyle::GetDefaultFontStyle(Fallback, Size);
	}
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

UFont* UCXUIFonts::Canvas(ECXFontRole Role, int32 Size, FName Typeface)
{
	using namespace CXUIFontsPrivate;
	UFont* Base = GetFont(Role);
	if (!Base)
	{
		return Size <= 8 ? GEngine->GetTinyFont() : GEngine->GetSmallFont();
	}

	const FString Key = FString::Printf(TEXT("%d_%d_%s"), static_cast<int32>(Role), Size, *Typeface.ToString());
	if (const TWeakObjectPtr<UFont>* Found = CanvasVariants.Find(Key))
	{
		if (UFont* Variant = Found->Get())
		{
			return Variant;
		}
	}

	UFont* Variant = NewObject<UFont>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UFont::StaticClass(), *FString::Printf(TEXT("CXCanvasFont_%s"), *Key)));
	Variant->FontCacheType = EFontCacheType::Runtime;
	Variant->CompositeFont = Base->CompositeFont;
	Variant->LegacyFontSize = Size;
	Variant->LegacyFontName = Typeface;
	Variant->AddToRoot();
	CanvasVariants.Add(Key, Variant);
	return Variant;
}

bool UCXUIFonts::ConfigureCompositeFont(UFont* Font, const TArray<FName>& TypefaceNames, const TArray<UFontFace*>& Faces, UFontFace* CjkFace, int32 LegacySize, FName LegacyTypeface)
{
	using namespace CXUIFontsPrivate;
	if (!Font || TypefaceNames.Num() == 0 || TypefaceNames.Num() != Faces.Num())
	{
		return false;
	}

	Font->Modify();
	Font->FontCacheType = EFontCacheType::Runtime;
	Font->LegacyFontSize = FMath::Max(1, LegacySize);
	Font->LegacyFontName = LegacyTypeface;

	FCompositeFont& Composite = Font->CompositeFont;
	Composite.DefaultTypeface.Fonts.Reset();
	for (int32 i = 0; i < Faces.Num(); ++i)
	{
		if (!Faces[i])
		{
			return false;
		}
		FTypefaceEntry& Entry = Composite.DefaultTypeface.Fonts.Emplace_GetRef(TypefaceNames[i]);
		Entry.Font = FFontData(Faces[i]);
	}

	Composite.SubTypefaces.Reset();
	Composite.FallbackTypeface = FCompositeFallbackFont();
	if (CjkFace)
	{
		// 한글·CJK 서브 폰트: 굵기 이름마다 같은 면(DroidSansFallback은 한 굵기뿐)
		FCompositeSubFont& Sub = Composite.SubTypefaces.AddDefaulted_GetRef();
#if WITH_EDITORONLY_DATA
		Sub.EditorName = TEXT("Hangul / CJK");
#endif
		for (const FName& Name : TypefaceNames)
		{
			FTypefaceEntry& Entry = Sub.Typeface.Fonts.Emplace_GetRef(Name);
			Entry.Font = FFontData(CjkFace);
		}
		for (const TPair<int32, int32>& Range : CjkRanges)
		{
			Sub.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(Range.Key), FInt32Range::BoundsType::Inclusive(Range.Value)));
		}

		// 그 밖에 두 폰트에 없는 문자도 같은 면으로(마지막 수단)
		FTypefaceEntry& FallbackEntry = Composite.FallbackTypeface.Typeface.Fonts.Emplace_GetRef(TEXT("Regular"));
		FallbackEntry.Font = FFontData(CjkFace);
	}

#if WITH_EDITORONLY_DATA
	Composite.MakeDirty();
#endif
	Font->PostEditChange();
	Font->MarkPackageDirty();
	return true;
}
