//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GMPMacros.h"

namespace GMP
{
FORCEINLINE FName ToMessageKey(const ANSICHAR* Key, EFindName FindType = FNAME_Add)
{
	return FName(Key, FindType);
}
FORCEINLINE FName ToMessageKey(const FString& Key, EFindName FindType = FNAME_Add)
{
	return FName(*Key, FindType);
}
FORCEINLINE FName& ToMessageKey(FName& Name, EFindName FindType = FNAME_Add)
{
	return Name;
}
inline FName ToMessageKey(const FName& Name, EFindName FindType = FNAME_Add)
{
	return Name;
}
inline FName ToMessageKey(uint64_t Key, EFindName FindType = FNAME_Add)
{
	return FName(*BytesToHex(reinterpret_cast<const uint8*>(&Key), sizeof(Key)), FindType);
}
inline FName ToMessageKey(uint32_t Key, EFindName FindType = FNAME_Add)
{
	return FName(*BytesToHex(reinterpret_cast<const uint8*>(&Key), sizeof(Key)), FindType);
}

template<EFindName EType>
struct TMSGKEYBase : public FName
{
	FORCEINLINE explicit operator bool() const { return (EType == FNAME_Add) ? true : !IsNone(); }

	using FName::FName;

	template<typename K>
	TMSGKEYBase(const K& In)
		: FName(ToMessageKey(In, EType))
	{
	}

	template<EFindName E>
	TMSGKEYBase(const TMSGKEYBase<E>& In)
		: FName(ToMessageKey(FName(In), EType))
	{
	}
};

using FMSGKEY = TMSGKEYBase<FNAME_Add>;
using FMSGKEYAny = TMSGKEYBase<!WITH_EDITOR ? FNAME_Find : FNAME_Add>;
struct FMSGKEYFind : public FMSGKEYAny
{
	explicit FMSGKEYFind(const FMSGKEY& In)
		: FMSGKEYAny(In)
	{
	}
#if !WITH_EDITOR
	explicit FMSGKEYFind(const FMSGKEYAny& In)
		: FMSGKEYAny(In)
	{
	}
#endif

protected:
#if !GMP_WITH_STATIC_MSGKEY
	friend class MSGKEY_TYPE;
#endif
	using FMSGKEYAny::FMSGKEYAny;
};
template<typename T>
const FName GMP_MSGKEY_HOLDER{T::Get()};

#if !defined(GMP_TRACE_MSG_STACK)
#define GMP_TRACE_MSG_STACK (1 && WITH_EDITOR && !GMP_WITH_STATIC_MSGKEY)
#endif


#if GMP_WITH_STATIC_MSGKEY
using MSGKEY_TYPE = FName;
#define MSGKEY(str) GMP::TMSGKEYTyped<C_STRING_TYPE(str)>{GMP::GMP_MSGKEY_HOLDER<C_STRING_TYPE(str)>}
#else
class MSGKEY_TYPE
{
public:
	FORCEINLINE operator FMSGKEY() const { return FMSGKEY(MsgKey); }
	FORCEINLINE operator FMSGKEYFind() const { return FMSGKEYFind(MsgKey); }
#if !WITH_EDITOR
	FORCEINLINE operator FMSGKEYAny() const { return FMSGKEYAny(MsgKey); }
#endif

#if GMP_TRACE_MSG_STACK
	template<size_t K>
	static FORCEINLINE MSGKEY_TYPE MAKE_MSGKEY_TYPE(const ANSICHAR (&MessageId)[K], const ANSICHAR* InFile, int32 InLine)
	{
		return MSGKEY_TYPE(MessageId, InFile, InLine);
	}
	const ANSICHAR* Ptr() const { return MsgKey; }
#else
	template<size_t K>
	static FORCEINLINE MSGKEY_TYPE MAKE_MSGKEY_TYPE(const ANSICHAR (&MessageId)[K])
	{
		return MSGKEY_TYPE(MessageId);
	}
#endif
protected:
	const ANSICHAR* MsgKey;
	explicit MSGKEY_TYPE(const ANSICHAR* Str)
		: MsgKey(Str)
	{
	}
#if GMP_TRACE_MSG_STACK
	explicit MSGKEY_TYPE(const ANSICHAR* Str, const ANSICHAR* InFile, int32 InLine)
		: MSGKEY_TYPE(Str)
	{
		GMPTraceEnter(InFile, InLine);
	}
	GMP_API void GMPTraceEnter(const ANSICHAR* InFile, int32 InLine);
	GMP_API void GMPTraceLeave();

public:
	~MSGKEY_TYPE() { GMPTraceLeave(); }
#endif
};

#if GMP_TRACE_MSG_STACK
#define MSGKEY(str) GMP::TMSGKEYTyped<C_STRING_TYPE(str)>{MSGKEY_TYPE::MAKE_MSGKEY_TYPE(str, UE_LOG_SOURCE_FILE(__FILE__), __LINE__)}
#else
#define MSGKEY(str) GMP::TMSGKEYTyped<C_STRING_TYPE(str)>{MSGKEY_TYPE::MAKE_MSGKEY_TYPE(str)}
#endif
#endif

template<typename KeyT>
struct TMSGKEYTyped
{
	using FKeyType = KeyT;  // compile-time key type; the typed entries do GetKeySlot<KeyT>() with it
	MSGKEY_TYPE Inner;      // value/trace semantics are delegated entirely to this single sub-object

#if GMP_WITH_STATIC_MSGKEY
	FORCEINLINE operator FMSGKEY() const { return FMSGKEY(Inner); }
	FORCEINLINE operator FMSGKEYFind() const { return FMSGKEYFind(FMSGKEY(Inner)); }
	FORCEINLINE operator FMSGKEYAny() const { return FMSGKEYAny(FMSGKEY(Inner)); }
	FORCEINLINE explicit operator FName() const { return FName(KeyT::Get()); }
#else
	FORCEINLINE operator FMSGKEY() const { return Inner; }
	FORCEINLINE operator FMSGKEYFind() const { return Inner; }
	FORCEINLINE explicit operator FName() const { return FName(KeyT::Get()); }
	FORCEINLINE operator const MSGKEY_TYPE&() const { return Inner; }
#if !WITH_EDITOR
	FORCEINLINE operator FMSGKEYAny() const { return Inner; }
#endif
#endif
	FORCEINLINE FName GetKey() const { return FName(KeyT::Get()); }
};
static_assert(std::is_aggregate<TMSGKEYTyped<C_STRING_TYPE("x")>>::value, "TMSGKEYTyped must stay an aggregate so brace-init guarantees copy elision (single MSGKEY_TYPE Inner, balanced trace enter/leave)");
}
using MSGKEY_TYPE = GMP::MSGKEY_TYPE;
