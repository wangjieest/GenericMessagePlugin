//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "Templates/UnrealTemplate.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GMP/GMPClass2Prop.h"
#include "GMP/GMPPropHolder.h"
#include "GMPLocalSharedStorage.generated.h"

UENUM(BlueprintType)
enum ELocalSharedOverrideMode : uint8
{
	Skip,
	Override,
};

UCLASS(Transient)
class GMP_API ULocalSharedStorage : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	template<typename T>
	static bool SetLocalSharedStorage(const UObject* InCtx, FName Key, T& Data, ELocalSharedOverrideMode Mode = ELocalSharedOverrideMode::Skip)
	{
		return SetLocalSharedStorage(InCtx, Key, Mode, GMP::TClass2Prop<T>::GetProperty(), std::addressof(Data));
	}

	template<typename T>
	static const T* GetLocalSharedStorage(const UObject* InCtx, FName Key)
	{
		return static_cast<T*>(GetLocalSharedStorageImpl(InCtx, Key, GMP::TClass2Prop<T>::GetProperty()));
	}

	template<typename T>
	static const T& GetLocalSharedStorage(const UObject* InCtx, FName Key, T& Default)
	{
		auto Ret = GetLocalSharedStorage<T>(InCtx, Key);
		return Ret ? *Ret : Default;
	}
#if GMP_WITH_MSG_HOLDER
	static bool SetLocalSharedMessage(const UObject* InCtx, FName Key, FGMPPropHeapHolderArray&& Params, ELocalSharedOverrideMode Mode = ELocalSharedOverrideMode::Override);
	static const FGMPPropHeapHolderArray* GetLocalSharedMessage(const UObject* InCtx, FName Key);
#endif
protected:
	UFUNCTION(CustomThunk, BlueprintCallable, Category="GMP|LocalShared", meta = (DisplayName = "SetLocalSharedStorage", Mode = "0", WorldContext = "InCtx", CustomStructureParam = "Data"))
	static bool K2_SetLocalSharedStorage(UObject* InCtx, FName Key, ELocalSharedOverrideMode Mode, bool bGameScope, int32& Data);
	DECLARE_FUNCTION(execK2_SetLocalSharedStorage);

	UFUNCTION(CustomThunk, BlueprintCallable, Category="GMP|LocalShared", meta = (DisplayName = "GetLocalSharedStorage", WorldContext = "InCtx", CustomStructureParam = "Data"))
	static bool K2_GetLocalSharedStorage(UObject* InCtx, FName Key, bool bGameScope, int32& Data);
	DECLARE_FUNCTION(execK2_GetLocalSharedStorage);

private:
	static bool SetLocalSharedStorageImpl(const UObject* InCtx, FName Key, ELocalSharedOverrideMode Mode, const FProperty* Prop, const void* Data);
	static void* GetLocalSharedStorageImpl(const UObject* InCtx, FName Key, const FProperty* Prop);
	static class ULocalSharedStorageInternal* GetInternal(const UObject* InCtx);
};
