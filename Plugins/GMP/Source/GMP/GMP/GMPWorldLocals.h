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

	GMP_API void AddObjectReference(UObject* InCtx, UObject* Obj);
	GMP_API void BindEditorEndDelegate(TDelegate<void(const bool)> Delegate);
	GMP_API UGameInstance* GetGameInstance(const UObject* InObj);
	GMP_API UWorld* GetWorld(const UObject* InObj);

	template<typename U>
	struct TLocalOps
	{
		template<typename T, typename F>
		static std::enable_if_t<std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& ObjCtor)
		{
			return &GetLocalVal<U>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ptr, auto* Ctx) {
				auto Obj = ObjCtor();
				Ptr = Obj;
				AddObjectReference(Ctx, Obj);
			});
		}
		template<typename T, typename F>
		static std::enable_if_t<!std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& SharedCtor)
		{
			return &GetLocalVal<U>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ref, auto* Ctx) {
				Ref = SharedCtor();
				if (TrueOnFirstCall([] {}))
				{
					if constexpr (std::is_same<U, UWorld>::value)
					{
						FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { GetStorage<T>().RemoveAllSwap([&](auto& Cell) { return Cell.WeakCtx == InWorld; }); });
					}
#if WITH_EDITOR
					if (GIsEditor)
					{
						BindEditorEndDelegate(TDelegate<void(const bool)>::CreateLambda([](const bool) { GetStorage<T>().Reset(); }));
					}
#endif
				}
			});
		}

		template<typename T>
		static T* LocalObject(const UObject* WorldContextObj)
		{
			return LocalObject<T>(WorldContextObj, [] {
				if constexpr (std::is_base_of<UObject, T>::value)
				{
					return NewObject<T>();
				}
				else
				{
					return MakeShared<T>();
				}
			});
		}

		template<typename T>
		static T* LocalPtr(const UObject* WorldContextObj)
		{
			return FindLocalVal<U>(GetUObject(WorldContextObj), GetStorage<T>());
		}

		static U* GetUObject(const UObject* WorldContextObj)
		{
			if constexpr (std::is_same<U, UWorld>::value)
			{
				return GetWorld(WorldContextObj);
			}
			else
			{
				return GetGameInstance(WorldContextObj);
			}
		}
		template<typename T>
		static auto& GetStorage()
		{
			if constexpr (std::is_base_of<UObject, T>::value)
			{
				return ObjectStorage<U, T>;
			}
			else
			{
				return SharedStorage<U, T>;
			}
		}
	};
}  // namespace WorldLocals

template<typename T, typename F>
T* WorldLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* WorldLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::LocalObject<T>(WorldContextObj);
}
template<typename T>
T* WorldLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::LocalPtr<T>(WorldContextObj);
}

template<typename T, typename F>
T* GameLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* GameLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::LocalObject<T>(WorldContextObj);
}
template<typename T>
T* GameLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::LocalPtr<T>(WorldContextObj);
}
}  // namespace GMP
