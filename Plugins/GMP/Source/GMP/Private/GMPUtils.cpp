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

void FMessageUtils::UnListenMessage(const FMSGKEYFind& MessageId, const UObject* Obj)
{
	if (ensure(Obj))
		GetMessageHub()->UnListenMessage(MessageId, Obj);
}

void FMessageUtils::UnListenMessage(const FMSGKEYFind& MessageId, FGMPKey GMPKey)
{
	if (ensure(GMPKey))
		GetMessageHub()->UnListenMessage(MessageId, GMPKey);
}

FMessageBody* FMessageUtils::GetCurrentMessageBody()
{
	return GetMessageHub()->GetCurrentMessageBody();
}

UGMPManager* FMessageUtils::GetManager()
{
	auto Ret = ::GetMutableDefault<UGMPManager>();
	GMP_CHECK(Ret);
	return Ret;
}

FMessageHub* FMessageUtils::GetMessageHub()
{
	return &GetManager()->GetHub();
}

}  // namespace GMP
