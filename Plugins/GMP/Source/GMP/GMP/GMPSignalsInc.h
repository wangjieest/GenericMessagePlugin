//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPSignals.inl"
#include "UObject/Interface.h"

#include "GMPSignalsInc.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGMPSignalHandle : public UInterface
{
	GENERATED_BODY()
};

class GMP_API IGMPSignalHandle
{
	GENERATED_BODY()
public:
	void DisconnectAll() { GMPSignalHandle.DisconnectAll(); }
	void Disconnect(FGMPKey Key) { GMPSignalHandle.Disconnect(Key); }

protected:
	friend class GMP::FMessageHub;
	GMP::FSigHandle GMPSignalHandle;
};
