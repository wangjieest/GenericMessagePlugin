//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPTypeTraits.h"

namespace GMP
{
#define GMP_ATTACHED_FUNCTION_INLINE_SIZE 32
#define GMP_ATTACHED_FUNCTION_ALIGN_SIZE 16
struct FStorageErase
{
	enum : int32
	{
		kSLOT_STORAGE_INLINE_SIZE = GMP_ATTACHED_FUNCTION_INLINE_SIZE,
		kSLOT_STORAGE_INLINE_ALIGNMENT = GMP_ATTACHED_FUNCTION_ALIGN_SIZE,
	};
	void* Callable = nullptr;
	void* HeapAllocation = nullptr;
};

template<typename TSig>
struct TGMPFunctionRef;

template<typename T>
struct TIsGMPFunctionRef : std::false_type
{
};
template<typename TSig>
struct TIsGMPFunctionRef<TGMPFunctionRef<TSig>> : std::true_type
{
};

template<typename T>
using TDisableGMPFunctionRef = std::enable_if_t<!TIsTFunctionRef<T>::Value && !TIsGMPFunctionRef<std::decay_t<T>>::value, std::nullptr_t>;
#define GMP_SFINAE_DISABLE_FUNCTIONREF(T) TDisableGMPFunctionRef<T> = nullptr

template<typename Functor, typename TSig>
struct TFunctorInvoker;
template<typename Functor, typename R, typename... TArgs>
struct TFunctorInvoker<Functor, R(TArgs...)>
{
	static_assert(TypeTraits::IsSameV<R(TArgs...), typename TypeTraits::TSigTraits<Functor>::TFuncType>, "sig mismatch");
	static R StaticCall(void* Obj, TArgs... Args) { return Invoke(*(Functor*)Obj, Args...); }
};

template<typename R, typename... TArgs>
struct TGMPFunctionRef<R(TArgs...)> final : private FStorageErase
{
	template<typename F, GMP_SFINAE_DISABLE_FUNCTIONREF(F)>
	TGMPFunctionRef(F&& InFunc)
	{
		GMP_DEBUGVIEW_LOG(TEXT("TGMPFunctionRef::TGMPFunctionRef()"));
		BindRef(std::forward<F>(InFunc));
	}
	TGMPFunctionRef(const TGMPFunctionRef&) = default;

	R operator()(TArgs... Args) const { return reinterpret_cast<R (*)(void*, TArgs...)>(Callable)(HeapAllocation, Args...); }

protected:
	template<typename F>
	void BindRef(F&& InFunc)
	{
		HeapAllocation = (void*)std::addressof(InFunc);
		Callable = (void*)&TFunctorInvoker<F, R(TArgs...)>::StaticCall;
	}
};

template<typename Base, int32 INLINE_SIZE = FStorageErase::kSLOT_STORAGE_INLINE_SIZE>
struct TAttachedCallableStore;

struct IErasedObject
{
	virtual void* GetObjectAddress() = 0;
	virtual uint32 GetObjectSize() const = 0;
	virtual void PlacementDtor() = 0;
	virtual void* MoveConstruct(FStorageErase* Target, uint32 InlineSize) = 0;
	virtual ~IErasedObject() = default;
};

template<typename T>
struct TTypedObject final : public IErasedObject
{
	static_assert(TypeTraits::IsSameV<T, std::decay_t<T>>, "err");
	template<typename... ArgTypes>
	explicit TTypedObject(ArgTypes&&... Args)
		: Obj(std::forward<ArgTypes>(Args)...)
	{
	}
	TTypedObject(TTypedObject&& Other) = default;

	virtual void* GetObjectAddress() override { return &Obj; }
	virtual uint32 GetObjectSize() const override { return sizeof(T); }
	virtual void PlacementDtor() override { this->~TTypedObject(); }
	virtual void* MoveConstruct(FStorageErase* Target, uint32 InlineSize) override;

	T Obj;
};

template<int32 INLINE_SIZE = FStorageErase::kSLOT_STORAGE_INLINE_SIZE>
struct TStorageErase : protected FStorageErase
{
	TStorageErase() = default;

	TStorageErase(TStorageErase&& Other)
	{
		Swap(HeapAllocation, Other.HeapAllocation);
		FMemory::Memcpy(&InlineAllocation, &Other.InlineAllocation, sizeof(InlineAllocation));
	}

	TStorageErase& operator=(TStorageErase&& Other)
	{
		if (this != &Other)
		{
			DestroyObject();
			Swap(Callable, Other.Callable);
			Swap(HeapAllocation, Other.HeapAllocation);
			FMemory::Memcpy(&InlineAllocation, &Other.InlineAllocation, sizeof(InlineAllocation));
		}
		return *this;
	}

	TStorageErase(const TStorageErase& Other) = delete;
	TStorageErase& operator=(const TStorageErase& Other) = delete;

	IErasedObject* GetErasedWrapper() const
	{
		IErasedObject* Result = (IErasedObject*)HeapAllocation;
		if (!Result)
			Result = (IErasedObject*)&InlineAllocation;
		return Result;
	}

	void* GetObjectAddress() const
	{
		IErasedObject* Owned = (IErasedObject*)HeapAllocation;
		if (!Owned)
		{
			Owned = (IErasedObject*)&InlineAllocation;
		}
		return Owned->GetObjectAddress();
	}

	template<typename Functor, typename DT = std::decay_t<Functor>, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	DT* ConstructObject(Functor&& InFunctor)
	{
		using FunctorType = TTypedObject<DT>;
		constexpr bool bUseInline = sizeof(FunctorType) <= INLINE_SIZE;
		void* NewAlloc;
		GMP_IF_CONSTEXPR(!bUseInline)
		{
			NewAlloc = FMemory::Malloc(sizeof(FunctorType), alignof(FunctorType));
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::ConstructObject()::Malloc %p"), NewAlloc);
			HeapAllocation = NewAlloc;
		}
		else
		{
			NewAlloc = &InlineAllocation;
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::ConstructObject()::Inline %p"), NewAlloc);
		}
		auto* NewOwned = new (NewAlloc) FunctorType(std::forward<Functor>(InFunctor));
		ensure(NewAlloc == NewOwned);
		return &NewOwned->Obj;
	}

	void DestroyObject()
	{
		if (Callable)
		{
			IErasedObject* Owned = GetErasedWrapper();
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::DestroyObject() %p"), Owned);
			Owned->PlacementDtor();
			if (HeapAllocation)
			{
				FMemory::Free(HeapAllocation);
				HeapAllocation = nullptr;
			}
			Callable = nullptr;
		}
	}

	FORCEINLINE static constexpr auto offsetofINLINE() { return offsetof(TStorageErase<INLINE_SIZE>, InlineAllocation); }

protected:
	TAlignedBytes<INLINE_SIZE, kSLOT_STORAGE_INLINE_ALIGNMENT> InlineAllocation;
	template<typename>
	friend struct TTypedObject;
	template<typename, int32>
	friend struct TAttachedCallableStore;
};

template<>
struct TStorageErase<0> : public FStorageErase
{
	IErasedObject* GetErasedWrapper() const { return (IErasedObject*)HeapAllocation; }
	void* GetObjectAddress() const { return ((IErasedObject*)HeapAllocation)->GetObjectAddress(); }

	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	std::decay_t<Functor>* ConstructObject(Functor&& InFunctor, void* NewAlloc)
	{
		using FunctorType = TTypedObject<std::decay_t<Functor>>;
		GMP_DEBUGVIEW_LOG(TEXT("TStorageErase<0>::ConstructObject() %p"), NewAlloc);
		auto* NewOwned = new (NewAlloc) FunctorType(std::forward<Functor>(InFunctor));
		ensure(NewAlloc == NewOwned);
		return &NewOwned->Obj;
	}

	void DestroyObject()
	{
		if (Callable)
		{
			Callable = nullptr;
			IErasedObject* Owned = GetErasedWrapper();
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase<0>::DestroyObject() %p"), Owned);
			if (ensure(Owned))
				Owned->PlacementDtor();
		}
	}

	FORCEINLINE static constexpr auto offsetofINLINE() { return sizeof(FStorageErase); }
};

template<typename T>
void* TTypedObject<T>::MoveConstruct(FStorageErase* Target, uint32 InlineSize)
{
	check(Target && !Target->HeapAllocation);
	auto* Storage = static_cast<TStorageErase<sizeof(T)>*>(Target);
	void* NewAlloc;
	if (sizeof(TTypedObject) > InlineSize)
	{
		NewAlloc = FMemory::Malloc(sizeof(TTypedObject), alignof(TTypedObject));
		GMP_DEBUGVIEW_LOG(TEXT("TTypedObject::MoveConstruct()::Malloc [%p] On Inc[%p]"), NewAlloc, this);
		Storage->HeapAllocation = NewAlloc;
	}
	else
	{
		NewAlloc = &Storage->InlineAllocation;
		GMP_DEBUGVIEW_LOG(TEXT("TTypedObject::MoveConstruct()::Inline [%p] On Inc[%p]"), NewAlloc, this);
	}

	auto* NewOwned = new (NewAlloc) TTypedObject(std::move(this->Obj));
	return &NewOwned->Obj;
}

template<typename Base, int32 INLINE_SIZE>
struct TAttachedCallableStore : public Base
{
public:
	TAttachedCallableStore(std::nullptr_t = nullptr) { GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::TAttachedCallableStore(nullptr)")); }

	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	TAttachedCallableStore(Functor&& InFunc)
	{
		GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::TAttachedCallableStore(functor)"));
		Bind(std::forward<Functor>(InFunc));
	}

	TAttachedCallableStore(TAttachedCallableStore&& Other)
		: Base(static_cast<Base&&>(Other))
	{
		Move(std::move(Other));
	}

	TAttachedCallableStore& operator=(TAttachedCallableStore&& Other)
	{
		if (this != &Other)
		{
			*static_cast<Base*>(this) = static_cast<Base&&>(Other);
			Move(std::move(Other));
		}
		return *this;
	}

	TAttachedCallableStore(const TAttachedCallableStore& Other) = delete;
	TAttachedCallableStore& operator=(const TAttachedCallableStore& Other) = delete;

	~TAttachedCallableStore()
	{
		GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::~TAttachedCallableStore()"));
		Storage.DestroyObject();
	}

	explicit operator bool() const { return !!Storage.Callable; }
	using FStorageType = TStorageErase<INLINE_SIZE>;

	FORCEINLINE void CheckCallable() const { checkf(GetCallable() && GetObjectAddress(), TEXT("Attempting to call an unbound Function!")); }
	FORCEINLINE bool IsBound() const { return !!GetCallable(); }
	FORCEINLINE auto GetCallable() const { return Storage.Callable; }
	FORCEINLINE auto GetObjectAddress() const { return Storage.GetObjectAddress(); }

protected:
	template<typename B, int32 S>
	friend struct TAttachedCallableStore;

	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	void Bind(Functor&& InFunc)
	{
		if (auto* ErasedObj = Storage.ConstructObject(std::forward<Functor>(InFunc)))
		{
			using DecayedFunctor = std::remove_pointer_t<decltype(ErasedObj)>;
			using TFuncType = typename UnrealCompatibility::TFunctionTraits<DecayedFunctor>::TFuncType;
			Storage.Callable = (void*)&TFunctorInvoker<DecayedFunctor, TFuncType>::StaticCall;
			GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::Bind() ErasedObj %p"), ErasedObj);

#if GMP_ENABLE_DEBUGVIEW
			new ((void*)&DebugViewStorage) TDebugView<DecayedFunctor>;
			DebugViewStorage.Ptr = (void*)ErasedObj;
#endif
		}
	}

	template<typename B, int32 S>
	void Move(TAttachedCallableStore<B, S>&& Other, uint32 InSize = INLINE_SIZE)
	{
		Storage.DestroyObject();
#if GMP_ENABLE_DEBUGVIEW
		new ((void*)&DebugViewStorage) TDebugView<void>;
		DebugViewStorage.Ptr = nullptr;
#endif
		if (!Other.Storage.Callable)
			return;

		if (Other.Storage.HeapAllocation)
		{
			GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::MoveConstruct() MoveHeap %p"), Other.Storage.HeapAllocation);
			Swap(Storage.HeapAllocation, Other.Storage.HeapAllocation);
			Swap(Storage.Callable, Other.Storage.Callable);
#if GMP_ENABLE_DEBUGVIEW
			DebugViewStorage.Ptr = Storage.HeapAllocation;
#endif
		}
		else if (auto IObj = (IErasedObject*)&Other.Storage.InlineAllocation)
		{
			IObj->MoveConstruct(&Storage, InSize);
			Swap(Storage.Callable, Other.Storage.Callable);
#if GMP_ENABLE_DEBUGVIEW
			DebugViewStorage.Ptr = IObj;
#endif
		}
	}
#if GMP_ENABLE_DEBUGVIEW
	struct IDebugView
	{
		virtual ~IDebugView() {}
	};
	template<typename T>
	struct TDebugView : IDebugView
	{
		T* Ptr = 0;
	};
	TDebugView<void> DebugViewStorage;
#endif

	FORCEINLINE static constexpr auto offsetofINLINE() { return offsetof(TAttachedCallableStore, Storage) + FStorageType::offsetofINLINE(); }
	void DerivedClassesCannotAddMembersAnymore() { static_assert(sizeof(TAttachedCallableStore) == INLINE_SIZE + TAttachedCallableStore::offsetofINLINE(), "derived classes cannot add members anymore"); }
	FStorageType Storage;
};

namespace EmptyBase
{
	struct FEmptyBase
	{
	};
	using FCallableStore = TAttachedCallableStore<FEmptyBase>;
}  // namespace EmptyBase

template<typename TSig>
struct TGMPFunction;

template<typename T>
struct TIsGMPCallable : std::false_type
{
};
template<typename TSig>
struct TIsGMPCallable<TGMPFunction<TSig>> : std::true_type
{
};

template<typename R, typename... TArgs>
struct TGMPFunction<R(TArgs...)> final : public EmptyBase::FCallableStore
{
	TGMPFunction() = default;
	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	TGMPFunction(Functor&& Val)
		: EmptyBase::FCallableStore(std::forward<Functor>(Val))
	{
		static_assert(TypeTraits::IsSameV<R(TArgs...), TypeTraits::TSigFuncType<Functor>>, "sig mismatch");
	}
	TGMPFunction(TGMPFunction&& Val) = default;
	TGMPFunction& operator=(TGMPFunction&& Val) = default;

	R operator()(TArgs... Args) const
	{
		CheckCallable();
		return reinterpret_cast<R (*)(void*, TArgs...)>(GetCallable())(GetObjectAddress(), Args...);
	}
	FORCEINLINE explicit operator bool() const { return IsBound(); }
};

}  // namespace GMP
