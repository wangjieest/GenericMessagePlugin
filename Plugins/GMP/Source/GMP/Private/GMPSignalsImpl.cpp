//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSignalsImpl.h"

#include "Misc/DelayedAutoRegister.h"

#include <algorithm>
#include <set>

#if UE_4_23_OR_LATER
#include "Containers/LockFreeList.h"
#endif

FGMPKey FGMPKey::NextGMPKey()
{
	static std::atomic<uint64> GNextID(1);
	uint64 Result = ++GNextID;
	if (Result == 0)
		Result = ++GNextID;
	return FGMPKey(Result);
}

using FOnGMPSigSourceDeleted = TMulticastDelegate<void(GMP::FSigSource)>;

namespace GMP
{
#ifndef GMP_SIGNALS_MULTI_THREAD_REMOVAL
#define GMP_SIGNALS_MULTI_THREAD_REMOVAL (UE_4_23_OR_LATER)
#endif

template<typename Type>
bool OnceOnGameThread(const Type&)
{
	static std::atomic<bool> bValue = true;
	bool bExpected = true;
	if (IsInGameThread() && bValue.compare_exchange_strong(bExpected, false))
	{
	}
	return bValue;
}

#if WITH_EDITOR
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
		In->AnySrcSigKeys.Reset();
		In->GetStorageMap().Reset();
	}

	static void StaticOnObjectRemoved(FSignalStore* In, FSigSource InSigSrc)
	{
		GMP_VERIFY_GAME_THREAD();
		ensure(!In->IsFiring());

		// Sources
		FSignalStore::FSigElmPtrSet SigElms;
		In->SourceObjs.RemoveAndCopyValue(InSigSrc, SigElms);
#if WITH_EDITOR
		FSignalStore::FSigElmPtrSet SrcElms = SigElms;
		FSignalStore::FSigElmPtrSet HandlerElms;
#endif
		// Handlers
		auto Obj = InSigSrc.TryGetUObject();
		if (Obj)
		{
			FSignalStore::FSigElmPtrSet Handlers;
			In->HandlerObjs.RemoveAndCopyValue(Obj, Handlers);
#if WITH_EDITOR
			HandlerElms = Handlers;
#endif
			SigElms.Append(MoveTemp(Handlers));
		}

		static FSignalStore::FSigElmPtrSet Dummy;
		FSignalStore::FSigElmPtrSet* Handlers = &Dummy;
		auto ObjWorld = Obj ? Obj->GetWorld() : (UWorld*)nullptr;
		if (auto Find = In->SourceObjs.Find(ObjWorld))
			Handlers = Find;

#if WITH_EDITOR
		{
			static auto ContainsSigElm = [](FSignalStore* In, FSigElm* InSigElm) -> bool {
				for (auto It = In->GetStorageMap().CreateConstIterator(); It; ++It)
				{
					if (It->Value.Get() == InSigElm)
					{
						return true;
					}
				}
				return false;
			};
			bool bAllExisted = true;
			for (auto SigElm : SigElms)
				bAllExisted &= ContainsSigElm(In, SigElm);
			ensureAlways(bAllExisted);
		}
#endif
		// Storage
		if (In->GetStorageMap().Num() > 0)
		{
			for (auto SigElm : SigElms)
			{
				Handlers->Remove(SigElm);

				auto Key = SigElm->GetGMPKey();

				// if (SigElm->GetHandler().IsExplicitlyNull())
				In->AnySrcSigKeys.Remove(Key);
				In->RemoveSigElmStorage(SigElm);
			}
		}
	}

	static void RemoveStorage(FSignalStore* In, FSigElm* InSigElm)
	{
		In->AnySrcSigKeys.Remove(InSigElm->GetGMPKey());
		if (auto Find = In->SourceObjs.Find(InSigElm->GetSource()))
		{
			Find->Remove(InSigElm);
		}
		if (!In->IsFiring())
		{
			In->RemoveSigElmStorage(InSigElm);
		}
		else
		{
			InSigElm->SetLeftTimes(0);
		}
	}

	template<bool bAllowDuplicate>
	static void RemoveSigElm(FSignalStore* In, FGMPKey Key)
	{
		auto SigElmFind = In->GetStorageMap().Find(Key);
		if (!SigElmFind)
			return;

		auto* SigElm = SigElmFind->Get();

		// Handlers
		auto& Handler = SigElm->GetHandler();
		GMP_IF_CONSTEXPR(bAllowDuplicate)
		{
			if (auto Find = In->HandlerObjs.Find(Handler))
			{
				Find->Remove(SigElm);
			}
		}
		else
		{
			// no need to seach any more
			In->HandlerObjs.Remove(Handler);
		}

		// Storage
		RemoveStorage(In, SigElm);
	}

	template<bool bAllowDuplicate>
	static void RemoveHandler(FSignalStore* In, const UObject* InHandler, FSigSource* InSigSrc = nullptr)
	{
		if (auto HandlerFind = In->HandlerObjs.Find(InHandler))
		{
			for (auto It = HandlerFind->CreateIterator(); It; ++It)
			{
				auto SigElm = *It;
				if (!ensure(SigElm))
				{
					It.RemoveCurrent();
					continue;
				}

				if (!InSigSrc || SigElm->GetSource() == *InSigSrc)
				{
					// Storage
					RemoveStorage(In, SigElm);
					It.RemoveCurrent();
					break;
				}
			}

			GMP_IF_CONSTEXPR(bAllowDuplicate)
			{
				if (HandlerFind->Num() == 0)
					In->HandlerObjs.Remove(InHandler);
			}
			else
			{
				// no need to seach any more
				ensure(HandlerFind->Num() == 0);
				In->HandlerObjs.Remove(InHandler);
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

class FGMPSourceAndHandlerDeleter final
#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
	: public FUObjectArray::FUObjectDeleteListener
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

#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
	virtual void NotifyUObjectDeleted(const UObjectBase* ObjectBase, int32 Index) override { RouterObjectRemoved(static_cast<const UObject*>(ObjectBase)); }
#else
	void OnWatchedObjectRemoved(const UObject* Object) { RouterObjectRemoved(Object); }
#endif
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
#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
		// FIXME: IsInGarbageCollectorThread()
		if (!UNLIKELY(IsInGameThread()))
		{
			GameThreadObjects.Push(*reinterpret_cast<FSigSource**>(&InSigSrc));
		}
		else
#else
		GMP_VERIFY_GAME_THREAD();
#endif
		{
#if WITH_EDITOR
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

#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
			if (!GameThreadObjects.IsEmpty())
			{
				TArray<FSigSource*> Objs;
				GameThreadObjects.PopAll(Objs);
				for (FSigSource* a : Objs)
				{
					RemoveObject(**reinterpret_cast<FSigSource**>(a));
				}
			}
#endif
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

#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
	TLockFreePointerListUnordered<FSigSource, PLATFORM_CACHE_LINE_SIZE> GameThreadObjects;
#endif
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

#if WITH_EDITOR
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
		if (auto SlotListImpl = Pin())
			FSignalUtils::RemoveSigElm<true>(static_cast<FSignalStore*>(SlotListImpl.Get()), Key);
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
	return Impl()->AnySrcSigKeys.Num() == 0;
}

void FSignalImpl::Disconnect()
{
	GMP_VERIFY_GAME_THREAD();
	Store = MakeSignals();
}

void FSignalImpl::Disconnect(FGMPKey Key)
{
	GMP_VERIFY_GAME_THREAD();
	FSignalUtils::RemoveSigElm<true>(Impl(), Key);
}

template<bool bAllowDuplicate>
void FSignalImpl::Disconnect(const UObject* Listener)
{
	GMP_VERIFY_GAME_THREAD();
	GMP_CHECK_SLOW(Listener);
	FSignalUtils::RemoveHandler<bAllowDuplicate>(Impl(), Listener);
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
	GMP_CHECK_SLOW(Listener);
	FSignalUtils::RemoveHandler<bAllowDuplicate>(Impl(), Listener, &InSigSrc);
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
	TScopeCounter ScopeCounter(StoreRef.ScopeCnt);

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
		FSignalUtils::RemoveSigElm<bAllowDuplicate>(&StoreRef, ID);
}
template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

template<bool bAllowDuplicate>
FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	GMP_VERIFY_GAME_THREAD();

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;
	TScopeCounter ScopeCounter(StoreRef.ScopeCnt);

	// excactly
	auto CallbackIDs = StoreRef.GetKeysBySrc<FOnFireResultArray>(InSigSrc, false);

	CallbackIDs.Append(StoreRef.AnySrcSigKeys);

	FMsgKeyArray EraseIDs;
	for (auto Idx = 0; Idx < CallbackIDs.Num(); ++Idx)
	{
		auto ID = CallbackIDs[Idx];
		auto Elem = StoreRef.FindSigElm(ID);
		if (!Elem)
		{
			continue;
		}

#if WITH_EDITOR
		auto Listener = Elem->GetHandler();
		if (!Listener.IsStale(true))
		{
			// if mutli world in one process : PIE
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
			FSignalUtils::RemoveSigElm<bAllowDuplicate>(&StoreRef, ID);
	}
#if WITH_EDITOR
	return CallbackIDs;
#endif
}

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

FSigSource FSigSource::NullSigSrc = FSigSource(nullptr);

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
	if (auto Set = SourceObjs.Find(InSigSrc))
	{
		for (auto Elm : *Set)
		{
			Results.Add(Elm->GetGMPKey());
		}
	}

	Results.Sort();

	if (UWorld* ObjWorld = FSignalUtils::GetSigSourceWorld(InSigSrc))
	{
		if (const FSigElmPtrSet* Set = SourceObjs.Find(ObjWorld))
		{
			if (Set->Num() > 0)
			{
				const auto OldNum = Results.Num();
				for (auto Elm : *Set)
				{
					Results.Add(Elm->GetGMPKey());
				}
				std::sort(&Results[OldNum], &Results[OldNum + Set->Num() - 1]);
			}
		}
	}

	if (bIncludeNoSrc)
	{
		Results.Append(AnySrcSigKeys);
	}

	return Results;
}
template TArray<FGMPKey> FSignalStore::GetKeysBySrc<TArray<FGMPKey>>(FSigSource InSigSrc, bool bIncludeNoSrc) const;

TArray<FGMPKey> FSignalStore::GetKeysByHandler(const UObject* InHandler) const
{
	GMP_VERIFY_GAME_THREAD();
	TArray<FGMPKey> Results;
	if (auto Set = HandlerObjs.Find(InHandler))
	{
		for (auto Elm : *Set)
		{
			if (ensure(Elm))
				Results.Add(Elm->GetGMPKey());
		}
	}
	return Results;
}

bool FSignalStore::IsAlive(const UObject* InHandler, FSigSource InSigSrc) const
{
	GMP_VERIFY_GAME_THREAD();
	if (auto* KeysFind = HandlerObjs.Find(InHandler))
	{
		for (auto It = KeysFind->CreateIterator(); It; ++It)
		{
			auto Ptr = *It;
			GMP_CHECK_SLOW(Ptr);
			if (Ptr->Source == InSigSrc)
			{
				return true;
			}
		}
	}
	return false;
}

void FSignalStore::RemoveSigElmStorage(FSigElm* SigElm)
{
	GMP_CHECK(SigElm && !IsFiring());
	auto Key = SigElm->GetGMPKey();
	auto SigSrc = SigElm->GetSource();
	GetStorageMap().Remove(Key);

#if WITH_EDITOR
	{
		ensureAlways(!AnySrcSigKeys.Contains(Key));

		if (auto Find = SourceObjs.Find(SigSrc))
		{
			ensureAlways(!Find || !Find->Num());
		}

		if (auto Obj = SigSrc.TryGetUObject())
		{
			if (auto Handlers = HandlerObjs.Find(Obj))
			{
				ensureAlways(!Handlers->Contains(SigElm));
			}
		}
	}
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
		HandlerObjs.FindOrAdd(InListener).Add(SigElm);
	}

	if (InSigSrc.SigOrObj())
	{
		SigElm->Source = InSigSrc;
		SourceObjs.FindOrAdd(InSigSrc).Add(SigElm);
	}
	else
	{
		AnySrcSigKeys.Emplace(Key);
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

#undef GMP_SIGNALS_MULTI_THREAD_REMOVAL

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
