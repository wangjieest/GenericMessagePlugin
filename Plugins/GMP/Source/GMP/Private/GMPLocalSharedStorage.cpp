//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPLocalSharedStorage.h"
#include "GMPLocalSharedStorageInternal.h"
#include "GMPWorldLocals.h"

#if GMP_WITH_MSG_HOLDER
bool ULocalSharedStorage::SetLocalSharedMessage(const UObject* InCtx, FName Key, FGMPPropHeapHolderArray&& Params, ELocalSharedOverrideMode Mode)
{
	auto Mgr = GetInternal(InCtx);
	auto Find = Mgr->MessageHolders.Find(Key);
	if (!Find || Mode == ELocalSharedOverrideMode::Override)
	{
		Mgr->MessageHolders.FindOrAdd(Key) = MoveTemp(Params);
		return true;
	}
	return false;
}

const FGMPPropHeapHolderArray* ULocalSharedStorage::GetLocalSharedMessage(const UObject* InCtx, FName Key)
{
	if (!InCtx)
		return nullptr;
	auto Mgr = GetInternal(InCtx);
	auto Find = Mgr->MessageHolders.Find(Key);
	return Find;
}
#endif

DEFINE_FUNCTION(ULocalSharedStorage::execK2_SetLocalSharedStorage)
{
	P_GET_OBJECT(UObject, InCtx);
	P_GET_STRUCT(FName, Key);
	P_GET_ENUM(ELocalSharedOverrideMode, Mode);
	P_GET_UBOOL(bGameScope);
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	auto Prop = Stack.MostRecentProperty;
	auto Ptr = Stack.MostRecentPropertyAddress;
	P_FINISH

	P_NATIVE_BEGIN
	if (bGameScope)
		InCtx = GMP::WorldLocals::GetGameInstance(InCtx);
	*(bool*)RESULT_PARAM = SetLocalSharedStorageImpl(InCtx, Key, Mode, Prop, Ptr);
	P_NATIVE_END
}

DEFINE_FUNCTION(ULocalSharedStorage::execK2_GetLocalSharedStorage)
{
	P_GET_OBJECT(UObject, InCtx);
	P_GET_STRUCT(FName, Key);
	P_GET_UBOOL(bGameScope);
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	auto Prop = Stack.MostRecentProperty;
	auto Ptr = Stack.MostRecentPropertyAddress;
	P_FINISH

	P_NATIVE_BEGIN
	if (bGameScope)
		InCtx = GMP::WorldLocals::GetGameInstance(InCtx);
	auto Result = GetLocalSharedStorageImpl(InCtx, Key, Prop);
	*(bool*)RESULT_PARAM = !!Result;
	if (Result)
	{
		Prop->CopyCompleteValue(Ptr, Result);
	}
	P_NATIVE_END
}

void ULocalSharedStorageInternal::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULocalSharedStorageInternal* This = CastChecked<ULocalSharedStorageInternal>(InThis);
	Collector.AddReferencedObjects(This->ObjectMap);
	for (auto& Pair : This->StructMap)
	{
		Pair.Value.AddStructReferencedObjects(Collector);
	}
	for (auto& Pair : This->PropertyStores)
	{
		Pair.Value->AddStructReferencedObjects(Collector);
	}
	for (auto& Pair : This->MessageHolders)
	{
		for (auto& Holder : Pair.Value)
		{
			Holder.AddStructReferencedObjects(Collector);
		}
	}
}

bool ULocalSharedStorage::SetLocalSharedStorageImpl(const UObject* InCtx, FName Key, ELocalSharedOverrideMode Mode, const FProperty* Prop, const void* Data)
{
	auto Mgr = GetInternal(InCtx);

	if (auto StructProp = CastField<FStructProperty>(Prop))
	{
		FInstancedStruct* Find = Mgr->StructMap.Find(Key);
		if (!Find || Mode == ELocalSharedOverrideMode::Override)
		{
			Mgr->StructMap.FindOrAdd(Key).InitializeAs(StructProp->Struct, (const uint8*)Data);
			return true;
		}
	}
	else if (auto ObjPropBase = CastField<FObjectProperty>(Prop))
	{
		auto* ObjPtr = Mgr->ObjectMap.Find(Key);
		if (!ObjPtr || Mode == ELocalSharedOverrideMode::Override)
		{
			Mgr->ObjectMap.FindOrAdd(Key) = *(UObject**)Data;
			return true;
		}
	}
	else
	{
		ULocalSharedStorageInternal::FPropertyStorePtr& StorePtr = Mgr->PropertyStores.FindOrAdd(Key);
		if (!StorePtr.IsValid() || Mode == ELocalSharedOverrideMode::Override)
		{
			static auto GetElementSize = [](const FProperty* Prop) {
#if UE_5_05_OR_LATER
				return Prop->GetElementSize();
#else
				return Prop->ElementSize;
#endif
			};
			StorePtr.Reset(FGMPPropHeapHolder::MakePropHolder(Prop, Data, nullptr));
			return true;
		}
	}
	return false;
}

void* ULocalSharedStorage::GetLocalSharedStorageImpl(const UObject* InCtx, FName Key, const FProperty* Prop)
{
	auto Mgr = GetInternal(InCtx);
	if (auto StructProp = CastField<FStructProperty>(Prop))
	{
		if (FInstancedStruct* Find = Mgr->StructMap.Find(Key))
		{
			return Find->GetMutableMemory();
		}
	}
	else if (auto ObjProp = CastField<FObjectProperty>(Prop))
	{
		if (TObjectPtr<UObject>* ObjPtr = Mgr->ObjectMap.Find(Key))
		{
			return (*ObjPtr).Get();
		}
	}
	else
	{
		if (ULocalSharedStorageInternal::FPropertyStorePtr* StorePtr = Mgr->PropertyStores.Find(Key))
		{
			return (*StorePtr)->GetAddr();
		}
	}
	return nullptr;
}

ULocalSharedStorageInternal* ULocalSharedStorage::GetInternal(const UObject* InCtx)
{
	ULocalSharedStorageInternal* Mgr = nullptr;
	if (!InCtx || InCtx->IsA<UGameInstance>())
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(static_cast<const UGameInstance*>(InCtx));
	}
	else if (auto LP = Cast<ULocalPlayer>(InCtx))
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(LP);
	}
	else
	{
		Mgr = GMP::WorldLocalObject<ULocalSharedStorageInternal>(InCtx);
	}
	return Mgr;
}
