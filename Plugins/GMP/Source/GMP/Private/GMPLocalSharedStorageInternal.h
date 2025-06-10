//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPLocalSharedStorage.h"
#if UE_5_06_OR_LATER
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

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
	struct FPropertyStore
	{
		const FProperty* Prop = nullptr;
		size_t Addr[1];

		struct FDeleter
		{
			void operator()(FPropertyStore* Ptr) const
			{
				if (Ptr && Ptr->Prop)
				{
					Ptr->Prop->DestroyValue(Ptr->Addr);
				}
				FMemory::Free(Ptr);
			}
		};
	};

	using FPropertyStorePtr = TUniquePtr<FPropertyStore, FPropertyStore::FDeleter>;
	TMap<FName, FPropertyStorePtr> PropertyStore;
};
