//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPRpcUtils.h"

#include "Engine/Engine.h"
#include "GMPBPLib.h"
#include "GMPRpcProxy.h"
#include "GameFramework/PlayerController.h"

namespace GMP
{
FString FRpcMessageUtils::ProxyGetNameSafe(APlayerController* PC)
{
	return ::GetNameSafe(PC);
}

UPackageMap* FRpcMessageUtils::GetPackageMap(APlayerController* PC)
{
	return UGMPBPLib::GetPackageMap(PC);
}

const int32 FRpcMessageUtils::GetMaxBytes()
{
	return UGMPRpcProxy::MaxByteCount;
}

void FRpcMessageUtils::PostRPCMsg(APlayerController* PC, UObject* Sender, const FString& MessageStr, TArray<uint8>& Buffer, bool bReliable)
{
	UGMPRpcProxy::CallMessageRemote(PC, Sender, MessageStr, Buffer, bReliable);
}

APlayerController* FRpcMessageUtils::GetLocalPC(UObject* Obj)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(Obj, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (!World->GetAuthGameMode())
			return World->GetFirstPlayerController();
	}
	return nullptr;
}

bool FRpcMessageUtils::Z_VerifyRPC(APlayerController* PC, const UObject* Obj, const FMSGKEY& MessageName, const TArray<FProperty*>& Props)
{
	UObject* WorldContext = GEngine->GetWorldFromContextObject(Obj, EGetWorldErrorMode::LogAndReturnNull);
	WorldContext = WorldContext ? WorldContext : PC;
	return UGMPRpcValidation::VerifyRpc(WorldContext, MessageName, Props);
}

int32 FRpcMessageUtils::GetPlayerLocalSequence(const APlayerController& PC)
{
	return UGMPRpcValidation::GetNextPlayerSequence(PC);
}
}  // namespace GMP

FGMPRpcBatchScope::FGMPRpcBatchScope(UGMPRpcProxy* InProxy)
	: Proxy(InProxy)
{
	UGMPRpcProxy::IncreaseBatchRef(Proxy);
}

FGMPRpcBatchScope::FGMPRpcBatchScope(APlayerController* PC)
	: FGMPRpcBatchScope(PC ? PC->FindComponentByClass<UGMPRpcProxy>() : (UGMPRpcProxy*)nullptr)
{
#if WITH_EDITOR
	VerifyFrameNumber = GFrameNumber;
#endif
}

FGMPRpcBatchScope::~FGMPRpcBatchScope()
{
#if WITH_EDITOR
	if (ensureAlways(VerifyFrameNumber == GFrameNumber))
#endif
	{
		UGMPRpcProxy::DecreaseBatchRef(Proxy);
	}
}
