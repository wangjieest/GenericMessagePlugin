//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPFunction.h"
#include "GMPKey.h"
#include "UObject/Interface.h"

#ifndef GMP_DEBUG_SIGNAL
#define GMP_DEBUG_SIGNAL WITH_EDITOR
#endif

#ifndef GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
#define GMP_SIGNAL_WITH_GLOBAL_SIGELMSET !GMP_DEBUG_SIGNAL
#endif

class UWorld;
namespace GMP
{
class FMessageHub;
class FSignalStore;
struct GMP_API FSignalBase
{
	static constexpr ESPMode SPMode = (UE_5_00_OR_LATER || PLATFORM_WEAKLY_CONSISTENT_MEMORY) ? ESPMode::ThreadSafe : ESPMode::NotThreadSafe;
	mutable TSharedPtr<FSignalStore, FSignalBase::SPMode> Store;
};
GMP_API int32& ShouldEnsureOnRepeatedListening();

template<typename T>
using ForwardParam = typename std::conditional<std::is_reference<T>::value || std::is_pointer<T>::value || TIsPODType<T>::Value, T, T&&>::type;

class GMP_API FSigCollection
{
public:
	void DisconnectAll();
	void Disconnect(FGMPKey Key);

private:
	class FConnection
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		: public TWeakPtr<void, FSignalBase::SPMode>
#endif
	{
	public:
		FGMPKey Key;
		using Super = TWeakPtr<void, FSignalBase::SPMode>;

		FConnection(FGMPKey InKey, Super&& In)
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
			: Key(InKey)
#else
			: Super(std::move(In))
			, Key(InKey)
#endif
		{
		}
	};

#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
	mutable TArray<FConnection> Connections;
#else
	mutable TIndirectArray<FConnection> Connections;
#endif
	friend struct FConnectionImpl;
};

template<typename T>
constexpr bool IsCollectionBase = !!std::is_base_of<FSigCollection, std::remove_cv_t<std::remove_reference_t<T>>>::value;

template<typename T>
using HasCollectionBase = std::integral_constant<bool, IsCollectionBase<T>>;

template<typename T>
std::enable_if_t<std::is_base_of<UObject, std::decay_t<T>>::value, T*> ToUObject(T* InObj)
{
	return InObj;
}
template<typename T>
std::enable_if_t<!std::is_base_of<UObject, T>::value, const UObject*> ToUObject(const T* InObj)
{
	return nullptr;
}

// auto disconnect signals
class FSigHandle : public FSigCollection
{
public:
	~FSigHandle() { DisconnectAll(); }
};
#define GMP_SIG_BASE_ALIGN 8
struct GMP_API alignas(GMP_SIG_BASE_ALIGN) ISigSource
{
#if GMP_DEBUG_SIGNAL
	ISigSource();
#endif
	~ISigSource();
};

template<typename T>
struct TExternalSigSource : public std::false_type
{
};

struct FSigSource
{
	using AddrType = intptr_t;
	enum EAddrMask : AddrType
	{
		EObject = 0x00,
		ESignal = 0x01,
		External = 0x02,
		ExtKey = 0x04,
		EAll = GMP_SIG_BASE_ALIGN - 1
	};

	explicit FSigSource(std::nullptr_t = nullptr) {}
	template<typename T>
	FSigSource(const T* InPtr)
		: Addr(ToAddr(InPtr))
	{
		static_assert((alignof(T) >= GMP_SIG_BASE_ALIGN) && (TExternalSigSource<T>::value || std::is_base_of<UObject, T>::value || std::is_base_of<ISigSource, T>::value), "err");
	}
	template<typename T>
	FSigSource(const TWeakObjectPtr<T>& InObj)
		: FSigSource(CastChecked<UObject>(InObj.Get()))
	{
	}
	template<typename T>
	FSigSource(const Z_GMP_OBJECT_NAME<T>& InObj)
		: FSigSource(CastChecked<UObject>(InObj.Get()))
	{
	}

	AddrType GetAddrValue() const { return Addr; }
	const void* GetRealAddr() const { return reinterpret_cast<void*>(Addr & ~EAll); }

	bool IsValid() const { return !!Addr; }

	bool IsUObject() const { return !(Addr & EAll); }
	bool IsSigInc() const { return (Addr & EAll) == ESignal; }
	bool IsExternal() const { return (Addr & EAll) == External; }
	bool IsExtKey() const { return (Addr & EAll) == ExtKey; }

	const void* SigOrObj() const {return !(Addr & (External | ExtKey)) ? GetRealAddr() : nullptr; }

	explicit operator bool() const { return IsValid(); }

	FORCEINLINE friend bool operator==(const FSigSource& Lhs, const FSigSource& Rhs) { return Lhs.Addr == Rhs.Addr; }
	FORCEINLINE friend uint32 GetTypeHash(const FSigSource& Src) { return ::GetTypeHash((void*)(Src.Addr)); }

	GMP_API UObject* TryGetUObject() const;
	GMP_API FString GetNameSafe() const;

	GMP_API static void RemoveSource(FSigSource InSigSrc);
	GMP_API static void RemoveSourceKey(FSigSource InSigSrc, FName InName);
	GMP_API static FSigSource NullSigSrc;
	GMP_API static FSigSource AnySigSrc;

	static FSigSource MakeSigSourceKey(const UObject* InObj, FName InName) { return InName.IsNone() ? FSigSource(InObj) : SigSourceKey(InObj, InName, true); }
	static FSigSource FindSigSourceKey(const UObject* InObj, FName InName) { return InName.IsNone() ? FSigSource(InObj) : SigSourceKey(InObj, InName, false); }

	GMP_API UWorld* GetSigSourceWorld() const;

private:
	GMP_API static FSigSource SigSourceKey(FSigSource InSig, FName InName = NAME_None, bool bCreate = true);
#if GMP_DEBUG_SIGNAL
	GMP_API static AddrType ObjectToAddr(const UObject* InObj);
#else
	FORCEINLINE static AddrType ObjectToAddr(const UObject* InObj) { return AddrType(InObj); }
#endif
	FORCEINLINE static FSigSource RawSigSource(const UObjectBase* InObj)
	{
		FSigSource Ret;
		Ret.Addr = AddrType(InObj);
		return Ret;
	}
	template<typename T>
	static std::enable_if_t<std::is_base_of<UObject, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		AddrType Ret = ObjectToAddr(InPtr);
		GMP_CHECK_SLOW(!(Ret & EAll));
		return Ret;
	}
	template<typename T>
	static std::enable_if_t<!std::is_base_of<UObject, T>::value && std::is_base_of<ISigSource, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		AddrType Ret = AddrType(static_cast<const ISigSource*>(InPtr));
		GMP_CHECK_SLOW(!(Ret & EAll));
		Ret |= ESignal;
		return Ret;
	}

	template<typename T>
	static std::enable_if_t<!std::is_base_of<UObject, T>::value && !std::is_base_of<ISigSource, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		static_assert(TExternalSigSource<T>::value, "err");
		AddrType Ret = AddrType(InPtr);
		GMP_CHECK_SLOW(!(Ret & EAll));
		Ret |= External;
		return Ret;
	}

	AddrType Addr = 0;
	friend class FSignalStore;
	friend class FGMPSourceAndHandlerDeleter;
};

}  // namespace GMP
