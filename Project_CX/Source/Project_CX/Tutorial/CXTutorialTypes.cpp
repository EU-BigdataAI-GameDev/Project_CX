// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTutorialTypes.h"

namespace CXTutorial
{
	/**
	 *  ================================ 튜토리얼 단계 표 ================================
	 *  문구·시간은 여기만 고치면 된다(발동·완료 조건은 CXTutorialSubsystem.cpp).
	 *  - 본문은 1~2줄, 짧게. 한 줄이 길면 HUD가 2줄까지 자동 줄바꿈한다.
	 *  - 토큰은 그 플레이어의 조작 장치로 바뀐다(아래 FormatText 표).
	 *  - 예시 문장 칩은 Jemini 서버의 규칙 조건(enemy_within / hp_below / ammo_below)과 행동(Decoy / HealSupport / ReloadSupport)에
	 *    맞춘 문장이다. LLM이 꺼진 mock 모드(Jemini/app/mock.py 키워드 판별)에서도 규칙으로 등록된다.
	 */
	const TArray<FCXTutorialStepDef>& GetSteps()
	{
		static const TArray<FCXTutorialStepDef> Steps = []()
		{
			TArray<FCXTutorialStepDef> Out;

			// 1) 이동·점프 — 두 사람, Map1 시작 직후. 게임을 막지 않는다. 움직이고 점프하면(또는 12초) 끝.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::Move;
				Def.Id = TEXT("Move");
				Def.Brother = { ECXTutorialMode::Hint, TEXT("이동과 점프"), TEXT("{move} 이동   ·   {jump} 점프"), TEXT(""), ECXTutorialSpot::None };
				Def.Sister = Def.Brother;
				Def.StartDelay = 1.5f;
				Def.Timeout = 12.0f;
				Out.Add(Def);
			}

			// 2) 첫 거미로봇 — 오빠: 슬로모션 + 사격 유도(쏘면 끝). 여동생: 적 해킹 한 줄 안내.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::FirstSpider;
				Def.Id = TEXT("FirstSpider");
				Def.Brother = { ECXTutorialMode::Action, TEXT("적 발견 — 거미 로봇"), TEXT("{fire_ro} 공격하세요."), TEXT("가까운 적을 자동으로 조준합니다."), ECXTutorialSpot::WorldTarget };
				Def.Sister = { ECXTutorialMode::Hint, TEXT("적 해킹"), TEXT("거미 로봇 가까이에서 {interact} — 화살표를 순서대로 입력하면 잠시 멈춥니다."), TEXT(""), ECXTutorialSpot::WorldTarget };
				Def.StartDelay = 0.3f;
				Def.BlockTimeout = 6.0f;	// 슬로모션 최대 6초(게임 시간 약 0.9초)
				Def.Timeout = 12.0f;
				Out.Add(Def);
			}

			// 3) 첫 해킹 단말기 — 여동생: 일시정지 설명 카드.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::FirstTerminal;
				Def.Id = TEXT("FirstTerminal");
				Def.Sister = { ECXTutorialMode::Info, TEXT("해킹 단말기"), TEXT("{interact} 해킹 시작 — 해킹하는 동안에는 움직일 수 없습니다."),
					TEXT("{dir} 조작   ·   {interact} 확인   ·   {cancel} 취소  (실패해도 다시 할 수 있습니다)"), ECXTutorialSpot::HackLinkPanel };
				Def.StartDelay = 0.3f;
				Def.BlockTimeout = 30.0f;
				Out.Add(Def);
			}

			// 4) 제미니에게 인사 — 두 사람, 1단계 뒤 조용할 때. 게임을 막지 않는다. 누가 채팅/음성을 보내면(또는 30초) 끝.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::JeminiHello;
				Def.Id = TEXT("JeminiHello");
				Def.Brother = { ECXTutorialMode::Hint, TEXT("제미니에게 말 걸기"), TEXT("{talk_do} 제미니에게 인사해 보세요."), TEXT("예)  안녕, 제미니!"), ECXTutorialSpot::JeminiPanel };
				Def.Sister = Def.Brother;
				Def.StartDelay = 4.0f;
				Def.Timeout = 30.0f;
				Out.Add(Def);
			}

			// 5) 제미니 규칙 — 두 사람, 4단계 뒤 제미니가 대답한 다음(또는 잠시 뒤). 일시정지 설명 카드 + 예시 문장 칩.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::JeminiRule;
				Def.Id = TEXT("JeminiRule");
				Def.Brother = { ECXTutorialMode::Info, TEXT("제미니에게 규칙 맡기기"), TEXT("\"~하면 ~해 줘\"라고 부탁하면 제미니가 지켜보다가 알아서 움직입니다."), TEXT("{chip_hint}"), ECXTutorialSpot::JeminiPanel };
				Def.Sister = Def.Brother;
				Def.StartDelay = 3.5f;
				Def.BlockTimeout = 40.0f;
				Def.Chips[0] = TEXT("거미가 가까이 오면 미끼를 던져 줘");
				Def.Chips[1] = TEXT("내 체력이 30 아래로 떨어지면 회복해 줘");
				Def.Chips[2] = TEXT("총알이 떨어지면 매번 장전해 줘");
				Out.Add(Def);
			}

			// 6) WARDEN — 두 사람, 누군가 처음 대화 거리(5.5m)에 들어왔을 때. 일시정지 설명 카드 + 증거 노트 강조.
			{
				FCXTutorialStepDef Def;
				Def.Step = ECXTutorialStep::Warden;
				Def.Id = TEXT("Warden");
				Def.Brother = { ECXTutorialMode::Info, TEXT("WARDEN 설득"), TEXT("WARDEN은 명령으로는 문을 열지 않습니다. 모은 증거를 근거로 말로 설득하세요."),
					TEXT("증거는 나눠 말해도 누적됩니다.   ({talk})"), ECXTutorialSpot::EvidencePanel };
				Def.Sister = Def.Brother;
				Def.StartDelay = 0.5f;
				Def.BlockTimeout = 40.0f;
				Out.Add(Def);
			}

			check(Out.Num() == static_cast<int32>(ECXTutorialStep::Count));
			return Out;
		}();
		return Steps;
	}

	int32 FindStepIndex(const FString& Id)
	{
		const TArray<FCXTutorialStepDef>& Steps = GetSteps();
		const FString Wanted = Id.TrimStartAndEnd();
		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			if (Wanted.Equals(Steps[i].Id, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		// 번호(1~6)도 받는다
		if (Wanted.IsNumeric())
		{
			const int32 Number = FCString::Atoi(*Wanted);
			return Steps.IsValidIndex(Number - 1) ? Number - 1 : INDEX_NONE;
		}
		return INDEX_NONE;
	}

	FString FormatText(const TCHAR* Text, bool bGamepad)
	{
		/**
		 *  조작 표기 — 실제 바인딩과 같아야 한다:
		 *  이동/점프 = IMC_Default(WASD·Space / 왼쪽 스틱·A), 사격 = IA_Fire(좌클릭) / UBrotherCombatComponent 런타임 매핑(RT),
		 *  상호작용·해킹 = UCoopInteractComponent(F / X), 해킹 취소·방향 = USisterHackComponent(Backspace·방향키 / B·D-패드),
		 *  제미니 = UCXJeminiInputComponent(Enter 채팅 / Y 누르는 동안 음성), 확인·건너뛰기 = UCXTutorialSubsystem::HandleKeyDown.
		 */
		struct FToken
		{
			const TCHAR* Name;
			const TCHAR* Keyboard;
			const TCHAR* Gamepad;
		};
		static const FToken Tokens[] = {
			{ TEXT("{move}"), TEXT("[W A S D]"), TEXT("[L 스틱]") },
			{ TEXT("{jump}"), TEXT("[SPACE]"), TEXT("[A]") },
			{ TEXT("{fire_ro}"), TEXT("마우스 좌클릭으로"), TEXT("[RT] 버튼으로") },
			{ TEXT("{interact}"), TEXT("[F]"), TEXT("[X]") },
			{ TEXT("{cancel}"), TEXT("[BACKSPACE]"), TEXT("[B]") },
			{ TEXT("{dir}"), TEXT("[방향키]"), TEXT("[D-패드]") },
			{ TEXT("{talk}"), TEXT("[ENTER] 채팅"), TEXT("[Y] 누른 채 말하기") },
			{ TEXT("{talk_do}"), TEXT("Enter를 누르고"), TEXT("[Y] 버튼을 누른 채 말로") },
			{ TEXT("{chip_hint}"), TEXT("숫자 키 [1] [2] [3]을 누르면 아래 예시가 채팅창에 들어갑니다."), TEXT("[Y] 버튼을 누른 채 아래 예시처럼 말해 보세요.") },
			{ TEXT("{confirm}"), TEXT("[ENTER]"), TEXT("[A]") },
			{ TEXT("{skip}"), TEXT("[ESC]"), TEXT("[START]") },
		};
		FString Out(Text ? Text : TEXT(""));
		if (!Out.Contains(TEXT("{")))
		{
			return Out;
		}
		for (const FToken& Token : Tokens)
		{
			Out.ReplaceInline(Token.Name, bGamepad ? Token.Gamepad : Token.Keyboard, ESearchCase::CaseSensitive);
		}
		return Out;
	}
}
