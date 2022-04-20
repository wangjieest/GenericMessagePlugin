//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPFunction.h"
#include "GMPKey.h"
#include "UObject/Interface.h"

namespace GMP
{
class FMessageHub;
class FSignalStore;
struct GMP_API FSignalBase
{
	mutable TSharedPtr<FSignalStore> Store;
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
	class Connection : public TWeakPtr<void>
	{
	public:
		FGMPKey Key;

		Connection(TWeakPtr<void>&& In, FGMPKey InKey)
			: TWeakPtr<void>(std::move(In))
			, Key(InKey)
		{
		}
	};
	mutable TIndirectArray<Connection> Connections;
	friend struct ConnectionImpl;
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

struct GMP_API ISigSource
{
#if WITH_EDITOR
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
		EAll = 0x03,
	};

	template<typename T>
	FSigSource(const T* InPtr)
		: Addr(ToAddr(InPtr))
	{
		static_assert((TExternalSigSource<T>::value || sizeof(T) >= 4) || std::is_base_of<UObject, T>::value || std::is_base_of<ISigSource, T>::value, "err");
	}
	FSigSource(std::nullptr_t = nullptr) {}

	AddrType GetAddrValue() const { return Addr; }
	const void* GetObjectAddr() const { return reinterpret_cast<const void*>(Addr & ~EAll); }

	bool IsValid() const { return !!Addr; }

	bool IsUObject() const { return !(Addr & EAll); }
	bool IsSigInc() const { return !(Addr & ESignal); }

	const void* SigOrObj() const { return !IsExternal() ? GetObjectAddr() : nullptr; }

	bool IsExternal() const { return !!(Addr & External); }

	const UObject* TryGetUObject() const { return (IsUObject()) ? reinterpret_cast<const UObject*>(Addr) : nullptr; }

	//explicit operator bool() const { return IsValid(); }

	FORCEINLINE friend bool operator==(const FSigSource& Lhs, const FSigSource& Rhs) { return Lhs.Addr == Rhs.Addr; }
	FORCEINLINE friend uint32 GetTypeHash(const FSigSource& Src) { return ::GetTypeHash((void*)(Src.Addr)); }

	FString GetNameSafe() const { return IsUObject() ? ::GetNameSafe(TryGetUObject()) : FString::Printf(TEXT("[%p]"), GetObjectAddr()); }

	GMP_API static void RemoveSource(FSigSource InSource);

private:
	template<typename T>
	std::enable_if_t<std::is_base_of<UObject, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		const UObject* InObj = InPtr;
		AddrType Ret = AddrType(InObj);
		checkSlow(!(Ret & EAll));
		return Ret;
	}
	template<typename T>
	std::enable_if_t<!std::is_base_of<UObject, T>::value && std::is_base_of<ISigSource, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		AddrType Ret = AddrType(InPtr);
		checkSlow(!(Ret & EAll));
		Ret |= ESignal;
		return Ret;
	}

	template<typename T>
	std::enable_if_t<!std::is_base_of<UObject, T>::value && !std::is_base_of<ISigSource, T>::value, AddrType> ToAddr(const T* InPtr)
	{
		static_assert(TExternalSigSource<T>::value, "err");
		AddrType Ret = AddrType(InPtr);
		checkSlow(!(Ret & EAll));
		Ret |= External;
		return Ret;
	}

	AddrType Addr = 0;
	friend class FSignalStore;
};
}  // namespace GMP
