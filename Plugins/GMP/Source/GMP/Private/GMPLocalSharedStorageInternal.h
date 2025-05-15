//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPLocalSharedStorage.h"
#include "InstancedStruct.h"

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
	UPROPERTY()
	TMap<FName, FInstancedStruct> StructMap;

	// auto gc
	UPROPERTY()
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
