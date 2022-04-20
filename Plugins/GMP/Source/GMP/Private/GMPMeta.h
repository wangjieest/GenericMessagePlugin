//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPMeta.generated.h"

USTRUCT()
struct FGMPTagMetaBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName Tag;

	UPROPERTY()
	TArray<FName> Parameters;

	UPROPERTY()
	TArray<FName> ResponseTypes;
};

USTRUCT()
struct FGMPTagMeta : public FGMPTagMetaBase
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString DecComment;
#endif
};

USTRUCT()
struct FGMPTagTypes
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FName> ParameterTypes;

	UPROPERTY()
	TArray<FName> ResponseTypes;
};

UCLASS(defaultconfig, config = GMPMeta)
class UGMPMeta : public UObject
{
	GENERATED_BODY()
public:
	UGMPMeta();
	GMP_API static const TArray<FName>* GetTagMeta(const UObject* InWorldContextObj, FName MsgTag);
	GMP_API static const TArray<FName>* GetSvrMeta(const UObject* InWorldContextObj, FName MsgTag);

protected:
	virtual void PostInitProperties() override;

	UPROPERTY()
	TMap<FName, FGMPTagTypes> GMPTypes;

	UPROPERTY(Config)
	TArray<FGMPTagMetaBase> MessageTagsList;

	void CollectTags(bool bSave);

#if WITH_EDITORONLY_DATA
	int32 GMPMetaVersion = 0;
	UPROPERTY(EditAnywhere, Config, Category = "GMPMeta")
	TArray<FString> GMPTagFileList;
#endif
};
