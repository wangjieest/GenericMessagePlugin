//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "GMPTypeTraits.h"
#include "Templates/SubclassOf.h"
#include "UObject/CoreNet.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "GMPRpcProxy.generated.h"

class APlayerController;

USTRUCT()
struct GMP_API FGMPRpcBatchData
{
	GENERATED_BODY()
public:
	FGMPRpcBatchData() = default;
	FGMPRpcBatchData(UObject* InObj, FString InKey, TArray<uint8>&& InBuff, bool bFunc)
		: Obj(InObj)
		, Key(MoveTemp(InKey))
		, Buff(MoveTemp(InBuff))
		, bFunction(bFunc)
	{
	}

	UPROPERTY()
	UObject* Obj = nullptr;

	UPROPERTY()
	FString Key;

	UPROPERTY()
	TArray<uint8> Buff;

	UPROPERTY()
	bool bFunction = false;
};

UCLASS(Transient)
class UGMPPropertiesContainer final : public UStruct
{
	GENERATED_BODY()
public:
	FName FindPropertyName(const FProperty* Property);
	virtual bool CanBeClusterRoot() const { return !UE_4_25_OR_LATER; }

protected:
	virtual void AddCppProperty(FProperty* Property) override;
	
	TMap<FProperty*, FName> FastLookups;
};

UCLASS(Transient)
class UGMPRpcValidation final : public UObject
{
	GENERATED_BODY()
public:
	static bool VerifyRpc(const UObject* Obj, const FName& MessageKey, const TArray<FProperty*>& Props);
	static const TArray<FProperty*>* Find(const UObject* Obj, const FName& MessageKey);

	TMap<FName, TArray<FProperty*>> RPCProcessors;

	static int32 GetNextPlayerSequence(const APlayerController& PC);
	int32 PlayerSequenceID = 0;
};

namespace GMP
{
struct FGMPRpcBatchScope;
}

//////////////////////////////////////////////////////////////////////////
UCLASS(NotBlueprintType, NotBlueprintable, Within = PlayerController)
class GMP_API UGMPRpcProxy final : public UActorComponent
{
	GENERATED_BODY()
public:
	UGMPRpcProxy();
	static const int32 MaxByteCount;

protected:
	virtual void BeginPlay() override;
	virtual void InitializeComponent() override;

	//////////////////////////////////////////////////////////////////////////
protected:
	bool CallLocalMessage(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer);
	bool LocalBroadcastMessage(const FString& MessageStr, const TArray<FProperty*>& Props, const UObject* InObject, const TArray<uint8>& Buffer);

	UFUNCTION(Server, Reliable, WithValidation)
	void Message_Request(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer);
	UFUNCTION(Client, Reliable)
	void Message_Notify(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer);
	UFUNCTION(Client, unreliable)
	void Unreliable_Notify(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer);

	//////////////////////////////////////////////////////////////////////////
protected:
	void CallLocalFunction(UObject* InUserObject, FName InFunctionName, const TArray<uint8>& Buffer);
	UFUNCTION(Server, Reliable, WithValidation)
	void RPC_Request(UObject* Object, const FString& FuncName, const TArray<uint8>& Buffer);
	UFUNCTION(Client, Reliable)
	void RPC_Notify(UObject* Object, const FString& FuncName, const TArray<uint8>& Buffer);

protected:
	void DispatchPendingProgress(const TArray<FGMPRpcBatchData>& Batcher);
	UFUNCTION(Server, Reliable, WithValidation)
	void Batch_Request(const TArray<FGMPRpcBatchData>& Batcher);
	UFUNCTION(Client, Reliable)
	void Batch_Notify(const TArray<FGMPRpcBatchData>& Batcher);

	UPROPERTY(Transient)
	TArray<FGMPRpcBatchData> PendingRPCs;
	int32 ScopedCnt = 0;

	void FlushPendingRPCs();
	static int32 IncreaseBatchRef(UGMPRpcProxy* Proxy) { return Proxy ? ++Proxy->ScopedCnt : 0; }
	static void DecreaseBatchRef(UGMPRpcProxy* Proxy)
	{
		if (Proxy && Proxy->ScopedCnt > 0 && --Proxy->ScopedCnt == 0)
			Proxy->FlushPendingRPCs();
	}
	friend struct FGMPRpcBatchScope;
public:
	static void CallMessageRemote(APlayerController* PC, const UObject* Sender, const FString& MessageStr, TArray<uint8>& Buffer, bool bReliable = true);
	static bool CallFunctionRemote(APlayerController* PC, UObject* InUserObject, FName InFunctionName, TArray<uint8>& Buffer);
};

//////////////////////////////////////////////////////////////////////////
UCLASS(NotBlueprintType, NotBlueprintable)
class GMP_API UGMPNewPawnPossessedBinder final : public UActorComponent
{
	GENERATED_BODY()
public:
	UGMPNewPawnPossessedBinder();

protected:
	virtual void InitializeComponent() override;
};
