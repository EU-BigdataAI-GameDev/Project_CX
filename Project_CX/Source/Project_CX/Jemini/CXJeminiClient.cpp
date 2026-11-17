// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJemini, Log, All);

namespace CXJemini
{
	namespace
	{
		TSharedRef<FJsonObject> StateObject(const FCXJeminiState& State)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("brother_ammo"), FMath::Max(0, State.BrotherAmmo));
			Obj->SetNumberField(TEXT("brother_ammo_max"), FMath::Max(1, State.BrotherAmmoMax));
			Obj->SetBoolField(TEXT("brother_reloading"), State.bBrotherReloading);
			Obj->SetNumberField(TEXT("brother_hp"), FMath::Max(0, State.BrotherHp));
			Obj->SetNumberField(TEXT("brother_hp_max"), FMath::Max(1, State.BrotherHpMax));
			Obj->SetBoolField(TEXT("brother_alive"), State.bBrotherAlive);
			Obj->SetNumberField(TEXT("brother_respawn_remaining"), FMath::Max(0.0f, State.BrotherRespawnRemaining));
			Obj->SetNumberField(TEXT("brother_shield_remaining"), FMath::Max(0.0f, State.BrotherShieldRemaining));
			Obj->SetNumberField(TEXT("damage_boost_remaining"), FMath::Max(0.0f, State.DamageBoostRemaining));
			Obj->SetNumberField(TEXT("sister_hp"), FMath::Max(0, State.SisterHp));
			Obj->SetNumberField(TEXT("sister_hp_max"), FMath::Max(1, State.SisterHpMax));
			Obj->SetBoolField(TEXT("sister_alive"), State.bSisterAlive);
			Obj->SetNumberField(TEXT("sister_respawn_remaining"), FMath::Max(0.0f, State.SisterRespawnRemaining));
			Obj->SetNumberField(TEXT("sister_shield_remaining"), FMath::Max(0.0f, State.SisterShieldRemaining));
			if (State.bHasPuzzle)
			{
				TSharedRef<FJsonObject> Puzzle = MakeShared<FJsonObject>();
				Puzzle->SetStringField(TEXT("type"), State.PuzzleType);
				Puzzle->SetNumberField(TEXT("progress"), FMath::Clamp(State.PuzzleProgress, 0.0f, 1.0f));
				if (!State.PuzzleKind.IsEmpty())
				{
					Puzzle->SetStringField(TEXT("kind"), State.PuzzleKind);
				}
				Obj->SetObjectField(TEXT("sister_puzzle"), Puzzle);
			}
			else
			{
				Obj->SetField(TEXT("sister_puzzle"), MakeShared<FJsonValueNull>());
			}
			Obj->SetNumberField(TEXT("enemy_alive_count"), FMath::Max(0, State.EnemyAliveCount));
			Obj->SetNumberField(TEXT("enemy_hackable_count"), FMath::Max(0, State.EnemyHackableCount));
			Obj->SetNumberField(TEXT("enemy_active_count"), FMath::Max(0, State.EnemyActiveCount));
			if (State.EnemyNearestDist >= 0.0f)
			{
				// 소수 1자리(m)
				Obj->SetNumberField(TEXT("enemy_nearest_dist"), FMath::RoundToFloat(State.EnemyNearestDist * 10.0f) / 10.0f);
			}
			else
			{
				Obj->SetField(TEXT("enemy_nearest_dist"), MakeShared<FJsonValueNull>());
			}
			Obj->SetNumberField(TEXT("terminal_locked_count"), FMath::Max(0, State.TerminalLockedCount));
			Obj->SetStringField(TEXT("speaker"), State.Speaker == TEXT("sister") ? TEXT("sister") : TEXT("brother"));
			Obj->SetBoolField(TEXT("drone_busy"), State.bDroneBusy);
			Obj->SetNumberField(TEXT("battery"), FMath::Clamp(State.Battery, 0, 100));
			// 기능 A: 보관 중인 규칙(서버는 개수 제한·목록·취소 대상에만 쓴다)
			TArray<TSharedPtr<FJsonValue>> Rules;
			for (const FCXJeminiRule& Rule : State.ActiveRules)
			{
				TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("id"), Rule.Id);
				R->SetStringField(TEXT("label"), Rule.Label);
				R->SetStringField(TEXT("action"), Rule.Action);
				R->SetBoolField(TEXT("held"), Rule.bHeld);
				Rules.Add(MakeShared<FJsonValueObject>(R));
			}
			Obj->SetArrayField(TEXT("active_rules"), Rules);
			// 기능 C: 대답 대기 중인 작전 제안(서버는 수락 자리 계산·짧은 대답 판별에만 쓴다)
			TArray<TSharedPtr<FJsonValue>> Pending;
			for (int32 i = 0; i < FMath::Min(2, State.PendingProposal.Num()); ++i)
			{
				TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("id"), 0);
				R->SetStringField(TEXT("label"), State.PendingProposal[i].Label);
				R->SetStringField(TEXT("action"), State.PendingProposal[i].Action);
				Pending.Add(MakeShared<FJsonValueObject>(R));
			}
			Obj->SetArrayField(TEXT("pending_proposal"), Pending);
			// 기능 C: WARDEN 힌트용(얻은 증거 id·마지막 거절 사유만)
			if (State.bHasWarden)
			{
				TSharedRef<FJsonObject> Warden = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Ids;
				for (const FString& Id : State.WardenEvidence)
				{
					Ids.Add(MakeShared<FJsonValueString>(Id));
				}
				Warden->SetArrayField(TEXT("evidence"), Ids);
				if (!State.WardenLastReason.IsEmpty())
				{
					Warden->SetStringField(TEXT("last_reason"), State.WardenLastReason.Left(32));
				}
				Warden->SetBoolField(TEXT("near"), State.bWardenNear);
				Obj->SetObjectField(TEXT("warden"), Warden);
			}
			return Obj;
		}

		FString GetStringOrEmpty(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field);

		/** rule JSON → FCXJeminiRule. when이 비거나 action이 없으면 false. */
		bool ParseRule(const TSharedPtr<FJsonObject>& Obj, FCXJeminiRule& Out)
		{
			if (!Obj.IsValid())
			{
				return false;
			}
			Out = FCXJeminiRule();
			const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
			if (Obj->TryGetArrayField(TEXT("when"), When) && When)
			{
				for (const TSharedPtr<FJsonValue>& Value : *When)
				{
					const TSharedPtr<FJsonObject> C = Value.IsValid() ? Value->AsObject() : nullptr;
					if (!C.IsValid())
					{
						continue;
					}
					FCXJeminiRuleCondition Cond;
					Cond.Type = GetStringOrEmpty(C, TEXT("type"));
					Cond.Who = GetStringOrEmpty(C, TEXT("who"));
					double Number = 0.0;
					for (const TCHAR* Field : { TEXT("cm"), TEXT("pct"), TEXT("n") })
					{
						if (C->TryGetNumberField(Field, Number))
						{
							Cond.Value = FMath::RoundToInt(Number);
						}
					}
					if (!Cond.Type.IsEmpty())
					{
						Out.When.Add(Cond);
					}
				}
			}
			const TSharedPtr<FJsonObject>* Do = nullptr;
			if (Obj->TryGetObjectField(TEXT("do"), Do) && Do)
			{
				Out.Action = GetStringOrEmpty(*Do, TEXT("action"));
				Out.ActionWho = GetStringOrEmpty(*Do, TEXT("who"));
			}
			double Reserve = 0.0;
			if (Obj->TryGetNumberField(TEXT("reserve_battery"), Reserve))
			{
				Out.ReserveBattery = FMath::Clamp(FMath::RoundToInt(Reserve), 0, 100);
			}
			Obj->TryGetBoolField(TEXT("repeat"), Out.bRepeat);
			Out.Label = GetStringOrEmpty(Obj, TEXT("label"));
			return Out.When.Num() > 0 && !Out.Action.IsEmpty();
		}

		FString ToJson(const TSharedRef<FJsonObject>& Obj)
		{
			FString Out;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
			return Out;
		}

		FString GetStringOrEmpty(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
		{
			FString Value;
			if (Obj.IsValid() && Obj->HasTypedField<EJson::String>(Field))
			{
				Value = Obj->GetStringField(Field);
			}
			return Value;
		}

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FEndpoint& Endpoint, const FString& Path)
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			FString Base = Endpoint.BaseUrl;
			Base.RemoveFromEnd(TEXT("/"));
			Request->SetURL(Base + Path);
			Request->SetVerb(TEXT("POST"));
			Request->SetTimeout(FMath::Max(1.0f, Endpoint.TimeoutSec));
			if (!Endpoint.ApiToken.IsEmpty())
			{
				Request->SetHeader(TEXT("X-API-Key"), Endpoint.ApiToken);
			}
			return Request;
		}

		void Dispatch(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request, int32 Battery, FOnCXJeminiResponse OnDone)
		{
			const double StartTime = FPlatformTime::Seconds();
			Request->OnProcessRequestComplete().BindLambda(
				[OnDone, Battery, StartTime](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
				{
					FCXJeminiResponse Result;
					const double Ms = (FPlatformTime::Seconds() - StartTime) * 1000.0;
					if (!bConnected || !Response.IsValid())
					{
						UE_LOG(LogCXJemini, Warning, TEXT("Jemini: request failed (no connection) after %.0f ms — is the server running?"), Ms);
						Result = MakeFallback(Battery, TEXT("NO_CONNECTION"));
					}
					else if (Response->GetResponseCode() != 200 || !ParseResponse(Response->GetContentAsString(), Result))
					{
						UE_LOG(LogCXJemini, Warning, TEXT("Jemini: HTTP %d after %.0f ms: %s"), Response->GetResponseCode(), Ms, *Response->GetContentAsString().Left(300));
						Result = MakeFallback(Battery, FString::Printf(TEXT("HTTP_%d"), Response->GetResponseCode()));
					}
					else
					{
						UE_LOG(LogCXJemini, Log, TEXT("Jemini: %s cmd=%s target=%s battery=%d reason=%s (%.0f ms) \"%s\""), *Result.Status, *Result.CommandName,
							*Result.CommandTarget, Result.BatteryAfter, *Result.Reason, Ms, *Result.Dialogue);
					}
					OnDone.ExecuteIfBound(Result);
				});
			if (!Request->ProcessRequest())
			{
				OnDone.ExecuteIfBound(MakeFallback(Battery, TEXT("REQUEST_NOT_STARTED")));
			}
		}

		void AppendUtf8(TArray<uint8>& Out, const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
	}

	FString StateToJson(const FCXJeminiState& State)
	{
		return ToJson(StateObject(State));
	}

	FCXJeminiResponse MakeFallback(int32 Battery, const FString& Reason)
	{
		static const TCHAR* Lines[] = {
			TEXT("앗, 신호가 끊겼어요. 다시 말해 줄래요?"),
			TEXT("지지직... 연결이 불안정해요. 한 번만 더요!"),
		};
		FCXJeminiResponse Out;
		Out.Status = TEXT("fallback");
		Out.Dialogue = Lines[FMath::RandRange(0, UE_ARRAY_COUNT(Lines) - 1)];
		Out.BatteryAfter = Battery;
		Out.Reason = Reason;
		return Out;
	}

	bool ParseResponse(const FString& Body, FCXJeminiResponse& Out)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->HasTypedField<EJson::String>(TEXT("status")))
		{
			return false;
		}
		Out = FCXJeminiResponse();
		Out.Status = Root->GetStringField(TEXT("status"));
		Out.Dialogue = GetStringOrEmpty(Root, TEXT("dialogue"));
		Out.Reason = GetStringOrEmpty(Root, TEXT("reason"));
		Out.Heard = GetStringOrEmpty(Root, TEXT("heard"));
		Root->TryGetBoolField(TEXT("mock"), Out.bMock);
		double Battery = 0.0;
		if (!Root->TryGetNumberField(TEXT("battery_after"), Battery))
		{
			return false;
		}
		Out.BatteryAfter = FMath::Clamp(FMath::RoundToInt(Battery), 0, 100);
		const TSharedPtr<FJsonObject>* Command = nullptr;
		if (Root->TryGetObjectField(TEXT("command"), Command) && Command && Command->IsValid())
		{
			Out.CommandName = GetStringOrEmpty(*Command, TEXT("name"));
			Out.CommandTarget = GetStringOrEmpty(*Command, TEXT("target"));
		}
		// 기능 A: 규칙 등록·취소·목록
		Out.RuleOp = GetStringOrEmpty(Root, TEXT("rule_op"));
		const TSharedPtr<FJsonObject>* RuleObj = nullptr;
		if (Root->TryGetObjectField(TEXT("rule"), RuleObj) && RuleObj)
		{
			Out.bHasRule = ParseRule(*RuleObj, Out.Rule);
		}
		double RuleId = 0.0;
		if (Root->TryGetNumberField(TEXT("rule_id"), RuleId))
		{
			Out.RuleId = FMath::RoundToInt(RuleId);
		}
		// 기능 C: 작전 제안(검증된 규칙 0~2개)
		const TArray<TSharedPtr<FJsonValue>>* Proposal = nullptr;
		if (Root->TryGetArrayField(TEXT("proposal"), Proposal) && Proposal)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Proposal)
			{
				FCXJeminiRule Rule;
				if (Value.IsValid() && ParseRule(Value->AsObject(), Rule) && Out.Proposal.Num() < 2)
				{
					Out.Proposal.Add(Rule);
				}
			}
		}
		return true;
	}

	void SendValidate(const FEndpoint& Endpoint, const FString& Action, const FString& Who, int32 ReserveBattery, const FCXJeminiState& State, FOnCXJeminiResponse OnDone)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("action"), Action);
		if (Who == TEXT("brother") || Who == TEXT("sister"))
		{
			Body->SetStringField(TEXT("who"), Who);
		}
		Body->SetNumberField(TEXT("reserve_battery"), FMath::Clamp(ReserveBattery, 0, 100));
		Body->SetObjectField(TEXT("state"), StateObject(State));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/validate"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		// LLM이 없는 검증이라 짧게(서버가 죽어 있으면 빨리 보류로)
		Request->SetTimeout(FMath::Clamp(Endpoint.TimeoutSec, 1.0f, 3.0f));
		TArray<uint8> Bytes;
		AppendUtf8(Bytes, ToJson(Body));
		Request->SetContent(MoveTemp(Bytes));
		Dispatch(Request, State.Battery, MoveTemp(OnDone));
	}

	void SendText(const FEndpoint& Endpoint, const FString& Text, const FCXJeminiState& State, FOnCXJeminiResponse OnDone)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("text"), Text.Left(300));
		Body->SetObjectField(TEXT("state"), StateObject(State));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/command"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		TArray<uint8> Bytes;
		AppendUtf8(Bytes, ToJson(Body));
		Request->SetContent(MoveTemp(Bytes));
		Dispatch(Request, State.Battery, MoveTemp(OnDone));
	}

	FHttpRequestPtr SendSttPartial(const FEndpoint& Endpoint, const TArray<uint8>& Wav, FOnCXJeminiPartial OnDone)
	{
		const FString Boundary = FString::Printf(TEXT("----CXJeminiStt%08x%08x"), FMath::Rand(), FMath::Rand());
		TArray<uint8> Body;
		Body.Reserve(Wav.Num() + 256);
		AppendUtf8(Body, FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"partial.wav\"\r\nContent-Type: audio/wav\r\n\r\n"), *Boundary));
		Body.Append(Wav);
		AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/stt/partial"));
		Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
		Request->SetContent(MoveTemp(Body));
		const double StartTime = FPlatformTime::Seconds();
		Request->OnProcessRequestComplete().BindLambda(
			[OnDone, StartTime](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
			{
				FString Text;
				bool bOk = false;
				if (bConnected && Response.IsValid() && Response->GetResponseCode() == 200)
				{
					TSharedPtr<FJsonObject> Root;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
					{
						Text = GetStringOrEmpty(Root, TEXT("text")).TrimStartAndEnd();
						bOk = !Text.IsEmpty();
					}
				}
				UE_LOG(LogCXJemini, Log, TEXT("Jemini STT partial: HTTP %d (%.0f ms) \"%s\""), Response.IsValid() ? Response->GetResponseCode() : 0,
					(FPlatformTime::Seconds() - StartTime) * 1000.0, *Text);
				OnDone.ExecuteIfBound(bOk, Text);
			});
		if (!Request->ProcessRequest())
		{
			OnDone.ExecuteIfBound(false, FString());
			return nullptr;
		}
		return Request;
	}

	void SendVoice(const FEndpoint& Endpoint, const TArray<uint8>& Wav, const FCXJeminiState& State, FOnCXJeminiResponse OnDone)
	{
		const FString Boundary = FString::Printf(TEXT("----CXJemini%08x%08x"), FMath::Rand(), FMath::Rand());
		TArray<uint8> Body;
		Body.Reserve(Wav.Num() + 1024);

		AppendUtf8(Body, FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"state\"\r\nContent-Type: application/json; charset=utf-8\r\n\r\n"), *Boundary));
		AppendUtf8(Body, StateToJson(State));
		AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n"), *Boundary));
		Body.Append(Wav);
		AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/command/voice"));
		Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
		Request->SetContent(MoveTemp(Body));
		Dispatch(Request, State.Battery, MoveTemp(OnDone));
	}
}
