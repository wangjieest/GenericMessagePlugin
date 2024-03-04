//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NeuronActionFactory.generated.h"

/*
 * just a entry for factory functions
 * extend factory function by derived fromt this class 
 */
UCLASS(Abstract, Const, Transient, notplaceable, NotBlueprintable, NotBlueprintType, BlueprintInternalUseOnly, meta = (NeuronAction))
class GMP_API UNeuronActionFactory : public UObject
{
	GENERATED_BODY()
};
