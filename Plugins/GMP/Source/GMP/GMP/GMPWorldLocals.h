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
	template<typename U, typename ObjectType>
	struct TWorldLocalObjectPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TWeakObjectPtr<ObjectType> Object;
	};
	// 	template<typename U, typename ObjectType>
	// 	TArray<TWorldLocalObjectPair<U, ObjectType>, TInlineAllocator<4>> ObjectStorage;
	template<template<typename> class C, typename U, typename ObjectType>
	C<TWorldLocalObjectPair<U, ObjectType>> TObjectStorage;

	template<typename U, typename T>
	struct TWorldLocalSharedPair
	{
		TWeakObjectPtr<U> WeakCtx;
		TSharedPtr<T> Object;
	};
	// 	template<typename U, typename T>
	// 	TArray<TWorldLocalSharedPair<U, T>, TInlineAllocator<4>> SharedStorage;

	template<template<typename> class C, typename U, typename T>
	C<TWorldLocalSharedPair<U, T>> TSharedStorage;

	template<typename U, typename S>
	auto& FindOrAdd(U* InCtx, S& Container)
	{
		for (int32 i = 0; i < Container.Num(); ++i)
		{
			auto Ctx = Container[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
					return Container[i].Object;
			}
			else
			{
				Container.RemoveAt(i);
				--i;
			}
		}

		const auto Index = Container.Emplace();
		auto& Ref = Container[Index];
		if (IsValid(InCtx))
			Ref.WeakCtx = InCtx;
		return Ref.Object;
	}
	template<typename U, typename S>
	auto Find(U* InCtx, S& Container) -> decltype(Container[0].Object.Get())
	{
		for (int32 i = 0; i < Container.Num(); ++i)
		{
			auto Ctx = Container[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
					return Container[i].Object.Get();
			}
			else
			{
				Container.RemoveAt(i);
				--i;
			}
		}
		return nullptr;
	}
	template<typename U, typename S>
	bool RemoveLocalVal(U* InCtx, S& Container)
	{
		for (int32 i = 0; i < Container.Num(); ++i)
		{
			auto Ctx = Container[i].WeakCtx;
			if (!Ctx.IsStale(true))
			{
				if (InCtx == Ctx.Get())
				{
					Container.RemoveAt(i);
					return true;
				}
			}
			else
			{
				Container.RemoveAt(i);
				--i;
			}
		}
		return false;
	}
	template<typename U, typename S, typename F>
	auto& GetLocalVal(U* InCtx, S& Container, const F& Ctor)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		auto& Ptr = FindOrAdd(InCtx, Container);
		if (!Ptr.IsValid())
			Ctor(Ptr, InCtx);
		GMP_CHECK(Ptr.IsValid());
		return *Ptr.Get();
	}
	template<typename U, typename S>
	auto FindLocalVal(U* InCtx, S& Container)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		return Find(InCtx, Container);
	}

	template<typename T>
	struct TLocalHolder
	{
		operator bool() const { return Val.IsValid(); }
		T* operator->() { return Val.Get(); }
		TLocalHolder() = default;
		TLocalHolder(TLocalHolder&&) = default;
		TLocalHolder& operator=(TLocalHolder&&) = default;
		void Steal(TSharedPtr<T>& In) { Val = MoveTemp(In); }

	protected:
		TSharedPtr<T> Val;
	};

	template<typename U, typename S>
	auto EraseStableVal(U* InCtx, S& Container, int32 Idx)
	{
		using T = std::remove_reference_t<decltype(*Container[0].Object.Get())>;
		TLocalHolder<T> Holder;
		if (Container.IsValidIndex(Idx))
		{
			Holder.Steal(Container[Idx].Object);
			Container.RemoveAt(Idx);
		}
		return Holder;
	}
	template<typename U, typename S>
	auto FindStableVal(U* InCtx, S& Container, int32 Idx) -> decltype(Container[0].Object.Get())
	{
		if (Container.IsValidIndex(Idx))
		{
			return Container[Idx].Object.Get();
		}
		return nullptr;
	}
	template<typename U, typename S>
	auto AllocStableVal(U* InCtx, S& Container, bool bErase = false)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		if (bErase)
		{
			for (int32 i = 0; i < Container.Num(); ++i)
			{
				auto Ctx = Container[i].WeakCtx;
				if (Ctx.IsStale(true))
				{
					Container.RemoveAt(i);
					--i;
				}
			}
		}

		const auto Index = Container.Emplace();
		auto& Ref = Container[Index];
		if (IsValid(InCtx))
			Ref.WeakCtx = InCtx;
		return Index;
	}
	template<typename U, typename S, typename F>
	int32 GetStableVal(U* InCtx, S& Container, const F& Ctor)
	{
		GMP_CHECK(!IsGarbageCollecting() && (!InCtx || IsValid(InCtx)));
		auto Index = AllocStableVal(InCtx, Container);
		Ctor(Container[Index].Object, InCtx);
		return Index;
	}
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

	template<typename U, template<typename> class C>
	struct TLocalOpsImpl
	{
		using S = std::conditional_t<std::is_same<U, UGameInstance>::value, UGameInstance, std::conditional_t<std::is_same<U, ULocalPlayer>::value, ULocalPlayer, UWorld>>;

		template<typename T>
		static void BindCleanup()
		{
			if (TrueOnFirstCall([] {}))
			{
				if constexpr (std::is_same<S, UWorld>::value)
				{
					FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) {
						auto& Container = GetStorage<T>();
						for (auto i = Container.Num() - 1; i >= 0; --i)
						{
							if (Container[i].WeakCtx == InWorld)
							{
								Container.RemoveAt(i);
								--i;
							}
						}
						// Container.Shrink();
					});
				}
#if WITH_EDITOR
				if (GIsEditor)
				{
					BindEditorEndDelegate(TDelegate<void(const bool)>::CreateLambda([](const bool) { GetStorage<T>().Empty(4); }));
				}
#endif
			}
		}

		template<typename T, typename F>
		static std::enable_if_t<std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& ObjCtor)
		{
			return &GetLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ptr, auto* Ctx) {
				GMP_LOG(TEXT("Allocating local object %s in %s"), ITS::TypeWStr<T>(), *GetNameSafe(Ctx));
				auto Obj = ObjCtor();
				Ptr = Obj;
				AddObjectReference(Ctx, Obj);
			});
		}
		template<typename T, typename F>
		static std::enable_if_t<!std::is_base_of<UObject, T>::value, T*> LocalObject(const UObject* WorldContextObj, const F& SharedCtor)
		{
			return &GetLocalVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ref, auto* Ctx) {
				GMP_LOG(TEXT("Allocating shared object %s in %s"), ITS::TypeWStr<T>(), *GetNameSafe(Ctx));
				Ref = SharedCtor();
				BindCleanup<T>();
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

		template<typename T, typename F>
		static std::enable_if_t<std::is_base_of<UObject, T>::value, int32> AllocStableLocal(const UObject* WorldContextObj, const F& ObjCtor)
		{
			return GetStableVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ptr, auto* Ctx) {
				GMP_LOG(TEXT("Allocating stable object %s in %s"), ITS::TypeWStr<T>(), *GetNameSafe(Ctx));
				auto Obj = ObjCtor();
				Ptr = Obj;
				AddObjectReference(Ctx, Obj);
			});
		}
		template<typename T, typename F>
		static std::enable_if_t<!std::is_base_of<UObject, T>::value, int32> AllocStableLocal(const UObject* WorldContextObj, const F& SharedCtor)
		{
			return GetStableVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), [&](auto& Ref, auto* Ctx) {
				GMP_LOG(TEXT("Allocating stable object %s in %s"), ITS::TypeWStr<T>(), *GetNameSafe(Ctx));
				Ref = SharedCtor();
				BindCleanup<T>();
			});
		}
		template<typename T>
		static auto EraseStableLocal(const UObject* WorldContextObj, int32 Idx)
		{
			return EraseStableVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), Idx);
		}
		template<typename T>
		static T* FindStableLocal(const UObject* WorldContextObj, int32 Idx)
		{
			return FindStableVal<S>(GetUObject(WorldContextObj), GetStorage<T>(), Idx);
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
				return TObjectStorage<C, S, T>;
			}
			else
			{
				return TSharedStorage<C, S, T>;
			}
		}
	};
	template<typename T>
	using TInlineArr = TArray<T, TInlineAllocator<4>>;
	template<typename U>
	using TInlineOps = TLocalOpsImpl<U, TInlineArr>;

	template<typename T>
	using TSparseArr = TSparseArray<T, TInlineSparseArrayAllocator<4>>;
	template<typename U>
	using TSparseOps = TLocalOpsImpl<U, TSparseArr>;
}  // namespace WorldLocals

template<typename T, typename U, typename F>
T* LocalObject(const U* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TInlineOps<U>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T, typename U>
T* LocalObject(const U* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<U>::template LocalObject<T>(WorldContextObj);
}
template<typename T, typename U>
T* LocalPtr(const U* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<U>::template LocalPtr<T>(WorldContextObj);
}
template<typename T, typename U>
bool RemoveLocal(const U* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<U>::template RemoveLocal<T>(WorldContextObj);
}

////////////////////////////////////////////////////////////////////////////////////////////////

template<typename T, typename F>
T* WorldLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TInlineOps<UWorld>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* WorldLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UWorld>::template LocalObject<T>(WorldContextObj);
}
template<typename T>
T* WorldLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UWorld>::template LocalPtr<T>(WorldContextObj);
}
template<typename T>
bool RemoveWorldLocal(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UWorld>::template RemoveLocal<T>(WorldContextObj);
}

template<typename T, typename F>
T* GameLocalObject(const UObject* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TInlineOps<UGameInstance>::template LocalObject<T>(WorldContextObj, Ctor);
}
template<typename T>
T* GameLocalObject(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UGameInstance>::template LocalObject<T>(WorldContextObj);
}
template<typename T>
T* GameLocalPtr(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UGameInstance>::template LocalPtr<T>(WorldContextObj);
}
template<typename T>
bool RemoveGameLocal(const UObject* WorldContextObj)
{
	return GMP::WorldLocals::TInlineOps<UGameInstance>::template RemoveLocal<T>(WorldContextObj);
}

////////////////////////////////////////////////////////////////////////////////////////////////

template<typename T, typename U, typename F>
int32 AllocStableObject(const U* WorldContextObj, const F& Ctor)
{
	return GMP::WorldLocals::TSparseOps<U>::template AllocStableLocal<T>(WorldContextObj, Ctor);
}
template<typename T, typename U>
T* FindStableObject(const U* WorldContextObj, int32 Idx)
{
	return GMP::WorldLocals::TSparseOps<U>::template FindStableLocal<T>(WorldContextObj, Idx);
}
template<typename T, typename U>
auto EraseStableObject(const U* WorldContextObj, int32 Idx)
{
	return GMP::WorldLocals::TSparseOps<U>::template EraseStableLocal<T>(WorldContextObj, Idx);
}
template<typename T, typename F>
FORCEINLINE int32 AllocStableObject(std::nullptr_t, const F& Ctor)
{
	return AllocStableObject<T, UGameInstance>(nullptr, Ctor);
}
template<typename T>
FORCEINLINE T* FindStableObject(std::nullptr_t, int32 Idx)
{
	return FindStableObject<T, UGameInstance>(nullptr, Idx);
}
template<typename T>
FORCEINLINE auto EraseStableObject(std::nullptr_t, int32 Idx)
{
	return EraseStableObject<T, UGameInstance>(nullptr, Idx);
}
}  // namespace GMP
