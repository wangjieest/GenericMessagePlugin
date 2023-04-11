//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPUtils.h"

#include "Engine/LatentActionManager.h"

namespace GMP
{
FLatentActionKeeper::FLatentActionKeeper(const FLatentActionInfo& LatentInfo)
	: ExecutionFunction(LatentInfo.ExecutionFunction)
	, LinkID(LatentInfo.Linkage)
	, CallbackTarget(LatentInfo.CallbackTarget)
{
}

void FLatentActionKeeper::SetLatentInfo(const struct FLatentActionInfo& LatentInfo)
{
	ExecutionFunction = LatentInfo.ExecutionFunction;
	LinkID = LatentInfo.Linkage;
	CallbackTarget = (const UObject*)LatentInfo.CallbackTarget;
}

bool FLatentActionKeeper::ExecuteAction(bool bClear) const
{
	if (LinkID != INDEX_NONE)
	{
		if (UObject* Target = CallbackTarget.Get())
		{
			if (UFunction* Function = Target->FindFunction(ExecutionFunction))
			{
				Target->ProcessEvent(Function, &LinkID);
				if (bClear)
					LinkID = INDEX_NONE;
				return true;
			}
		}
	}
	GMP_WARNING(TEXT("FExecutionInfo::DoCallback Failed."));
	return false;
}

extern bool IsGMPModuleInited();

void FMessageUtils::UnListenMessage(const FMSGKEYFind& MessageId, const UObject* Obj)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(Obj))
#endif
	{
		GetMessageHub()->UnListenMessage(MessageId, Obj);
	}
}

void FMessageUtils::UnListenMessage(const FMSGKEYFind& MessageId, FGMPKey GMPKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(GMPKey))
#endif
	{
		GetMessageHub()->UnListenMessage(MessageId, GMPKey);
	}
}

void FMessageUtils::ScriptUnListenMessage(const FMSGKEYFind& K, FGMPKey InKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnListenMessage(K, InKey);
	}
}

void FMessageUtils::ScriptUnListenMessage(const FMSGKEYFind& K, const UObject* Listenner)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnListenMessage(K, Listenner);
	}
}

void FMessageUtils::ScriptRemoveSigSource(const FSigSource InSigSrc)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		FSigSource::RemoveSource(InSigSrc);
	}
}

FMessageBody* FMessageUtils::GetCurrentMessageBody()
{
	return GetMessageHub()->GetCurrentMessageBody();
}

UGMPManager* FMessageUtils::GetManager()
{
	auto Ret = ::GetMutableDefault<UGMPManager>();
	GMP_CHECK(Ret && IsGMPModuleInited());
	return Ret;
}

FMessageHub* FMessageUtils::GetMessageHub()
{
	return &GetManager()->GetHub();
}

}  // namespace GMP
