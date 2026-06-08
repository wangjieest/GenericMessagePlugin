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
FXConsoleVariableRef CVar_ShouldClearWorldSubOjbects(TEXT("gmp.flag.clearWorldSubs"), bShouldClearWorldSubOjbects, TEXT(""));
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

struct FSignalUtils
{
	static auto& GetSigElmSet(const FSignalStore* In)
	{
		return In->SigElmArray;
	}

	static TUniquePtr<FSigElm>* FindArraySlot(const FSignalStore* In, FGMPKey Key)
	{
		for (auto& Up : GetSigElmSet(In))
			if (Up && Up->GetGMPKey() == Key)
				return &Up;
		return nullptr;
	}
	static int32 RemoveArrayByKey(const FSignalStore* In, FGMPKey Key)
	{
		return GetSigElmSet(In).RemoveAll([Key](const TUniquePtr<FSigElm>& Up) { return Up && Up->GetGMPKey() == Key; });
	}
	static bool ContainsArrayKey(const FSignalStore* In, FGMPKey Key) { return FindArraySlot(In, Key) != nullptr; }

#if GMP_DEBUG_SIGNAL
	static void CheckArrayConsistency(const FSignalStore* In)
	{
		if (!In || In->IsFiring())
			return;
		auto& Arr = GetSigElmSet(In);
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			const TUniquePtr<FSigElm>& A = Arr[i];
			ensureAlwaysMsgf(A.IsValid(), TEXT("GMP flat-store has a null slot at %d (key=%s)"), i, *In->MessageKey.ToString());
			if (!A)
				continue;
			for (int32 j = i + 1; j < Arr.Num(); ++j)
			{
				if (Arr[j] && Arr[j]->GetGMPKey() == A->GetGMPKey())
				{
					ensureAlwaysMsgf(false, TEXT("GMP flat-store duplicate GMPKey %llu at [%d,%d] (key=%s)"),
						(unsigned long long)A->GetGMPKey().GetKey(), i, j, *In->MessageKey.ToString());
				}
			}
		}
	}
#endif

	template<typename F>
	static void RemoveOp(const FSignalStore* In, FGMPKey Key, const F& Func)
	{
		if (auto Find = FindArraySlot(In, Key))
		{
			auto Elm = Find->Get();
			GMPDebug(In->MessageKey, Elm, TEXT("RemoveOp"));
			Func(Elm);
			GetSigElmSet(In).RemoveAll([Key](const TUniquePtr<FSigElm>& Up) { return Up && Up->GetGMPKey() == Key; });
		}
	}
	static TArray<FGMPKey> GetSigElmSetKeys(const FSignalStore* In)
	{
		TArray<FGMPKey> Keys;
		for (auto& Elem : GetSigElmSet(In))
		{
			if (Elem)
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
		for (auto& Up : GetSigElmSet(In))
		{
			FSigElm* Elem = Up.Get();
			if (!Elem)
				continue;
			const FWeakObjectPtr& H = Elem->GetHandler();
			const bool bMatchObj = !Obj.IsExplicitlyNull() && (H == Obj);
			if (bMatchObj || H.IsStale())
				ResultKeys.Add(Elem->GetGMPKey());
		}
		return ResultKeys;
	}

	static void StaticOnObjectRemoved(FSignalStore* In, FSigSource InSigSrc)
	{
		GMP_VERIFY_GAME_THREAD();
		ensure(!In->IsFiring());
		auto Obj = InSigSrc.TryGetUObject();

		GMPDebug(In->MessageKey, nullptr, TEXT("StaticOnObjectRemoved"));

		FSigSource WorldSrc;
		if (bShouldClearWorldSubOjbects && Obj && (!Obj->IsA<UGameInstance>() && !Obj->IsA<UGameViewportClient>()))
		{
			if (UWorld* ObjWorld = Obj->GetWorld())
				WorldSrc = FSigSource(ObjWorld);
		}

		FMsgKeyArray ToRemove;
		for (auto& Up : FSignalUtils::GetSigElmSet(In))
		{
			FSigElm* Elem = Up.Get();
			if (!Elem)
				continue;
			const FSigSource ElmSrc = Elem->GetSource();
			const FWeakObjectPtr& H = Elem->GetHandler();
			const bool bMatch = (ElmSrc == InSigSrc)
				|| (WorldSrc.IsValid() && ElmSrc == WorldSrc)
				|| (Obj && H == Obj)
				|| H.IsStale();
			if (bMatch)
				ToRemove.Add(Elem->GetGMPKey());
		}
		for (auto Key : ToRemove)
			RemoveArrayByKey(In, Key);
	}

	template<bool bAllowDuplicate>
	static void RemoveSigElmImpl(FSignalStore* In, FSigElm* SigElm)
	{
		GMPDebug(In->MessageKey, SigElm, TEXT("RemoveSigElmImpl"));
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
		FSignalUtils::RemoveOp(In, Key, [](FSigElm* SigElm) {});
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

	template<bool bAllowDuplicate, typename FInvoke>
	static void FireCore(FSignalStore& StoreRef, FInvoke&& PerElem)
	{
		GMP_VERIFY_GAME_THREAD();
		TScopeCounter<decltype(StoreRef.ScopeCnt)> ScopeCounter(StoreRef.ScopeCnt);

		FMsgKeyArray EraseIDs;
		{
			TArray<FSigElm*, TInlineAllocator<16>> Snapshot;
			Snapshot.Reserve(GetSigElmSet(&StoreRef).Num());
			for (auto& Up : GetSigElmSet(&StoreRef))
				if (Up)
					Snapshot.Add(Up.Get());

			for (FSigElm* Elem : Snapshot)
			{
				bool bShouldErase = !Elem->IsInvokable();
				if (!bShouldErase)
				{
					PerElem(Elem);
					bShouldErase = !Elem->TestTimes();
				}
				if (bShouldErase)
				{
					EraseIDs.Add(Elem->GetGMPKey());
					GMPDebug(StoreRef.MessageKey, Elem, TEXT("EraseOnFire"));
				}
			}
		}

		for (auto Key : EraseIDs)
		{
			DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, Key);
		}
	}

	template<bool bAllowDuplicate, typename FInvoke>
	static FSignalImpl::FOnFireResults FireWithSigSourceCore(FSignalStore& StoreRef, FSigSource InSigSrc, FInvoke&& PerElem)
	{
		GMP_VERIFY_GAME_THREAD();
		TScopeCounter<decltype(StoreRef.ScopeCnt)> ScopeCounter(StoreRef.ScopeCnt);

		const FSigSource SrcWorld = InSigSrc.GetSigSourceWorld();

		TArray<FSigElm*, TInlineAllocator<16>> Matched;
		for (auto& Up : GetSigElmSet(&StoreRef))
		{
			FSigElm* Elem = Up.Get();
			if (!Elem)
				continue;
			const FSigSource ElmSrc = Elem->GetSource();
			const bool bMatch = (ElmSrc == InSigSrc)
				|| (SrcWorld.IsValid() && ElmSrc == SrcWorld)
				|| (ElmSrc == FSigSource::AnySigSrc);
			if (bMatch)
				Matched.Add(Elem);
		}
		Matched.Sort([](const FSigElm& A, const FSigElm& B) { return A.GetGMPKey() < B.GetGMPKey(); });

		FMsgKeyArray EraseIDs;
#if WITH_EDITOR
		FSignalImpl::FOnFireResults CallbackIDs;
#endif
		for (FSigElm* Elem : Matched)
		{
#if WITH_EDITOR
			CallbackIDs.Add(Elem->GetGMPKey());
#endif
#if GMP_DEBUG_SIGNAL
			auto Listener = Elem->GetHandler();
			if (!Listener.IsStale())
			{
				auto SigObj = InSigSrc.TryGetUObject();
				if (Listener.Get() && SigObj && Listener.Get()->GetWorld() != SigObj->GetWorld())
					continue;
			}
#endif
			bool bShouldErase = !Elem->IsInvokable();
			if (!bShouldErase)
			{
				PerElem(Elem);
				bShouldErase = !Elem->TestTimes();
			}
			if (bShouldErase)
			{
				EraseIDs.Add(Elem->GetGMPKey());
				GMPDebug(StoreRef.MessageKey, Elem, TEXT("EraseOnFireWithSigSource"));
			}
		}

		if (EraseIDs.Num() > 0)
		{
			for (auto Key : EraseIDs)
			{
				DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, Key);
			}
		}
#if WITH_EDITOR
		return CallbackIDs;
#endif
	}

	template<bool bAllowDuplicate>
	static void FireImpl(FSignalStore& StoreRef, const TGMPFunctionRef<void(FSigElm*)>& Invoker)
	{
		FireCore<bAllowDuplicate>(StoreRef, [&](FSigElm* Elem) { Invoker(Elem); });
	}
	template<bool bAllowDuplicate>
	static FSignalImpl::FOnFireResults FireWithSigSourceImpl(FSignalStore& StoreRef, FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker)
	{
		return FireWithSigSourceCore<bAllowDuplicate>(StoreRef, InSigSrc, [&](FSigElm* Elem) { Invoker(Elem); });
	}

	FORCEINLINE static void InvokeRaw(FSigElm* Elem, const void* a0, const void* a1)
	{
#if GMP_WITH_DIRECT_SIGNAL
		GMP::GMPInvokeRaw(Elem, a0, a1);
#else
		Elem->CheckCallable();
		reinterpret_cast<void (*)(void*, const void*, const void*)>(Elem->GetCallable())(Elem->GetObjectAddress(), a0, a1);
#endif
	}

	template<bool bAllowDuplicate>
	static FSignalImpl::FOnFireResults FireWithSigSourceRaw(FSignalStore& StoreRef, FSigSource InSigSrc, const void* a0, const void* a1)
	{
		GMP_VERIFY_GAME_THREAD();
		TScopeCounter<decltype(StoreRef.ScopeCnt)> ScopeCounter(StoreRef.ScopeCnt);

		const FSigSource SrcWorld = InSigSrc.GetSigSourceWorld();

		TArray<FSigElm*, TInlineAllocator<16>> Matched;
		for (auto& Up : GetSigElmSet(&StoreRef))
		{
			FSigElm* Elem = Up.Get();
			if (!Elem)
				continue;
			const FSigSource ElmSrc = Elem->GetSource();
			const bool bMatch = (ElmSrc == InSigSrc)
				|| (SrcWorld.IsValid() && ElmSrc == SrcWorld)
				|| (ElmSrc == FSigSource::AnySigSrc);
			if (bMatch)
				Matched.Add(Elem);
		}
		Matched.Sort([](const FSigElm& A, const FSigElm& B) { return A.GetGMPKey() < B.GetGMPKey(); });

		FMsgKeyArray EraseIDs;
#if WITH_EDITOR
		FSignalImpl::FOnFireResults CallbackIDs;
#endif
		for (FSigElm* Elem : Matched)
		{
#if WITH_EDITOR
			CallbackIDs.Add(Elem->GetGMPKey());
#endif
#if GMP_DEBUG_SIGNAL
			auto Listener = Elem->GetHandler();
			if (!Listener.IsStale())
			{
				auto SigObj = InSigSrc.TryGetUObject();
				if (Listener.Get() && SigObj && Listener.Get()->GetWorld() != SigObj->GetWorld())
					continue;
			}
#endif
			bool bShouldErase = !Elem->IsInvokable();
			if (!bShouldErase)
			{
				InvokeRaw(Elem, a0, a1);
				bShouldErase = !Elem->TestTimes();
			}
			if (bShouldErase)
			{
				EraseIDs.Add(Elem->GetGMPKey());
				GMPDebug(StoreRef.MessageKey, Elem, TEXT("EraseOnFireRaw"));
			}
		}

		for (auto Key : EraseIDs)
		{
			DisconnectHandlerByID<bAllowDuplicate>(&StoreRef, Key);
		}
#if WITH_EDITOR
		return CallbackIDs;
#endif
	}
};  // namespace GMP

class FGMPSourceAndHandlerDeleter final
	: public FUObjectArray::FUObjectDeleteListener
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
		if (auto* Hooks = FSigSource::GetStoreMsgHooks())
			if (Hooks->OnSourceRemoved)
				Hooks->OnSourceRemoved(InSig);

		FSigStoreSet RemovedStores;
		if (MessageMappings.RemoveAndCopyValue(InSig, RemovedStores))
		{
			for (auto It = RemovedStores.CreateIterator(); It; ++It)
			{
				if (auto Pin = It->Pin())
				{
					FSignalStore& Store = *Pin;
					FSignalUtils::StaticOnObjectRemoved(&Store, InSig);
				}
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
	// TryGet(false): no ensure. Under GMP_WITH_STATIC_STORE the per-type static store objects are constructed
	// at static-init time (before the Deleter exists) -- they self-register later via BindStaticStores. The
	// default-true TryGet() would trip a handled ensure during that early construction.
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->SignalStores.Add(this);
}

// De-registration + content clear, factored out of ~FSignalStore so the static-store custom deleter can run
// the SAME teardown WITHOUT freeing the (static) object memory. (See GMPRegisterStaticStore / FStaticStoreDeleter.)
void FSignalStore::Cleanup()
{
	GMP_VERIFY_GAME_THREAD();
#if GMP_WITH_DIRECT_SIGNAL && !GMP_WITH_STATIC_STORE
	if (OwnerSlot && OwnerSlot->Ptr == this)
		OwnerSlot->Ptr = nullptr;
#endif
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->SignalStores.RemoveSwap(this);
#if GMP_WITH_MSG_HOLDER
	if (auto* Hooks = FSigSource::GetStoreMsgHooks())
		if (Hooks->OnStoreDestroyed)
			Hooks->OnStoreDestroyed(this);
#endif
	Reset();
}

FSignalStore::~FSignalStore()
{
	Cleanup();
}

void FSignalStore::Reset()
{
	// Reset only clears listeners (SigElm). Stored/late-replay messages are independent of listeners and are NOT
	// touched here -- they are dropped only when the store is destroyed (OnStoreDestroyed) or their source goes away.
	FSignalUtils::GetSigElmSet(this).Reset();
}

static const FSigSource::FStoreMsgHooks* GGMPStoreMsgHooks = nullptr;
void FSigSource::RegisterStoreMsgHooks(const FSigSource::FStoreMsgHooks* InHooks)
{
	GGMPStoreMsgHooks = InHooks;
}
const FSigSource::FStoreMsgHooks* FSigSource::GetStoreMsgHooks()
{
	return GGMPStoreMsgHooks;
}

#if GMP_WITH_STATIC_STORE
TArray<FStaticStoreEntry>& GMPGetStaticStoreRegistry()
{
	static TArray<FStaticStoreEntry> Registry;
	return Registry;
}

void GMPRegisterStaticStore(FSignalStore* InStore, const ANSICHAR* InKeyStr)
{
	// Called during static init (before FName/Hub exist) -- just record; do NOT touch FName/Hub here.
	GMPGetStaticStoreRegistry().Add(FStaticStoreEntry{InStore, InKeyStr});
}

TSharedRef<FSignalStore, FSignalBase::SPMode> GMPBindStaticStore(FSignalStore* InStore, FName Key)
{
	GMP_VERIFY_GAME_THREAD();
	if (InStore->MessageKey.IsNone())
		InStore->MessageKey = Key;
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->SignalStores.AddUnique(InStore);
	// AsShared() reuses the single control block set up by TStaticSignalStore<T>::SharedRef's ctor.
	return InStore->AsShared();
}

void GMPEnsureStaticStoreRegistered(FSignalStore* InStore)
{
	GMP_VERIFY_GAME_THREAD();
	if (auto Deleter = FGMPSourceAndHandlerDeleter::TryGet(false))
		Deleter->SignalStores.AddUnique(InStore);
}
#endif  // GMP_WITH_STATIC_STORE

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
			Deleter->SigSourceKeys.Add(Ret);
			break;
		}

		if (!FindSet)
			break;

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
		FSignalUtils::DisconnectHandlerByID<true>(static_cast<FSignalStore*>(Pin().Get()), Key);
		Reset();
	}
	template<typename S>
	static void Insert(const FSigCollection& C, FGMPKey Key, S& Store)
	{
		C.Connections.Add(new FConnectionImpl(Key, Store));
	}

protected:
	bool IsValid() { return TWeakPtr<void, FSignalBase::SPMode>::IsValid() && Key; }
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
#if GMP_WITH_STATIC_STORE
	Store->Reset();
#elif GMP_WITH_DIRECT_SIGNAL
	auto Key = Store->MessageKey;
	FStaticSignalSlot* Slot = Store->OwnerSlot;
	Store = MakeSignals(Key);
	if (Slot)
	{
		Slot->Ptr = Store.Get();
		Store->OwnerSlot = Slot;
	}
#else
	auto Key = Store->MessageKey;
	Store = MakeSignals(Key);
#endif
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
void FSignalImpl::OnFire(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const
{
	(void)ExpectSig;
	GMP_CNOTE_ONCE(Store.IsUnique(), TEXT("maybe unsafe, should avoid reentry."));
	// Hold a shared ref across the fire to keep a destroyable (dynamic/by-name/modular) store alive through
	// reentry, then delegate to the raw-store core. Static direct fire skips this entirely (see GMPHub.cpp).
	auto StoreHolder = Store;
	FSignalUtils::FireImpl<bAllowDuplicate>(*StoreHolder, Invoker);
}
template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;
template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;

template<bool bAllowDuplicate>
FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const
{
	(void)ExpectSig;
	auto StoreHolder = Store;
	return FSignalUtils::FireWithSigSourceImpl<bAllowDuplicate>(*StoreHolder, InSigSrc, Invoker);
}

template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;
template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;

#if GMP_WITH_DIRECT_SIGNAL
#if WITH_EDITOR
TArray<FGMPKey, TInlineAllocator<16>> GMPFireWithSigSourceDirectRaw(FSignalStore* RawStore, FSigSource InSigSrc, const void* a0, const void* a1)
{
	return FSignalUtils::FireWithSigSourceRaw<false>(*RawStore, InSigSrc, a0, a1);
}
#else
void GMPFireWithSigSourceDirectRaw(FSignalStore* RawStore, FSigSource InSigSrc, const void* a0, const void* a1)
{
	FSignalUtils::FireWithSigSourceRaw<false>(*RawStore, InSigSrc, a0, a1);
}
#endif

#if GMP_WITH_INLINE_FIRE_ENABLED
void GMPEraseKeysAfterFire(FSignalStore* RawStore, const FGMPKey* Keys, int32 Num, bool bAllowDuplicate)
{
	for (int32 i = 0; i < Num; ++i)
	{
		if (bAllowDuplicate)
			FSignalUtils::DisconnectHandlerByID<true>(RawStore, Keys[i]);
		else
			FSignalUtils::DisconnectHandlerByID<false>(RawStore, Keys[i]);
	}
}
#endif
#endif

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
	auto Find = FSignalUtils::FindArraySlot(this, Key);
	const FSigElm* Ret = Find ? Find->Get() : nullptr;
	return const_cast<FSigElm*>(Ret);
}

template<typename ArrayT>
ArrayT FSignalStore::GetKeysBySrc(FSigSource InSigSrc, bool bIncludeNoSrc) const
{
	GMP_VERIFY_GAME_THREAD();
	ArrayT Results;
	const FSigSource SrcWorld = InSigSrc.GetSigSourceWorld();
	for (auto& Up : SigElmArray)
	{
		FSigElm* Elem = Up.Get();
		if (!Elem)
			continue;
		const FSigSource ElmSrc = Elem->GetSource();
		const bool bMatch = (ElmSrc == InSigSrc)
			|| (SrcWorld.IsValid() && ElmSrc == SrcWorld)
			|| (bIncludeNoSrc && ElmSrc == FSigSource::AnySigSrc);
		if (bMatch)
			Results.Add(Elem->GetGMPKey());
	}
	Results.Sort();
	return Results;
}
template TArray<FGMPKey> FSignalStore::GetKeysBySrc<TArray<FGMPKey>>(FSigSource InSigSrc, bool bIncludeNoSrc) const;

TArray<FGMPKey> FSignalStore::GetKeysByHandler(const UObject* InHandler) const
{
	GMP_VERIFY_GAME_THREAD();
	TArray<FGMPKey> Keys;
	for (auto& Up : SigElmArray)
	{
		FSigElm* Elem = Up.Get();
		if (Elem && Elem->GetHandler() == InHandler)
			Keys.Add(Elem->GetGMPKey());
	}
	return Keys;
}

bool FSignalStore::IsAlive(const UObject* InHandler, FSigSource InSigSrc) const
{
	GMP_VERIFY_GAME_THREAD();
	for (auto& Up : SigElmArray)
	{
		FSigElm* Elem = Up.Get();
		if (Elem && Elem->GetHandler() == InHandler && (!InSigSrc || Elem->GetSource() == InSigSrc))
			return true;
	}
	return false;
}

void FSignalStore::RemoveSigElmStorage(FGMPKey SigKey)
{
	GMP_CHECK(!IsFiring());
	FSignalUtils::RemoveArrayByKey(this, SigKey);
#if GMP_DEBUG_SIGNAL
	ensureAlwaysMsgf(!FSignalUtils::ContainsArrayKey(this, SigKey), TEXT("GMP flat-store still has key %llu after remove"), (unsigned long long)SigKey.GetKey());
	FSignalUtils::CheckArrayConsistency(this);
#endif
}

FSigElm* FSignalStore::AddSigElmImpl(FGMPKey Key, const UObject* InListener, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor)
{
	FSigElm* SigElm = FindSigElm(Key);
	if (!SigElm)
	{
		SigElm = Ctor();
		GMP_CHECK(SigElm);
		FSignalUtils::GetSigElmSet(this).Add(TUniquePtr<FSigElm>(SigElm));
	}

	if (InListener)
	{
		SigElm->Handler = InListener;
#if WITH_EDITOR
		ensureAlways(!FWeakObjectPtr(InListener).IsStale());
#endif
	}
	SigElm->Source = InSigSrc.SigOrObj() ? InSigSrc : FSigSource::AnySigSrc;
	FGMPSourceAndHandlerDeleter::AddMessageMapping(InSigSrc, this);
	GMPDebug(MessageKey, SigElm, TEXT("AddSigElmImpl"));
#if GMP_DEBUG_SIGNAL
	FSignalUtils::CheckArrayConsistency(this);
#endif
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
