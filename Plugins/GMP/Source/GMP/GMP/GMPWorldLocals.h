//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
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
	template<typename U, typename S>
	bool RemoveLocalVal(U* InCtx, S& s)
	{
		for (int32 i = 0; i < s.Num(); ++i)
		{
			auto Ctx = s[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
				{
					s.RemoveAt(i);
					return true;
				}
			}
			else
			{
				s.RemoveAt(i);
				--i;
			}
		}
		return false;
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
	FORCEINLINE UObject* GetContext(const UObject* InObj)
	{
		if constexpr (std::is_same<U, UGameInstance>::value)
		{
			return GetGameInstance(InObj);
		}
		else if constexpr (std::is_same<U, UWorld>::value)
		{
			return GetWorld(InObj);
		}
		else
		{
			return (UObject*)InObj;
		}
	}

	template<typename U>
	struct TLocalOps
	{
		using S = std::conditional_t<std::is_same<U, UGameInstance>::value, UGameInstance, std::conditional_t<std::is_same<U, ULocalPlayer>::value, ULocalPlayer, UWorld>>;
		template<typename T, typename F>
		static std::enable_if_t<std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& ObjCtor)
		{
			return &GetLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ptr, auto* Ctx) {
				auto Obj = ObjCtor();
				Ptr = Obj;
				AddObjectReference(Ctx, Obj);
			});
		}
		template<typename T, typename F>
		static std::enable_if_t<!std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& SharedCtor)
		{
			return &GetLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ref, auto* Ctx) {
				Ref = SharedCtor();
				if (TrueOnFirstCall([] {}))
				{
					if constexpr (std::is_same<S, UWorld>::value)
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
		static bool RemoveLocal(const UObject* WorldContextObj)
		{
			return RemoveLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>());

		}

		template<typename T>
		static T* LocalPtr(const UObject* WorldContextObj)
		{
			return FindLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>());
		}

		static S* GetUObject(const UObject* WorldContextObj)
		{
			if constexpr (std::is_same<S, UGameInstance>::value)
			{
				return GetGameInstance(WorldContextObj);
			}
			else if constexpr (std::is_same<S, UWorld>::value)
			{
				return GetWorld(WorldContextObj);
			}
			else
			{
				return const_cast<S*>(CastChecked<S>(WorldContextObj));
			}
		}
		template<typename T>
		static auto& GetStorage()
		{
			if constexpr (std::is_base_of<UObject, T>::value)
			{
				return ObjectStorage<S, T>;
			}
			else
			{
				return SharedStorage<S, T>;
			}
		}
	};
}  // namespace WorldLocals

template<typename T, typename U, typename F>
T* LocalObject(const U* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TLocalOps<U>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T, typename U>
T* LocalObject(const U* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<U>::template LocalObject<T>(WorldContextObj);
}
template<typename T, typename U>
T* LocalPtr(const U* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<U>::template LocalPtr<T>(WorldContextObj);
}
template<typename T, typename U>
bool RemoveLocal(const U* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<U>::template RemoveLocal<T>(WorldContextObj);
}

template<typename T, typename F>
T* WorldLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* WorldLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::template LocalObject<T>(WorldContextObj);
}
template<typename T>
T* WorldLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::template LocalPtr<T>(WorldContextObj);
}
template<typename T>
bool RemoveWorldLocal(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UWorld>::template RemoveLocal<T>(WorldContextObj);
}

template<typename T, typename F>
T* GameLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* GameLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::template LocalObject<T>(WorldContextObj);
}
template<typename T>
T* GameLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::template LocalPtr<T>(WorldContextObj);
}
template<typename T>
bool RemoveGameLocal(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TLocalOps<UGameInstance>::template RemoveLocal<T>(WorldContextObj);
}

}  // namespace GMP
