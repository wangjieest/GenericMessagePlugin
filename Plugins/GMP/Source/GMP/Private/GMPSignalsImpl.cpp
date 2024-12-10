//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSignalsImpl.h"

#include "Containers/LockFreeList.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/DelayedAutoRegister.h"

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

struct FSignalUtils
{
	static void ShutdownSingal(FSignalStore* In)
	{
		In->SourceObjs.Reset();
		In->HandlerObjs.Reset();
		In->GetStorageMap().Reset();
	}

	static void StaticOnObjectRemoved(FSignalStore* In, FSigSource InSigSrc)
	{
		GMP_VERIFY_GAME_THREAD();
		ensure(!In->IsFiring());

		// SourcePtrs
		FSignalStore::FSigElmKeySet SourcePtrs;
		In->SourceObjs.RemoveAndCopyValue(InSigSrc, SourcePtrs);

		// HandlerPtrs
		auto Obj = InSigSrc.TryGetUObject();
		if (Obj)
		{
			FSignalStore::FSigElmKeySet HandlerPtrs;
			In->HandlerObjs.RemoveAndCopyValue(Obj, HandlerPtrs);
			SourcePtrs.Append(MoveTemp(HandlerPtrs));
		}

		static FSignalStore::FSigElmKeySet Dummy;
		FSignalStore::FSigElmKeySet* Handlers = &Dummy;
		auto ObjWorld = Obj ? Obj->GetWorld() : (UWorld*)nullptr;
		if (auto Find = In->SourceObjs.Find(ObjWorld))
			Handlers = Find;

#if GMP_DEBUG_SIGNAL
		{
			bool bAllExisted = true;
			for (auto SigKey : SourcePtrs)
				bAllExisted &= In->GetStorageMap().Contains(SigKey);
			ensureAlways(bAllExisted);
		}
#endif
		// Storage
		if (In->GetStorageMap().Num() > 0)
		{
			for (auto SigKey : SourcePtrs)
			{
				Handlers->Remove(SigKey);
				In->RemoveSigElmStorage(SigKey);
			}
		}
	}

	template<bool bAllowDuplicate>
	static void RemoveSigElmImpl(FSignalStore* In, FSigElm* SigElm)
	{
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
		TUniquePtr<FSigElm> SigElm;
		if (In && In->GetStorageMap().RemoveAndCopyValue(Key, SigElm))
		{
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
			if (FSigElmKeySet* KeySet = In->SourceObjs.Find(SigElm->GetSource()))
			{
				KeySet.Remove(Key);
				if (!KeySet.Num())
				{
					In->SourceObjs.Remove(SigSrc);
				}
			}
#endif
		}
	}

	template<bool bAllowDuplicate>
	static void DisconnectObjectHandler(FSignalStore* In, const UObject* InHandler, FSigSource* InSigSrc = nullptr)
	{
		GMP_CHECK_SLOW(InHandler);
		FSignalStore::FSigElmKeySet HandlerKeys;
		if (!In->HandlerObjs.RemoveAndCopyValue(InHandler, HandlerKeys))
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

	static UWorld* GetSigSourceWorld(FSigSource InSigSrc)
	{
		do
		{
			auto Obj = InSigSrc.TryGetUObject();
			if (!Obj)
				break;

			auto ObjWorld = Obj->GetWorld();
			if (!ObjWorld || ObjWorld == Obj)
				break;
			return ObjWorld;
		} while (false);
		return nullptr;
	}
};  // namespace GMP

class FGMPSourceAndHandlerDeleter final : public FUObjectArray::FUObjectDeleteListener
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
			FSignalUtils::ShutdownSingal(Ptr);
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
			auto RemoveObject = [&](FSigSource InObj) {
				FSigStoreSet RemovedStores;
				if (MessageMappings.RemoveAndCopyValue(InObj, RemovedStores))
				{
					for (auto It = RemovedStores.CreateIterator(); It; ++It)
					{
						if (auto Pin = It->Pin())
							FSignalUtils::StaticOnObjectRemoved(Pin.Get(), InObj);
					}
				}
				ObjNameMappings.Remove(InObj);
			};

			if (!GameThreadObjects.IsEmpty())
			{
				TArray<FSigSource*> Objs;
				GameThreadObjects.PopAll(Objs);
				for (FSigSource* a : Objs)
				{
					RemoveObject(**reinterpret_cast<FSigSource**>(a));
				}
			}
			RemoveObject(InSigSrc);
		}
	}

	TArray<FSignalStore*, TInlineAllocator<32>> SignalStores;
	TMap<FSigSource, FSigStoreSet> MessageMappings;

	TMap<FSigSource, std::set<FName, FNameFastLess>> ObjNameMappings;

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

	TLockFreePointerListUnordered<FSigSource, PLATFORM_CACHE_LINE_SIZE> GameThreadObjects;
};

void CreateGMPSourceAndHandlerDeleter()
{
	GMP_VERIFY_GAME_THREAD();
	if (TrueOnFirstCall([] {}))
	{
		FGMPSourceAndHandlerDeleter::GetMessageSourceDeleter() = new FGMPSourceAndHandlerDeleter();
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
}

FSigSource FSigSource::ObjNameFilter(const UObject* InObj, FName InName, bool bCreate)
{
	GMP_VERIFY_GAME_THREAD();
	GMP_CHECK_SLOW(InObj);
	FSigSource Ret;
	do
	{
		auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false);
		if (!Deleter)
			break;

		if (bCreate)
		{
			Ret.Addr = (intptr_t)(&*Deleter->ObjNameMappings.FindOrAdd(InObj).emplace(InName).first) | FSigSource::External;
			break;
		}

		auto FindSet = Deleter->ObjNameMappings.Find(InObj);
		if (!FindSet)
			break;

		auto FindName = FindSet->find(InName);
		if (FindName == FindSet->end())
			break;

		Ret.Addr = (intptr_t)(&*FindName) | FSigSource::External;
	} while (false);

	return Ret;
}

#if GMP_DEBUG_SIGNAL
FSigSource::AddrType FSigSource::ObjectToAddr(const UObject* InObj)
{
	auto GameInst = Cast<UGameInstance>(InObj);
	auto GameViewport = Cast<UGameViewportClient>(InObj);
	if (GIsEditor && (GameInst || GameViewport))
	{
		GMP::TrueOnWorldFisrtCall(InObj, [&] {
			UE_LOG(LogGMP, Warning, TEXT("FSigSource::ObjectToAddr using %s->GetWorld() instead of %s: %s"), GameInst ? TEXT("GameInstance") : TEXT("GameViewportClient"), *InObj->GetClass()->GetName(), *InObj->GetPathName());
			return true;
		});
		AddrType Ret = AddrType(InObj->GetWorld());
		GMP_CHECK_SLOW(!(Ret & EAll));
		return Ret;
	}
	else
	{
		AddrType Ret = AddrType(InObj);
		GMP_CHECK_SLOW(!(Ret & EAll));
		return Ret;
	}
}
#endif

TSharedRef<FSignalStore, FSignalBase::SPMode> FSignalImpl::MakeSignals()
{
	auto SignalImpl = MakeShared<FSignalStore, FSignalBase::SPMode>();
	return SignalImpl;
}
struct ConnectionImpl : public FSigCollection::Connection
{
	using FSigCollection::Connection::Connection;

	bool TestDisconnect(FGMPKey In)
	{
		GMP_VERIFY_GAME_THREAD();
		if (IsValid(In))
		{
			Disconnect();
			Reset();
		}
		return !IsValid();
	}
	void Disconnect()
	{
		GMP_VERIFY_GAME_THREAD();
		FSignalUtils::DisconnectHandlerByID<true>(static_cast<FSignalStore*>(Pin().Get()), Key);
	}

	FORCEINLINE static void Insert(const FSigCollection& C, ConnectionImpl* In) { C.Connections.Add(In); }

protected:
	bool IsValid() { return TWeakPtr<void, FSignalBase::SPMode>::IsValid() && Key; }
	bool IsValid(FGMPKey In) { return Key == In && IsValid(); }
};

void FSignalImpl::BindSignalConnection(const FSigCollection& Collection, FGMPKey Key) const
{
	ConnectionImpl::Insert(Collection, new ConnectionImpl(Store, Key));
}

bool FSignalImpl::IsEmpty() const
{
	return Impl()->GetStorageMap().Num() == 0;
}

void FSignalImpl::Disconnect()
{
	GMP_VERIFY_GAME_THREAD();
	Store = MakeSignals();
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

	TArray<FGMPKey> CallbackIDs;
	StoreRef.GetStorageMap().GetKeys(CallbackIDs);

	auto CallbackNums = CallbackIDs.Num();
	FMsgKeyArray EraseIDs;
	for (auto Idx = 0; Idx < CallbackNums; ++Idx)
	{
		auto ID = CallbackIDs[Idx];
		auto Elem = StoreRef.FindSigElm(ID);
		if (!Elem)
		{
			EraseIDs.Add(ID);
			continue;
		}

		if (!Elem->TestInvokable([&] { Invoker(Elem); }))
		{
			EraseIDs.Add(ID);
		}
	}

	for (auto ID : EraseIDs)
		FSignalUtils::DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, ID);
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
		auto ID = CallbackIDs[Idx];
		auto Elem = StoreRef.FindSigElm(ID);
		if (!Elem)
		{
			continue;
		}

#if GMP_DEBUG_SIGNAL
		auto Listener = Elem->GetHandler();
		if (!Listener.IsStale(true))
		{
			// if multi world in one process : PIE
			auto SigObj = InSigSrc.TryGetUObject();
			if (Listener.Get() && SigObj && Listener.Get()->GetWorld() != SigObj->GetWorld())
				continue;
		}
#endif
		if (!Elem->TestInvokable([&] { Invoker(Elem); }))
		{
			EraseIDs.Add(ID);
		}
	}

	if (EraseIDs.Num() > 0)
	{
		for (auto ID : EraseIDs)
			FSignalUtils::DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, ID);
	}
#if GMP_DEBUG_SIGNAL
	return CallbackIDs;
#endif
}

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

FSigSource FSigSource::NullSigSrc = FSigSource(nullptr);
struct FAnySigSrcType
{
};
template<>
struct TExternalSigSource<FAnySigSrcType> : public std::true_type
{
};
FSigSource FSigSource::AnySigSrc = FSigSource((FAnySigSrcType*)0xFFFFFFFFFFFFFFF8);

void FSigSource::RemoveSource(FSigSource InSigSrc)
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(true))
		Deleter->RouterObjectRemoved(InSigSrc);
}

FSigElm* FSignalStore::FindSigElm(FGMPKey Key) const
{
	GMP_VERIFY_GAME_THREAD();
	auto Find = GetStorageMap().Find(Key);
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

	if (UWorld* ObjWorld = FSignalUtils::GetSigSourceWorld(InSigSrc))
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
	TUniquePtr<FSigElm> SigElm;
	GetStorageMap().RemoveAndCopyValue(SigKey, SigElm);
	if (SigElm)
	{
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
	}
#else
	GetStorageMap().Remove(SigKey);
#endif
}

FSigElm* FSignalStore::AddSigElmImpl(FGMPKey Key, const UObject* InListener, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor)
{
	GMP_VERIFY_GAME_THREAD();
	auto& Ref = GetStorageMap().FindOrAdd(Key);
	FSigElm* SigElm = Ref.Get();
	if (!SigElm)
	{
		SigElm = Ctor();
		Ref.Reset(SigElm);
	}

	if (InListener)
	{
		SigElm->Handler = InListener;
		HandlerObjs.FindOrAdd(InListener).Add(Key);
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

	return SigElm;
}

bool FSignalStore::IsAlive(FGMPKey Key) const
{
	GMP_VERIFY_GAME_THREAD();
	auto Find = FindSigElm(Key);
	return Find && !Find->GetHandler().IsStale(true);
}

void FSigCollection::DisconnectAll()
{
	GMP_VERIFY_GAME_THREAD();
	for (auto& C : Connections)
	{
		static_cast<ConnectionImpl&>(C).Disconnect();
	}
	Connections.Reset();
}
void FSigCollection::Disconnect(FGMPKey Key)
{
	GMP_VERIFY_GAME_THREAD();
	for (auto i = Connections.Num(); i >= 0; --i)
	{
		if (static_cast<ConnectionImpl&>(Connections[i]).TestDisconnect(Key))
			Connections.RemoveAtSwap(i);
	}
}
}  // namespace GMP
