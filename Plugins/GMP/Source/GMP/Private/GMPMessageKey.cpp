//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "GMPMessageKey.h"
#include "GMPSignalsImpl.h"
#include "GMPStruct.h"
#include "XConsoleManager.h"

namespace GMP
{
static bool GGMPTraceScriptSource = true;
static FXConsoleVariableRef CVarGMPTraceScriptSource(
	TEXT("gmp.TraceScriptSource"),
	GGMPTraceScriptSource,
	TEXT("Collect Blueprint/Lua source locations for GMP message tags (editor only)."),
	ECVF_Default);

bool IsScriptSourceTraceEnabled()
{
	return GGMPTraceScriptSource;
}

#if GMP_TRACE_MSG_STACK
	static TMap<FMSGKEY, TSet<FString>> MsgkeyLocations;
	static TMap<FMSGKEY, TSet<FString>> MsgkeyListenLocations;
	static TMap<FMSGKEY, TSet<FString>> MsgkeyNotifyLocations;
	static TArray<TPair<const void*, FString>> MsgKeyStack;
#if GMP_TRACE_BP_STACK
	static TArray<FString> BPMsgKeyStack;
#endif

	const TCHAR* DebugNativeMsgFileLine(FName Key)
	{
		if (auto Find = MsgkeyLocations.Find(Key))
		{
			struct FTmpMsgLocation : public TThreadSingleton<FTmpMsgLocation>
			{
				TStringBuilder<2048> TmpMsgLocation;
			};
			auto& Ref = FTmpMsgLocation::Get().TmpMsgLocation;
			Ref.Reset();
			Ref.Append(FString::Join(*Find, TEXT(" | ")));
			return *Ref;
		}
		return TEXT("Unkown");
	}

	void GetMessageTagSourceLocations(FName MsgKey, TArray<FString>& OutLocations)
	{
		if (auto Find = MsgkeyLocations.Find(MsgKey))
		{
			OutLocations = Find->Array();
		}
	}

	// Records the current top-of-stack MSGKEY location into the listen/notify table; called from the hub where direction is known.
	void TraceMessageKeyDirection(FName MsgKey, bool bSend)
	{
		if (const auto Find = MsgkeyLocations.Find(MsgKey))
		{
			auto& Dst = bSend ? MsgkeyNotifyLocations : MsgkeyListenLocations;
			Dst.FindOrAdd(MsgKey).Append(*Find);
		}
	}

	void GetMessageTagSourceLocationsTyped(FName MsgKey, TArray<FString>& OutListen, TArray<FString>& OutNotify)
	{
		if (const auto Find = MsgkeyListenLocations.Find(MsgKey))
		{
			OutListen = Find->Array();
		}
		if (const auto Find = MsgkeyNotifyLocations.Find(MsgKey))
		{
			OutNotify = Find->Array();
		}
	}

	void TraceScriptMessageSource(FName MsgKey, const FString& Loc, bool bIsListen)
	{
		if (!IsScriptSourceTraceEnabled())
		{
			return;
		}
		auto& Dst = bIsListen ? MsgkeyListenLocations : MsgkeyNotifyLocations;
		Dst.FindOrAdd(MsgKey).Add(Loc);
	}

#else
	void GetMessageTagSourceLocations(FName MsgKey, TArray<FString>& OutLocations) {}
	void TraceMessageKeyDirection(FName MsgKey, bool bSend) {}
	void GetMessageTagSourceLocationsTyped(FName MsgKey, TArray<FString>& OutListen, TArray<FString>& OutNotify) {}
	void TraceScriptMessageSource(FName MsgKey, const FString& Loc, bool bIsListen) {}
#endif
	const TCHAR* DebugCurrentMsgFileLine()
	{
#if GMP_TRACE_MSG_STACK
		if (MsgKeyStack.Num() > 0)
		{
			if (auto MsgKey = GMP::FMSGKEYFind(*MsgKeyStack.Last().Value))
				return DebugNativeMsgFileLine(*MsgKeyStack.Last().Value);
		}
#endif
		return TEXT("Unkown");
	}
	
#if GMP_TRACE_MSG_STACK
#if GMP_TRACE_BP_STACK
	void GMPTraceEnterBP(const FString& MsgStr, FString&& Loc)
	{
		if (auto MsgKey = FMSGKEYFind(MsgStr))
		{
			MsgkeyLocations.FindOrAdd(MsgKey).Emplace(MoveTemp(Loc));
		}
		BPMsgKeyStack.Emplace(MsgStr);
	}

	void GMPTraceLeaveBP(const FString& MsgStr)
	{
		ensureAlways(MsgStr == BPMsgKeyStack.Pop(EAllowShrinking::No));
	}
#endif

	void MSGKEY_TYPE::GMPTraceEnter(const ANSICHAR* File, int32 Line)
	{
		if (FMSGKEYFind(*this))
		{
			MsgkeyLocations.FindOrAdd(*this).Emplace(FString::Printf(TEXT("%s:%d"), ANSI_TO_TCHAR(File), Line));
		}
		MsgKeyStack.Emplace(this->Ptr(), this->Ptr());
	}

	void MSGKEY_TYPE::GMPTraceLeave()
	{
		ensureAlways(this->Ptr() == MsgKeyStack.Pop(EAllowShrinking::No).Key);
	}


#endif

}
