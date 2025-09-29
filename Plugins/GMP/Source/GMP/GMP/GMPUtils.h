//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPHub.h"

class IModuleInterface;

#if !defined(GMP_MULTIWORLD_SUPPORT)
#define GMP_MULTIWORLD_SUPPORT 1
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

class GMP_API FMessageUtils
{
public:
	static void UnbindMessage(const FMSGKEYFind& K, FGMPKey id);
	static void UnbindMessage(const FMSGKEYFind& K, const UObject* Listener);
	template<typename T>
	FORCEINLINE static void UnbindMessage(const FMSGKEYFind& K, const UObject* Listener, const T&)
	{
		UnbindMessage(K, Listener);
	}

#if 0
	[[deprecated(" Please using UnbindMessage")]]
	FORCEINLINE static void UnListenMessage(const FMSGKEYFind& K, FGMPKey id)
	{
		return UnbindMessage(K, id);
	}
	[[deprecated(" Please using UnbindMessage")]]
	FORCEINLINE static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner)
	{
		return UnbindMessage(K, Listenner);
	}
	template<typename T>
	[[deprecated(" Please using UnbindMessage")]]
	FORCEINLINE static void UnListenMessage(const FMSGKEYFind& K, const UObject* Listenner, const T&)
	{
		UnbindMessage(K, Listenner);
	}
#endif

	template<typename T, typename F>
	FORCEINLINE static FGMPKey ListenMessage(const MSGKEY_TYPE& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
	}

	template<typename T, typename F>
	FORCEINLINE static FGMPKey ListenObjectMessage(FSigSource InSigSrc, const MSGKEY_TYPE& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->ListenObjectMessage(K, InSigSrc, Listener, Forward<F>(f), Options);
	}

	template<typename T, typename F>
	FORCEINLINE static FGMPKey ListenWorldMessage(const UWorld* InWorld, const MSGKEY_TYPE& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(!!InWorld);
		return GetMessageHub()->ListenObjectMessage(K, InWorld, Listener, Forward<F>(f), Options);
	}
	template<typename T, typename F>
	FORCEINLINE static FGMPKey ListenWorldMessage(const UObject* WorldContext, const MSGKEY_TYPE& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return ListenWorldMessage(WorldContext->GetWorld(), K, Listener, Forward<F>(f), Options);
	}

	template<typename... TArgs>
	FORCEINLINE static auto SendObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, Forward<TArgs>(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto NotifyObjectMessage(FSigSource InSigSrc, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(InSigSrc);
		return GetMessageHub()->SendObjectMessage(K, InSigSrc, NoRef(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE static auto SendWorldMessage(const UWorld* InWorld, const FMSGKEYFind& K, TArgs&&... Args)
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
	FORCEINLINE static auto NotifyWorldMessage(const UWorld* InWorld, const FMSGKEYFind& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(!!InWorld);
		return GetMessageHub()->SendObjectMessage(K, InWorld, NoRef(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto NotifyWorldMessage(const UObject* WorldContext, const FMSGKEYFind& K, TArgs&&... Args)
	{
		return NotifyWorldMessage(WorldContext->GetWorld(), K, Forward<TArgs>(Args)...);
	}
	
#if GMP_WITH_MSG_HOLDER
	template<typename... TArgs>
	FORCEINLINE static auto StoreObjectMessage(const UObject* InObj, const MSGKEY_TYPE& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(!!InObj);
		return GetMessageHub()->StoreObjectMessage(K, InObj, Forward<TArgs>(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto OnceObjectMessage(const UObject* InObj, const MSGKEY_TYPE& K, TArgs&&... Args)
	{
		GMP_CHECK_SLOW(!!InObj);
		return GetMessageHub()->OnceObjectMessage(K, InObj, Forward<TArgs>(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE static auto RemoveStoredObjectMessage(const UObject* InObj, const MSGKEY_TYPE& K)
	{
		GMP_CHECK_SLOW(!!InObj);
		return GetMessageHub()->RemoveStoredObjectMessage(K, InObj);
	}
#endif
	
#if GMP_MULTIWORLD_SUPPORT
	template<typename... TArgs>
	[[deprecated(" Please using SendObjectMessage than SendMessage to support multi-worlds debugging.")]] FORCEINLINE static auto SendMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, Forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	[[deprecated(" Please using NotifyObjectMessage than NotifyMessage to support multi-worlds debugging.")]] FORCEINLINE static auto NotifyMessage(const FMSGKEYFind& K, TArgs&&... Args)
	{
		return GetMessageHub()->SendObjectMessage(K, FSigSource::NullSigSrc, NoRef(Args)...);
	}
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
	FORCEINLINE static FGMPKey UnsafeListenMessage(const MSGKEY_TYPE& K, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, GMP_LISTENER_ANY(), Forward<F>(f), Options);
	}
	template<typename T, typename F>
	FORCEINLINE static FGMPKey UnsafeListenMessage(const MSGKEY_TYPE& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ListenObjectMessage(K, FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
	}

	template<typename F>
	FORCEINLINE static bool ApplyMessageBoy(FMessageBody& Body, const F& Lambda)
	{
		return Hub::ApplyMessageBoy(Body, Lambda);
	}

	FORCEINLINE static bool ScriptNotifyMessage(const FMSGKEYAny& K, FTypedAddresses& Param, FSigSource SigSource = FSigSource::NullSigSrc) { return GetMessageHub()->ScriptNotifyMessage(K, Param, SigSource); }

	template<typename T, typename F>
	FORCEINLINE static FGMPKey ScriptListenMessage(const FName& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(Listener);
		return GetMessageHub()->ScriptListenMessage(FSigSource::NullSigSrc, K, Listener, Forward<F>(f), Options);
	}
	template<typename T, typename F>
	FORCEINLINE static FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FName& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(Listener);
		return GetMessageHub()->ScriptListenMessage(WatchedObj, K, Listener, Forward<F>(f), Options);
	}

	static void ScriptUnbindMessage(const FMSGKEYAny& K, const UObject* Listener);
	static void ScriptUnbindMessage(const FMSGKEYAny& K, FGMPKey InKey);
	[[deprecated(" Please using ScriptUnbindMessage")]] FORCEINLINE static void ScriptUnListenMessage(const FMSGKEYAny& K, const UObject* Listener) { return ScriptUnbindMessage(K, Listener); }
	[[deprecated(" Please using ScriptUnbindMessage")]] FORCEINLINE void ScriptUnListenMessage(const FMSGKEYAny& K, FGMPKey InKey) { return ScriptUnbindMessage(K, InKey); }

	static void ScriptRemoveSigSource(const FSigSource InSigSrc);

	static FMessageBody* GetCurrentMessageBody();
	static UGMPManager* GetManager();
	static FMessageHub* GetMessageHub();
};

class GMP_API FGMPModuleUtils
{
private:
	static void OnModuleLifetimeImpl(FName ModuleName, TUniqueFunction<void(IModuleInterface*)> Startup, TUniqueFunction<void(IModuleInterface*)> Shutdown);

public:
	template<typename ModuleType>
	static void OnModuleLifetime(FName ModuleName, TDelegate<void(ModuleType*)> InStartup, TDelegate<void(ModuleType*)> InShutdown = {})
	{
		TUniqueFunction<void(IModuleInterface*)> Startup;
		TUniqueFunction<void(IModuleInterface*)> Shutdown;
		if (InStartup.IsBound())
		{
			Startup = [InStartup{MoveTemp(InStartup)}](IModuleInterface* Inc) { InStartup.ExecuteIfBound(static_cast<ModuleType*>(Inc)); };
		}
		if (InShutdown.IsBound())
		{
			Shutdown = [InShutdown{MoveTemp(InShutdown)}](IModuleInterface* Inc) { InShutdown.ExecuteIfBound(static_cast<ModuleType*>(Inc)); };
		}
		OnModuleLifetimeImpl(ModuleName, MoveTemp(Startup), MoveTemp(Shutdown));
	}
};
}  // namespace GMP
