//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPLocalSharedStorage.h"
#include "GMPLocalSharedStorageInternal.h"
#include "GMPWorldLocals.h"

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
	for (auto& Pair : This->StructMap)
	{
		Pair.Value.AddStructReferencedObjects(Collector);
	}
	Collector.AddReferencedObjects(This->ObjectMap);
}

bool ULocalSharedStorage::SetLocalSharedStorageImpl(UObject* InCtx, FName Key, ELocalSharedOverrideMode Mode, const FProperty* Prop, const void* Data)
{
	ULocalSharedStorageInternal* Mgr = nullptr;
	if (!InCtx || InCtx->IsA<UGameInstance>())
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(static_cast<UGameInstance*>(InCtx));
	}
	else if (auto LP = Cast<ULocalPlayer>(InCtx))
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(LP);
	}
	else
	{
		Mgr = GMP::WorldLocalObject<ULocalSharedStorageInternal>(InCtx);
	}

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
		constexpr size_t PropPtrSize = sizeof(FProperty*);
		constexpr size_t AddrOffset = STRUCT_OFFSET(ULocalSharedStorageInternal::FPropertyStore, Addr);
		static_assert(AddrOffset == PropPtrSize, "err");

		ULocalSharedStorageInternal::FPropertyStorePtr& StorePtr = Mgr->PropertyStore.FindOrAdd(Key);
		if (!StorePtr.IsValid() || Mode == ELocalSharedOverrideMode::Override)
		{
			static auto GetElementSize = [](const FProperty* Prop) {
#if UE_5_05_OR_LATER
				return Prop->GetElementSize();
#else
				return Prop->ElementSize;
#endif
			};
			auto Addr = static_cast<ULocalSharedStorageInternal::FPropertyStore*>(FMemory::Malloc(PropPtrSize + GetElementSize(Prop), Prop->GetMinAlignment()));
			Addr->Prop = Prop;
			Prop->InitializeValue(Addr->Addr);
			StorePtr.Reset(Addr);
			return true;
		}
	}
	return false;
}

void* ULocalSharedStorage::GetLocalSharedStorageImpl(UObject* InCtx, FName Key, const FProperty* Prop)
{
	ULocalSharedStorageInternal* Mgr = nullptr;
	if (!InCtx || InCtx->IsA<UGameInstance>())
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(static_cast<UGameInstance*>(InCtx));
	}
	else if (auto LP = Cast<ULocalPlayer>(InCtx))
	{
		Mgr = GMP::LocalObject<ULocalSharedStorageInternal>(LP);
	}
	else
	{
		Mgr = GMP::WorldLocalObject<ULocalSharedStorageInternal>(InCtx);
	}

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
		if (ULocalSharedStorageInternal::FPropertyStorePtr* StorePtr = Mgr->PropertyStore.Find(Key))
		{
			return (*StorePtr)->Addr;
		}
	}
	return nullptr;
}
