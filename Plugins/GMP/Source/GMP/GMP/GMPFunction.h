//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPTypeTraits.h"

namespace GMP
{
#ifndef GMP_FUNCTION_DEBUGVIEW
#define GMP_FUNCTION_DEBUGVIEW (WITH_EDITOR && PLATFORM_WINDOWS)
#endif
#ifndef GMP_FUNCTION_PREDEFINED_INLINE_SIZE
#define GMP_FUNCTION_PREDEFINED_INLINE_SIZE 32
#endif
#ifndef GMP_FUNCTION_PREDEFINED_ALIGN_SIZE
#define GMP_FUNCTION_PREDEFINED_ALIGN_SIZE 16
#endif

#ifndef GMP_FUNCTION_USING_TAGGED_INLNE_SIZE
#define GMP_FUNCTION_USING_TAGGED_INLNE_SIZE 1
#endif

struct alignas(GMP_FUNCTION_PREDEFINED_ALIGN_SIZE) FStorageEraseBase
{
	void* Callable = nullptr;
	void* HeapAllocation = nullptr;

	static constexpr int32_t kInlineSize = GMP_FUNCTION_PREDEFINED_INLINE_SIZE;
	static constexpr int32_t kAlignSize = GMP_FUNCTION_PREDEFINED_ALIGN_SIZE;
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
#define GMP_SFINAE_DISABLE_FUNCTIONREF(T) GMP::TDisableGMPFunctionRef<T> = nullptr

template<typename Functor, typename TSig>
struct TFunctorInvoker;
template<typename Functor, typename R, typename... TArgs>
struct TFunctorInvoker<Functor, R(TArgs...)>
{
	static_assert(TypeTraits::IsSameV<R(TArgs...), typename TypeTraits::TSigTraits<Functor>::TFuncType>, "sig mismatch");
	static R StaticCall(void* Obj, TArgs... Args) { return Invoke(*(Functor*)Obj, Args...); }
};

template<typename R, typename... TArgs>
struct TGMPFunctionRef<R(TArgs...)> final : private FStorageEraseBase
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

struct FStorageErase;
struct IErasedObject
{
	virtual void* GetObjectAddress() = 0;
	virtual uint32_t GetObjectSize() const = 0;
	virtual void PlacementDtor() = 0;
	virtual void* MoveConstruct(FStorageErase* Target, uint32_t InlineSize) = 0;
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
	virtual uint32_t GetObjectSize() const override { return sizeof(T); }
	virtual void PlacementDtor() override { this->~TTypedObject(); }
	virtual void* MoveConstruct(FStorageErase* Target, uint32_t InlineSize) override;

	T Obj;
};

struct FStorageErase : public FStorageEraseBase
{
	FORCEINLINE void* GetHeapAllocationImpl() const { return (void*)((std::size_t)HeapAllocation & (SIZE_MAX - 1u)); }
	void SetHeapAllocation(void* InAddr) { HeapAllocation = InAddr; }

	void* HeapMalloc(SIZE_T Count, uint32_t Alignment)
	{
		auto NewAlloc = FMemory::Malloc(Count, Alignment);
		SetHeapAllocation(NewAlloc);
		return NewAlloc;
	}
	void HeapFree()
	{
		if (auto HeapPtr = GetHeapAllocation())
			FMemory::Free(HeapPtr);
		HeapAllocation = nullptr;
	}

#if !GMP_FUNCTION_USING_TAGGED_INLNE_SIZE
	FORCEINLINE bool UseInlineData() const { return false; }
	FORCEINLINE void* GetHeapAllocation() const { return HeapAllocation; }
	FORCEINLINE int32_t InlineOffset() const { return 0; }
	FORCEINLINE auto GetInlinedSize() const { return 0; }
	FORCEINLINE void* GetInlineAllocation() const { return nullptr; }
	FORCEINLINE void* GetAllocation() const { return HeapAllocation; }
	FORCEINLINE void SetInlineAllocation(void* InAddr, int32_t InSize) {}
#else
	FORCEINLINE bool UseInlineData() const { return std::ptrdiff_t(HeapAllocation) & 1; }
	void* GetInlineAllocation() const { return UseInlineData() ? (uint8*)&HeapAllocation + sizeof(HeapAllocation) : nullptr; }
	FORCEINLINE void* GetHeapAllocation() const { return !UseInlineData() ? GetHeapAllocationImpl() : nullptr; }
	FORCEINLINE int32_t InlineOffset() const { return UseInlineData() ? (int32_t)(std::ptrdiff_t)((uint8*)GetHeapAllocationImpl() - (uint8*)GetInlineAllocation()) : int32_t(0); }
	FORCEINLINE auto GetInlinedSize() const
	{
		return [](auto Offset) -> uint32_t { return Offset > 0 && Offset < 65536 ? Offset : 0; }(InlineOffset());
	}
	void* GetAllocation() const { return UseInlineData() ? GetInlineAllocation() : GetHeapAllocationImpl(); }

	void SetInlineAllocation(void* InAddr, int32_t InSize) { HeapAllocation = (uint8*)InAddr + InSize + 1; }

	IErasedObject* GetErasedWrapper() const { return (IErasedObject*)GetAllocation(); }
	void* GetObjectAddress() const { return GetErasedWrapper()->GetObjectAddress(); }
	void DestroyObject()
	{
		if (Callable)
		{
			IErasedObject* Owned = (IErasedObject*)GetAllocation();
			GMP_DEBUGVIEW_LOG(TEXT("FStorageErase::DestroyObject() %p"), Owned);
			Callable = nullptr;
			Owned->PlacementDtor();
		}
		HeapFree();
	}

#endif
};

static_assert(alignof(FStorageErase) == GMP_FUNCTION_PREDEFINED_ALIGN_SIZE, "err");

template<typename Base, int32_t INLINE_SIZE = FStorageEraseBase::kInlineSize>
struct TAttachedCallableStore;

template<int32_t INLINE_SIZE = FStorageEraseBase::kInlineSize, int32_t INLINE_ALIGNMENT = FStorageEraseBase::kAlignSize>
struct TStorageErase;

template<int32_t INLINE_SIZE, int32_t INLINE_ALIGNMENT>
struct TStorageErase : protected FStorageErase
{
	template<typename Functor, typename DT = std::decay_t<Functor>, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	DT* ConstructObject(Functor&& InFunctor)
	{
		using FunctorType = TTypedObject<DT>;
		void* NewAlloc = GetHeapAllocation();
		check(!NewAlloc);
		GMP_IF_CONSTEXPR(sizeof(FunctorType) > INLINE_SIZE)
		{
			NewAlloc = HeapMalloc(sizeof(FunctorType), alignof(FunctorType));
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::ConstructObject()::Malloc %p"), NewAlloc);
		}
		else
		{
			NewAlloc = &InlineAllocation;
			SetInlineAllocation(NewAlloc, INLINE_SIZE);
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::ConstructObject()::Inline %p"), NewAlloc);
		}
		auto* NewOwned = new (NewAlloc) FunctorType(std::forward<Functor>(InFunctor));
		ensure(NewAlloc == NewOwned);
		return &NewOwned->Obj;
	}

#if !GMP_FUNCTION_USING_TAGGED_INLNE_SIZE
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
	void DestroyObject()
	{
		if (Callable)
		{
			IErasedObject* Owned = GetErasedWrapper();
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase::DestroyObject() %p"), Owned);
			Callable = nullptr;
			Owned->PlacementDtor();
			HeapFree();
		}
	}
#endif
	FORCEINLINE void* GetInlineAllocation() { return &InlineAllocation; }
	FORCEINLINE static constexpr auto offsetofINLINE()
	{
		using SelfType = TStorageErase<INLINE_SIZE, INLINE_ALIGNMENT>;
		return offsetof(SelfType, InlineAllocation);
	}

#if WITH_EDITOR
	void SetInlineAllocation(void* InAddr, int32_t InSize)
	{
		ensure(InAddr == &InlineAllocation);
		FStorageErase::SetInlineAllocation(InAddr, InSize);
	}
#endif

public:
	TStorageErase() = default;
	TStorageErase(TStorageErase&& Other)
	{
		if (Other.UseInlineData())
		{
			Swap(Callable, Other.Callable);
			FMemory::Memcpy(&InlineAllocation, &Other.InlineAllocation, sizeof(InlineAllocation));
			Swap(HeapAllocation, Other.HeapAllocation);
			SetInlineAllocation(&InlineAllocation, INLINE_SIZE);
		}
		else
		{
			Swap(Callable, Other.Callable);
			FMemory::Memcpy(&InlineAllocation, &Other.InlineAllocation, sizeof(InlineAllocation));
			Swap(HeapAllocation, Other.HeapAllocation);
		}
	}
	TStorageErase& operator=(TStorageErase&& Other)
	{
		if (this != &Other)
		{
			DestroyObject();
			new (this) TStorageErase(Other);
		}
		return *this;
	}
	TStorageErase(const TStorageErase& Other) = delete;
	TStorageErase& operator=(const TStorageErase& Other) = delete;

protected:
	std::aligned_storage_t<INLINE_SIZE, INLINE_ALIGNMENT> InlineAllocation;
	template<typename>
	friend struct TTypedObject;
	template<typename, int32>
	friend struct TAttachedCallableStore;
	template<int32, int32>
	friend struct TStorageErase;
};

template<int32_t INLINE_ALIGNMENT>
struct TStorageErase<0, INLINE_ALIGNMENT> : public FStorageErase
{
	template<typename Functor, typename DT = std::decay_t<Functor>, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	std::decay_t<Functor>* ConstructObject(Functor&& InFunctor)
	{
		check(!GetHeapAllocation());
		using FunctorType = TTypedObject<DT>;
		void* NewAlloc = HeapMalloc(sizeof(FunctorType), alignof(FunctorType));
		GMP_DEBUGVIEW_LOG(TEXT("TStorageErase<0>::ConstructObject()::Malloc %p"), NewAlloc);
		auto* NewOwned = new (NewAlloc) FunctorType(std::forward<Functor>(InFunctor));
		ensure(NewAlloc == NewOwned);
		return &NewOwned->Obj;
	}

#if !GMP_FUNCTION_USING_TAGGED_INLNE_SIZE
	IErasedObject* GetErasedWrapper() const { return (IErasedObject*)GetHeapAllocation(); }
	void* GetObjectAddress() const { return GetErasedWrapper()->GetObjectAddress(); }
	void DestroyObject()
	{
		if (Callable)
		{
			IErasedObject* Owned = GetErasedWrapper();
			GMP_DEBUGVIEW_LOG(TEXT("TStorageErase<0>::DestroyObject() %p"), Owned);
			Callable = nullptr;
			if (ensure(Owned))
				Owned->PlacementDtor();
		}
	}
#endif
	FORCEINLINE static constexpr auto offsetofINLINE() { return sizeof(FStorageErase); }
};

template<typename T>
void* TTypedObject<T>::MoveConstruct(FStorageErase* Target, uint32_t InlineSize)
{
	auto* TargetStore = static_cast<TStorageErase<>*>(Target);
	check(TargetStore && !TargetStore->GetHeapAllocation());
	void* NewAlloc;
	if (InlineSize > sizeof(TTypedObject))
	{
		NewAlloc = &TargetStore->InlineAllocation;
		TargetStore->SetInlineAllocation(NewAlloc, InlineSize);
		GMP_DEBUGVIEW_LOG(TEXT("TTypedObject::MoveConstruct()::Inline [%p] On Inc[%p]"), NewAlloc, this);
	}
	else
	{
		NewAlloc = TargetStore->HeapMalloc(sizeof(TTypedObject), alignof(TTypedObject));
		GMP_DEBUGVIEW_LOG(TEXT("TTypedObject::MoveConstruct()::Malloc [%p] On Inc[%p]"), NewAlloc, this);
	}

	auto* NewOwned = new (NewAlloc) TTypedObject(std::move(this->Obj));
	return &NewOwned->Obj;
}
namespace Internal
{
	struct FEmptyBase
	{
	};

	struct FEBOTest : public FEmptyBase
	{
	};
	static_assert(sizeof(FEBOTest) == 1, "err");
}  // namespace Internal

template<typename Base, int32_t INLINE_SIZE>
struct alignas(alignof(FStorageEraseBase)) TAttachedCallableStore : public Base
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

	FORCEINLINE void CheckCallable() const { checkf(GetCallable() && GetObjectAddress(), TEXT("Attempting to call an unbound Function!")); }
	FORCEINLINE bool IsBound() const { return !!GetCallable(); }
	FORCEINLINE auto GetCallable() const { return Storage.Callable; }
	FORCEINLINE auto GetObjectAddress() const { return Storage.GetObjectAddress(); }

	void Reset() { Storage.Reset(); }

protected:
	static constexpr auto ActualAlignVal = (!GMP_FUNCTION_DEBUGVIEW && (alignof(Base) < FStorageEraseBase::kAlignSize || sizeof(Base) % FStorageEraseBase::kAlignSize == 0)) ? 1 : FStorageEraseBase::kAlignSize;
	using FStorageType = TStorageErase<INLINE_SIZE, ActualAlignVal>;

	template<typename B, int32_t S>
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

#if GMP_FUNCTION_DEBUGVIEW
			new ((void*)&DebugViewStorage) TDebugView<DecayedFunctor>;
			DebugViewStorage.Ptr = (void*)ErasedObj;
#endif
		}
	}

	template<typename B, int32_t S>
	void Move(TAttachedCallableStore<B, S>&& Other, uint32_t InSize = INLINE_SIZE)
	{
		Storage.DestroyObject();
#if GMP_FUNCTION_DEBUGVIEW
		new ((void*)&DebugViewStorage) TDebugView<void>;
		DebugViewStorage.Ptr = nullptr;
#endif
		if (!Other.Storage.Callable)
			return;

		if (auto HeapObj = (IErasedObject*)Other.Storage.GetHeapAllocation())
		{
			GMP_DEBUGVIEW_LOG(TEXT("TAttachedCallableStore::MoveConstruct() MoveHeap %p"), HeapObj);
			Swap(Storage.HeapAllocation, Other.Storage.HeapAllocation);
			Swap(Storage.Callable, Other.Storage.Callable);
#if GMP_FUNCTION_DEBUGVIEW
			DebugViewStorage.Ptr = Storage.GetErasedWrapper();
#endif
		}
		else if (auto InlineObj = (IErasedObject*)Other.Storage.GetInlineAllocation())
		{
			InlineObj->MoveConstruct(&Storage, InSize);
			Swap(Storage.Callable, Other.Storage.Callable);
#if GMP_FUNCTION_DEBUGVIEW
			DebugViewStorage.Ptr = Storage.GetErasedWrapper();
#endif
		}
	}

#if GMP_FUNCTION_DEBUGVIEW
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
	void DerivedClassesCannotAddMembersAnymore()
	{
#if GMP_FUNCTION_DEBUGVIEW
		static_assert(sizeof(TDebugView<void>) == FStorageEraseBase::kAlignSize, "derived classes cannot add members anymore");
#endif
		static_assert(sizeof(TAttachedCallableStore) == INLINE_SIZE + TAttachedCallableStore::offsetofINLINE(), "derived classes cannot add members anymore");
	}
	FStorageType Storage;
};

namespace Internal
{
	constexpr auto kSizeofFStorageErase = sizeof(FStorageErase);
	using FEmptyCallableStore = TAttachedCallableStore<FEmptyBase, kSizeofFStorageErase>;
	struct FWeakObjPtr : public FWeakObjectPtr
	{
		using FWeakObjectPtr::FWeakObjectPtr;
		std::size_t PaddingData;
	};
	constexpr auto kSizeofWeakObjPtr = sizeof(FWeakObjPtr);
	static_assert(kSizeofWeakObjPtr == 16, "err");

	using FWeakCallableStore = TAttachedCallableStore<FWeakObjPtr, kSizeofFStorageErase>;
	constexpr auto kSizeofGMPFunction = sizeof(FEmptyCallableStore);
	constexpr auto kSizeofGMPWeakFunction = sizeof(FWeakCallableStore);
}  // namespace Internal

template<typename TSig>
struct TGMPFunction;
template<typename TSig>
struct TGMPWeakFunction;

namespace Internal
{
	template<typename T>
	struct TIsGMPCallable : std::false_type
	{
	};
	template<typename TSig>
	struct TIsGMPCallable<TGMPFunction<TSig>> : std::true_type
	{
	};
	template<typename TSig>
	struct TIsGMPCallable<TGMPWeakFunction<TSig>> : std::true_type
	{
	};
}  // namespace Internal

template<typename R, typename... TArgs>
struct TGMPFunction<R(TArgs...)> final : public Internal::FEmptyCallableStore
{
	TGMPFunction() = default;
	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	TGMPFunction(Functor&& Val)
		: Internal::FEmptyCallableStore(std::forward<Functor>(Val))
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

template<typename R, typename... TArgs>
struct TGMPWeakFunction<R(TArgs...)> final : public Internal::FWeakCallableStore
{
	TGMPWeakFunction() = default;
	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	TGMPWeakFunction(Functor&& Val, const UObject* InObj = nullptr)
		: Internal::FWeakCallableStore(std::forward<Functor>(Val))
	{
		static_assert(TypeTraits::IsSameV<R(TArgs...), TypeTraits::TSigFuncType<Functor>>, "sig mismatch");
		static_cast<FWeakObjectPtr&>(*this) = InObj;
	}
	TGMPWeakFunction(TGMPWeakFunction&& Val) = default;
	TGMPWeakFunction& operator=(TGMPWeakFunction&& Val) = default;

	R operator()(TArgs... Args) const
	{
		CheckCallable();
		return reinterpret_cast<R (*)(void*, TArgs...)>(GetCallable())(GetObjectAddress(), Args...);
	}

	FORCEINLINE bool IsBound() const { return !!GetCallable() && !FWeakObjectPtr::IsStale(true); }
	FORCEINLINE explicit operator bool() const { return IsBound(); }
	FORCEINLINE UObject* GetUObject() const { return FWeakObjectPtr::Get(); }

	bool ExecuteIfBound(TArgs... Args) const
	{
		if (IsBound())
		{
			reinterpret_cast<R (*)(void*, TArgs...)>(GetCallable())(GetObjectAddress(), Args...);
			return true;
		}
		return false;
	}
	bool SingleshotIfBound(TArgs... Args) const
	{
		if (IsBound())
		{
			reinterpret_cast<R (*)(void*, TArgs...)>(GetCallable())(GetObjectAddress(), Args...);
			return true;
		}

		Reset();
		return false;
	}

	std::size_t& UserData() { return PaddingData; }
	const std::size_t& UserData() const { return PaddingData; }

	auto GMPFunction() &&
	{
		TGMPFunction<R(TArgs...)> RawFunction;
		RawFunction.Move(*this);
		return RawFunction;
	}
};
}  // namespace GMP
