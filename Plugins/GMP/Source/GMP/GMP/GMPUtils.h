//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPHub.h"
#include "GMPMessageKeySlot.h"

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

#if GMP_WITH_MSG_HOLDER
	template<typename... TArgs>
	FORCEINLINE static auto RemoveStoredObjectMessage(const UObject* InObj, const MSGKEY_TYPE& K)
	{
		GMP_CHECK_SLOW(!!InObj);
		return GetMessageHub()->RemoveStoredObjectMessage(K, InObj);
	}
#endif

#if GMP_WITH_DIRECT_SIGNAL
	template<typename KeyT, typename... TArgs>
	static auto SendObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
	template<typename KeyT, typename... TArgs>
	static auto NotifyObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);

	template<typename KeyT, typename... TArgs>
	static auto SendWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
	template<typename KeyT, typename... TArgs>
	static auto SendWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
	template<typename KeyT, typename... TArgs>
	static auto NotifyWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
	template<typename KeyT, typename... TArgs>
	static auto NotifyWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);

	template<typename KeyT, typename T, typename F>
	static FGMPKey ListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {});
	template<typename KeyT, typename T, typename F>
	static FGMPKey ListenObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {});
	template<typename KeyT, typename T, typename F>
	static FGMPKey ListenWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {});
	template<typename KeyT, typename T, typename F>
	static FGMPKey ListenWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {});

	// UnsafeListenMessage (listener-any / explicit-listener) typed overloads -- TMSGKEYTyped gap filled here so it is
	// reachable strictly via MSGKEY once the runtime-key UnsafeListenMessage overloads below are removed.
	template<typename KeyT, typename F>
	static FGMPKey UnsafeListenMessage(const TMSGKEYTyped<KeyT>& K, F&& f, GMP::FGMPListenOptions Options = {});
	template<typename KeyT, typename T, typename F>
	static FGMPKey UnsafeListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options = {});

#if GMP_WITH_MSG_HOLDER
	template<typename KeyT, typename... TArgs>
	static auto StoreObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
	template<typename KeyT, typename... TArgs>
	static auto OnceObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
#endif
#endif  // GMP_WITH_DIRECT_SIGNAL

public:
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
		return GetMessageHub()->ScriptListenMessage(WatchedObj, K, Listener, Forward<F>(f), Options);
	}

#if GMP_WITH_DIRECT_SIGNAL
	template<typename F>
	FORCEINLINE static FGMPKey ScriptListenMessageRaw(const FName& K, const UObject* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		GMP_CHECK_SLOW(Listener);
		return GetMessageHub()->ScriptListenMessageRaw(FSigSource::NullSigSrc, K, Listener, Forward<F>(f), Options);
	}
	template<typename F>
	FORCEINLINE static FGMPKey ScriptListenMessageRaw(FSigSource WatchedObj, const FName& K, const UObject* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		return GetMessageHub()->ScriptListenMessageRaw(WatchedObj, K, Listener, Forward<F>(f), Options);
	}

	template<typename KeyT, typename F>
	FORCEINLINE static FGMPKey ScriptListenMessageRawStatic(FSigSource WatchedObj, const UObject* Listener, F&& f, GMP::FGMPListenOptions Options = {})
	{
		auto Slot = GMP::GetKeySlot<KeyT>();
		return GetMessageHub()->ScriptListenMessageRawByStore(Slot.GetStore(), WatchedObj, Slot.GetKey(), Listener, Forward<F>(f), Options);
	}
	template<typename KeyT>
	FORCEINLINE static bool ScriptNotifyMessageStatic(GMP::FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		auto Slot = GMP::GetKeySlot<KeyT>();
		return GetMessageHub()->ScriptNotifyMessageByStore(Slot.GetStore(), Slot.GetKey(), Param, InSigSrc);
	}
#endif

	static void ScriptUnbindMessage(const FMSGKEYAny& K, const UObject* Listener);
	static void ScriptUnbindMessage(const FMSGKEYAny& K, FGMPKey InKey);
	[[deprecated(" Please using ScriptUnbindMessage")]] FORCEINLINE static void ScriptUnListenMessage(const FMSGKEYAny& K, const UObject* Listener) { return ScriptUnbindMessage(K, Listener); }
	[[deprecated(" Please using ScriptUnbindMessage")]] FORCEINLINE void ScriptUnListenMessage(const FMSGKEYAny& K, FGMPKey InKey) { return ScriptUnbindMessage(K, InKey); }

	static void ScriptRemoveSigSource(const FSigSource InSigSrc);

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

// Runtime-available enumeration of all registered message tag signatures (from the baked GMPMeta table); for script backends that batch-bind per tag.
GMP_API void EnumerateMessageTagMetas(const UObject* InWorldContextObj, TFunctionRef<void(FName Tag, const TArray<FName>& ParamTypes, const TArray<FName>& ResTypes)> Visitor);
}  // namespace GMP
