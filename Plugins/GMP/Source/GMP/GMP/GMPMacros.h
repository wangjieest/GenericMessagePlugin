//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"

#if UE_5_01_OR_LATER
#define GMP_IF_CONSTEXPR if constexpr
#elif defined(PLATFORM_COMPILER_HAS_IF_CONSTEXPR) && PLATFORM_COMPILER_HAS_IF_CONSTEXPR
#define GMP_IF_CONSTEXPR if constexpr
#else
#define GMP_IF_CONSTEXPR if
#endif
#if (__cplusplus >= 201703L) || (defined(_HAS_CXX17) && _HAS_CXX17) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define GMP_INLINE inline
#define GMP_NORETURN [[noreturn]]
#define GMP_NODISCARD [[nodiscard]]
#define GMP_MAYBE_UNUSED [[maybe_unused]]
#define GMP_USE_STD_VARIANT 1
#else
#define GMP_INLINE
#define GMP_NORETURN
#define GMP_NODISCARD
#define GMP_MAYBE_UNUSED
#define GMP_USE_STD_VARIANT 0
#endif

extern GMP_API int32 GEnableGMPLogging;
#define GMP_CWARNING(C, FMT, ...) UE_CLOG(C, LogGMP, Warning, FMT, ##__VA_ARGS__)
#define GMP_WARNING(FMT, ...) GMP_CWARNING(!!GEnableGMPLogging, FMT, ##__VA_ARGS__)
#define GMP_ERROR(FMT, ...) UE_CLOG(!!GEnableGMPLogging, LogGMP, Error, FMT, ##__VA_ARGS__)
#define GMP_LOG(FMT, ...) UE_CLOG(!!GEnableGMPLogging, LogGMP, Log, FMT, ##__VA_ARGS__)
#define GMP_TRACE(FMT, ...) UE_CLOG(!!GEnableGMPLogging, LogGMP, Verbose, FMT, ##__VA_ARGS__)

#if !defined(GMP_DEBUGGAME)
#if WITH_EDITOR && defined(UE_BUILD_DEBUGGAME) && UE_BUILD_DEBUGGAME
#define GMP_DEBUGGAME 1
#else
#define GMP_DEBUGGAME 0
#endif
#endif

#if GMP_DEBUGGAME
#define GMP_DEBUG_LOG(FMT, ...) GMP_LOG(FMT, ##__VA_ARGS__)
#define GMP_TRACE_FMT(FMT, ...) GMP_TRACE(TEXT("GMP-TRACE:[%s] ") FMT, ITS::TypeWStr<decltype(this)>(), ##__VA_ARGS__);
#define GMP_TRACE_THIS() GMP_TRACE(TEXT("GMP-TRACE:[%s]"), ITS::TypeStr<decltype(this)>());

#else
#define GMP_DEBUG_LOG(FMT, ...) void(0)
#define GMP_TRACE_FMT(FMT, ...) void(0)
#define GMP_TRACE_THIS() void(0)
#endif
#define GMP_TO_STR_(STR) #STR
#define GMP_TO_STR(STR) GMP_TO_STR_(STR)

#if  !defined(GMP_ENABLE_DEBUGVIEW)
#define GMP_ENABLE_DEBUGVIEW (GMP_DEBUGGAME && WITH_EDITOR)
#endif
#if GMP_ENABLE_DEBUGVIEW
#define GMP_DEBUGVIEW_LOG(FMT, ...) GMP_TRACE(FMT, ##__VA_ARGS__)
#define GMP_DEBUGVIEW_FMT(FMT, ...) GMP_TRACE(TEXT("GMP-DEBUG:[%s] ") FMT, ITS::TypeWStr<decltype(this)>(), ##__VA_ARGS__);
#define GMP_DEBUGVIEW_THIS() GMP_TRACE(TEXT("GMP-DEBUG:[%s]"), ITS::TypeWStr<decltype(this)>());
#else
#define GMP_DEBUGVIEW_LOG(FMT, ...) void(0)
#define GMP_DEBUGVIEW_FMT(FMT, ...) void(0)
#define GMP_DEBUGVIEW_THIS() void(0)
#endif

#if UE_BUILD_SHIPPING
#define GMP_NOTE(T, Fmt, ...) ensureAlwaysMsgf(false, Fmt, ##__VA_ARGS__)
#define GMP_CNOTE(C, T, Fmt, ...) ensureAlwaysMsgf(C, Fmt, ##__VA_ARGS__)
#define GMP_CNOTE_ONCE(C, Fmt, ...) ensureMsgf(C, Fmt, ##__VA_ARGS__)
#else
#define GMP_NOTE(T, Fmt, ...)            \
	[&] {                                \
		GMP_WARNING(Fmt, ##__VA_ARGS__); \
		return ensureAlways(!(T));       \
	}()

#define GMP_CNOTE(C, T, Fmt, ...)        \
	(!!(C) || [&] {                      \
		GMP_WARNING(Fmt, ##__VA_ARGS__); \
		return ensureAlways(!(T));       \
	}())
#define GMP_CNOTE_ONCE(C, Fmt, ...)      \
	(!!(C) || [&] {                      \
		GMP_WARNING(Fmt, ##__VA_ARGS__); \
		return ensure(false);            \
	}())
#endif

#if !defined(GMP_FORCE_DOUBLE_PROPERTY)
#define GMP_FORCE_DOUBLE_PROPERTY 0
#endif

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
#define GMP_WITH_DYNAMIC_TYPE_CHECK 1
#define GMP_WITH_DYNAMIC_CALL_CHECK 1
#define GMP_CHECK_SLOW check
#else
#define GMP_WITH_DYNAMIC_TYPE_CHECK 0
#define GMP_WITH_DYNAMIC_CALL_CHECK 0
#define GMP_CHECK_SLOW checkSlow
#endif

#if !defined(GMP_WITH_NO_CLASS_CHECK)
#define GMP_WITH_NO_CLASS_CHECK UE_BUILD_SHIPPING
#endif

#if !defined(GMP_WITH_TYPE_INFO_EXTENSION)
#define GMP_WITH_TYPE_INFO_EXTENSION 0
#endif

#if !defined(GMP_WITH_NULL_PROPERTY)
#define GMP_WITH_NULL_PROPERTY 1
#endif

#define GMP_WITH_TYPENAME (GMP_WITH_DYNAMIC_TYPE_CHECK || GMP_WITH_DYNAMIC_CALL_CHECK || GMP_WITH_TYPE_INFO_EXTENSION)

#if WITH_EDITOR || UE_BUILD_TEST
#define GMP_VALIDATE_MSGF ensureAlwaysMsgf
#define GMP_FORCEINLINE_DEBUGGABLE
#define GMP_CHECK check
#else
#define GMP_VALIDATE_MSGF checkf
#define GMP_FORCEINLINE_DEBUGGABLE FORCEINLINE_DEBUGGABLE
#define GMP_CHECK check
#endif

#if !defined(GMP_WITH_EXACT_OBJECT_TYPE)
#define GMP_WITH_EXACT_OBJECT_TYPE 0
#endif

#define GMP_WITH_STATIC_MSGKEY (!WITH_EDITOR)

#if !defined(GMP_USE_NEW_PROP_FROM_STRING)
#define GMP_USE_NEW_PROP_FROM_STRING 1
#endif  // GMP_USE_NEW_PROP_FROM_STRING

#if !defined(GMP_DELEGATE_INVOKABLE)
#define GMP_DELEGATE_INVOKABLE 1
#endif  // GMP_DELEGATE_INVOKABLE

#define Z_GMP_OBJECT_NAME TObjectPtr
#define NAME_GMP_TObjectPtr TEXT(GMP_TO_STR(Z_GMP_OBJECT_NAME))
#if !UE_5_00_OR_LATER
struct FObjectPtr
{
	UObject* Ptr;
	UObject* opertor->() const { return Ptr; }
};
template<typename T>
struct Z_GMP_OBJECT_NAME : private FObjectPtr
{
	T* Get() const { return CastChecked<T>(Ptr); }
};
static_assert(std::is_base_of<FObjectPtr, Z_GMP_OBJECT_NAME<UObject>>::value, "err");
#else
static_assert(sizeof(FObjectPtr) == sizeof(Z_GMP_OBJECT_NAME<UObject>), "err");
#endif
namespace GMP
{
template<uint8 N = 4>
struct TWorldFlag
{
protected:
	template<typename F>
	bool TestImpl(const UObject* WorldContextObj, const F& f)
	{
		UObject* World = WorldContextObj ? (UObject*)WorldContextObj->GetWorld() : nullptr;
		// check(!World || IsValid(World));
		for (int32 i = 0; i < Storage.Num(); ++i)
		{
			auto WeakWorld = Storage[i];
			if (!WeakWorld.IsStale(true))
			{
				if (World == WeakWorld.Get())
					return f(true, World);
			}
			else
			{
				Storage.RemoveAtSwap(i);
				--i;
			}
		}
		return f(false, World);
	}

public:
	bool Test(const UObject* WorldContextObj, bool bAdd = false)
	{
		return TestImpl(WorldContextObj, [bAdd, this](bool b, UObject* World) {
			if (!b)
				Storage.Add(MakeWeakObjectPtr(World));
			return b;
		});
	}

	bool TrueOnWorldFisrtCall(const UObject* WorldContextObj)
	{
		return TestImpl(WorldContextObj, [&](bool b, UObject* World) {
			if (!b)
			{
				Storage.Add(MakeWeakObjectPtr(World));
				return true;
			}
			return false;
		});
	}

protected:
	TArray<TWeakObjectPtr<UObject>, TInlineAllocator<N>> Storage;
};

template<typename F>
bool FORCENOINLINE UE_DEBUG_SECTION TrueOnWorldFisrtCall(const UObject* Obj, const F& f)
{
	static TWorldFlag<> Flag;
	return Flag.TrueOnWorldFisrtCall(Obj) && f();
}
}  // namespace GMP

#if WITH_EDITOR
#if UE_5_00_OR_LATER
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                                                                             \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                                                                        \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, #C, UE_LOG_SOURCE_FILE(__FILE__), __LINE__, PLATFORM_RETURN_ADDRESS(), F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                                                                  \
		{                                                                                                                                                         \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                                                                        \
			return false;                                                                                                                                         \
		}                                                                                                                                                         \
		return true;                                                                                                                                              \
	}
#elif UE_5_00_OR_LATER
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                                                                                                   \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                                                                                              \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, FDebug::FFailureInfo{#C, UE_LOG_SOURCE_FILE(__FILE__), __LINE__, PLATFORM_RETURN_ADDRESS()}, F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                                                                                        \
		{                                                                                                                                                                               \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                                                                                              \
			return false;                                                                                                                                                               \
		}                                                                                                                                                                               \
		return true;                                                                                                                                                                    \
	}
#else
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                                                  \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                                             \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, #C, UE_LOG_SOURCE_FILE(__FILE__), __LINE__, F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                                       \
		{                                                                                                                              \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                                             \
			return false;                                                                                                              \
		}                                                                                                                              \
		return true;                                                                                                                   \
	}
#endif

#define ensureWorld(W, C) (LIKELY(!!(C)) || (GMP::TrueOnWorldFisrtCall(W, Z_GMP_FMT_DEBUG(, C, TEXT(""))) && ([]() { PLATFORM_BREAK(); }(), false)))
#define ensureWorldMsgf(W, C, F, ...) (LIKELY(!!(C)) || (GMP::TrueOnWorldFisrtCall(W, Z_GMP_FMT_DEBUG(&, C, F, ##__VA_ARGS__)) && ([]() { PLATFORM_BREAK(); }(), false)))

#else

#define ensureWorld(W, C) ensure(C)
#define ensureWorldMsgf(W, C, F, ...) ensureMsgf(C, F, ##__VA_ARGS__)

#endif

#define ensureThis(C) ensureWorld(this, C)
#define ensureThisMsgf(C, F, ...) ensureWorldMsgf(this, C, F, ##__VA_ARGS__)

#ifndef GMP_LOG_COMPILLE_TIME_VERBOSITY
#define GMP_LOG_COMPILLE_TIME_VERBOSITY All
#endif
GMP_API DECLARE_LOG_CATEGORY_EXTERN(LogGMP, Log, GMP_LOG_COMPILLE_TIME_VERBOSITY);

#define GMP_WITH_MSG_HOLDER 1
#define GMP_MSG_HOLDER_DEFAULT_INLINE_SIZE 8
