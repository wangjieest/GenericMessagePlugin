//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Commandlets/Commandlet.h"

#include "XConsolePythonSupport.generated.h"

UCLASS(NotBlueprintType)
class UXConsolePythonSupport : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
#if WITH_EDITOR
public:
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static void XConsolePauseCommandPipeline(UWorld* InWorld, const FString& Reason);
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static void XConsoleContinueCommandPipeline(UWorld* InWorld, const FString& Reason);

	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static int32 XConsoleGetPipelineInteger();
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static void XConsoleSetPipelineInteger(const int32& InVal);

	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static FString XConsoleGetPipelineString();
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	static void XConsoleSetPipelineString(const FString& InVal);
#endif
	virtual bool IsEditorOnly() const override { return true; }
};

UCLASS(NotBlueprintType)
class UXConsoleExecCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UXConsoleExecCommandlet();

	virtual int32 Main(const FString& Params) override;
	virtual bool IsEditorOnly() const override { return true; }
};
