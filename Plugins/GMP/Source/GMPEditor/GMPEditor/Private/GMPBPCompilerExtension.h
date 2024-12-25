//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "BlueprintCompilerExtension.h"
#include "GMPBPCompilerExtension.generated.h"

UCLASS()
class UGMPBPCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

public:
	UGMPBPCompilerExtension(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};
