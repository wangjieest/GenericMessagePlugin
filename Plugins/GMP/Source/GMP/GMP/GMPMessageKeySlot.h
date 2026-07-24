//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "GMPMacros.h"

#if GMP_WITH_DIRECT_SIGNAL
#include "GMPSignalsImpl.h"
#include "GMPMessageKey.h"

namespace GMP
{
//////////////////////////////////////////////////////////////////////////
// Direct-Signal: let MSGKEY_SLOT directly reference the process-unique global Signal store; hot path uses a
// raw pointer (or the static store object itself) and skips the TMap<FName> lookup.

#if !GMP_WITH_STATIC_STORE
// ---- Modular/Editor mode: slot is a lightweight handle (FStaticSignalSlot) bound lazily via ResolvePtr.

// Self-registration list node + global head (intrusive, no platform section dependency).
struct FSlotNode
{
	FStaticSignalSlot* Slot;
	FSlotNode* Next;
};
GMP_API FSlotNode*& GetStaticSlotListHead();

struct FSlotRegistrar
{
	FSlotNode Node;
	explicit FSlotRegistrar(FStaticSignalSlot* InSlot) noexcept
	{
		Node.Slot = InSlot;
		Node.Next = GetStaticSlotListHead();
		GetStaticSlotListHead() = &Node;
	}
};

template<typename T>
struct TStaticSignalSlot
{
	static FStaticSignalSlot Slot;
	static FSlotRegistrar Reg;
};
template<typename T>
FStaticSignalSlot TStaticSignalSlot<T>::Slot{T::Get()};
template<typename T>
FSlotRegistrar TStaticSignalSlot<T>::Reg{&TStaticSignalSlot<T>::Slot};

template<typename T>
FORCEINLINE FStaticSignalSlot& GetStaticSignalSlot()
{
	(void)&TStaticSignalSlot<T>::Reg;
	return TStaticSignalSlot<T>::Slot;
}

template<typename KeyT>
struct TKeySlot
{
	using FKeyType = KeyT;
	FStaticSignalSlot& Slot;
	FORCEINLINE operator FStaticSignalSlot&() const { return Slot; }
	FORCEINLINE FStaticSignalSlot* operator->() const { return &Slot; }
	FORCEINLINE FName GetKey() const { return FName(KeyT::Get()); }
	FORCEINLINE operator FName() const { return FName(KeyT::Get()); }
	FORCEINLINE FSignalStore* GetStore() const { return Slot.GetStore(); }
};

template<typename KeyT>
FORCEINLINE TKeySlot<KeyT> GetKeySlot()
{
	return TKeySlot<KeyT>{GetStaticSignalSlot<KeyT>()};
}

#else  // GMP_WITH_STATIC_STORE
GMP_API void GMPRegisterStaticStore(FSignalStore* InStore, const ANSICHAR* InKeyStr);

class GMP_API FGMPStaticSlotRegistry
{
public:
	struct FNode
	{
		void (*Ctor)() = nullptr;
		FNode* Next = nullptr;
	};

	struct FAutoReg
	{
		FNode Node;
		explicit FAutoReg(void (*InCtor)()) noexcept
		{
			Node.Ctor = InCtor;
			Register(Node);
		}
	};

	static void Register(FNode& Node) noexcept;
	static void ConstructAll();

private:
	static FNode*& Head() noexcept;
};

template<typename T>
struct FStaticSlotHolder
{
	FSignalStore Store;
	FSignalBase  Signal;
	FStaticSlotHolder()
	{
		Store.MessageKey = FName(T::Get());
		Signal.Store = TSharedPtr<FSignalStore, FSignalBase::SPMode>(&Store, FStaticStoreDeleter{});
		GMPRegisterStaticStore(&Store, T::Get());
	}
};
template<typename T>
FORCEINLINE FStaticSlotHolder<T>& GetStaticSlot()
{
	static FStaticSlotHolder<T> Inst;
	return Inst;
}

template<typename T>
struct TStaticSlotCtor
{
	static void Construct() { (void)GetStaticSlot<T>(); }
	static FGMPStaticSlotRegistry::FAutoReg Reg;
};
template<typename T>
FGMPStaticSlotRegistry::FAutoReg TStaticSlotCtor<T>::Reg{&TStaticSlotCtor<T>::Construct};

template<typename KeyT>
struct TKeySlot
{
	using FKeyType = KeyT;
	FSignalStore* StorePtr;          // compile-time-known address of the static store (fire reads it directly)
	FSignalBase* SignalPtr;          // compile-time-known address of the static signal shell (listen uses it)
	const ANSICHAR* KeyStr;
	FORCEINLINE FSignalStore* GetStore() const { return StorePtr; }
	FORCEINLINE FSignalBase* GetSignal() const { return SignalPtr; }
	FORCEINLINE FName GetKey() const { return FName(KeyStr); }
	FORCEINLINE operator FName() const { return FName(KeyStr); }
};

template<typename KeyT>
FORCEINLINE TKeySlot<KeyT> GetKeySlot()
{
	(void)&TStaticSlotCtor<KeyT>::Reg;
	FStaticSlotHolder<KeyT>& S = GetStaticSlot<KeyT>();
	return TKeySlot<KeyT>{&S.Store, &S.Signal, KeyT::Get()};
}

#endif  // GMP_WITH_STATIC_STORE

}  // namespace GMP

#endif  // GMP_WITH_DIRECT_SIGNAL
