//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// HTTP transport implementation — the sole place that includes HTTPServer. A single host TU includes
// this file once and predefines MCP_HTTP_API to its export macro; every other module sees only the
// declarations in McpHttpBinding.h and needs no HTTPServer dependency of its own.
//
// Threading: FHttpServerModule ticks on the game thread, so handlers arrive there and the entry point
// asserts it. Tools may finish on any thread, so responses are marshalled back before OnComplete.
//
// Security: the endpoint is unauthenticated, so it must stay on loopback. Start() reads the listener
// configuration and refuses rather than silently exposing an editor control channel. It does not write
// that configuration — listeners are shared per port, so other subsystems would inherit the change.
#pragma once

#ifndef UNREAL_GMP_MCP_HTTPBINDING_INL
#define UNREAL_GMP_MCP_HTTPBINDING_INL

#include "Mcp/McpHttpBinding.h"

#if GMP_WITH_MCP

#include "Async/Async.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerConstants.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace MCP_NAMESPACE
{
namespace HttpBindingDetail
{

struct FBindingState
{
	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle RouteHandle;
	uint32 Port = 0;
};

inline FBindingState& State()
{
	static FBindingState Instance;
	return Instance;
}

// Mirrors FHttpServerConfig: a per-port ListenerOverrides entry wins over DefaultBindAddress.
inline FString ResolveBindAddress(uint32 Port)
{
	FString BindAddress = TEXT("localhost");
	if (!GConfig)
	{
		return BindAddress;
	}

	static const TCHAR* SectionName = TEXT("HTTPServer.Listeners");
	GConfig->GetString(SectionName, TEXT("DefaultBindAddress"), BindAddress, GEngineIni);

	TArray<FString> Overrides;
	if (GConfig->GetArray(SectionName, TEXT("ListenerOverrides"), Overrides, GEngineIni))
	{
		for (const FString& Entry : Overrides)
		{
			int32 EntryPort = 0;
			if (FParse::Value(*Entry, TEXT("Port="), EntryPort) && (uint32)EntryPort == Port)
			{
				FParse::Value(*Entry, TEXT("BindAddress="), BindAddress);
			}
		}
	}
	return BindAddress;
}

inline bool IsLoopbackAddress(const FString& BindAddress)
{
	return BindAddress.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) || BindAddress.Equals(TEXT("127.0.0.1")) || BindAddress.Equals(TEXT("::1"));
}

// TMap<FString, ...> compares keys case-sensitively, while HTTP header names are not.
inline const TArray<FString>* FindHeader(const FHttpServerRequest& Request, const TCHAR* Name)
{
	for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
	{
		if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

inline FString FindHeaderValue(const FHttpServerRequest& Request, const TCHAR* Name)
{
	const TArray<FString>* Values = FindHeader(Request, Name);
	return (Values && Values->Num() > 0) ? (*Values)[0] : FString();
}

// Absent Origin is the normal case for non-browser clients; a present one must be loopback.
inline bool IsOriginAllowed(const FHttpServerRequest& Request)
{
	const FString Origin = FindHeaderValue(Request, TEXT("origin"));
	if (Origin.IsEmpty())
	{
		return true;
	}
	return Origin.StartsWith(TEXT("http://localhost")) || Origin.StartsWith(TEXT("http://127.0.0.1")) || Origin.StartsWith(TEXT("https://localhost"))
		   || Origin.StartsWith(TEXT("https://127.0.0.1"));
}

// A missing header means 2025-03-26 per spec, so it is accepted alongside every revision we serve.
// Anything else is a hard 400.
inline bool IsProtocolVersionAcceptable(const FString& Version)
{
	return Version.IsEmpty() || Version.Equals(TEXT("2025-03-26")) || IsSupportedProtocolVersion(Version);
}

inline void Respond(const FHttpResultCallback& OnComplete, EHttpServerResponseCodes Code, const FString& Body, const FString& SessionId)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
	Response->Code = Code;
	// Stateless here, but echoing keeps a session-tracking client happy.
	if (!SessionId.IsEmpty())
	{
		Response->Headers.Add(TEXT("Mcp-Session-Id"), {SessionId});
	}
	OnComplete(MoveTemp(Response));
}

// Tools may complete off the game thread; HttpConnection is not thread safe.
inline void RespondOnGameThread(const FHttpResultCallback& OnComplete, EHttpServerResponseCodes Code, const FString& Body, const FString& SessionId)
{
	if (IsInGameThread())
	{
		Respond(OnComplete, Code, Body, SessionId);
		return;
	}
	AsyncTask(ENamedThreads::GameThread, [OnComplete, Code, Body, SessionId]() { Respond(OnComplete, Code, Body, SessionId); });
}

inline bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Guards a future transport that dispatches off-thread: editor state would be read under no lock.
	check(IsInGameThread());

	const FString SessionId = FindHeaderValue(Request, TEXT("mcp-session-id"));

	// 403 rather than 400: 2025-11-25 pinned this down for a rejected Origin.
	if (!IsOriginAllowed(Request))
	{
		Respond(OnComplete, EHttpServerResponseCodes::Forbidden, FString(), SessionId);
		return true;
	}

	// GET is the SSE stream; declining it is how a non-streaming server states it offers none.
	if (Request.Verb != EHttpServerRequestVerbs::VERB_POST)
	{
		Respond(OnComplete, EHttpServerResponseCodes::BadMethod, FString(), SessionId);
		return true;
	}

	if (!IsProtocolVersionAcceptable(FindHeaderValue(Request, TEXT("mcp-protocol-version"))))
	{
		Respond(OnComplete, EHttpServerResponseCodes::BadRequest, FString(), SessionId);
		return true;
	}

	const FString Body = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num()));

	// Returning true hands ownership of the answer to the dispatcher, which may complete later.
	FDispatcher::HandleMessage(Body,
		[OnComplete, SessionId](const FString& ResponseText)
		{
			// A notification produces no body; 202 is the spec's answer for it.
			const EHttpServerResponseCodes Code = ResponseText.IsEmpty() ? EHttpServerResponseCodes::Accepted : EHttpServerResponseCodes::Ok;
			RespondOnGameThread(OnComplete, Code, ResponseText, SessionId);
		});
	return true;
}

}  // namespace HttpBindingDetail

MCP_HTTP_API FHttpBinding::EStartResult FHttpBinding::Start(const FHttpBinding::FConfig& Config)
{
	using namespace HttpBindingDetail;

	FBindingState& Binding = State();
	if (Binding.RouteHandle.IsValid())
	{
		return EStartResult::AlreadyRunning;
	}

	if (Config.bLoopbackOnly && !IsLoopbackAddress(ResolveBindAddress(Config.Port)))
	{
		return EStartResult::NotLoopback;
	}

	// Shared per port: another subsystem may already own this listener.
	Binding.Router = FHttpServerModule::Get().GetHttpRouter(Config.Port);
	if (!Binding.Router.IsValid())
	{
		return EStartResult::RouterUnavailable;
	}

	Binding.RouteHandle = Binding.Router->BindRoute(FHttpPath(Config.Path), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateStatic(&HandleRequest));
	if (!Binding.RouteHandle.IsValid())
	{
		Binding.Router.Reset();
		return EStartResult::RouteBindFailed;
	}

	Binding.Port = Config.Port;
	FHttpServerModule::Get().StartAllListeners();
	return EStartResult::Started;
}

MCP_HTTP_API void FHttpBinding::Stop()
{
	using namespace HttpBindingDetail;

	FBindingState& Binding = State();
	if (Binding.Router.IsValid() && Binding.RouteHandle.IsValid())
	{
		Binding.Router->UnbindRoute(Binding.RouteHandle);
	}
	Binding.RouteHandle.Reset();
	Binding.Router.Reset();
	Binding.Port = 0;
}

MCP_HTTP_API bool FHttpBinding::IsRunning()
{
	return HttpBindingDetail::State().RouteHandle.IsValid();
}

MCP_HTTP_API uint32 FHttpBinding::GetPort()
{
	return HttpBindingDetail::State().Port;
}

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_HTTPBINDING_INL
