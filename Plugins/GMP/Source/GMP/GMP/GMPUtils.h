//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPHub.h"

#if !defined(GMP_MULTIWORLD_SUPPORT)
#define GMP_MULTIWORLD_SUPPORT 1
#endif

#if GMP_WITH_STATIC_MSGKEY
using MSGKEY_TYPE = FName;
#define MSGKEY(str) GMP_MSGKEY_HOLDER<C_STRING_TYPE(str)>
#else
class MSGKEY_TYPE
{
public:
	FORCEINLINE operator GMP::FMSGKEY() const { return GMP::FMSGKEY(MsgKey); }
#if !WITH_EDITOR
	FORCEINLINE operator GMP::FMSGKEYFind() const { return GMP::FMSGKEYFind(MsgKey); }
#endif

	template<size_t K>
	static FORCEINLINE MSGKEY_TYPE MAKE_MSGKEY_TYPE(const ANSICHAR (&MessageId)[K])
	{
		return MSGKEY_TYPE(MessageId);
	}
#if GMP_TRACE_MSG_STACK
	template<size_t K>
	static FORCEINLINE MSGKEY_TYPE MAKE_MSGKEY_TYPE(const ANSICHAR (&MessageId)[K], const ANSICHAR* InFile, int32 InLine)
	{
		return MSGKEY_TYPE(MessageId, InFile, InLine);
	}
	~MSGKEY_TYPE() { GMP::FMessageHub::GMPTrackLeave(this); }
	const ANSICHAR* Ptr() const { return MsgKey; }
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
		GMP::FMessageHub::GMPTrackEnter(this, InFile, InLine);
	}

#endif
};

#if GMP_TRACE_MSG_STACK
#define MSGKEY(str) MSGKEY_TYPE::MAKE_MSGKEY_TYPE(str, UE_LOG_SOURCE_FILE(__FILE__), __LINE__)
#else
#define MSGKEY(str) MSGKEY_TYPE::MAKE_MSGKEY_TYPE(str)
#endif
#endif

#define GMP_LISTENER_ANY() static_cast<UObject*>(nullptr)
namespace GMP
{
template<typename T, std::enable_if_t<std::is_pointer<T>::value || std::is_arithmetic<T>::value, int32> = 0>
FORCEINLINE T NoRef(T&& Val)
{
	return Val;
}
template<typename T, std::enable_if_t<!std::is_pointer<T>::value && !std::is_arithmetic<T>::value, int32> = 0>
FORCEINLINE T& NoRef(T&& Val)
{
	return Val;
}

struct GMP_API FLatentActionKeeper
{
	FLatentActionKeeper() = default;

	void SetLatentInfo(const struct FLatentActionInfo& LatentInfo);
	bool ExecuteAction(bool bClear = true) const;
	FLatentActionKeeper(const struct FLatentActionInfo& LatentInfo);

protected:
	FName ExecutionFunction;
	mutable int32 LinkID = 0;
	FWeakObjectPtr CallbackTarget;
};

class GMP_API FMessageUtils
{
public:
	static void UnbindMessage(const FMSGKEYFind& K, FGMPKey id);
	static void UnbindMessage(const FMSGKEYFind& K, const UObject* Listenner);
	template<typename T>
	FORCEINLINE_DEBUGGABLE static void UnbindMessage(const FMSGKEYFind& K, const UObject* Listenner, const T&)
	{
		UnbindMessage(K, Listenner);
	}
	[[deprecated(" Please using UnbindMessage")]] 
	static void UnListenMessage(const FMSGKEYFind& K, FGMPKey id) { return UnbindMessage(K,id); }
	[[deprecated(" Please using UnbindMessage")]] 
	static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner) { return UnbindMessage(K, Listenner); }
	template<typename T>
	[[deprecated(" Please using UnbindMessage")]] 
	FORCEINLINE_DEBUGGABLE static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner, const T&)
	{
		UnbindMessage(K, Listenner);
	}

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenMessage(const MSGKEY_TYPE& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, Listenner, Forward<F>(f), Options);
	}

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenObjectMessage(FSigSource InSigSrc, const MSGKEY_TYPE& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->ListenObjectMessage(K, InSigSrc, Listenner, Forward<F>(f), Options);
	}

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenWorldMessage(const UWorld* InWorld, const MSGKEY_TYPE& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(!!InWorld);
		return GetMessageHub()->ListenObjectMessage(K, InWorld, Listenner, Forward<F>(f), Options);
	}
	template<typename T, typename F>
	FORCEINLINE static FGMPKey ListenWorldMessage(const UObject* WorldContext, const MSGKEY_TYPE& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return ListenWorldMessage(WorldContext->GetWorld(), K, Listenner, Forward<F>(f), Options);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto SendObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto NotifyObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, NoRef(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto SendWorldMessage(const UWorld* InWorld, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(!!InWorld);
		return GetMessageHub()->SendObjectMessage(K, InWorld, Forward<TArgs>(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto SendWorldMessage(const UObject* WorldContext, const FMSGKEYFind& K, TArgs&&... Args)
	{
		return SendWorldMessage(WorldContext->GetWorld(), K, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto NotifyWorldMessage(const UWorld* InWorld, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(!!InWorld);
		return GetMessageHub()->SendObjectMessage(K, InWorld, NoRef(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto NotifyWorldMessage(const UObject* WorldContext, const FMSGKEYFind& K, TArgs&&... Args)
	{
		return NotifyWorldMessage(WorldContext->GetWorld(), K, Forward<TArgs>(Args)...);
	}

#if GMP_MULTIWORLD_SUPPORT
	// clang-format off
	template<typename... TArgs>
	[[deprecated(" Please using SendObjectMessage than SendMessage to support multi-worlds debugging.")]] 
	FORCEINLINE static auto SendMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	[[deprecated(" Please using NotifyObjectMessage than NotifyMessage to support multi-worlds debugging.")]] 
	FORCEINLINE static auto NotifyMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, NoRef(Args)...);
	}
	// clang-format on
#else
	template<typename... TArgs>
	FORCEINLINE static auto SendMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE static auto NotifyMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, NoRef(Args)...);
	}
#endif

public:
	template<typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey UnsafeListenMessage(const MSGKEY_TYPE& K, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, GMP_LISTENER_ANY(), Forward<F>(f), Options);
	}

	template<typename F>
	FORCEINLINE static bool ApplyMessageBoy(FMessageBody& Body, const F& Lambda)
	{
		return Hub::ApplyMessageBoy(Body, Lambda);
	}

	FORCEINLINE static bool ScriptNotifyMessage(const FMSGKEYFind& K, FTypedAddresses& Param, FSigSource SigSource = FSigSource::NullSigSrc) { return GetMessageHub()->ScriptNotifyMessage(K, Param, SigSource); }

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ScriptListenMessage(const FName& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(Listenner);
		return GetMessageHub()->ScriptListenMessage(FSigSource::NullSigSrc, K, Listenner, Forward<F>(f), Options);
	}
	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FName& K, T* Listenner, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(Listenner);
		return GetMessageHub()->ScriptListenMessage(WatchedObj, K, Listenner, Forward<F>(f), Options);
	}

	static void ScriptUnbindMessage(const FMSGKEYFind& K, const UObject* Listenner);
	static void ScriptUnbindMessage(const FMSGKEYFind& K, FGMPKey InKey);
	[[deprecated(" Please using ScriptUnbindMessage")]] 
	static void ScriptUnListenMessage(const FMSGKEYFind& K, const UObject* Listenner) { return ScriptUnbindMessage(K, Listenner); }
	[[deprecated(" Please using ScriptUnbindMessage")]] 
	static void ScriptUnListenMessage(const FMSGKEYFind& K, FGMPKey InKey) { return ScriptUnbindMessage(K, InKey); }

	static void ScriptRemoveSigSource(const FSigSource InSigSrc);

	static FMessageBody* GetCurrentMessageBody();
	static UGMPManager* GetManager();
	static FMessageHub* GetMessageHub();
};
}  // namespace GMP
