//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "GMPMessageKey.h"
#include "GMPSignalsImpl.h"
#include "GMPStruct.h"

namespace GMP
{
#if GMP_TRACE_MSG_STACK
	static TMap<FMSGKEY, TSet<FString>> MsgkeyLocations;
	static TArray<TPair<const void*, FString>> MsgKeyStack;
	static TArray<FString> BPMsgKeyStack;

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
