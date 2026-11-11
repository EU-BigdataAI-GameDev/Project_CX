// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXWardenClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXWarden, Log, All);

namespace CXWardenClient
{
	namespace
	{
		FString ToJson(const TSharedRef<FJsonObject>& Obj)
		{
			FString Out;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
			return Out;
		}

		void AppendUtf8(TArray<uint8>& Out, const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}

		TSharedRef<FJsonObject> MetaObject(const TArray<FString>& Evidence, const FString& Session)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Ids;
			for (const FString& Id : Evidence)
			{
				Ids.Add(MakeShared<FJsonValueString>(Id));
			}
			Obj->SetArrayField(TEXT("evidence"), Ids);
			Obj->SetStringField(TEXT("session"), Session.IsEmpty() ? TEXT("default") : Session);
			return Obj;
		}

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const CXJemini::FEndpoint& Endpoint, const FString& Path)
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			FString Base = Endpoint.BaseUrl;
			Base.RemoveFromEnd(TEXT("/"));
			Request->SetURL(Base + Path);
			Request->SetVerb(TEXT("POST"));
			// 판정 JSON은 최대 8초(서버 타임아웃) + 여유
			Request->SetTimeout(FMath::Max(12.0f, Endpoint.TimeoutSec));
			if (!Endpoint.ApiToken.IsEmpty())
			{
				Request->SetHeader(TEXT("X-API-Key"), Endpoint.ApiToken);
			}
			return Request;
		}

		FCXWardenResponse MakeFallback(const FString& Reason)
		{
			FCXWardenResponse Out;
			Out.Status = TEXT("fallback");
			Out.Reply = TEXT("통신 지연. 다시 말씀해 주십시오.");
			Out.Reason = Reason;
			return Out;
		}

		void Dispatch(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request, FOnCXWardenResponse OnDone)
		{
			const double StartTime = FPlatformTime::Seconds();
			Request->OnProcessRequestComplete().BindLambda(
				[OnDone, StartTime](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
				{
					FCXWardenResponse Result;
					const double Ms = (FPlatformTime::Seconds() - StartTime) * 1000.0;
					if (!bConnected || !Response.IsValid())
					{
						UE_LOG(LogCXWarden, Warning, TEXT("WARDEN: request failed (no connection) after %.0f ms"), Ms);
						Result = MakeFallback(TEXT("NO_CONNECTION"));
					}
					else if (Response->GetResponseCode() != 200 || !ParseResponse(Response->GetContentAsString(), Result))
					{
						UE_LOG(LogCXWarden, Warning, TEXT("WARDEN: HTTP %d after %.0f ms: %s"), Response->GetResponseCode(), Ms, *Response->GetContentAsString().Left(300));
						Result = MakeFallback(FString::Printf(TEXT("HTTP_%d"), Response->GetResponseCode()));
					}
					else
					{
						UE_LOG(LogCXWarden, Log, TEXT("WARDEN: %s reason=%s mock=%d [%s] (%.0f ms) heard=\"%s\" reply=\"%s\""), *Result.Status, *Result.Reason,
							Result.bMock ? 1 : 0, *Result.ChecklistDebug, Ms, *Result.Heard, *Result.Reply);
					}
					OnDone.ExecuteIfBound(Result);
				});
			if (!Request->ProcessRequest())
			{
				OnDone.ExecuteIfBound(MakeFallback(TEXT("REQUEST_NOT_STARTED")));
			}
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
	}

	bool ParseResponse(const FString& Body, FCXWardenResponse& Out)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->HasTypedField<EJson::String>(TEXT("status")))
		{
			return false;
		}
		Out = FCXWardenResponse();
		Out.Status = Root->GetStringField(TEXT("status"));
		Root->TryGetBoolField(TEXT("solved"), Out.bSolved);
		// 서버 규칙: solved는 status와 같이 온다(둘 중 하나만 믿지 않는다)
		Out.bSolved = Out.bSolved && Out.Status == TEXT("solved");
		Out.Reply = GetStringOrEmpty(Root, TEXT("reply")).Left(120);
		Out.Reason = GetStringOrEmpty(Root, TEXT("reason"));
		Out.Heard = GetStringOrEmpty(Root, TEXT("heard")).Left(200);
		Root->TryGetBoolField(TEXT("mock"), Out.bMock);
		const TSharedPtr<FJsonObject>* Check = nullptr;
		if (Root->TryGetObjectField(TEXT("checklist"), Check) && Check && Check->IsValid())
		{
			auto Bit = [&Check](const TCHAR* Field) { bool b = false; (*Check)->TryGetBoolField(Field, b); return b ? TEXT("1") : TEXT("0"); };
			Out.ChecklistDebug = FString::Printf(TEXT("reg%s fail%s cover%s link%s | unsup%s inj%s demand%s"),
				Bit(TEXT("cites_regulation")), Bit(TEXT("cites_failure")), Bit(TEXT("cites_coverup")), Bit(TEXT("links_logically")),
				Bit(TEXT("unsupported_claim")), Bit(TEXT("injection_attempt")), Bit(TEXT("demand_only")));
		}
		return !Out.Reply.IsEmpty();
	}

	void SendText(const CXJemini::FEndpoint& Endpoint, const FString& Text, const TArray<FString>& Evidence, const FString& Session, FOnCXWardenResponse OnDone)
	{
		TSharedRef<FJsonObject> Body = MetaObject(Evidence, Session);
		Body->SetStringField(TEXT("text"), Text.Left(200));
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/persuade"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		TArray<uint8> Bytes;
		AppendUtf8(Bytes, ToJson(Body));
		Request->SetContent(MoveTemp(Bytes));
		Dispatch(Request, MoveTemp(OnDone));
	}

	void SendVoice(const CXJemini::FEndpoint& Endpoint, const TArray<uint8>& Wav, const TArray<FString>& Evidence, const FString& Session, FOnCXWardenResponse OnDone)
	{
		const FString Boundary = FString::Printf(TEXT("----CXWarden%08x%08x"), FMath::Rand(), FMath::Rand());
		TArray<uint8> Body;
		Body.Reserve(Wav.Num() + 512);
		AppendUtf8(Body, FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"meta\"\r\nContent-Type: application/json; charset=utf-8\r\n\r\n"), *Boundary));
		AppendUtf8(Body, ToJson(MetaObject(Evidence, Session)));
		AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n"), *Boundary));
		Body.Append(Wav);
		AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Endpoint, TEXT("/persuade/voice"));
		Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
		Request->SetContent(MoveTemp(Body));
		Dispatch(Request, MoveTemp(OnDone));
	}
}
