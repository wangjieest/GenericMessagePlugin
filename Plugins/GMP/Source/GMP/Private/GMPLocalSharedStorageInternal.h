//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPLocalSharedStorage.h"
#if UE_5_05_OR_LATER
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif
#include "GMPStruct.h"
#include "GMPLocalSharedStorageInternal.generated.h"


UCLASS(Transient)
class ULocalSharedStorageInternal : public UObject
{
	GENERATED_BODY()
public:
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

protected:
	friend class ULocalSharedStorage;
	// auto gc
	UPROPERTY(Transient)
	TMap<FName, FInstancedStruct> StructMap;

	// auto gc
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UObject>> ObjectMap;

	// no gc
	using FPropertyStorePtr = TUniquePtr<FGMPPropHeapHolder>;
	TMap<FName, FPropertyStorePtr> PropertyStores;

	// msgs
	TMap<FName, FGMPPropHeapHolderArray> MessageHolders;
};
