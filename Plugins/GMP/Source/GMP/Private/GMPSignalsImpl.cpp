//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSignalsImpl.h"

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

using FOnGMPSigSourceDeleted = TUnrealMulticastDelegate<void, GMP::FSigSource>;

namespace GMP
{
#ifndef GMP_SIGNALS_MULTI_THREAD_REMOVAL
#define GMP_SIGNALS_MULTI_THREAD_REMOVAL (UE_4_23_OR_LATER && 1)
#endif

#if WITH_EDITOR
static TSet<FSigSource> GMPSigIncs;
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
	static void RemoveExactly(FSignalStore* In, const UObject* InHandler, FSigSource InSigSrc)
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

				if (Ptr->GetSource() == InSigSrc)
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
		checkSlow(IsInGameThread());
#endif
		{
#if WITH_EDITOR
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

	static void AddMessageMapping(FSigSource InSigSrc, FSignalStore* InPtr)
	{
		if (InSigSrc.IsValid())
			TryGet()->MessageMappings.FindOrAdd(InSigSrc).Add(InPtr->AsShared());
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
	GMPSigIncs.Add(this);
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

FSigSource FSigSource::CombineObjName(const UObject* InObj, FName InName, bool bCreate)
{
	check(InObj && IsInGameThread());
	FSigSource Ret;
	do
	{
		auto Deleter = FGMPSourceAndHandlerDeleter::TryGet();
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
void FSignalImpl::DisconnectExactly(const UObject* Listener, FSigSource InSigSrc)
{
	checkSlow(IsInGameThread() && Listener);
	FSignalUtils::RemoveExactly<bAllowDuplicate>(Impl(), Listener, InSigSrc);
}

template GMP_API void FSignalImpl::DisconnectExactly<true>(const UObject* Listener, FSigSource InSigSrc);
template GMP_API void FSignalImpl::DisconnectExactly<false>(const UObject* Listener, FSigSource InSigSrc);

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
FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const
{
	checkSlow(IsInGameThread());

	auto StoreHolder = Store;
	FSignalStore& StoreRef = *StoreHolder;

	// excactly
	auto CallbackIDs = StoreRef.GetKeysBySrc<FOnFireResultArray>(InSigSrc);

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

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

FSigSource FSigSource::NullSigSrc = FSigSource(nullptr);

void FSigSource::RemoveSource(FSigSource InSigSrc)
{
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet())
		Deleter->RouterObjectRemoved(InSigSrc);
}

FSigElm* FSignalStore::FindSigElm(FGMPKey Key) const
{
	auto Find = GetStorageMap().Find(Key);
	const FSigElm* Ret = Find ? Find->Get() : nullptr;
	return const_cast<FSigElm*>(Ret);
}

template<typename ArrayT>
ArrayT FSignalStore::GetKeysBySrc(FSigSource InSigSrc) const
{
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

	return Results;
}
template TArray<FGMPKey> FSignalStore::GetKeysBySrc<TArray<FGMPKey>>(FSigSource InSigSrc) const;

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

bool FSignalStore::IsAlive(const UObject* InHandler, FSigSource InSigSrc) const
{
	if (auto* KeysFind = HandlerObjs.Find(InHandler))
	{
		for (auto It = KeysFind->CreateIterator(); It; ++It)
		{
			auto Ptr = *It;
			checkSlow(Ptr);
			if (Ptr->Source == InSigSrc)
			{
				return true;
			}
		}
	}
	return false;
}

FSigElm* FSignalStore::AddSigElmImpl(FGMPKey Key, const UObject* InListener, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor)
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
