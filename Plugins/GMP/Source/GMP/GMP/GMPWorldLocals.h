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
#include "Engine/GameInstance.h"

namespace GMP
{
namespace WorldLocals
{
	template<typename U, typename S>
	static auto& FindOrAdd(U* InCtx, S& s)
	{
		for (int32 i = 0; i < s.Num(); ++i)
		{
			auto Ctx = s[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
					return s[i].Object;
			}
			else
			{
				s.RemoveAt(i);
				--i;
			}
		}
		auto& Ref = Add_GetRef(s);
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

	template<typename U, typename S>
	static auto Find(U* InCtx, S& s) -> decltype(s[0].Object.Get())
	{
		for (int32 i = 0; i < s.Num(); ++i)
		{
			auto Ctx = s[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
					return s[i].Object.Get();
			}
			else
			{
				s.RemoveAt(i);
				--i;
			}
		}
		return nullptr;
	}
	template<typename U, typename S>
	auto FindLocalVal(U* InCtx, S& s)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		return Find(InCtx, s);
	}
	GMP_API void AddObjectReference(UObject* InCtx, UObject* Obj);
	GMP_API void BindEditorEndDelegate(TDelegate<void(const bool)> Delegate);

	template<typename U, typename ObjectType>
	struct TWorldLocalObjectPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TWeakObjectPtr<ObjectType> Object;
	};
	template<typename U, typename ObjectType>
	TArray<TWorldLocalObjectPair<U, ObjectType>, TInlineAllocator<4>> ObjectStorage;

	template<typename U, typename T>
	struct TWorldLocalSharedPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TSharedPtr<T> Object;
	};
	template<typename U, typename T>
	TArray<TWorldLocalSharedPair<U, T>, TInlineAllocator<4>> SharedStorage;

	inline UGameInstance* GetGameInstance(const UObject* InObj)
	{
		//GMP_CHECK_SLOW(InObj);
		return (InObj && InObj->GetWorld()) ? InObj->GetWorld()->GetGameInstance() : nullptr;
	}
	inline UWorld* GetWorld(const UObject* InObj)
	{
		//GMP_CHECK_SLOW(InObj);
		return InObj ? InObj->GetWorld() : nullptr;
	}

}  // namespace WorldLocals

template<typename ObjectType>
std::enable_if_t<std::is_base_of<UObject, ObjectType>::value, ObjectType*> WorldLocalObject(const UObject* WorldContextObj)
{
	return &GMP::WorldLocals::GetLocalVal<UWorld>(GMP::WorldLocals::GetWorld(WorldContextObj), GMP::WorldLocals::ObjectStorage<UWorld, ObjectType>, [&](auto& Ptr, auto* World) {
		auto Obj = NewObject<ObjectType>();
		Ptr = Obj;
		GMP::WorldLocals::AddObjectReference(World, Obj);
	});
}

template<typename T, typename F>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T&> WorldLocalObject(const UObject* WorldContextObj, const F& SharedCtor)
{
	static auto& SharedStorage = GMP::WorldLocals::SharedStorage<UWorld, T>;
	return GMP::WorldLocals::GetLocalVal<UWorld>(GMP::WorldLocals::GetWorld(WorldContextObj), SharedStorage, [&](auto& Ref, auto* World) {
		Ref = SharedCtor();
		if (TrueOnFirstCall([] {}))
		{
			FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { SharedStorage.RemoveAllSwap([&](auto& Cell) { return Cell.WeakCtx == InWorld; }); });
#if WITH_EDITOR
			GMP::WorldLocals::BindEditorEndDelegate(TDelegate<void(const bool)>::CreateLambda([](const bool) { SharedStorage.Reset(); }));
#endif
		}
	});
}
template<typename T>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T&> WorldLocalObject(const UObject* WorldContextObj)
{
	return WorldLocalObject<T>(WorldContextObj, [] { return MakeShared<T>(); });
}
template<typename T>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T*> WorldLocalPtr(const UObject* WorldContextObj)
{
	static auto& SharedStorage = GMP::WorldLocals::SharedStorage<UWorld, T>;
	return GMP::WorldLocals::FindLocalVal<UWorld>(GMP::WorldLocals::GetWorld(WorldContextObj), SharedStorage);
}

template<typename ObjectType>
std::enable_if_t<std::is_base_of<UObject, ObjectType>::value, ObjectType*> GameLocalObject(const UObject* WorldContextObj)
{
	GMP_CHECK(WorldContextObj);
	return &GMP::WorldLocals::GetLocalVal<UGameInstance>(GMP::WorldLocals::GetGameInstance(WorldContextObj), GMP::WorldLocals::ObjectStorage<UGameInstance, ObjectType>, [&](auto& Ptr, auto* Inst) {
		auto Obj = NewObject<ObjectType>();
		Ptr = Obj;
		GMP::WorldLocals::AddObjectReference(Inst, Obj);
	});
}
template<typename T, typename F>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T&> GameLocalObject(const UObject* WorldContextObj, const F& SharedCtor)
{
	static auto& SharedStorage = GMP::WorldLocals::SharedStorage<UGameInstance, T>;
	return GMP::WorldLocals::GetLocalVal<UGameInstance>(GMP::WorldLocals::GetGameInstance(WorldContextObj), SharedStorage, [&](auto& Ref, auto* Inst) {
		Ref = SharedCtor();
#if WITH_EDITOR
		if (TrueOnFirstCall([] {}))
		{
			GMP::WorldLocals::BindEditorEndDelegate(TDelegate<void(const bool)>::CreateLambda([](const bool) { SharedStorage.Reset(); }));
		}
#endif
	});
}
template<typename T>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T&> GameLocalObject(const UObject* WorldContextObj)
{
	return GameLocalObject<T>(WorldContextObj, [] { return MakeShared<T>(); });
}
template<typename T>
std::enable_if_t<!std::is_base_of<UObject, T>::value, T*> GameLocalPtr(const UObject* WorldContextObj)
{
	static auto& SharedStorage = GMP::WorldLocals::SharedStorage<UGameInstance, T>;
	return GMP::WorldLocals::FindLocalVal<UGameInstance>(GMP::WorldLocals::GetGameInstance(WorldContextObj), SharedStorage);
}
}  // namespace GMP
