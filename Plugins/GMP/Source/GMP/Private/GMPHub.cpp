//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPHub.h"

#include "CoreUObject.h"

#include "Algo/BinarySearch.h"
#include "Algo/ForEach.h"
#include "Engine/UserDefinedStruct.h"
#include "GMPMeta.h"
#include "GMPSignalsImpl.h"
#include "GMPSignalsInc.h"
#include "GMPUtils.h"
#include "GMPWorldLocals.h"
#include "HAL/ThreadSingleton.h"
#include "Misc/ScopeExit.h"
#include "UObject/ObjectKey.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"
#include "GMPUnion.h"
#include "XConsoleManager.h"

#if GMP_WITH_DIRECT_SIGNAL
#include "GMPHubOpt.h"
#endif

#if GMP_SIGNAL_BACKEND_FLEX
#include "GMPFlexBackend.h"
#endif

#if UE_4_23_OR_LATER
#include "Containers/LockFreeList.h"
#endif

#if WITH_EDITOR
#include "Editor.h"
#endif

#ifndef GMP_DISABLE_HUB_OPTIMIZATION
#define GMP_DISABLE_HUB_OPTIMIZATION WITH_EDITOR
#endif

#if GMP_DISABLE_HUB_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif
GMP_API const TCHAR* GMPGetNativeTagType()
{
	static FString StrHolder{TEXT("Native")};
	return *StrHolder;
}

int32 GEnableGMPLogging = !UE_BUILD_SHIPPING;
FXConsoleVariableRef CVar_EnableGMPListeningLog(TEXT("GMP.EnableLogging"), GEnableGMPLogging, TEXT(""));
int32 GWarningNoListeners = 0;
FXConsoleVariableRef CVar_EnableGMPNoListenersLog(TEXT("GMP.EnableNoListenerLog"), GWarningNoListeners, TEXT(""));

namespace GMP
{
#if GMP_WITH_MSG_HOLDER
	class FGMPStoreMsgHolder final : public FGCObject
	{
	public:
		FGMPStoreMsgHolder()
		{
			static const FSigSource::FStoreMsgHooks Hooks = {
				&FGMPStoreMsgHolder::OnStoreDestroyed,
				&FGMPStoreMsgHolder::OnSourceRemoved,
			};
			FSigSource::RegisterStoreMsgHooks(&Hooks);
		}

		static FGMPStoreMsgHolder*& InstancePtr()
		{
			static FGMPStoreMsgHolder* Ptr = nullptr;
			return Ptr;
		}
		static FGMPStoreMsgHolder& GetOrCreate()
		{
			GMP_CHECK(IsInGameThread());
			if (!InstancePtr())
				InstancePtr() = new FGMPStoreMsgHolder();
			return *InstancePtr();
		}

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			for (auto& StorePair : StoreMsgsMap)
				for (auto& Pair : StorePair.Value)
					Pair.Value.AddStructReferencedObjects(Collector);
		}
		virtual FString GetReferencerName() const override { return TEXT("FGMPStoreMsgHolder"); }

		static void OnStoreDestroyed(FSignalStore* Store)
		{
			if (FGMPStoreMsgHolder* Inst = InstancePtr())
				Inst->StoreMsgsMap.Remove(Store);
		}
		static void OnSourceRemoved(FSigSource InSigSrc)
		{
			if (FGMPStoreMsgHolder* Inst = InstancePtr())
				for (auto& StorePair : Inst->StoreMsgsMap)
					StorePair.Value.Remove(InSigSrc);
		}

		TMap<const FSignalStore*, FGMPStoreSourceMsgs> StoreMsgsMap;
	};

	static FORCEINLINE bool StoreHasSourceMsgs(const FSignalStore* Store)
	{
		const FGMPStoreMsgHolder* Inst = FGMPStoreMsgHolder::InstancePtr();
		if (!Inst)
			return false;
		const FGMPStoreSourceMsgs* Found = Inst->StoreMsgsMap.Find(Store);
		return Found && Found->Num() > 0;
	}
	static FORCEINLINE FGMPStoreSourceMsgs& StoreSourceMsgs(const FSignalStore* Store)
	{
		return FGMPStoreMsgHolder::GetOrCreate().StoreMsgsMap.FindOrAdd(Store);
	}
#endif  // GMP_WITH_MSG_HOLDER

	bool FMessageHub::ShouldWarningNoListeners()
	{
		return !!GWarningNoListeners;
	};

#if GMP_SIGNAL_BACKEND_FLEX
	#if GMP_WITH_DIRECT_SIGNAL
	using FGMPMsgSignal = TFlexMsgSignal<false, const FGMPTypedAddr*, const FGMPExtra*>;
	#else
	using FGMPMsgSignal = TFlexMsgSignal<false, FMessageBody&>;
	#endif
#elif GMP_WITH_DIRECT_SIGNAL
	using FGMPMsgSignal = TSignal<false, const FGMPTypedAddr*, const FGMPExtra*>;
#else
	using FGMPMsgSignal = TSignal<false, FMessageBody&>;
#endif
#if GMP_WITH_STATIC_STORE
	static FSignalStore* TryAdoptStaticStore(FGMPSignalMap& Map, FName Name)
	{
		static TMap<FName, FSignalStore*> Index;
		static int32 BuiltCount = -1;
		auto& Registry = GMPGetStaticStoreRegistry();
		if (BuiltCount != Registry.Num())  // registry can grow as more keys get ODR-used; rebuild index lazily
		{
			Index.Reset();
			for (const FStaticStoreEntry& E : Registry)
				if (E.Store)
					Index.Add(FName(E.KeyStr), E.Store);
			BuiltCount = Registry.Num();
		}
		if (FSignalStore** Found = Index.Find(Name))
		{
			FSignalBase& Base = Map.Add(Name);
			Base.Store = GMPBindStaticStore(*Found, Name);  // no-delete shared ref over the static object
			return *Found;
		}
		return nullptr;
	}

	static FSignalBase* FindSigWithStaticAdopt(FGMPSignalMap& Map, FName Name)
	{
		if (auto Ptr = FindSig(Map, Name))
			return Ptr;
		if (TryAdoptStaticStore(Map, Name))
			return Map.Find(Name);
		return nullptr;
	}
#endif

	template<bool bAdd>
	FSignalBase* GetSig(FGMPSignalMap& Map, FName Name)
	{
		auto Find = Map.Find(Name);
		GMP_IF_CONSTEXPR(bAdd)
		{
			if (!Find)
			{
#if GMP_WITH_STATIC_STORE
				// Prefer a static store for this key (so name-path and slot-path share it) before making a dynamic one.
				if (TryAdoptStaticStore(Map, Name))
					return Map.Find(Name);
#endif
				Find = &Map.Add(Name);
				Find->Store = FGMPMsgSignal::MakeSignals(Name);
			}
		}
		return Find;
	}
	template GMP_API FSignalBase* GetSig<true>(FGMPSignalMap& Map, FName Name);
	template GMP_API FSignalBase* GetSig<false>(FGMPSignalMap& Map, FName Name);

	FORCEINLINE_DEBUGGABLE static auto FireMsgBodyAdapt(FGMPMsgSignal* SignalPtr, FSigSource InSigSrc, FMessageBody& Msg)
	{
#if GMP_WITH_DIRECT_SIGNAL
		const auto P = Msg.GetParams();  // TArrayView by value (params are an inline trailing block now)
		FArrayTypeNames TypeNamesStk;
		if (!Msg.TypeNames)
		{
#if GMP_WITH_TYPENAME
			TypeNamesStk.Reserve(P.Num());
			for (auto& A : P)
				TypeNamesStk.Add(A.TypeName);
			Msg.TypeNames = TypeNamesStk.GetData();
#else
			if (auto* Types = FMessageBody::GetMessageTypes(InSigSrc.TryGetUObject(), Msg.MessageKey()))
				Msg.TypeNames = Types->GetData();
#endif
		}
		auto Holder = SignalPtr->Store;  // keep the dynamic store alive across the fire
		return GMPFireWithSigSourceDirectRaw(Holder.Get(), InSigSrc, P.GetData(), static_cast<const FGMPExtra*>(&Msg));
#else
		return SignalPtr->FireWithSigSource(InSigSrc, Msg);
#endif
	}

	FORCEINLINE_DEBUGGABLE static void InvokeSlotMsgBodyAdapt(FSigElm* Elem, FSigSource InSigSrc, FMessageBody& Msg)
	{
		Elem->CheckCallable();
#if GMP_WITH_DIRECT_SIGNAL
		// Msg IS-A FGMPExtra: no rebuild. Ensure TypeNames, then invoke the three-arg callable with &Msg as the extra.
		const auto P = Msg.GetParams();  // TArrayView by value (params are an inline trailing block now)
		FArrayTypeNames TypeNamesStk;
		if (!Msg.TypeNames)
		{
#if GMP_WITH_TYPENAME
			TypeNamesStk.Reserve(P.Num());
			for (auto& A : P)
				TypeNamesStk.Add(A.TypeName);
			Msg.TypeNames = TypeNamesStk.GetData();
#else
			if (auto* Types = FMessageBody::GetMessageTypes(InSigSrc.TryGetUObject(), Msg.MessageKey()))
				Msg.TypeNames = Types->GetData();
#endif
		}
		reinterpret_cast<void (*)(void*, const FGMPTypedAddr*, const FGMPExtra*)>(Elem->GetCallable())(Elem->GetObjectAddress(), P.GetData(), static_cast<const FGMPExtra*>(&Msg));
#else
		(void)InSigSrc;
		reinterpret_cast<void (*)(void*, FMessageBody&)>(Elem->GetCallable())(Elem->GetObjectAddress(), Msg);
#endif
	}

#if GMP_WITH_DIRECT_SIGNAL && !GMP_WITH_STATIC_STORE
	FSlotNode*& GetStaticSlotListHead()
	{
		static FSlotNode* Head = nullptr;
		return Head;
	}
#endif  // GMP_WITH_DIRECT_SIGNAL && !GMP_WITH_STATIC_STORE

#if GMP_WITH_DIRECT_SIGNAL
	FSignalBase* FMessageHub::FillDirectSigBase(FSignalStore* DirectStore, FSignalBase& OutTmp) const
	{
		// lazy key fixup is needed here -- Store->MessageKey is always valid by the time any direct path runs.
		OutTmp.Store = DirectStore->AsShared();
		return &OutTmp;
	}

#if GMP_WITH_STATIC_STORE
	void FMessageHub::BindDirectSignalSlots()
	{
		check(IsInGameThread());
		for (const FStaticStoreEntry& E : GMPGetStaticStoreRegistry())
		{
			if (!E.Store)
				continue;
			const FName Key = FName(E.KeyStr);
			MessageSignals.FindOrAdd(Key).Store = GMPBindStaticStore(E.Store, Key);
		}
	}
#else
	void FMessageHub::BindDirectSignalSlots()
	{
		check(IsInGameThread());
		for (FSlotNode* N = GetStaticSlotListHead(); N; N = N->Next)
		{
			FStaticSignalSlot* Slot = N->Slot;
			if (!Slot)
				continue;
			if (Slot->Key.IsNone())
				Slot->Key = FName(Slot->KeyStr);
			auto* Base = static_cast<FGMPMsgSignal*>(GetSig<true>(MessageSignals, Slot->Key));
			Slot->Ptr = Base->Store.Get();
			Base->Store->OwnerSlot = Slot;
		}
	}

	FSignalStore* FMessageHub::ResolveDirectSlotStore(const FName& Key)
	{
		check(IsInGameThread());
		auto* Base = static_cast<FGMPMsgSignal*>(GetSig<true>(MessageSignals, Key));
		return Base->Store.Get();
	}

	FSignalStore* FStaticSignalSlot::ResolvePtr() const
	{
		// Fast path: already bound by BindDirectSignalSlots, or by a prior cold resolve.
		if (Ptr)
			return Ptr;

		// Cold path: Ptr still null (listen/send before the EndOfEngineInit batch bind). Bind it now by key.
		auto& MutSelf = const_cast<FStaticSignalSlot&>(*this);
		if (MutSelf.Key.IsNone())
			MutSelf.Key = FName(KeyStr);
		auto* Hub = FMessageUtils::GetMessageHub();
		FSignalStore* Store = Hub->ResolveDirectSlotStore(MutSelf.Key);
		MutSelf.Ptr = Store;
		// Modular: per-DLL slot copies / late DLLs. First slot owns the store (canonical); a later duplicate
		// must NOT steal ownership, since rebuild writes Ptr back through OwnerSlot.
		if (Store && !Store->OwnerSlot)
			Store->OwnerSlot = &MutSelf;
		GMP_WARNING(TEXT("[DirectSignal] slot [%s] resolved lazily (before EndOfEngineInit batch-bind, or modular duplicate); ")
					TEXT("bound on demand."), *MutSelf.Key.ToString());
		return MutSelf.Ptr;
	}
#endif  // GMP_WITH_STATIC_STORE
#endif  // GMP_WITH_DIRECT_SIGNAL

#if GMP_TRACE_MSG_STACK
	static TSet<FName> TracedKeys;
	FAutoConsoleCommand CVAR_GMPTraceMessageKey(TEXT("GMP.TraceMessageKey"), TEXT("TraceMessageKey Arr(space splitted)"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
													TracedKeys.Empty(Args.Num());
													for (auto Key : Args)
													{
														TracedKeys.Add(*Key);
													}
												}));

	void FMessageHub::TraceMessageKey(const FName& MessageKey, FSigSource InSigSrc)
	{
		if (TracedKeys.Contains(NAME_None) || TracedKeys.Contains(MessageKey))
		{
			GMP_DEBUG_LOG(TEXT("GMPTraceMessageKey: %s:%s"), *InSigSrc.GetNameSafe(), *MessageKey.ToString());
		}
	}
#endif

#if !UE_BUILD_SHIPPING
	float FMessageBody::GetTimeSecondsStatic(FSigSource InSigSrc)
	{
		const UObject* SrcObj = InSigSrc.TryGetUObject();
		auto World = SrcObj ? SrcObj->GetWorld() : GWorld;
		return World ? World->GetTimeSeconds() : 0.f;
	}
#endif

#if WITH_EDITOR
	FString FMessageBody::MessageToString() const
	{
		FString Result = FString::Printf(TEXT("%d Params"), Size);
		if (auto Types = GetMessageTypes(nullptr))
		{
			Result = FString::JoinBy(*Types, TEXT(","), [](const FName& Name) { return Name.ToString(); });
		}
#if GMP_WITH_TYPENAME
		else
		{
			Result = FString::JoinBy(GetParams(), TEXT(","), [](const FGMPTypedAddr& Addr) { return Addr.TypeName.ToString(); });
		}
#endif
		return FString::Printf(TEXT("(%s)"), *Result);
	};
#endif

	namespace Hub
	{
#if WITH_EDITOR
		static bool bTraceAllMessages = false;
		FXConsoleVariableRef CVar_DebugAllMessages(TEXT("GMP.TraceAllMessages"), bTraceAllMessages, TEXT("TraceAllMessages"));

		struct FDebugMessageInfo
		{
			FWeakObjectPtr Obj;
			FString CallInfo;
			TArray<FGMPKey> Records;
		};

		constexpr int32 BIT_OFFSET = 8;
		constexpr int32 BIT_LIMIT = 1 << BIT_OFFSET;
		constexpr int32 BIT_MASK = BIT_LIMIT - 1;

		struct FDebugCircularInfos
		{
			TArray<FDebugMessageInfo, TFixedAllocator<BIT_LIMIT>> Infos;
			int32 StartIdx;
			FDebugCircularInfos() { StartIdx = 0; }

			int32 Num() const { return Infos.Num(); }
			auto& operator[](int32 i) { return Infos[(i + StartIdx) & BIT_MASK]; }
			template<typename T>
			void AppendCallInfo(FSigSource InSigSrc, const FMessageBody& Msg, T&& IDs)
			{
				if (IDs.Num() == 0)
					return;
				FDebugMessageInfo* Info = (Infos.Num() >= BIT_LIMIT) ? &Infos[StartIdx++] : &Add_GetRef(Infos);
				Info->Obj = InSigSrc.TryGetUObject();
				Msg.ToString(Info->CallInfo);
				if (bTraceAllMessages)
				{
					GMP_TRACE(TEXT("GMP-Message : %s : [%zd]"), *Info->CallInfo, Cnt++);
				}
				Info->Records.Append(MoveTemp(IDs));
				StartIdx &= BIT_MASK;
			}
			size_t Cnt = 1;
		};

		static TMap<FName, FDebugCircularInfos> DebugHistoryCalls;
		auto& GetHistoryCalls()
		{
			return DebugHistoryCalls;
		}

		static TMap<FName, TMap<FSigSource, int32>> EntrySources;
		struct FRecursionDetection
		{
			FRecursionDetection(FName InKey, FSigSource InSigSrc)
				: Key(InKey)
				, CurSigSrc(InSigSrc)
			{
				++EntrySources.FindOrAdd(Key).FindOrAdd(CurSigSrc, 0);
			}
			~FRecursionDetection()
			{
				auto& Ref = EntrySources.FindChecked(Key);
				if (--Ref.FindChecked(CurSigSrc) <= 0)
					Ref.Remove(CurSigSrc);
			}
			explicit operator bool() const { return EntrySources.FindChecked(Key).FindChecked(CurSigSrc) <= 1; }

		protected:
			FName Key;
			FSigSource CurSigSrc;
		};
#endif

		template<bool bSingleShot>
		auto& GetSends()
		{
			static TMap<FName, FArrayTypeNames> Types;
			return Types;
		}

		template<bool bSingleShot>
		auto& GetRecvs()
		{
			static TMap<FName, FArrayTypeNames> Types;
			return Types;
		}

		FMessageHub::CallbackMapType& GMPResponses()
		{
#if 1
			static FMessageHub::CallbackMapType CallbackMap;
			return CallbackMap;
#elif 1
			static FMessageHub::CallbackMapType& CallbackMap = [] {
				static auto MapStorage = MakeUnique<FMessageHub::CallbackMapType>();
				FWorldDelegates::OnWorldBeginTearDown.AddStatic([](UWorld* InWorld) { MapStorage->Reset(); });
#if WITH_EDITOR
				FEditorDelegates::EndPIE.AddStatic([](const bool) { MapStorage->Reset(); });
#endif
				return *MapStorage;
			}();
			return CallbackMap;
#else
			return WorldLocalObject<FMessageHub::CallbackMapType>();
#endif
		}

	}  // namespace Hub

	FGMPKey FMessageBody::GetNextSequenceID()
	{
		static volatile int64 Seq = 0;
		if (FPlatformAtomics::InterlockedAdd(&Seq, 1))
			return FGMPKey(Seq);
		return FGMPKey(FPlatformAtomics::InterlockedAdd(&Seq, 1));
	}

#if UE_5_00_OR_LATER
	struct FMessageHubVerifier : public FScopeLock
	{
		FMessageHubVerifier(FMessageHub* InHub)
			: FScopeLock(&GetCriticalSection())
		{
		}

	private:
		static FCriticalSection& GetCriticalSection()
		{
			static FCriticalSection MessageHubsLock;
			return MessageHubsLock;
		}
	};
#else
	struct FMessageHubVerifier
	{
		FMessageHubVerifier(FMessageHub* InHub) { GMP_CHECK(IsInGameThread()); }
	};
#endif

	static TSet<FMessageHub*> MessageHubs;
	FMessageHub::FMessageHub()
	{
		FMessageHubVerifier Verifier{this};
		MessageHubs.Add(this);
	}

	FMessageHub::~FMessageHub()
	{
		FMessageHubVerifier Verifier{this};
		MessageHubs.Remove(this);
	}

	bool FMessageHub::IsValidHub() const
	{
		FMessageHubVerifier Verifier{const_cast<FMessageHub*>(this)};
		return MessageHubs.Contains(this);
	}

	bool FMessageHub::IsResponseOn(FGMPKey Key) const
	{
		return Hub::GMPResponses().Contains(Key);
	}

	FGMPKey FMessageHub::RequestMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponseSig&& OnRsp, const FArrayTypeNames* SingleshotTypes)
	{
		bool bExsitResponder = OnRsp && CallbackMarks.Contains(MessageKey);
		if (bExsitResponder && ensureAlwaysMsgf(!Hub::GMPResponses().Contains(OnRsp.GetId()), TEXT("duplicate sequence %zu!"), OnRsp.GetId()))
		{
			// R/R contract: Seq (GMPResponses key) must cross the fire via extra->Seq to the responder, else R/R mismatches.
			const FGMPKey Seq = OnRsp.GetId();
			Hub::GMPResponses().Emplace(Seq, MoveTemp(OnRsp));

			auto SignalPtr = static_cast<FGMPMsgSignal*>(Ptr);
#if GMP_WITH_DIRECT_SIGNAL
			FArrayTypeNames TypeNamesStk;
			const FName* TypeNamesPtr = nullptr;
#if GMP_WITH_TYPENAME
			TypeNamesStk.Reserve(Param.Num());
			for (auto& A : Param)
				TypeNamesStk.Add(A.TypeName);
			TypeNamesPtr = TypeNamesStk.GetData();
#else
			if (auto* Types = FMessageBody::GetMessageTypes(InSigSrc.TryGetUObject(), MessageKey))
				TypeNamesPtr = Types->GetData();
#endif
			const FGMPExtra Extra{Param.Num(), 0.f, TypeNamesPtr, InSigSrc, MessageKey, Seq};
			// Welded three-arg fire (zero thunk); dynamic store -> pin a holder across the fire. Result discarded.
			auto Holder = SignalPtr->Store;
			GMPFireWithSigSourceDirectRaw(Holder.Get(), InSigSrc, Param.GetData(), &Extra);
#else
			GMP_MSGBODY_ON_STACK(Msg, Param.Num(), Param.GetData(), MessageKey, InSigSrc, Seq);
#if WITH_EDITOR
			if (GIsEditor)
			{
				Hub::FRecursionDetection Detector(MessageKey, InSigSrc);
				GMP_CNOTE_ONCE(Detector, TEXT("Recursion Detected! :%s"), *InSigSrc.GetNameSafe());
				auto IDs = FireMsgBodyAdapt(SignalPtr, InSigSrc, Msg);
				Hub::GetHistoryCalls().FindOrAdd(MessageKey).AppendCallInfo(InSigSrc, Msg, MoveTemp(IDs));
			}
			else
#endif
			{
				FireMsgBodyAdapt(SignalPtr, InSigSrc, Msg);
			}
#endif  // GMP_WITH_DIRECT_SIGNAL
			return Seq;
		}
#if WITH_EDITOR
		GMP_CWARNING(!bExsitResponder && ShouldWarningNoListeners(), TEXT("no listeners when %s(MSGKEY(\"%s\"))"), *FString(__func__), *MessageKey.ToString());
#endif
		return {};
	}

	template<typename SignalT, typename ObjT>
	FORCEINLINE_DEBUGGABLE static FSigElm* ConnectBodySlot(SignalT* Ptr, ObjT* Obj, FGMPMessageSig&& Slot, FSigSource InSigSrc, FGMPListenOptions Options)
	{
#if GMP_WITH_DIRECT_SIGNAL
		auto Adapter = [Slot = MoveTemp(Slot)](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) {
			const FGMPExtra LocalExtra = extra ? *extra : FGMPExtra{};
			GMP_MSGBODY_ON_STACK_EXTRA(Body, LocalExtra.Size, paddrs, LocalExtra, LocalExtra.Seq);
			Slot(Body);
		};
		return Ptr->Connect(Obj, MoveTemp(Adapter), InSigSrc, Options);
#else
		return Ptr->Connect(Obj, std::move(Slot), InSigSrc, Options);
#endif
	}

#if GMP_WITH_DIRECT_SIGNAL
	template<typename SignalT, typename ObjT>
	FORCEINLINE_DEBUGGABLE static FSigElm* ConnectRawSlot(SignalT* Ptr, ObjT* Obj, FGMPRawSig&& Slot, FSigSource InSigSrc, FGMPListenOptions Options)
	{
		return Ptr->Connect(Obj, MoveTemp(Slot), InSigSrc, Options);
	}
#endif

	FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPMessageSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		GetSig<true>(MessageSignals, MessageKey);

		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			if (auto Elem = ConnectBodySlot(Ptr, Listener.GetObj(), std::move(Slot), InSigSrc, Options))
			{
				auto Inc = Listener.GetInc();
				if (Inc)
				{
					Ptr->BindSignalConnection(Inc->GMPSignalHandle, Elem->GetGMPKey());
				}
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				FGMPStructUnion* InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr);
				if (InsStruct)
				{
					GMP_LOG(TEXT("FMessageHub::%sListenMessage Key[%s] [%s:%s] Watched[%s] %d"),
							FTagTypeSetter::GetType().Get(TEXT("")),
							*MessageKey.ToString(),
							Inc ? TEXT("SignalHanlder") : TEXT("Listener"),
							*GetNameSafe(Listener.GetObj()),
							*InSigSrc.GetNameSafe(),
							InsStruct->GetFlags());
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
					{
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
					}
				}
				else
#endif
				{
					GMP_LOG(TEXT("FMessageHub::%sListenMessage Key[%s] [%s:%s] Watched[%s]"),
							FTagTypeSetter::GetType().Get(TEXT("")),
							*MessageKey.ToString(),
							Inc ? TEXT("SignalHanlder") : TEXT("Listener"),
							*GetNameSafe(Listener.GetObj()),
							*InSigSrc.GetNameSafe());
				}
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPMessageSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		GetSig<true>(MessageSignals, MessageKey);

		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			if (auto Elem = ConnectBodySlot(Ptr, Listener, std::move(Slot), InSigSrc, Options))
			{
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					GMP_LOG(TEXT("FMessageHub::%sListenMessage Key[%s] [SigCollection:%p] Watched[%s] %d"), FTagTypeSetter::GetType().Get(TEXT("")), *MessageKey.ToString(), Listener, *InSigSrc.GetNameSafe(), InsStruct->GetFlags());

					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
					{
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
					}
				}
				else
#endif
				{
					GMP_LOG(TEXT("FMessageHub::%sListenMessage Key[%s] [SigCollection:%p] Watched[%s]"), FTagTypeSetter::GetType().Get(TEXT("")), *MessageKey.ToString(), Listener, *InSigSrc.GetNameSafe());
				}
			}
		}
		return Ret;
	}

#if GMP_WITH_DIRECT_SIGNAL
	FGMPKey FMessageHub::ListenMessageImpl(FSignalBase* DirectBase, const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPMessageSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		if (!ensure(DirectBase))
			return Ret;
		if (auto Ptr = static_cast<FGMPMsgSignal*>(DirectBase))
		{
			if (auto Elem = ConnectBodySlot(Ptr, Listener.GetObj(), std::move(Slot), InSigSrc, Options))
			{
				auto Inc = Listener.GetInc();
				if (Inc)
				{
					Ptr->BindSignalConnection(Inc->GMPSignalHandle, Elem->GetGMPKey());
				}
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
					{
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
					}
				}
#endif
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(FSignalBase* DirectBase, const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPMessageSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		if (!ensure(DirectBase))
			return Ret;
		if (auto Ptr = static_cast<FGMPMsgSignal*>(DirectBase))
		{
			if (auto Elem = ConnectBodySlot(Ptr, Listener, std::move(Slot), InSigSrc, Options))
			{
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
					{
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
					}
				}
#endif
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPRawSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		GetSig<true>(MessageSignals, MessageKey);

		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			if (auto Elem = ConnectRawSlot(Ptr, Listener.GetObj(), std::move(Slot), InSigSrc, Options))
			{
				auto Inc = Listener.GetInc();
				if (Inc)
					Ptr->BindSignalConnection(Inc->GMPSignalHandle, Elem->GetGMPKey());
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
				}
#endif
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPRawSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		GetSig<true>(MessageSignals, MessageKey);

		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			if (auto Elem = ConnectRawSlot(Ptr, Listener, std::move(Slot), InSigSrc, Options))
			{
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
				}
#endif
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(FSignalBase* DirectBase, const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPRawSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		if (!ensure(DirectBase))
			return Ret;
		if (auto Ptr = static_cast<FGMPMsgSignal*>(DirectBase))
		{
			if (auto Elem = ConnectRawSlot(Ptr, Listener.GetObj(), std::move(Slot), InSigSrc, Options))
			{
				auto Inc = Listener.GetInc();
				if (Inc)
					Ptr->BindSignalConnection(Inc->GMPSignalHandle, Elem->GetGMPKey());
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
				}
#endif
			}
		}
		return Ret;
	}

	FGMPKey FMessageHub::ListenMessageImpl(FSignalBase* DirectBase, const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPRawSig&& Slot, FGMPListenOptions Options)
	{
		FGMPKey Ret;
		if (!ensure(DirectBase))
			return Ret;
		if (auto Ptr = static_cast<FGMPMsgSignal*>(DirectBase))
		{
			if (auto Elem = ConnectRawSlot(Ptr, Listener, std::move(Slot), InSigSrc, Options))
			{
				Ret = Elem->GetGMPKey();
#if GMP_WITH_MSG_HOLDER
				if (auto InsStruct = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr))
				{
					auto Replay = MsgStoreToTypedAddresses(InsStruct);
					FTypedAddresses& Arr = Replay.Addrs;
					GMP_MSGBODY_ON_STACK(Body, Arr.Num(), Arr.GetData(), MessageKey, InSigSrc, Ret);
					InvokeSlotMsgBodyAdapt(Elem, InSigSrc, Body);
					if (InsStruct->GetFlags(FGMPStructUnion::MsgStoreFlagsMask) == 1)
						StoreSourceMsgs(Ptr->Store.Get()).Remove(InSigSrc);
				}
#endif
			}
		}
		return Ret;
	}
#endif  // GMP_WITH_DIRECT_SIGNAL

	void FMessageHub::UnbindMessageImpl(const FName& MessageKey, FGMPKey InKey)
	{
#if GMP_WITH_STATIC_STORE
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSigWithStaticAdopt(MessageSignals, MessageKey)))
#else
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
#endif
		{
			CallbackMarks.Remove(MessageKey);
			if (InKey)
			{
				GMP_LOG(TEXT("FMessageHub::%sUnbindMessageImpl Key[%s] UnListen ID[%s]"), FTagTypeSetter::GetType().Get(TEXT("")), *MessageKey.ToString(), *InKey.ToString());
				Ptr->Disconnect(InKey);
			}
		}
	}

	void FMessageHub::UnbindMessageImpl(const FName& MessageKey, const UObject* Listener)
	{
#if GMP_WITH_STATIC_STORE
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSigWithStaticAdopt(MessageSignals, MessageKey)))
#else
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
#endif
		{
			CallbackMarks.Remove(MessageKey);
			if (Listener)
			{
				GMP_LOG(TEXT("FMessageHub::%sUnbindMessageImpl Key[%s] UnListen Obj[%s]"), FTagTypeSetter::GetType().Get(TEXT("")), *MessageKey.ToString(), *GetNameSafe(Listener));
				Ptr->Disconnect(Listener);
			}
		}
	}

	void FMessageHub::UnbindMessageImpl(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc)
	{
#if GMP_WITH_STATIC_STORE
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSigWithStaticAdopt(MessageSignals, MessageKey)))
#else
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
#endif
		{
			CallbackMarks.Remove(MessageKey);
			if (Listener)
			{
				GMP_LOG(TEXT("FMessageHub::%sUnbindMessageImpl Key[%s] UnListen Obj[%s] Src[%p]"), FTagTypeSetter::GetType().Get(TEXT("")), *MessageKey.ToString(), *GetNameSafe(Listener), (void*)InSigSrc.GetAddrValue());
				Ptr->Disconnect(Listener, InSigSrc);
			}
		}
	}

	FGMPKey FMessageHub::NotifyMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Params)
	{
		GMP_MSGBODY_ON_STACK(Msg, Params.Num(), Params.GetData(), MessageKey, InSigSrc, FGMPKey{});
		auto Seq = Msg.Sequence();
		{
			auto SignalPtr = static_cast<FGMPMsgSignal*>(Ptr);
#if WITH_EDITOR
			if (GIsEditor)
			{
				Hub::FRecursionDetection Detector(MessageKey, InSigSrc);

				auto IDs = FireMsgBodyAdapt(SignalPtr, InSigSrc, Msg);
				Hub::GetHistoryCalls().FindOrAdd(MessageKey).AppendCallInfo(InSigSrc, Msg, MoveTemp(IDs));
			}
			else
#endif
			{
				FireMsgBodyAdapt(SignalPtr, InSigSrc, Msg);
			}
		}
		return Seq;
	}

#if GMP_WITH_DIRECT_SIGNAL
	void FMessageHub::NotifyMessageDirectRaw(FSignalStore* DirectStore, FSigSource InSigSrc, const FGMPTypedAddr* paddrs, const FGMPExtra* extra)
	{
		if (!DirectStore)
			return;
#if !GMP_WITH_STATIC_STORE
		auto Holder = DirectStore->AsShared();
#endif
		GMPFireWithSigSourceDirectRaw(DirectStore, InSigSrc, paddrs, extra);
	}

	bool FMessageHub::NotifyMessageDirectImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param)
	{
		auto SignalPtr = static_cast<FGMPMsgSignal*>(Ptr);
#if WITH_EDITOR
		if (GIsEditor)
		{
			GMP_MSGBODY_ON_STACK(Msg, Param.Num(), Param.GetData(), MessageKey, InSigSrc, FGMPKey{});
			Hub::FRecursionDetection Detector(MessageKey, InSigSrc);
			auto IDs = FireMsgBodyAdapt(SignalPtr, InSigSrc, Msg);
			Hub::GetHistoryCalls().FindOrAdd(MessageKey).AppendCallInfo(InSigSrc, Msg, MoveTemp(IDs));
			return true;
		}
#endif
		FArrayTypeNames TypeNamesStk;
		const FName* TypeNamesPtr = nullptr;
#if GMP_WITH_TYPENAME
		TypeNamesStk.Reserve(Param.Num());
		for (auto& A : Param)
			TypeNamesStk.Add(A.TypeName);
		TypeNamesPtr = TypeNamesStk.GetData();
#else
		if (auto* Types = FMessageBody::GetMessageTypes(InSigSrc.TryGetUObject(), MessageKey))
			TypeNamesPtr = Types->GetData();
#endif
		const FGMPExtra Extra{Param.Num(), 0.f, TypeNamesPtr, InSigSrc, MessageKey, FGMPKey{}};
		auto Holder = SignalPtr->Store;
		GMPFireWithSigSourceDirectRaw(Holder.Get(), InSigSrc, Param.GetData(), &Extra);
		return true;
	}
#endif

	bool FMessageHub::IsAlive(const FName& MessageKey, FGMPKey Key) const
	{
		if (auto Ptr = static_cast<const FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			return !Key || Ptr->IsAlive(Key);
		}
		return false;
	}

	FGMPKey FMessageHub::IsAlive(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc) const
	{
		const FGMPMsgSignal* Ptr = IsValid(Listener) ? static_cast<const FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)) : nullptr;
		return Ptr && Ptr->IsAlive(Listener, InSigSrc);
	}

	bool FMessageHub::IsAlive(const FSignalBase* Ptr) const
	{
		return Ptr && static_cast<const FGMPMsgSignal*>(Ptr)->Store->IsAlive();
	}

#if WITH_EDITOR
	namespace Hub
	{
		bool GetListeners(const FSignalStore* Ptr, FSigSource InSigSrc, TArray<FWeakObjectPtr>& OutArray, int32 MaxCnt = 0)
		{
			TArray<FGMPKey> Results = Ptr->GetKeysBySrc(InSigSrc);
			Results.Sort();
			const int32 StartIdx = (MaxCnt > 0 && Results.Num() > MaxCnt) ? Results.Num() - MaxCnt : 0;

			auto ListenerNums = Results.Num();
			OutArray.Reserve(ListenerNums);

			for (auto i = Results.Num() - 1; i >= StartIdx; --i)
			{
				if (auto Elem = Ptr->FindSigElm(Results[i]))
				{
					auto Listener = Elem->GetHandler();
					if (!Listener.IsStale(true))
					{
						if (IsValid(InSigSrc.TryGetUObject()) && Listener.Get() && Listener.Get()->GetWorld() != InSigSrc.TryGetUObject()->GetWorld())
							continue;

						OutArray.Add(Listener);
					}
				}
			}
			return StartIdx != 0;
		}
		bool GetHandlers(const FSignalStore* Ptr, const UObject* Listener, TSet<FGMPKey>& OutArray, int32 MaxCnt = 0)
		{
			TArray<FGMPKey> Results = Ptr->GetKeysByHandler(Listener);
			OutArray.Reserve(Results.Num());
			Results.Sort();
			const int32 StartIdx = (MaxCnt > 0 && Results.Num() > MaxCnt) ? Results.Num() - MaxCnt : 0;
			for (auto i = Results.Num() - 1; i >= StartIdx; --i)
			{
				OutArray.Add(Results[i]);
			}
			return OutArray.Num() > 0;
		}
	}  // namespace Hub

	bool FMessageHub::GetListeners(FSigSource InSigSrc, FName MessageKey, TArray<FWeakObjectPtr>& OutArray, int32 MaxCnt)
	{
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			return Hub::GetListeners(Ptr->Store.Get(), InSigSrc, OutArray, MaxCnt);
		}
		return false;
	}

	bool FMessageHub::GetCallInfos(const UObject* Listener, FName MessageKey, TArray<FString>& OutArray, int32 MaxCnt)
	{
		if (auto Ptr = static_cast<FGMPMsgSignal*>(FindSig(MessageSignals, MessageKey)))
		{
			TSet<FGMPKey> OutKeys;
			if (Hub::GetHandlers(Ptr->Store.Get(), Listener, OutKeys, MaxCnt))
			{
				if (auto ArrFind = Hub::GetHistoryCalls().Find(MessageKey))
				{
					auto& Infos = *ArrFind;
					auto StartIdx = (MaxCnt > 0 && Infos.Num() > MaxCnt) ? Infos.Num() - MaxCnt : 0;
					for (auto i = Infos.Num() - 1; i >= StartIdx; --i)
					{
						auto& Info = Infos[i];
						if (Info.Records.Num() < OutKeys.Num())
						{
							for (auto Rec : Info.Records)
							{
								if (OutKeys.Contains(Rec))
								{
									OutArray.Add(FString::Printf(TEXT("%c %s"), !Info.Obj.IsStale() ? TEXT('+') : TEXT('-'), *Info.CallInfo));
									break;
								}
							}
						}
						else
						{
							for (auto Key : OutKeys)
							{
								if (Algo::BinarySearch(Info.Records, Key) != INDEX_NONE)
								{
									OutArray.Add(FString::Printf(TEXT("%c %s"), !Info.Obj.IsStale() ? TEXT('+') : TEXT('-'), *Info.CallInfo));
									break;
								}
							}
						}
					}
					if (StartIdx != 0)
						OutArray.Add(TEXT("..."));
				}
				return true;
			}
		}
		return false;
	}
#endif

#if GMP_WITH_DYNAMIC_CALL_CHECK
	struct FTagTypeStack : public TThreadSingleton<FTagTypeStack>
	{
	protected:
		TArray<FString, TInlineAllocator<8>> TagTypes;
		static auto& GetTagTypeStack() { return FTagTypeStack::Get().TagTypes; }

	public:
		static int32 TypeCnt() { return GetTagTypeStack().Num(); }
		static bool IsEmpty() { return TypeCnt() == 0; }

		static void PushType(const TCHAR* InType) { GetTagTypeStack().Push(InType); }
		static TOptional<FString> PopType()
		{
			auto Stack = MoveTemp(GetTagTypeStack());
			if (Stack.Num() == 1)
			{
				return Stack.Pop();
			}
			return {};
		}
		static FString DumpStack(bool bClear = false)
		{
			FString StackStr;
			auto& Stack = GetTagTypeStack();
			if (Stack.Num() > 0)
			{
				StackStr = FString::Join(Stack, TEXT("->"));
				GMP_LOG(TEXT("TagTypeStack: %s"), *StackStr);
				if (bClear)
				{
					Stack.Reset();
				}
			}
			return StackStr;
		}
		static TOptional<const TCHAR*> PeekType()
		{
			auto& Stack = GetTagTypeStack();
			if (Stack.Num() > 0)
			{
				return *Stack.Last();
			}
			return {};
		}
	};

	FMessageHub::FTagTypeSetter::FTagTypeSetter(const TCHAR* Type)
	{
		ensureAlways(IsInGameThread());
		if (Type && ensureMsgf(FTagTypeStack::IsEmpty(), TEXT("TagType not consumed %s"), *FTagTypeStack::DumpStack()))
		{
			FTagTypeStack::PushType(Type);
		}
	}
	FMessageHub::FTagTypeSetter::~FTagTypeSetter()
	{
		ensureAlways(IsInGameThread());
		ensureMsgf(FTagTypeStack::IsEmpty(), TEXT("TagType not consumed %s"), *FTagTypeStack::DumpStack(true));
	}
	TOptional<const TCHAR*> FMessageHub::FTagTypeSetter::GetType()
	{
		return FTagTypeStack::PeekType();
	}
#else
	FMessageHub::FTagTypeSetter::FTagTypeSetter(const TCHAR* Type)
	{
	}
	FMessageHub::FTagTypeSetter::~FTagTypeSetter()
	{
	}
	TOptional<const TCHAR*> FMessageHub::FTagTypeSetter::GetType()
	{
		return {};
	}
#endif

#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
	static FMessageHub::FOnUpdateMessageTagDelegate OnUpdateMessageTagDelegate;
	static auto& GetDelayInits()
	{
		struct FDelayInitMsgData
		{
			FString MsgId;
			FArrayTypeNames ReqParams;
			FArrayTypeNames RspNames;
			FString TagType;
		};
		static TArray<FDelayInitMsgData> DelayInits;
		return DelayInits;
	}

	void FMessageHub::InitMessageTagBinding(FMessageHub::FOnUpdateMessageTagDelegate&& InBinding)
	{
		OnUpdateMessageTagDelegate = MoveTemp(InBinding);
		auto& DelayInits = GetDelayInits();
		if (DelayInits.Num() > 0 && !IsRunningCommandlet())
		{
			for (auto& Elm : DelayInits)
			{
				OnUpdateMessageTagDelegate.Execute(Elm.MsgId, &Elm.ReqParams, &Elm.RspNames, *Elm.TagType);
				GMP_LOG(TEXT("DelayInited MSGKEY: \"%s\""), *Elm.MsgId);
			}
			DelayInits.Reset();
		}
	}
#endif

#define GMP_MSG_HOLDER_DUPLICATED 0
#if GMP_WITH_MSG_HOLDER
	void FMessageHub::StoreObjectMessageImpl(FSignalBase* Ptr, FSigSource InSigSrc, const FGMPPropStackRefArray& Params, int32 Flags)
	{
#if GMP_WITH_STATIC_STORE
		GMPEnsureStaticStoreRegistered(Ptr->Store.Get());
#endif
		auto Find = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(InSigSrc) : nullptr);
		if (!Find)
		{
			Find = &StoreSourceMsgs(Ptr->Store.Get()).FindOrAdd(InSigSrc);
		}
		Find->InitAsMsgStore(Ptr->Store->MessageKey, Params, Flags & FGMPStructUnion::MsgStoreFlagsMask);
#if GMP_MSG_HOLDER_DUPLICATED
		if (UWorld* ObjWorld = InSigSrc.GetSigSourceWorld())
		{
			StoreSourceMsgs(Ptr->Store.Get()).FindOrAdd(ObjWorld) = *Find;
		}
#endif
	}
#if GMP_WITH_DIRECT_SIGNAL && GMP_WITH_MSG_HOLDER
	FGMPStructUnion* FMessageHub::FindStoredMessageDirect(FSignalStore* DirectStore, FSigSource InSigSrc) const
	{
		return (DirectStore && StoreHasSourceMsgs(DirectStore)) ? StoreSourceMsgs(DirectStore).Find(InSigSrc) : nullptr;
	}
	void FMessageHub::RemoveStoredMessageDirect(FSignalStore* DirectStore, FSigSource InSigSrc)
	{
		if (DirectStore && StoreHasSourceMsgs(DirectStore))
			StoreSourceMsgs(DirectStore).Remove(InSigSrc);
	}
#endif
	int32 FMessageHub::RemoveObjectMessageImpl(FSignalBase* Ptr, FSigSource InSigSrc)
	{
		FGMPStructUnion Union;
		int32 Ret = 0;
		if (StoreHasSourceMsgs(Ptr->Store.Get()) && StoreSourceMsgs(Ptr->Store.Get()).RemoveAndCopyValue(InSigSrc, Union))
		{
			++Ret;
		}
#if GMP_MSG_HOLDER_DUPLICATED
		if (UWorld* ObjWorld = InSigSrc.GetSigSourceWorld())
		{
			if (auto Find = (StoreHasSourceMsgs(Ptr->Store.Get()) ? StoreSourceMsgs(Ptr->Store.Get()).Find(ObjWorld) : nullptr))
			{
				if (Find->GetMemory() == Union.GetMemory())
				{
					StoreSourceMsgs(Ptr->Store.Get()).Remove(ObjWorld);
					++Ret;
				}
			}
		}
#endif
		return Ret;
	}
	FStoreReplayAddrs FMessageHub::AsTypedAddresses(const FGMPStructUnion* InData)
	{
		FStoreReplayAddrs Replay;
		if (InData->IsValid())
		{
			int32 NumIface = 0;
			for (TFieldIterator<FProperty> CountIt(InData->GetScriptStruct()); CountIt; ++CountIt)
			{
				if (CastField<FInterfaceProperty>(*CountIt))
					++NumIface;
			}
			Replay.IfaceSlots.Reserve(NumIface);

			for (TFieldIterator<FProperty> PropIt(InData->GetScriptStruct()); PropIt; ++PropIt)
			{
				if (CastField<FInterfaceProperty>(*PropIt))
				{
					const FScriptInterface* SI = reinterpret_cast<const FScriptInterface*>(PropIt->ContainerPtrToValuePtr<void>(InData->GetMemory()));
					int32 SlotIdx = Replay.IfaceSlots.Emplace(MakeUnique<FStoreReplayAddrs::FIfaceReplaySlot>());
					FStoreReplayAddrs::FIfaceReplaySlot& Slot = *Replay.IfaceSlots[SlotIdx];
					FMemory::Memcpy(Slot.Block, SI, sizeof(FScriptInterface));
					static_assert(sizeof(FScriptInterface) == 16, "FScriptInterface must be 16B (ObjectPointer+InterfacePointer)");
					Slot.IfaceVal = SI->GetInterface();
					*reinterpret_cast<void**>(Slot.Block + 16) = &Slot.IfaceVal;
					// NAME_GMPSkipValidate: stored name is the bare interface but the read side expects TGMPNativeInterface<IXxx> (already type-checked at store).
					Replay.Addrs.Emplace(Slot.Block
#if GMP_WITH_TYPENAME
									,
								NAME_GMPSkipValidate
#endif
					);
				}
				else
				{
					Replay.Addrs.Emplace(PropIt->ContainerPtrToValuePtr<void>(InData->GetMemory())
#if GMP_WITH_TYPENAME
									,
								*PropIt
#endif
					);
				}
			}
		}
		return Replay;
	}

	FStoreReplayAddrs FMessageHub::MsgStoreToTypedAddresses(const FGMPStructUnion* InData)
	{
#if GMP_WITH_SINGLE_STRUCT_STORE
		if (InData->IsValid() && InData->IsSingleStructStore())
		{
			FStoreReplayAddrs Replay;
			Replay.Addrs.Emplace(InData->GetMemory()
#if GMP_WITH_TYPENAME
				, GMP::Class2Prop::TTraitsStructBase::GetProperty(InData->GetScriptStruct())
#endif
			);
			return Replay;
		}
#endif
		return AsTypedAddresses(InData);
	}
#endif

	extern const TCHAR* DebugCurrentMsgFileLine();
	namespace Hub
	{
		using FArrType = const FArrayTypeNames&;
		using FuncType = bool(FArrType&, FArrType&);
		static auto Skip(FArrType& l, FArrType& r)
		{
			return true;
		};
		static auto LhsNoMore(FArrType& l, FArrType& r)
		{
			return l.Num() <= r.Num();
		};
		static auto RhsNoMore(FArrType& l, FArrType& r)
		{
			return l.Num() >= r.Num();
		};

		static void AssingIfPossible(const FName& l, const FName& r)
		{
		}
		static void AssingIfPossible(FName& l, const FName& r)
		{
			l = r;
		}

		struct FTagDefinition
		{
			const FArrayTypeNames* ParameterTypes = nullptr;
			const FArrayTypeNames* ResponseTypes = nullptr;
		};

		static bool DoesSignatureCompatible(bool bSend, const FName& MessageId, const FTagDefinition& TypeDefinition, FTagDefinition& OutDefinition, const TCHAR* TagType, TStringBuilder<256>& TypeErrorInfo)
		{
#if GMP_WITH_DYNAMIC_CALL_CHECK
			TOptional<FString> TagTypeStr;
			if (!TagType)
			{
				TagTypeStr = FTagTypeStack::PopType();
				if (TagTypeStr)
					TagType = *TagTypeStr.GetValue();
			}
#endif
			static auto IsSameType = [](auto& lhs, auto& rhs, bool bPreCond = true, bool bFixCommonCls = false) {
				if (!bPreCond)
					return false;

				int32 Min = FMath::Min(lhs.Num(), rhs.Num());
				for (int32 i = 0; i < Min; ++i)
				{
					if ((lhs[i] == rhs[i]))
						continue;

					if (lhs[i] == NAME_GMPSkipValidate || rhs[i] == NAME_GMPSkipValidate)
						continue;

					if (!lhs[i].IsValid() || !rhs[i].IsValid())
						continue;

					if (lhs[i].IsNone() || rhs[i].IsNone())
						continue;

					if (FNameSuccession::MatchEnums(lhs[i], rhs[i]))
						continue;
					if (FNameSuccession::MatchEnums(rhs[i], lhs[i]))
						continue;

					if (FNameSuccession::IsDerivedFrom(lhs[i], rhs[i]))
					{
						AssingIfPossible(lhs[i], rhs[i]);
						continue;
					}
					if (FNameSuccession::IsDerivedFrom(rhs[i], lhs[i]))
					{
						AssingIfPossible(rhs[i], lhs[i]);
						continue;
					}

					if (bFixCommonCls)
					{
						const auto ComomName = FNameSuccession::FindCommonBase(lhs[i], rhs[i]);
						if (!ComomName.IsNone())
						{
							AssingIfPossible(lhs[i], ComomName);
							AssingIfPossible(rhs[i], ComomName);
						}
					}
					return false;
				}
				return true;
			};

			static auto JoinNames = [](auto& Names) { return FString::JoinBy(Names, TEXT(","), [](auto& Name) { return Name.ToString(); }); };
			static auto ProcessTypes = [](bool bSend, const FName& MessageId, auto& Sends, auto& Recvs, auto& InTypes, auto*& OutTypes, auto& OutInfo) {
				auto PtrSend = Sends.Find(MessageId);
				auto PtrRecv = Recvs.Find(MessageId);

				if (!bSend)
				{
					if (PtrSend && !IsSameType(InTypes, *PtrSend, LhsNoMore(InTypes, *PtrSend)))
					{
						OutTypes = PtrSend;
						OutInfo.Appendf(TEXT("GMPHub : [%s] Revcs more than Sends : [%s] <-> [%s]"), *MessageId.ToString(), *JoinNames(InTypes), *JoinNames(*PtrSend));
						OutInfo.Appendf(TEXT("GMPHub : [%s] Revcs more than Sends : %s"), *MessageId.ToString(), DebugCurrentMsgFileLine());
						UE_DEBUG_BREAK();
						return false;
					}

					bool ParamMore = true;
					if (PtrRecv)
					{
						ParamMore = InTypes.Num() > PtrRecv->Num();
						if (!IsSameType(InTypes, *PtrRecv, true, !PtrSend))
						{
							OutTypes = PtrRecv;
							OutInfo.Appendf(TEXT("GMPHub : [%s] Revcs mismatch : [%s] <-> [%s]"), *MessageId.ToString(), *JoinNames(InTypes), *JoinNames(*PtrRecv));
							OutInfo.Appendf(TEXT("GMPHub : [%s] Revcs mismatch : %s"), *MessageId.ToString(), DebugCurrentMsgFileLine());
							UE_DEBUG_BREAK();
							return false;
						}
					}
					if (ParamMore)
					{
						PtrRecv = &Recvs.Emplace(MessageId, InTypes);
					}
				}
				else
				{
					if (PtrRecv && !IsSameType(InTypes, *PtrRecv, RhsNoMore(InTypes, *PtrRecv)))
					{
						OutTypes = PtrRecv;
						OutInfo.Appendf(TEXT("GMPHub : [%s] Sends less than Revcs : [%s] <-> [%s]"), *MessageId.ToString(), *JoinNames(InTypes), *JoinNames(*PtrRecv));
						OutInfo.Appendf(TEXT("GMPHub : [%s] Sends less than Revcs : %s"), *MessageId.ToString(), DebugCurrentMsgFileLine());
						UE_DEBUG_BREAK();
						return false;
					}

					bool ParamLess = true;
					if (PtrSend)
					{
						ParamLess = InTypes.Num() < PtrSend->Num();
						if (!IsSameType(InTypes, *PtrSend, true, !PtrRecv))
						{
							OutTypes = PtrSend;
							OutInfo.Appendf(TEXT("GMPHub : [%s] Sends mismatch : [%s] <-> [%s]"), *MessageId.ToString(), *JoinNames(InTypes), *JoinNames(*PtrSend));
							OutInfo.Appendf(TEXT("GMPHub : [%s] Sends mismatch : %s"), *MessageId.ToString(), DebugCurrentMsgFileLine());
							UE_DEBUG_BREAK();
							return false;
						}
					}

					if (ParamLess)
					{
						PtrSend = &Sends.Emplace(MessageId, InTypes);
					}
				}

				if (PtrSend && PtrRecv && !IsSameType(*PtrRecv, *PtrSend, LhsNoMore(*PtrRecv, *PtrSend)))
				{
					OutInfo.Appendf(TEXT("GMPHub : [%s] Not Same Type : [%s] <-> [%s]"), *MessageId.ToString(), *JoinNames(*PtrSend), *JoinNames(*PtrRecv));
					OutInfo.Appendf(TEXT("GMPHub : [%s] Not Same Type : %s"), *MessageId.ToString(), DebugCurrentMsgFileLine());
					UE_DEBUG_BREAK();
					return false;
				}

				if (!OutTypes)
					OutTypes = bSend ? PtrSend : PtrRecv;
				return true;
			};

			if (TypeDefinition.ResponseTypes)
			{
				if (!ProcessTypes(bSend, MessageId, GetSends<true>(), GetRecvs<true>(), *TypeDefinition.ResponseTypes, OutDefinition.ResponseTypes, TypeErrorInfo))
					return false;
			}
			else
			{
				OutDefinition.ResponseTypes = bSend ? GetSends<true>().Find(MessageId) : GetRecvs<true>().Find(MessageId);
			}

			if (TypeDefinition.ParameterTypes)
			{
				if (!ProcessTypes(bSend, MessageId, GetSends<false>(), GetRecvs<false>(), *TypeDefinition.ParameterTypes, OutDefinition.ParameterTypes, TypeErrorInfo))
					return false;
			}
			else
			{
				OutDefinition.ParameterTypes = bSend ? GetSends<false>().Find(MessageId) : GetRecvs<false>().Find(MessageId);
			}

#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
			if (GIsEditor && TagType)
			{
#if 0
			ensureMsgf(IsRunningCommandlet() || OnUpdateMessageTagDelegate.IsBound(), TEXT("listen or notify message too early, please use GMP::OnGMPTagReady() instead"));
			if (OutDefinition.ParameterTypes)
				OnUpdateMessageTagDelegate.ExecuteIfBound(MessageId.ToString(), OutDefinition.ParameterTypes, OutDefinition.ResponseTypes, TagType);
#else
				if (!IsRunningCommandlet() && OutDefinition.ParameterTypes)
				{
					auto& DelayInits = GetDelayInits();
					if (!OnUpdateMessageTagDelegate.IsBound())
					{
						auto& Ref = DelayInits.AddDefaulted_GetRef();
						Ref.MsgId = MessageId.ToString();
						Ref.TagType = TagType;
						if (OutDefinition.ParameterTypes)
							Ref.ReqParams = *OutDefinition.ParameterTypes;
						if (OutDefinition.ResponseTypes)
							Ref.RspNames = *OutDefinition.ResponseTypes;
					}
					else
					{
						if (!ensure(!DelayInits.Num()))
						{
							for (auto& Elm : DelayInits)
							{
								OnUpdateMessageTagDelegate.Execute(Elm.MsgId, &Elm.ReqParams, &Elm.RspNames, TagType);
							}
							DelayInits.Reset();
						}
						OnUpdateMessageTagDelegate.Execute(MessageId.ToString(), OutDefinition.ParameterTypes, OutDefinition.ResponseTypes, TagType);
					}
				}
#endif
			}
#endif
			return true;
		}
	}  // namespace Hub

	const TCHAR* FMessageHub::GetNativeTagType()
	{
		return GMPGetNativeTagType();
	}

	const TCHAR* FMessageHub::GetScriptTagType()
	{
		return TEXT("Script");
	}
	const TCHAR* FMessageHub::GetBlueprintTagType()
	{
		return TEXT("Blueprint");
	}

	void FMessageHub::ResponseMessageImpl(FGMPKey RequestSequence, FTypedAddresses& Params, const FArrayTypeNames* SingleshotTypes, FSigSource InSigSrc, const TCHAR* Tag)
	{
		FTagTypeSetter SetMsgTagType(Tag);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		ON_SCOPE_EXIT
		{
			FTagTypeStack::PopType();
		};
#endif
		FResponseSig Val;
		if (Hub::GMPResponses().RemoveAndCopyValue(RequestSequence.Key, Val))
		{
#if GMP_WITH_DYNAMIC_CALL_CHECK
			const FArrayTypeNames* OldParams = nullptr;
			FArrayTypeNames Types;
			if (!SingleshotTypes)
			{
				if (auto ResponseTypes = UGMPMeta::GetSvrMeta(nullptr, Val.GetRec()))
				{
					Types = *ResponseTypes;
					SingleshotTypes = &Types;
				}
#if GMP_WITH_TYPENAME
				if (!SingleshotTypes)
				{
					Algo::ForEach(Params, [&](auto& Cell) { Types.Add(Cell.TypeName); });
					SingleshotTypes = &Types;
				}
#endif
			}

			if (!ensure(SingleshotTypes))
			{
			}
			else if (!ensureAlwaysMsgf(FMessageHub::IsSingleshotCompatible(true, *Val.GetRec().ToString(), *SingleshotTypes, OldParams), TEXT("RequestMessage Singleshot Mismatch")))
			{
				return;
			}
#endif
#if GMP_WITH_DIRECT_SIGNAL
			const FGMPExtra Extra{Params.Num(), 0.f, nullptr, InSigSrc, Val.GetRec(), RequestSequence};
			Val(Params.GetData(), &Extra);
#else
			GMP_MSGBODY_ON_STACK(Msg, Params.Num(), Params.GetData(), Val.GetRec(), InSigSrc, RequestSequence);
			Val(Msg);
#endif
		}
	}

	bool FMessageHub::IsSignatureCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, const TCHAR* TagType)
	{
#if GMP_WITH_DYNAMIC_CALL_CHECK
		Hub::FTagDefinition TagDefinition;
		TagDefinition.ParameterTypes = &TypeNames;

		Hub::FTagDefinition OutTagDefinition;
		ON_SCOPE_EXIT
		{
			OldTypes = OutTagDefinition.ParameterTypes;
		};
		TStringBuilder<256> ErrorInfo;
		if (!Hub::DoesSignatureCompatible(bCall, MessageId, TagDefinition, OutTagDefinition, TagType, ErrorInfo))
		{
			GMP_ERROR(TEXT("%s"), *ErrorInfo);
			return false;
		}
#endif
		return true;
	}

	bool FMessageHub::IsSingleshotCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, const TCHAR* TagType)
	{
#if GMP_WITH_DYNAMIC_CALL_CHECK
		Hub::FTagDefinition TagDefinition;
		TagDefinition.ResponseTypes = &TypeNames;

		Hub::FTagDefinition OutTagDefinition;
		ON_SCOPE_EXIT
		{
			OldTypes = OutTagDefinition.ParameterTypes;
		};
		TStringBuilder<256> ErrorInfo;
		if (!Hub::DoesSignatureCompatible(bCall, MessageId, TagDefinition, OutTagDefinition, TagType, ErrorInfo))
		{
			GMP_ERROR(TEXT("%s"), *ErrorInfo);
			return false;
		}
#endif
		return true;
	}

	bool FMessageBody::IsSignatureCompatible(bool bCall, const FArrayTypeNames*& OldTypes)
	{
#if GMP_WITH_DYNAMIC_CALL_CHECK
#if GMP_WITH_TYPENAME
		FArrayTypeNames LocalTypeNames;
		LocalTypeNames.Reserve(Size);
		for (const FGMPTypedAddr& Param : GetParams())
			LocalTypeNames.Add(Param.TypeName);
		return FMessageHub::IsSignatureCompatible(bCall, Key, LocalTypeNames, OldTypes);
#else
		auto TypeNames = GetMessageTypes(nullptr);
		return ensure(TypeNames) && FMessageHub::IsSignatureCompatible(bCall, MessageId, *TypeNames, OldTypes);
#endif

#else
		return true;
#endif
	}
}  // namespace GMP

namespace
{
	static FDelayedAutoRegisterHelper DelayInnerInitUGMPManager(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
#if GMP_WITH_DYNAMIC_CALL_CHECK
		{
			FCoreUObjectDelegates::PreLoadMap.AddLambda([](const FString& MapName) {
				GMP::Hub::GetSends<true>().Empty();
				GMP::Hub::GetRecvs<true>().Empty();
				GMP::Hub::GetSends<false>().Empty();
				GMP::Hub::GetRecvs<false>().Empty();
				GMP::Hub::GMPResponses().Empty();
			});
#if WITH_EDITOR
			if (GIsEditor)
			{
				FEditorDelegates::PreBeginPIE.AddLambda([](bool bIsSimulating) {
					GMP::Hub::GetSends<true>().Empty();
					GMP::Hub::GetRecvs<true>().Empty();
					GMP::Hub::GetSends<false>().Empty();
					GMP::Hub::GetRecvs<false>().Empty();
					GMP::Hub::GetHistoryCalls().Empty();
					GMP::Hub::GMPResponses().Empty();
				});
			}
#endif
		}
#endif
	});

#if GMP_WITH_DIRECT_SIGNAL
	static FDelayedAutoRegisterHelper DelayBindDirectSignalSlots(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
		GMP::FMessageUtils::GetMessageHub()->BindDirectSignalSlots();
	});
#endif
}  // namespace

#if GMP_DISABLE_HUB_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif


