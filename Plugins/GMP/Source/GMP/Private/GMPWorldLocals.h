//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "Engine/GameEngine.h"
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
#include "Engine/World.h"
namespace GMP
{
namespace WorldLocals
{
	template<typename V>
	static auto& FindOrAdd(UWorld* InWorld, V& v)
	{
		for (int32 i = 0; i < v.Num(); ++i)
		{
			auto World = v[i].WeakWorld;
			if (!World.IsStale(true))
			{
				if (InWorld == World.Get())
					return v[i].Object;
			}
			else
			{
				v.RemoveAt(i);
				--i;
			}
		}
		auto& Ref = Add_GetRef(v);
		if (IsValid(InWorld))
			Ref.WeakWorld = InWorld;
		return Ref.Object;
	}

	template<typename V, typename F>
	auto& GetLocalVal(V& v, const UObject* WorldContextObj, const F& Ctor)
	{
		UWorld* World = WorldContextObj ? WorldContextObj->GetWorld() : nullptr;
		GMP_CHECK(!IsGarbageCollecting() && (!World || IsValid(World)));
		auto& Ptr = FindOrAdd(World, v);
		if (!Ptr.IsValid())
			Ctor(Ptr, World);
		GMP_CHECK(Ptr.IsValid());
		return *Ptr.Get();
	}

	GMP_API void AddObjectReference(UWorld* World, UObject* Obj);

	template<typename ObjectType>
	struct TWorldLocalObjectPair
	{
		TWeakObjectPtr<UWorld> WeakWorld;
		TWeakObjectPtr<ObjectType> Object;
	};
	template<typename ObjectType>
	TArray<TWorldLocalObjectPair<ObjectType>, TInlineAllocator<4>> ObjectStorage;

	template<typename ObjectType>
	struct TWorldLocalSharedPair
	{
		TWeakObjectPtr<UWorld> WeakWorld;
		TSharedPtr<ObjectType> Object;
	};
	template<typename ObjectType>
	TArray<TWorldLocalSharedPair<ObjectType>, TInlineAllocator<4>> SharedStorage;

}  // namespace WorldLocals

template<typename ObjectType>
std::enable_if_t<std::is_base_of<UObject, ObjectType>::value, ObjectType&> WorldLocalObject(const UObject* WorldContextObj)
{
	return WorldLocals::GetLocalVal(WorldLocals::ObjectStorage<ObjectType>, WorldContextObj, [&](auto& Ptr, auto* World) {
		auto Obj = NewObject<ObjectType>();
		Ptr = Obj;
		WorldLocals::AddObjectReference(World, Obj);
	});
}

template<typename ObjectType>
std::enable_if_t<!std::is_base_of<UObject, ObjectType>::value, ObjectType&> WorldLocalObject(const UObject* WorldContextObj)
{
	return WorldLocals::GetLocalVal(WorldLocals::SharedStorage<ObjectType>, WorldContextObj, [&](auto& Ref, auto* World) {
		Ref = MakeShared<ObjectType>();
		if (TrueOnFirstCall([] {}))
		{
			FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { WorldLocals::SharedStorage<ObjectType>.RemoveAllSwap([&](auto& Cell) { return Cell.WeakWorld == InWorld; }); });
#if WITH_EDITOR
			FEditorDelegates::EndPIE.AddStatic([](const bool) { WorldLocals::SharedStorage<ObjectType>.Reset(); });
#endif
		}
	});
}
}  // namespace GMP
#endif
