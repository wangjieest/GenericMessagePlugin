//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/World.h"
#include "GMPTypeTraits.h"
#include "Templates/SubclassOf.h"
#include "UObject/CoreNet.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#if defined(GENERICSTORAGES_API)
#define GMP_USING_WORLDLOCALSTORAGES 1
#else
#define GMP_USING_WORLDLOCALSTORAGES 0
#endif

#if GMP_USING_WORLDLOCALSTORAGES
#include "DeferredComponentRegistry.h"
#include "WorldLocalStorages.h"
namespace GMP
{
namespace WorldLocals
{
	template<typename ObjectType>
	WorldLocalStorages::TGenericWorldLocalStorage<ObjectType> Storage;
}

template<typename ObjectType>
decltype(auto) WorldLocalObject(const UObject* WorldContextObj)
{
	return WorldLocals::Storage<ObjectType>.GetLocalValue(WorldContextObj);
}
}  // namespace GMP
#else
#if WITH_EDITOR
#include "Editor.h"
#endif
namespace GMP
{
namespace WorldLocals
{
	template<typename U, typename V>
	static auto& FindOrAdd(U* InCtx, V& v)
	{
		for (int32 i = 0; i < v.Num(); ++i)
		{
			auto Ctx = v[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
					return v[i].Object;
			}
			else
			{
				v.RemoveAt(i);
				--i;
			}
		}
		auto& Ref = Add_GetRef(v);
		if (IsValid(InCtx))
			Ref.WeakCtx = InCtx;
		return Ref.Object;
	}

	template<typename U, typename V, typename F>
	auto& GetLocalVal(U* InCtx, V& v, const F& Ctor)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		auto& Ptr = FindOrAdd(InCtx, v);
		if (!Ptr.IsValid())
			Ctor(Ptr, InCtx);
		GMP_CHECK(Ptr.IsValid());
		return *Ptr.Get();
	}

	GMP_API void AddObjectReference(UObject* InCtx, UObject* Obj);

	template<typename U, typename ObjectType>
	struct TWorldLocalObjectPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TWeakObjectPtr<ObjectType> Object;
	};
	template<typename U, typename ObjectType>
	TArray<TWorldLocalObjectPair<U, ObjectType>, TInlineAllocator<4>> ObjectStorage;

	template<typename U, typename ObjectType>
	struct TWorldLocalSharedPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TSharedPtr<ObjectType> Object;
	};
	template<typename U, typename ObjectType>
	TArray<TWorldLocalSharedPair<U, ObjectType>, TInlineAllocator<4>> SharedStorage;

}  // namespace WorldLocals

template<typename ObjectType>
std::enable_if_t<std::is_base_of<UObject, ObjectType>::value, ObjectType*> GameLocalObject(const UObject* WorldContextObj)
{
	GMP_CHECK(WorldContextObj);
	return &WorldLocals::GetLocalVal<UGameInstance>(WorldContextObj->GetWorld() ? WorldContextObj->GetWorld()->GetGameInstance() : nullptr, WorldLocals::ObjectStorage<UGameInstance, ObjectType>, [&](auto& Ptr, auto* Inst) {
		auto Obj = NewObject<ObjectType>();
		Ptr = Obj;
		WorldLocals::AddObjectReference(Inst, Obj);
	});
}
template<typename ObjectType>
std::enable_if_t<!std::is_base_of<UObject, ObjectType>::value, ObjectType&> GameLocalObject(const UObject* WorldContextObj)
{
	GMP_CHECK(WorldContextObj);
	static auto& SharedStorage = WorldLocals::SharedStorage<UGameInstance, ObjectType>;
	return WorldLocals::GetLocalVal<UGameInstance>(WorldContextObj->GetWorld() ? WorldContextObj->GetWorld()->GetGameInstance() : nullptr, SharedStorage, [&](auto& Ref, auto* Inst) {
		Ref = MakeShared<ObjectType>();
#if WITH_EDITOR
		if (TrueOnFirstCall([] {}))
		{
			// FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { SharedStorage.RemoveAllSwap([&](auto& Cell) { return Cell.WeakCtx == InWorld; }); });
			FEditorDelegates::EndPIE.AddStatic([](const bool) { SharedStorage.Reset(); });
		}
#endif
	});
}

template<typename ObjectType>
std::enable_if_t<std::is_base_of<UObject, ObjectType>::value, ObjectType*> WorldLocalObject(const UObject* WorldContextObj)
{
	return &WorldLocals::GetLocalVal<UWorld>(WorldContextObj ? WorldContextObj->GetWorld() : nullptr, WorldLocals::ObjectStorage<UWorld, ObjectType>, [&](auto& Ptr, auto* World) {
		auto Obj = NewObject<ObjectType>();
		Ptr = Obj;
		WorldLocals::AddObjectReference(World, Obj);
	});
}

template<typename ObjectType>
std::enable_if_t<!std::is_base_of<UObject, ObjectType>::value, ObjectType&> WorldLocalObject(const UObject* WorldContextObj)
{
	static auto& SharedStorage = WorldLocals::SharedStorage<UWorld, ObjectType>;
	return WorldLocals::GetLocalVal<UWorld>(WorldContextObj ? WorldContextObj->GetWorld() : nullptr, SharedStorage, [&](auto& Ref, auto* World) {
		Ref = MakeShared<ObjectType>();
		if (TrueOnFirstCall([] {}))
		{
			FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { SharedStorage.RemoveAllSwap([&](auto& Cell) { return Cell.WeakCtx == InWorld; }); });
#if WITH_EDITOR
			FEditorDelegates::EndPIE.AddStatic([](const bool) { SharedStorage.Reset(); });
#endif
		}
	});
}
}  // namespace GMP
#endif
