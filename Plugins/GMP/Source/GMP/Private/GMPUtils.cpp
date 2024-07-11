//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPUtils.h"

#include "Engine/LatentActionManager.h"

namespace GMP
{
extern bool IsGMPModuleInited();

void FMessageUtils::UnbindMessage(const FMSGKEYFind& MessageId, const UObject* Obj)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(Obj))
#endif
	{
		GetMessageHub()->UnbindMessage(MessageId, Obj);
	}
}

void FMessageUtils::UnbindMessage(const FMSGKEYFind& MessageId, FGMPKey GMPKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(GMPKey))
#endif
	{
		GetMessageHub()->UnbindMessage(MessageId, GMPKey);
	}
}

void FMessageUtils::ScriptUnbindMessage(const FMSGKEYFind& K, FGMPKey InKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnbindMessage(K, InKey);
	}
}

void FMessageUtils::ScriptUnbindMessage(const FMSGKEYFind& K, const UObject* Listenner)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnbindMessage(K, Listenner);
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
