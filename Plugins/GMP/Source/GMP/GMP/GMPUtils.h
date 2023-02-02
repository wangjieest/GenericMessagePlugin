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
struct MSGKEY_TYPE
{
	FORCEINLINE operator GMP::FMSGKEY() const { return GMP::FMSGKEY(MsgKey); }
#if !WITH_EDITOR
	FORCEINLINE operator GMP::FMSGKEYFind() const { return GMP::FMSGKEYFind(MsgKey); }
#endif
	template<size_t K>
	static FORCEINLINE MSGKEY_TYPE MAKE_MSGKEY_TYPE(const char (&MessageId)[K])
	{
		return MSGKEY_TYPE(MessageId);
	}

protected:
	const char* MsgKey;
	explicit MSGKEY_TYPE(const char* Str)
		: MsgKey(Str)
	{
	}
};

template<size_t K>
FORCEINLINE MSGKEY_TYPE MSGKEY(const char (&MessageId)[K])
{
	return MSGKEY_TYPE::MAKE_MSGKEY_TYPE(MessageId);
}
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
	static void UnListenMessage(const FMSGKEYFind& K, FGMPKey id);
	static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner);
	template<typename T>
	static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner, const T&)
	{
		UnListenMessage(K, Listenner);
	}

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenMessage(const MSGKEY_TYPE& K, T* Listenner, F&& f, int32 Times = -1)
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, Listenner, Forward<F>(f), Times);
	}

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenObjectMessage(FSigSource InSigSrc, const MSGKEY_TYPE& K, T* Listenner, F&& f, int32 Times = -1)
	{
		checkSlow(InSigSrc);
		return GetMessageHub()->ListenObjectMessage(K, InSigSrc, Listenner, Forward<F>(f), Times);
	}
	
	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ListenWorldMessage(const UObject* WorldContext, const MSGKEY_TYPE& K, T* Listenner, F&& f, int32 Times = -1)
	{
		checkSlow(IsValid(WorldContext));
		return GetMessageHub()->ListenObjectMessage(K, WorldContext->GetWorld(), Listenner, Forward<F>(f), Times);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto SendObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		checkSlow(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto NotifyObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		checkSlow(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, NoRef(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto SendWorldMessage(const UObject* WorldContext, const FMSGKEYFind& K, TArgs&&... Args)
	{
		checkSlow(IsValid(WorldContext));
		return GetMessageHub()->SendObjectMessage(K, WorldContext->GetWorld(), Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE_DEBUGGABLE static auto NotifyWorldMessage(const UObject* WorldContext, const FMSGKEYFind& K, TArgs&&... Args)
	{
		checkSlow(IsValid(WorldContext));
		return GetMessageHub()->SendObjectMessage(K, WorldContext->GetWorld(), NoRef(Args)...);
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
	FORCEINLINE_DEBUGGABLE static FGMPKey UnsafeListenMessage(const MSGKEY_TYPE& K, F&& f, int32 Times = -1)
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, GMP_LISTENER_ANY(), Forward<F>(f), Times);
	}

	template<typename F>
	FORCEINLINE static bool ApplyMessageBoy(FMessageBody& Body, const F& Lambda)
	{
		return Hub::ApplyMessageBoy(Body, Lambda);
	}

	FORCEINLINE static bool ScriptNotifyMessage(const FMSGKEYFind& K, FTypedAddresses& Param, const UObject* SigSource = nullptr) { return GetMessageHub()->ScriptNotifyMessage(K, Param, SigSource); }

	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ScriptListenMessage(const FName& K, T* Listenner, F&& f, int32 Times = -1)
	{
		checkSlow(Listenner);
		return GetMessageHub()->ScriptListenMessage(FSigSource::NullSigSrc, K, Listenner, Forward<F>(f), Times);
	}
	template<typename T, typename F>
	FORCEINLINE_DEBUGGABLE static FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FName& K, T* Listenner, F&& f, int32 Times = -1)
	{
		checkSlow(Listenner);
		return GetMessageHub()->ScriptListenMessage(WatchedObj, K, Listenner, Forward<F>(f), Times);
	}

	FORCEINLINE_DEBUGGABLE static void ScriptUnListenMessage(const FMSGKEYFind& K, const UObject* Listenner) { GetMessageHub()->ScriptUnListenMessage(K, Listenner); }
	FORCEINLINE_DEBUGGABLE static void ScriptUnListenMessage(const FMSGKEYFind& K, FGMPKey InKey) { GetMessageHub()->ScriptUnListenMessage(K, InKey); }

	FORCEINLINE_DEBUGGABLE static void ScriptRemoveSigSource(const FSigSource InSigSrc) { FSigSource::RemoveSource(InSigSrc); }

	static FMessageBody* GetCurrentMessageBody();
	static UGMPManager* GetManager();
	static FMessageHub* GetMessageHub();
};
}  // namespace GMP
