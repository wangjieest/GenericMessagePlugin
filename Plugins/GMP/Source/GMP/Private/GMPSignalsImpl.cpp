//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSignalsImpl.h"

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

using FOnGMPSigSourceDeleted = TUnrealMulticastDelegate<void, GMP::FSigSource>;

namespace GMP
{
#ifndef GMP_SIGNALS_MULTI_THREAD_REMOVAL
#define GMP_SIGNALS_MULTI_THREAD_REMOVAL (UE_4_23_OR_LATER && 1)
#endif

#if WITH_EDITOR
static TSet<FSigSource> GMPSigSources;
#endif

struct FSignalUtils
{
	static void ShutdownSingal(FSignalStore* In)
	{
		In->SourceObjs.Reset();
		In->HandlerObjs.Reset();
		In->AnySrcSigKeys.Reset();
		In->GetStorageMap().Reset();
	}

	static void StaticOnObjectRemoved(FSignalStore* In, FSigSource InObj)
	{
		checkSlow(IsInGameThread());

		// Sources
		FSignalStore::FSigElmPtrSet SigElms;
		In->SourceObjs.RemoveAndCopyValue(InObj, SigElms);

		// Handlers
		if (auto Obj = InObj.TryGetUObject())
		{
			FSignalStore::FSigElmPtrSet Handlers;
			In->HandlerObjs.RemoveAndCopyValue(Obj, Handlers);
			SigElms.Append(Handlers);
		}

		// Storage
		for (auto SigElm : SigElms)
		{
			auto Key = SigElm->GetGMPKey();

			// if (SigElm->GetHandler().IsExplicitlyNull())
			In->AnySrcSigKeys.Remove(Key);
			In->GetStorageMap().Remove(Key);
		}
	}

	template<bool bAllowDuplicate>
	static void RemoveSigElm(FSignalStore* In, FGMPKey Key)
	{
		In->AnySrcSigKeys.Remove(Key);

		// Storage
		decltype(In->GetStorageMap().FindRef(Key)) SigElm;
		if (!In->GetStorageMap().RemoveAndCopyValue(Key, SigElm))
			return;

		// Sources
		if (auto Find = In->SourceObjs.Find(SigElm->GetSource()))
		{
			Find->Remove(SigElm.Get());
		}

		// Handlers
		auto Handler = SigElm->GetHandler();
		GMP_IF_CONSTEXPR(bAllowDuplicate)
		{
			if (auto Find = In->HandlerObjs.Find(Handler))
			{
				Find->Remove(SigElm.Get());
			}
		}
		else
		{
			// no need to seach any more
			In->HandlerObjs.Remove(Handler);
		}
	}

	template<bool bAllowDuplicate>
	static void RemoveExactly(FSignalStore* In, const UObject* InHandler, FSigSource InSource)
	{
		if (auto HandlerFind = In->HandlerObjs.Find(InHandler))
		{
			for (auto It = HandlerFind->CreateIterator(); It; ++It)
			{
				auto Ptr = *It;
				if (!ensure(Ptr))
				{
					It.RemoveCurrent();
					continue;
				}

				if (Ptr->GetSource() == InSource)
				{
					auto Key = Ptr->GetGMPKey();
					In->AnySrcSigKeys.Remove(Key);
					In->GetStorageMap().Remove(Key);
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
};

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
	using FSigStoreSet = TSet<TWeakPtr<FSignalStore>>;
	void OnUObjectArrayShutdown()
	{
		MessageMappings.Reset();
		for (auto Ptr : SignalStores)
		{
			FSignalUtils::ShutdownSingal(Ptr);
		}
	}

	void RouterObjectRemoved(FSigSource InSource)
	{
#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
		// FIXME: IsInGarbageCollectorThread()
		if (!UNLIKELY(IsInGameThread()))
		{
			GameThreadObjects.Push(*reinterpret_cast<FSigSource**>(&InSource));
		}
		else
#else
		checkSlow(IsInGameThread());
#endif
		{
#if WITH_EDITOR
			GMPSigSources.Remove(InSource);
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
			RemoveObject(InSource);
		}
	}

	TArray<FSignalStore*, TInlineAllocator<32>> SignalStores;
	TMap<FSigSource, FSigStoreSet> MessageMappings;

	static TUniquePtr<FGMPSourceAndHandlerDeleter> GGMPMessageSourceDeleter;
	static void TryCreate()
	{
		if (TrueOnFirstCall([] {}))
		{
			GGMPMessageSourceDeleter = MakeUnique<FGMPSourceAndHandlerDeleter>();
			FCoreDelegates::OnPreExit.AddStatic(&FGMPSourceAndHandlerDeleter::OnPreExit);
		}
	}
	static FGMPSourceAndHandlerDeleter* TryGet() { return GGMPMessageSourceDeleter.Get(); }

	static void AddMessageMapping(FSigSource InSource, FSignalStore* InPtr)
	{
		if (InSource.IsValid())
			TryGet()->MessageMappings.FindOrAdd(InSource).Add(InPtr->AsShared());
	}

	static void OnPreExit()
	{
		if (ensure(GGMPMessageSourceDeleter))
		{
			GGMPMessageSourceDeleter->OnUObjectArrayShutdown();
			GGMPMessageSourceDeleter.Reset();
		}
	}

#if GMP_SIGNALS_MULTI_THREAD_REMOVAL
	TLockFreePointerListUnordered<FSigSource, PLATFORM_CACHE_LINE_SIZE> GameThreadObjects;
#endif
};
TUniquePtr<FGMPSourceAndHandlerDeleter> FGMPSourceAndHandlerDeleter::GGMPMessageSourceDeleter;

#if WITH_EDITOR
ISigSource::ISigSource()
{
	check(IsInGameThread());
	GMPSigSources.Add(this);
}
#endif

ISigSource::~ISigSource()
{
	check(IsInGameThread());
	FSigSource::RemoveSource(this);
}

FSignalStore::FSignalStore()
{
	check(IsInGameThread());
	FGMPSourceAndHandlerDeleter::TryCreate();
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet())
		Deleter->SignalStores.Add(this);
}

FSignalStore::~FSignalStore()
{
	check(IsInGameThread());
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet())
		Deleter->SignalStores.RemoveSwap(this);
}

TSharedRef<FSignalStore> FSignalImpl::MakeSignals()
{
	auto SignalImpl = MakeShared<FSignalStore>();

	return SignalImpl;
}
struct ConnectionImpl : public FSigCollection::Connection
{
	using FSigCollection::Connection::Connection;

	bool TestDisconnect(FGMPKey In)
	{
		checkSlow(IsInGameThread());
		if (IsValid(In))
		{
			Disconnect();
			Reset();
		}
		return !IsValid();
	}
	void Disconnect()
	{
		auto SlotListImpl = Pin();
		if (SlotListImpl.IsValid())
			FSignalUtils::RemoveSigElm<true>(static_cast<FSignalStore*>(SlotListImpl.Get()), Key);
	}

	FORCEINLINE static void Insert(const FSigCollection& C, ConnectionImpl* In) { C.Connections.Add(In); }

protected:
	bool IsValid() { return TWeakPtr<void>::IsValid() && Key; }
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
	checkSlow(IsInGameThread());
	Store = MakeSignals();
}

void FSignalImpl::Disconnect(FGMPKey Key)
{
	checkSlow(IsInGameThread());
	FSignalUtils::RemoveSigElm<true>(Impl(), Key);
}

void FSignalImpl::Disconnect(const UObject* Listener)
{
	checkSlow(IsInGameThread() && Listener);
	FSignalUtils::StaticOnObjectRemoved(Impl(), Listener);
}

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
void FSignalImpl::Disconnect(const FDelegateHandle& Handle)
{
	checkSlow(IsInGameThread());
	Disconnect(GetDelegateHandleID(Handle));
}
#endif

template<bool bAllowDuplicate>
void FSignalImpl::DisconnectExactly(const UObject* Listener, FSigSource InSource)
{
	checkSlow(IsInGameThread() && Listener);
	FSignalUtils::RemoveExactly<bAllowDuplicate>(Impl(), Listener, InSource);
}

template GMP_API void FSignalImpl::DisconnectExactly<true>(const UObject* Listener, FSigSource InSource);
template GMP_API void FSignalImpl::DisconnectExactly<false>(const UObject* Listener, FSigSource InSource);

template<bool bAllowDuplicate>
void FSignalImpl::OnFire(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	checkSlow(IsInGameThread());
	GMP_CNOTE_ONCE(Store.IsUnique(), TEXT("maybe unsafe, should avoid reentry."));

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;
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

		switch (Elem->TestInvokable())
		{
			case 1:
				Invoker(Elem);
			case 0:
				EraseIDs.Add(ID);
				break;
			default:
				Invoker(Elem);
				break;
		}
	}

	for (auto ID : EraseIDs)
		FSignalUtils::RemoveSigElm<bAllowDuplicate>(&StoreRef, ID);
}
template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

template<bool bAllowDuplicate>
FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource(FSigSource InSource, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	checkSlow(IsInGameThread());

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;

	// excactly
	auto Find = StoreRef.SourceObjs.Find(InSource);
	auto CallbackIDs = StoreRef.GetKeysBySrc<TArray<FGMPKey, TInlineAllocator<16>>>(InSource);
	CallbackIDs.Sort();

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
			auto SigSource = InSource.TryGetUObject();
			if (Listener.Get() && SigSource && Listener.Get()->GetWorld() != SigSource->GetWorld())
				continue;
		}
#endif
		switch (Elem->TestInvokable())
		{
			case 1:
				Invoker(Elem);
			case 0:
				EraseIDs.Add(ID);
				break;
			default:
				Invoker(Elem);
				break;
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

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSource, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSource, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

void FSigSource::RemoveSource(FSigSource InSource)
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet())
		Deleter->RouterObjectRemoved(InSource);
}

FSigElm* FSignalStore::FindSigElm(FGMPKey Key) const
{
	auto Find = GetStorageMap().Find(Key);
	const FSigElm* Ret = Find ? Find->Get() : nullptr;
	return const_cast<FSigElm*>(Ret);
}

template<typename ArrayT>
ArrayT FSignalStore::GetKeysBySrc(FSigSource InSource) const
{
	ArrayT Results;
	if (auto Set = SourceObjs.Find(InSource))
	{
		for (auto Elm : *Set)
		{
			Results.Add(Elm->GetGMPKey());
		}
	}
	return Results;
}
template TArray<FGMPKey> FSignalStore::GetKeysBySrc<TArray<FGMPKey>>(FSigSource InSource) const;

TArray<FGMPKey> FSignalStore::GetKeysByHandler(const UObject* InHandler) const
{
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

bool FSignalStore::IsAlive(const UObject* InHandler, FSigSource InSource) const
{
	if (auto* KeysFind = HandlerObjs.Find(InHandler))
	{
		for (auto It = KeysFind->CreateIterator(); It; ++It)
		{
			auto Ptr = *It;
			checkSlow(Ptr);
			if (Ptr->Source == InSource)
			{
				return true;
			}
		}
	}
	return false;
}

FSigElm* FSignalStore::AddSigElmImpl(FGMPKey Key, const UObject* InListener, FSigSource InSource, const TGMPFunctionRef<FSigElm*()>& Ctor)
{
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

	if (InSource.SigOrObj())
	{
		SigElm->Source = InSource;
		SourceObjs.FindOrAdd(InSource).Add(SigElm);
	}
	else
	{
		AnySrcSigKeys.Emplace(Key);
	}
	FGMPSourceAndHandlerDeleter::AddMessageMapping(InSource, this);

	return SigElm;
}

bool FSignalStore::IsAlive(FGMPKey Key) const
{
	auto Find = FindSigElm(Key);
	return Find && !Find->GetHandler().IsStale(true);
}

#undef GMP_SIGNALS_MULTI_THREAD_REMOVAL

void FSigCollection::DisconnectAll()
{
	for (auto& C : Connections)
	{
		static_cast<ConnectionImpl&>(C).Disconnect();
	}
	Connections.Reset();
}
void FSigCollection::Disconnect(FGMPKey Key)
{
	for (auto i = Connections.Num(); i >= 0; --i)
	{
		if (static_cast<ConnectionImpl&>(Connections[i]).TestDisconnect(Key))
			Connections.RemoveAtSwap(i);
	}
}
}  // namespace GMP