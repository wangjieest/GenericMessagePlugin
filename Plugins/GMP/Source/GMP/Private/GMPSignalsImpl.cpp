//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSignalsImpl.h"

#include "Containers/LockFreeList.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/DelayedAutoRegister.h"
#include "XConsoleManager.h"

#include <algorithm>
#include <set>

namespace GMP
{
#ifndef GMP_KEY_ORDER_BITS_MAX
#define GMP_KEY_ORDER_BITS_MAX 32
#endif
#ifndef GMP_KEY_ORDER_BITS
#define GMP_KEY_ORDER_BITS (GMP_WITH_SIGNAL_ORDER ? GMP_KEY_ORDER_BITS_MAX : 0)
#endif
#if (GMP_KEY_ORDER_BITS_MAX > 32 || GMP_KEY_ORDER_BITS > GMP_KEY_ORDER_BITS_MAX || GMP_KEY_ORDER_BITS < 0)
#error "GMP_KEY_ORDER_BITS must be in [0, GMP_KEY_ORDER_BITS_MAX]"
#endif
#if GMP_KEY_ORDER_BITS == 32
static const int32 MaxListenOrder = INT_MAX;
#else
static const int32 MaxListenOrder = (1 << (GMP_KEY_ORDER_BITS - 1)) - 1;
#endif
static const int32 MinListenOrder = -MaxListenOrder - 1;
FGMPListenOrder FGMPListenOrder::MaxOrder{MaxListenOrder};
FGMPListenOrder FGMPListenOrder::MinOrder{MinListenOrder};
FGMPListenOptions FGMPListenOptions::Default(-1, 0);
}  // namespace GMP

#if !UE_BUILD_SHIPPING
static int64 GMPDebugKey = 0;
static FName GMPDebugMsgKey = NAME_None;
FXConsoleCommandLambda XVar_GMPDebugGMPKey(TEXT("gmp.key.debug"), [](int64 In, UWorld* InWorld) { GMPDebugKey = In; });
FXConsoleCommandLambda CVar_GMPDebugMsgKey(TEXT("gmp.msgkey.debug"), [](FName In, UWorld* InWorld) { GMPDebugMsgKey = In; });
#endif
static bool bShouldClearWorldSubOjbects = true;
FAutoConsoleVariableRef CVar_ShouldClearWorldSubOjbects(TEXT("gmp.flag.clearWorldSubs"), bShouldClearWorldSubOjbects, TEXT(""));
static void GMPDebug(FName MessageKey, GMP::FSigElm* Elm, const TCHAR* Desc)
{
#if !UE_BUILD_SHIPPING
	if (GMPDebugMsgKey == MessageKey)
	{
		auto Key = Elm ? (int64)Elm->GetGMPKey() : 0;
		if (Elm && Key == GMPDebugKey)
		{
			GMP_DEBUG_LOG(TEXT("GMP Debug SigElm  %s: Msg=[%s] Key=%lld, Listener=%s, SigSrc=%s"), Desc, *GMPDebugMsgKey.ToString(), Key, *GetNameSafe(Elm->GetHandler().Get()), *Elm->GetSource().GetNameSafe());
		}
	}
#endif
}

FGMPKey FGMPKey::NextGMPKey(GMP::FGMPListenOptions Options)
{
	static std::atomic<int64> GNextID(1);
	int64 GMPKey = ++GNextID;
	// static_assert((-1ll << (64 - GMP_KEY_ORDER_BITS)) == -(1ll << (64 - GMP_KEY_ORDER_BITS)), "err");
	if (GMPKey == (GMP_WITH_SIGNAL_ORDER ? ((1ull << GMP_KEY_ORDER_BITS)) : 0))
		GMPKey = GNextID = 1;

#if (GMP_KEY_ORDER_BITS > 0)
	int32 Order = FMath::Clamp(Options.Order, GMP::MinListenOrder, GMP::MaxListenOrder);
	GMPKey = (int64)GMPKey | ((int64)Order << (64 - GMP_KEY_ORDER_BITS));
#endif

	return FGMPKey(GMPKey);
}

FGMPKey FGMPKey::NextGMPKey()
{
	return NextGMPKey(GMP::FGMPListenOptions::Default);
}

using FOnGMPSigSourceDeleted = TMulticastDelegate<void(GMP::FSigSource)>;

namespace GMP
{
struct FSigSourceExtKey
{
	FSigSource SrcObj;
	FName Name;
	FSigSourceExtKey() = default;
	FSigSourceExtKey(FSigSource InObj, FName InName = NAME_None)
		: SrcObj(InObj)
		, Name(InName)
	{
		GMP_CHECK_SLOW(!SrcObj.IsExtKey());
	}
	bool operator<(const FSigSourceExtKey& Other) const { return Name.FastLess(Other.Name); }
	friend bool operator<(const FSigSourceExtKey& Self, FName InName) { return Self.Name.FastLess(InName); }
	friend bool operator<(FName InName, const FSigSourceExtKey& Self) { return InName.FastLess(Self.Name); }
	FString GetNameSafe() const { return FString::Printf(TEXT("%s#%s"), *SrcObj.GetNameSafe(), *Name.ToString()); }
};

template<typename Type>
bool OnceOnGameThread(const Type&)
{
	static std::atomic<bool> bValue{true};
	bool bExpected = true;
	if (IsInGameThread() && bValue.compare_exchange_strong(bExpected, false))
	{
	}
	return bValue;
}

#if GMP_DEBUG_SIGNAL
static TSet<FSigSource> GMPSigIncs;
#endif

static FCriticalSection* GetGMPCritical()
{
	static FCriticalSection GMPCritical;
	return &GMPCritical;
}

#define GMP_THREAD_LOCK() FScopeLock GMPLock(GetGMPCritical())
#define GMP_VERIFY_GAME_THREAD() GMP_CHECK(IsInGameThread())
static TSet<TUniquePtr<FSigElm>, FSigElm::FKeyFuncs> GlobalSigElmSet;

struct FSignalUtils
{
	static auto& GetSigElmSet(const FSignalStore* In)
	{
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		return GlobalSigElmSet;
#else
		return In ? In->SigElmSet : GlobalSigElmSet;
#endif
	}

	template<typename F>
	static void RemoveOp(const FSignalStore* In, FGMPKey Key, const F& Func)
	{
		if (auto Find = GetSigElmSet(In).Find(Key))
		{
			auto Elm = Find->Get();
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
			GMPDebug(In->MessageKey, Elm, TEXT("RemoveOp"));
#endif
			Func(Elm);
			GetSigElmSet(In).Remove(Key);
		}
	}
	static TArray<FGMPKey> GetSigElmSetKeys(const FSignalStore* In)
	{
		TArray<FGMPKey> Keys;
		for (auto& Elem : GetSigElmSet(In))
		{
			Keys.Add(Elem->GetGMPKey());
		}
		return Keys;
	}

	static void ShutdownSignal(FSignalStore* In)
	{
		if (In)
		{
			In->Reset();
		}
	}

	static FSignalStore::FSigElmKeySet& RemoveAndCopyInvalidHandlerObjs(FSignalStore* In, FSignalStore::FSigElmKeySet& ResultKeys, FWeakObjectPtr Obj = nullptr)
	{
		FSignalStore::FSigElmKeySet Removed;
		if (!Obj.IsExplicitlyNull() && In->HandlerObjs.RemoveAndCopyValue(Obj, Removed))
		{
			ResultKeys.Append(Removed);
		}

		for (auto It = In->HandlerObjs.CreateIterator(); It; ++It)
		{
			if (It->Key.IsStale())
			{
				ResultKeys.Append(It->Value);
				It.RemoveCurrent();
			}
		}
		return ResultKeys;
	}

	static void StaticOnObjectRemoved(FSignalStore* In, FSigSource InSigSrc)
	{
		GMP_VERIFY_GAME_THREAD();
		ensure(!In->IsFiring());
		auto Obj = InSigSrc.TryGetUObject();

		GMPDebug(In->MessageKey, nullptr, TEXT("StaticOnObjectRemoved"));

		FSignalStore::FSigElmKeySet SigKeys;
		In->SourceObjs.RemoveAndCopyValue(InSigSrc, SigKeys);
#if GMP_WITH_MSG_HOLDER
		In->SourceMsgs.Remove(InSigSrc);
#endif

		static FSignalStore::FSigElmKeySet Dummy;
		FSignalStore::FSigElmKeySet* Handlers = &Dummy;
		if (bool bShouldIncludeWorld = bShouldClearWorldSubOjbects && Obj && (!Obj->IsA<UGameInstance>() && !Obj->IsA<UGameViewportClient>()))
		{
			auto ObjWorld = bShouldIncludeWorld ? Obj->GetWorld() : (UWorld*)nullptr;
			if (auto Find = In->SourceObjs.Find(ObjWorld))
				Handlers = Find;
		}

		auto& StorageRef = FSignalUtils::GetSigElmSet(In);
#if GMP_DEBUG_SIGNAL
		if (StorageRef.Num() > 0)
		{
#if GMP_DEBUGGAME
			bool bAlreadyEnsured = false;
			for (auto SigKey : SigKeys)
			{
				bool bExisted = StorageRef.Contains(SigKey);
				bAlreadyEnsured = bAlreadyEnsured || ensureAlways(bExisted);
			}
			bAlreadyEnsured = false;
#else
			bool bAllExisted = true;
			for (auto SigKey : SigKeys)
				bAllExisted = bAllExisted && StorageRef.Contains(SigKey);
			ensure(bAllExisted);
#endif
		}
#endif

#if !GMP_DEBUG_SIGNAL
		if (StorageRef.Num() > 0)
		{
			for (auto SigKey : RemoveAndCopyInvalidHandlerObjs(In, SigKeys, Obj))
			{
				Handlers->Remove(SigKey);
				StorageRef.Remove(SigKey);
			}
		}
#else
		for (auto SigKey : RemoveAndCopyInvalidHandlerObjs(In, SigKeys, Obj))
		{
			Handlers->Remove(SigKey);
			FSignalUtils::RemoveOp(In, SigKey, [&](FSigElm* Elm) {
				auto SigSrc = Elm->GetSource();
				if (FSignalStore::FSigElmKeySet* KeySet = In->SourceObjs.Find(SigSrc))
				{
					KeySet->Remove(SigKey);
					if (!KeySet->Num())
					{
						In->SourceObjs.Remove(SigSrc);
					}
				}
			});
		}
#endif
	}

	template<bool bAllowDuplicate>
	static void RemoveSigElmImpl(FSignalStore* In, FSigElm* SigElm)
	{
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		GMPDebug(In->MessageKey, SigElm, TEXT("RemoveSigElmImpl"));
#endif
		// Sources
		if (auto Keys = In->SourceObjs.Find(SigElm->GetSource()))
		{
			Keys->Remove(SigElm->GetGMPKey());
		}

		// Handlers
		auto& Handler = SigElm->GetHandler();
		GMP_IF_CONSTEXPR(bAllowDuplicate)
		{
			if (auto Keys = In->HandlerObjs.Find(Handler))
			{
				Keys->Remove(SigElm->GetGMPKey());
			}
		}
		else
		{
			// no need to search any more
			In->HandlerObjs.Remove(Handler);
		}

		// Storage
		if (!In->IsFiring())
		{
			In->RemoveSigElmStorage(SigElm->GetGMPKey());
		}
		else
		{
			SigElm->SetLeftTimes(0);
		}
	}

	template<bool bAllowDuplicate>
	static void DisconnectHandlerByID(FSignalStore* In, FGMPKey Key)
	{
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		if (!In)
		{
			FSignalUtils::RemoveOp(In, Key, [](FSigElm* SigElm) {});
			return;
		}
#endif
		FSignalUtils::RemoveOp(In, Key, [&](FSigElm* SigElm) {
			GMP_IF_CONSTEXPR(bAllowDuplicate)
			{
				if (auto Keys = In->HandlerObjs.Find(SigElm->GetHandler()))
				{
					Keys->Remove(Key);
				}
			}
			else
			{
				// no need to search any more
				In->HandlerObjs.Remove(SigElm->GetHandler());
			}

#if GMP_DEBUG_SIGNAL
			auto SigSrc = SigElm->GetSource();
			if (FSignalStore::FSigElmKeySet* KeySet = In->SourceObjs.Find(SigSrc))
			{
				KeySet->Remove(Key);
				if (!KeySet->Num())
				{
					In->SourceObjs.Remove(SigSrc);
				}
			}
#endif
		});
	}

	template<bool bAllowDuplicate>
	static void DisconnectObjectHandler(FSignalStore* In, const UObject* InHandler, FSigSource* InSigSrc = nullptr)
	{
		GMP_CHECK_SLOW(InHandler);
		FSignalStore::FSigElmKeySet HandlerKeys;
		if (!RemoveAndCopyInvalidHandlerObjs(In, HandlerKeys, InHandler).Num())
			return;

		if (!InSigSrc)
		{
			for (auto It = HandlerKeys.CreateIterator(); It; ++It)
			{
				auto SigKey = *It;
				if (auto SigElm = In->FindSigElm(SigKey))
					RemoveSigElmImpl<bAllowDuplicate>(In, SigElm);
				It.RemoveCurrent();
				GMP_IF_CONSTEXPR(!bAllowDuplicate)
				{
					break;
				}
			}
		}
		else
		{
			for (auto It = HandlerKeys.CreateIterator(); It; ++It)
			{
				auto SigKey = *It;
				if (auto SigElm = In->FindSigElm(SigKey))
				{
					if (SigElm->GetSource() == *InSigSrc)
					{
						RemoveSigElmImpl<bAllowDuplicate>(In, SigElm);
						It.RemoveCurrent();
						GMP_IF_CONSTEXPR(!bAllowDuplicate)
						{
							break;
						}
					}
				}
			}
		}
	}
};  // namespace GMP

class FGMPSourceAndHandlerDeleter final
	: public FUObjectArray::FUObjectDeleteListener
#if GMP_WITH_MSG_HOLDER
	, public FGCObject
#endif
{
public:
	FGMPSourceAndHandlerDeleter()
	{
		ensure(UObjectInitialized());
		GUObjectArray.AddUObjectDeleteListener(this);
	}

	~FGMPSourceAndHandlerDeleter()
	{
		ensure(UObjectInitialized());
		GUObjectArray.RemoveUObjectDeleteListener(this);
	}

	virtual void NotifyUObjectDeleted(const UObjectBase* ObjectBase, int32 Index) override { RouterObjectRemoved(FSigSource::RawSigSource(ObjectBase)); }
	using FSigStoreSet = TSet<TWeakPtr<FSignalStore, FSignalBase::SPMode>>;
	void OnUObjectArrayShutdown()
	{
		GMP_THREAD_LOCK();
		MessageMappings.Reset();
		for (auto Ptr : SignalStores)
		{
			FSignalUtils::ShutdownSignal(Ptr);
		}
	}
	void RemoveSigSourceImpl(FSigSource InSig) {
		FSigStoreSet RemovedStores;
		if (MessageMappings.RemoveAndCopyValue(InSig, RemovedStores))
		{
			for (auto It = RemovedStores.CreateIterator(); It; ++It)
			{
				if (auto Pin = It->Pin())
					FSignalUtils::StaticOnObjectRemoved(Pin.Get(), InSig);
			}
		}

		std::set<FSigSourceExtKey, std::less<>> ToBeRemoved;
		if (SigSourceExtStorages.RemoveAndCopyValue(InSig, ToBeRemoved))
		{
			for (auto& ExtKey : ToBeRemoved)
			{
				FSigSource ExtSig;
				ExtSig.Addr = FSigSource::AddrType(&ExtKey) | FSigSource::ExtKey;
				SigSourceKeys.Remove(ExtSig);
			}
		}
	}
	void RouterObjectRemoved(FSigSource InSigSrc)
	{
		// FIXME: IsInGarbageCollectorThread()
		if (!UNLIKELY(IsInGameThread()))
		{
			GameThreadObjects.Push(*reinterpret_cast<FSigSource**>(&InSigSrc));
		}
		else
		{
#if GMP_DEBUG_SIGNAL
			GMP_THREAD_LOCK();
			GMPSigIncs.Remove(InSigSrc);
#endif
			if (!GameThreadObjects.IsEmpty())
			{
				TArray<FSigSource*> Objs;
				GameThreadObjects.PopAll(Objs);
				for (FSigSource* Sig : Objs)
				{
					RemoveSigSourceImpl(**reinterpret_cast<FSigSource**>(Sig));
				}
			}
			RemoveSigSourceImpl(InSigSrc);
		}
	}

	TArray<FSignalStore*, TInlineAllocator<32>> SignalStores;
	TMap<FSigSource, FSigStoreSet> MessageMappings;
#if GMP_WITH_MSG_HOLDER
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (auto& Signal : SignalStores)
		{
			Signal->AddReferencedObjects(Collector);
		}
	}
	virtual FString GetReferencerName() const { return TEXT("FGMPSourceAndHandlerDeleter"); }
#endif
	static auto& GetMessageSourceDeleter()
	{
		static FGMPSourceAndHandlerDeleter* GGMPMessageSourceDeleter = nullptr;
		return GGMPMessageSourceDeleter;
	}
	static void OnPreExit()
	{
		FGMPSourceAndHandlerDeleter* GGMPMessageSourceDeleter = nullptr;
		{
			GMP_THREAD_LOCK();
			Swap(GGMPMessageSourceDeleter, GetMessageSourceDeleter());
		}

		if (GGMPMessageSourceDeleter)
		{
			GMP_VERIFY_GAME_THREAD();
			GGMPMessageSourceDeleter->OnUObjectArrayShutdown();
			delete GGMPMessageSourceDeleter;
		}
	}

	static auto TryGet(bool bEnsure = true)
	{
		struct FGMPSrcHandler
		{
			FGMPSourceAndHandlerDeleter* operator->() const { return Deleter; }

			FGMPSrcHandler(FGMPSourceAndHandlerDeleter* InDeleter)
				: Deleter(InDeleter)
			{
				if (!InDeleter)
					GetGMPCritical()->Unlock();
			}
			~FGMPSrcHandler()
			{
				if (Deleter)
					GetGMPCritical()->Unlock();
			}
			explicit operator bool() const { return !!Deleter; }

			FGMPSrcHandler(FGMPSrcHandler&& Other)
				: Deleter(Other.Deleter)
			{
				Other.Deleter = nullptr;
			}

			FGMPSrcHandler& operator=(FGMPSrcHandler&& Other)
			{
				Swap(Deleter, Other.Deleter);
				return *this;
			}

		private:
			FGMPSrcHandler(const FGMPSrcHandler&) = delete;
			FGMPSrcHandler& operator=(const FGMPSrcHandler&) = delete;
			FGMPSourceAndHandlerDeleter* Deleter;
		};

		GetGMPCritical()->Lock();

		auto Ptr = GetMessageSourceDeleter();
		ensure(!bEnsure || Ptr);
		return FGMPSrcHandler{Ptr};
	}

	static void AddMessageMapping(FSigSource InSigSrc, FSignalStore* InPtr)
	{
		if (InSigSrc.IsValid())
			TryGet()->MessageMappings.FindOrAdd(InSigSrc).Add(InPtr->AsShared());
	}

	void RemoveSigSourceKey(FSigSource InSigSrcKey)
	{
		if (!ensureAlways(InSigSrcKey.IsExtKey()))
			return;

		if (SigSourceKeys.Remove(InSigSrcKey))
		{
			FSigSourceExtKey ExtKey = *static_cast<const FSigSourceExtKey*>(InSigSrcKey.GetRealAddr());
			const FSigSource InSigSrc = ExtKey.SrcObj;
			if (auto Deleter = TryGet(false))
			{
				auto FindSet = Deleter->SigSourceExtStorages.Find(InSigSrc);
				if (ensureAlways(FindSet))
				{
					FindSet->erase(ExtKey);
					if (FindSet->empty())
					{
						Deleter->SigSourceExtStorages.Remove(InSigSrc);
					}
				}
			}
		}
	}
	bool ContainsSigSourceKeys(FSigSource InSig) { return SigSourceKeys.Contains(InSig); }
	TSet<FSigSource> SigSourceKeys;
	TMap<FSigSource, std::set<FSigSourceExtKey, std::less<>>> SigSourceExtStorages;

	TLockFreePointerListUnordered<FSigSource, PLATFORM_CACHE_LINE_SIZE> GameThreadObjects;
};

void CreateGMPSourceAndHandlerDeleter()
{
	GMP_VERIFY_GAME_THREAD();
	if (TrueOnFirstCall([] {}))
	{
		FGMPSourceAndHandlerDeleter::GetMessageSourceDeleter() = new FGMPSourceAndHandlerDeleter();
#if GMP_DEBUG_SIGNAL
		FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool bSessionEnded, bool bCleanupResources) {
			if (!World)
				return;
			if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
				Deleter->RouterObjectRemoved(World);
		});
#endif
		FCoreDelegates::OnPreExit.AddStatic(&FGMPSourceAndHandlerDeleter::OnPreExit);
	}
}
void DestroyGMPSourceAndHandlerDeleter()
{
	FGMPSourceAndHandlerDeleter::OnPreExit();
}

FDelayedAutoRegisterHelper DelayCreateDeleter(EDelayedRegisterRunPhase::PreObjectSystemReady, [] { CreateGMPSourceAndHandlerDeleter(); });

#if GMP_DEBUG_SIGNAL
ISigSource::ISigSource()
{
	GMP_THREAD_LOCK();
	GMPSigIncs.Add(this);
}
#endif

ISigSource::~ISigSource()
{
	GMP_VERIFY_GAME_THREAD();
	FSigSource::RemoveSource(this);
}

FSignalStore::FSignalStore()
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet())
		Deleter->SignalStores.Add(this);
}

FSignalStore::~FSignalStore()
{
	GMP_VERIFY_GAME_THREAD();
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->SignalStores.RemoveSwap(this);
	Reset();
}

void FSignalStore::Reset()
{
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
	FSigElmKeySet TobeRemoved;
	for (auto& Pair : SourceObjs)
	{
		TobeRemoved.Append(Pair.Value);
	}
	for (auto& Pair : HandlerObjs)
	{
		TobeRemoved.Append(Pair.Value);
	}
	for (auto& Key : TobeRemoved)
	{
		FSignalUtils::GetSigElmSet(this).Remove(Key);
	}
#else
	FSignalUtils::GetSigElmSet(this).Reset();
#endif
	SourceObjs.Reset();
#if GMP_WITH_MSG_HOLDER
	SourceMsgs.Reset();
#endif
	HandlerObjs.Reset();
}

UObject* FSigSource::TryGetUObject() const
{
	if (IsUObject())
	{
		return reinterpret_cast<UObject*>(Addr);
	}
	else if (IsExtKey())
	{
		auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false);
		if (Deleter && ensureAlways(Deleter->ContainsSigSourceKeys(*this)))
		{
			return static_cast<const FSigSourceExtKey*>(GetRealAddr())->SrcObj.TryGetUObject();
		}
	}
	return nullptr;
}

FString FSigSource::GetNameSafe() const
{
	if (IsUObject())
	{
		return ::GetNameSafe(reinterpret_cast<UObject*>(Addr));
	}
	else if (IsExtKey())
	{
		auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false);
		if (Deleter && ensureAlways(Deleter->ContainsSigSourceKeys(*this)))
		{
			return static_cast<const FSigSourceExtKey*>(GetRealAddr())->GetNameSafe();
		}
	}
	return FString::Printf(TEXT("[%p]"), reinterpret_cast<const void*>(Addr));
}

FSigSource FSigSource::SigSourceKey(FSigSource InSig, FName InName, bool bCreate)
{
	GMP_VERIFY_GAME_THREAD();
	GMP_CHECK_SLOW(InSig);
	FSigSource Ret;
	do
	{
		auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false);
		if (!Deleter)
			break;
		auto FindSet = Deleter->SigSourceExtStorages.Find(InSig);
		if (!FindSet && bCreate)
		{
			const FSigSourceExtKey* Ptr = &*Deleter->SigSourceExtStorages.FindOrAdd(InSig).emplace(InSig, InName).first;
			Ret.Addr = (intptr_t)(Ptr) | FSigSource::ExtKey;
			Deleter->SigSourceKeys.Add(InSig);
			break;
		}

		auto FindExt = FindSet->find(InName);
		if (FindExt == FindSet->end())
			break;

		Ret.Addr = (intptr_t)(&*FindExt) | FSigSource::ExtKey;
	} while (false);

	return Ret;
}

#if GMP_DEBUG_SIGNAL
FSigSource::AddrType FSigSource::ObjectToAddr(const UObject* InObj)
{
	AddrType Ret = AddrType(InObj);
	GMP_CHECK_SLOW(!(Ret & EAll));
	return Ret;
}
#endif

TSharedRef<FSignalStore, FSignalBase::SPMode> FSignalImpl::MakeSignals(FName MessageKey)
{
	auto SignalImpl = MakeShared<FSignalStore, FSignalBase::SPMode>();
	SignalImpl->MessageKey = MessageKey;
	return SignalImpl;
}
struct FConnectionImpl : public FSigCollection::FConnection
{
	using FSigCollection::FConnection::FConnection;

	bool TestDisconnect(FGMPKey In)
	{
		GMP_VERIFY_GAME_THREAD();
		if (IsValid(In))
		{
			Disconnect();
		}
		return !IsValid();
	}
	void Disconnect()
	{
		GMP_VERIFY_GAME_THREAD();
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		FSignalUtils::DisconnectHandlerByID<true>(nullptr, Key);
#else
		FSignalUtils::DisconnectHandlerByID<true>(static_cast<FSignalStore*>(Pin().Get()), Key);
		Reset();
#endif
	}
	template<typename S>
	static void Insert(const FSigCollection& C, FGMPKey Key, S& Store)
	{
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
		C.Connections.Emplace(Key, Store);
#else
		C.Connections.Add(new FConnectionImpl(Key, Store));
#endif
	}

protected:
#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
	FORCEINLINE bool IsValid() { return true; }
#else
	bool IsValid() { return TWeakPtr<void, FSignalBase::SPMode>::IsValid() && Key; }
#endif
	bool IsValid(FGMPKey In) { return Key == In && IsValid(); }
};

struct FAutoConnectionImpl : public FConnectionImpl
{
	using FConnectionImpl::FConnectionImpl;
	~FAutoConnectionImpl() { Disconnect(); }
};

TSharedPtr<void> FSignalImpl::BindSignalConnection(FGMPKey Key) const
{
	return MakeShared<FAutoConnectionImpl>(Key, Store);
}
void FSignalImpl::BindSignalConnection(const FSigCollection& Collection, FGMPKey Key) const
{
	FConnectionImpl::Insert(Collection, Key, Store);
}

void FSignalImpl::Disconnect()
{
	GMP_VERIFY_GAME_THREAD();
	auto Key = Store->MessageKey;
	Store = MakeSignals(Key);
}

void FSignalImpl::Disconnect(FGMPKey Key)
{
	GMP_VERIFY_GAME_THREAD();
	FSignalUtils::DisconnectHandlerByID<true>(Impl(), Key);
}
template<bool bAllowDuplicate>
void FSignalImpl::Disconnect(const UObject* Listener)
{
	GMP_VERIFY_GAME_THREAD();
	FSignalUtils::DisconnectObjectHandler<bAllowDuplicate>(Impl(), Listener);
}

#if GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
void FSignalImpl::StaticDisconnect(FGMPKey Key)
{
	GMP_VERIFY_GAME_THREAD();
	FSignalUtils::DisconnectHandlerByID<true>(nullptr, Key);
}
#endif

template GMP_API void FSignalImpl::Disconnect<true>(const UObject* Listener);
template GMP_API void FSignalImpl::Disconnect<false>(const UObject* Listener);

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
void FSignalImpl::Disconnect(const FDelegateHandle& Handle)
{
	GMP_VERIFY_GAME_THREAD();
	Disconnect(GetDelegateHandleID(Handle));
}
#endif

template<bool bAllowDuplicate>
void FSignalImpl::DisconnectExactly(const UObject* Listener, FSigSource InSigSrc)
{
	GMP_VERIFY_GAME_THREAD();
	FSignalUtils::DisconnectObjectHandler<bAllowDuplicate>(Impl(), Listener, &InSigSrc);
}

template GMP_API void FSignalImpl::DisconnectExactly<true>(const UObject* Listener, FSigSource InSigSrc);
template GMP_API void FSignalImpl::DisconnectExactly<false>(const UObject* Listener, FSigSource InSigSrc);

template<bool bAllowDuplicate>
void FSignalImpl::OnFire(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	GMP_VERIFY_GAME_THREAD();
	GMP_CNOTE_ONCE(Store.IsUnique(), TEXT("maybe unsafe, should avoid reentry."));

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;
	TScopeCounter<decltype(StoreRef.ScopeCnt)> ScopeCounter(StoreRef.ScopeCnt);

	TArray<FGMPKey> CallbackIDs = FSignalUtils::GetSigElmSetKeys(&StoreRef);
	auto CallbackNums = CallbackIDs.Num();
	FMsgKeyArray EraseIDs;
	for (auto Idx = 0; Idx < CallbackNums; ++Idx)
	{
		auto Key = CallbackIDs[Idx];
		auto Elem = StoreRef.FindSigElm(Key);
		if (!Elem)
		{
			EraseIDs.Add(Key);
			continue;
		}
		bool bShouldErase = !Elem->IsInvokable();
		if (!bShouldErase)
		{
			Invoker(Elem);
			bShouldErase = !Elem->TestTimes();
		}
		if (bShouldErase)
		{
			EraseIDs.Add(Key);
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
			GMPDebug(StoreRef.MessageKey, Elem, TEXT("EraseOnFire"));
#endif
		}
	}

	for (auto Key : EraseIDs)
	{
		FSignalUtils::DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, Key);
	}
}
template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

template<bool bAllowDuplicate>
FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	GMP_VERIFY_GAME_THREAD();

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;
	TScopeCounter<decltype(StoreRef.ScopeCnt)> ScopeCounter(StoreRef.ScopeCnt);

	FMsgKeyArray EraseIDs;
	auto CallbackIDs = StoreRef.GetKeysBySrc<FOnFireResultArray>(InSigSrc);
	for (auto Idx = 0; Idx < CallbackIDs.Num(); ++Idx)
	{
		auto Key = CallbackIDs[Idx];
		auto Elem = StoreRef.FindSigElm(Key);
		if (!Elem)
		{
			continue;
		}

#if GMP_DEBUG_SIGNAL
		auto Listener = Elem->GetHandler();
		if (!Listener.IsStale())
		{
			// if multi world in one process : PIE
			auto SigObj = InSigSrc.TryGetUObject();
			if (Listener.Get() && SigObj && Listener.Get()->GetWorld() != SigObj->GetWorld())
				continue;
		}
#endif
		
		bool bShouldErase = !Elem->IsInvokable();
		if (!bShouldErase)
		{
			Invoker(Elem);
			bShouldErase = !Elem->TestTimes();
		}
		if (bShouldErase)
		{
			EraseIDs.Add(Key);
#if !GMP_SIGNAL_WITH_GLOBAL_SIGELMSET
			GMPDebug(StoreRef.MessageKey, Elem, TEXT("EraseOnFireWithSigSource"));
#endif
		}
	}

	if (EraseIDs.Num() > 0)
	{
		for (auto Key : EraseIDs)
		{
			FSignalUtils::DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, Key);
		}
	}
#if WITH_EDITOR
	return CallbackIDs;
#endif
}

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

FSigSource FSigSource::NullSigSrc = FSigSource(nullptr);
FSigSource FSigSource::AnySigSrc = FSigSource(reinterpret_cast<UObject*>(0xFFFFFFFFFFFFFFF8));
inline UWorld* FSigSource::GetSigSourceWorld() const
{
	do
	{
		auto Obj = TryGetUObject();
		if (!Obj)
			break;

		auto ObjWorld = Obj->GetWorld();
		if (!ObjWorld || ObjWorld == Obj)
			break;
		return ObjWorld;
	} while (false);
	return nullptr;
}

void FSigSource::RemoveSource(FSigSource InSigSrc)
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->RouterObjectRemoved(InSigSrc);
}

void FSigSource::RemoveSourceKey(FSigSource InSigSrc, FName InName)
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
	{
		auto Sig = SigSourceKey(InSigSrc, InName, false);
		if (Sig.IsValid())
		{
			Deleter->RemoveSigSourceKey(Sig);
		}
	}
}

FSigElm* FSignalStore::FindSigElm(FGMPKey Key) const
{
	GMP_VERIFY_GAME_THREAD();
	auto Find = FSignalUtils::GetSigElmSet(this).Find(Key);
	const FSigElm* Ret = Find ? Find->Get() : nullptr;
	return const_cast<FSigElm*>(Ret);
}

template<typename ArrayT>
ArrayT FSignalStore::GetKeysBySrc(FSigSource InSigSrc, bool bIncludeNoSrc) const
{
	GMP_VERIFY_GAME_THREAD();
	ArrayT Results;
	static auto AppendResult = [](ArrayT& Ret, const FSigElmKeySet* Set) {
		if (Set)
		{
			auto Arr = Set->Array();
			Arr.Sort();
			Ret.Append(Arr);
		}
	};
	AppendResult(Results, SourceObjs.Find(InSigSrc));

	if (UWorld* ObjWorld = InSigSrc.GetSigSourceWorld())
	{
		AppendResult(Results, SourceObjs.Find(ObjWorld));
	}

	if (bIncludeNoSrc)
	{
		AppendResult(Results, SourceObjs.Find(FSigSource::AnySigSrc));
	}

	return Results;
}
template TArray<FGMPKey> FSignalStore::GetKeysBySrc<TArray<FGMPKey>>(FSigSource InSigSrc, bool bIncludeNoSrc) const;

TArray<FGMPKey> FSignalStore::GetKeysByHandler(const UObject* InHandler) const
{
	GMP_VERIFY_GAME_THREAD();
	if (auto Set = HandlerObjs.Find(InHandler))
	{
		return Set->Array();
	}
	return {};
}

bool FSignalStore::IsAlive(const UObject* InHandler, FSigSource InSigSrc) const
{
	GMP_VERIFY_GAME_THREAD();
	if (auto* KeysFind = HandlerObjs.Find(InHandler))
	{
		for (auto It = KeysFind->CreateIterator(); It; ++It)
		{
			auto SigElm = FindSigElm(*It);
			if (SigElm && (!InSigSrc || SigElm->Source == InSigSrc))
				return true;
		}
	}
	return false;
}

void FSignalStore::RemoveSigElmStorage(FGMPKey SigKey)
{
	GMP_CHECK(!IsFiring());
#if GMP_DEBUG_SIGNAL
	FSignalUtils::RemoveOp(this, SigKey, [&](FSigElm* SigElm) {
		auto SigSrc = SigElm->GetSource();
		auto Sources = SourceObjs.Find(SigSrc);
		ensureAlways(!Sources || !Sources->Contains(SigKey));

		auto Obj = SigSrc.TryGetUObject();
		if (Obj->IsValidLowLevel())
		{
			auto Handlers = HandlerObjs.Find(Obj);
			ensureAlways(!Handlers || !Handlers->Contains(SigKey));
		}

		auto Handlers = HandlerObjs.Find(SigElm->GetHandler());
		ensureAlways(!Handlers || !Handlers->Contains(SigKey));
	});
#else
	FSignalUtils::GetSigElmSet(this).Remove(SigKey);
#endif
}
#if GMP_WITH_MSG_HOLDER
void FSignalStore::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : SourceMsgs)
	{
		Pair.Value.AddStructReferencedObjects(Collector);
	}
}
#endif

FSigElm* FSignalStore::AddSigElmImpl(FGMPKey Key, const UObject* InListener, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor)
{
	FSigElm* SigElm = FindSigElm(Key);
	if (!SigElm)
	{
		SigElm = Ctor();
		GMP_CHECK(SigElm);
		FSignalUtils::GetSigElmSet(this).Emplace(SigElm);
	}

	if (InListener)
	{
		FWeakObjectPtr WeakListener = InListener;
		SigElm->Handler = InListener;
		HandlerObjs.FindOrAdd(WeakListener).Add(Key);
#if WITH_EDITOR
		ensureAlways(!WeakListener.IsStale());
		for (auto It = HandlerObjs.CreateIterator(); It; ++It)
		{
			if (It->Key.IsStale())
			{
				It.RemoveCurrent();
			}
		}
#endif
	}

	if (InSigSrc.SigOrObj())
	{
		SigElm->Source = InSigSrc;
		SourceObjs.FindOrAdd(InSigSrc).Add(Key);
	}
	else
	{
		SourceObjs.FindOrAdd(FSigSource::AnySigSrc).Add(Key);
	}
	FGMPSourceAndHandlerDeleter::AddMessageMapping(InSigSrc, this);
	GMPDebug(MessageKey, SigElm, TEXT("AddSigElmImpl"));
	return SigElm;
}

bool FSignalStore::IsAlive(FGMPKey Key) const
{
	FSigElm* SigElm = FindSigElm(Key);
	return SigElm && !SigElm->GetHandler().IsStale();
}

bool FSignalStore::IsAlive() const
{
	GMP_VERIFY_GAME_THREAD();
	for (auto& Elm : FSignalUtils::GetSigElmSet(this))
	{
		return !Elm->GetHandler().IsStale();
	}
	return false;
}

void FSigCollection::DisconnectAll()
{
	GMP_VERIFY_GAME_THREAD();
	for (auto& C : Connections)
	{
		static_cast<FConnectionImpl&>(C).Disconnect();
	}
	Connections.Reset();
}
void FSigCollection::Disconnect(FGMPKey Key)
{
	GMP_VERIFY_GAME_THREAD();
	for (auto i = Connections.Num() - 1; i >= 0; --i)
	{
		if (static_cast<FConnectionImpl&>(Connections[i]).TestDisconnect(Key))
			Connections.RemoveAtSwap(i);
	}
}
}  // namespace GMP
