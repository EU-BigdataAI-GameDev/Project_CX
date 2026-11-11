// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CXUIFonts.generated.h"

class UFont;
class UFontFace;

/**
 *  UI 폰트 역할(.claude/Plans/UI 폰트·게임명 적용.md).
 *  - Game:   Oxanium — 플레이어에게 전달하는 게임 정보(HUD 수치·레이블, 메뉴 제목·버튼, 알림, 시작화면).
 *  - System: IBM Plex Mono — 게임 세계 속 시스템이 출력하는 데이터(해킹 퍼즐·단말기, 시스템 메시지, 로그, 제미니 출력).
 *  두 폰트 모두 한글이 없어서 Font 에셋의 서브 폰트(한글·CJK 범위)로 엔진 DroidSansFallback을 쓴다.
 */
UENUM()
enum class ECXFontRole : uint8
{
	Game,
	System
};

/** 폰트 에셋 경로. DefaultGame.ini [/Script/Project_CX.CXUIFontSettings]에서 바꿀 수 있다. */
UCLASS(config = Game)
class PROJECT_CX_API UCXUIFontSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Oxanium 컴포지트 폰트(런타임 캐시). */
	UPROPERTY(config, EditAnywhere, Category = "Fonts")
	FSoftObjectPath GameFont = FSoftObjectPath(TEXT("/Game/Project_CX/UI/Fonts/F_Oxanium.F_Oxanium"));

	/** IBM Plex Mono 컴포지트 폰트(런타임 캐시). */
	UPROPERTY(config, EditAnywhere, Category = "Fonts")
	FSoftObjectPath SystemFont = FSoftObjectPath(TEXT("/Game/Project_CX/UI/Fonts/F_IBMPlexMono.F_IBMPlexMono"));
};

/**
 *  프로젝트 공용 UI 폰트 도우미. 메뉴(UMG/Slate)와 HUD(Canvas)가 모두 여기서 폰트를 받는다.
 *  에셋을 못 찾으면 엔진 기본 폰트(Roboto)로 대신한다(게임은 그대로 돈다).
 */
UCLASS()
class PROJECT_CX_API UCXUIFonts : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 역할별 Font 에셋. 없으면 nullptr. */
	static UFont* GetFont(ECXFontRole Role);

	/** UMG/Slate용 폰트 정보. Typeface: Light / Regular / Medium / SemiBold / Bold. */
	static FSlateFontInfo Slate(ECXFontRole Role, int32 Size, FName Typeface = TEXT("Regular"), int32 LetterSpacing = 0);

	/**
	 *  Canvas(AHUD::DrawText)용 폰트. Canvas는 UFont의 LegacyFontSize/LegacyFontName으로 크기·굵기를 정하므로
	 *  크기·굵기 조합마다 런타임 사본을 한 번 만들어 캐시한다. 에셋이 없으면 엔진 Small/Tiny 폰트.
	 */
	static UFont* Canvas(ECXFontRole Role, int32 Size, FName Typeface = TEXT("Regular"));

	/**
	 *  에디터 Python용(컴포지트 폰트 구조체는 Python에 노출되지 않는다): Font 에셋을 런타임 캐시 컴포지트 폰트로 채운다.
	 *  TypefaceNames[i] ↔ Faces[i]가 기본 서체. CjkFace가 있으면 한글·CJK·기호 범위 서브 폰트로 넣는다(모든 이름에 같은 면).
	 */
	UFUNCTION(BlueprintCallable, Category = "CX|UI")
	static bool ConfigureCompositeFont(UFont* Font, const TArray<FName>& TypefaceNames, const TArray<UFontFace*>& Faces, UFontFace* CjkFace, int32 LegacySize = 10, FName LegacyTypeface = TEXT("Regular"));
};
