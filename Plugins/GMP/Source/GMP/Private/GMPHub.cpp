//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPHub.h"

#include "CoreUObject.h"

#include "Algo/BinarySearch.h"
#include "Algo/ForEach.h"
#include "Engine/UserDefinedStruct.h"
#include "GMPMeta.h"
#include "GMPSignalsImpl.h"
#include "GMPSignalsInc.h"
#include "GMPWorldLocals.h"
#include "Misc/ScopeExit.h"
#include "UObject/ObjectKey.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"

#if UE_4_23_OR_LATER
#include "Containers/LockFreeList.h"
#endif

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace GMP
{
using FGMPMsgSignal = TSignal<false, FMessageBody&>;

#if GMP_DEBUGGAME
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

#if WITH_EDITOR
float FMessageBody::GetTimeSeconds()
{
	auto World = GetSigSource() ? GetSigSource()->GetWorld() : GWorld;
	return World ? World->GetTimeSeconds() : 0.f;
}

FString FMessageBody::MessageToString() const
{
	FString Result = FString::Printf(TEXT("%d Params"), Params.Num());
	if (auto Types = GetMessageTypes(nullptr))
	{
		Result = FString::JoinBy(*Types, TEXT(","), [](const FName& Name) { return Name.ToString(); });
	}
#if GMP_WITH_TYPENAME
	else
	{
		Result = FString::JoinBy(Params, TEXT(","), [](const FGMPTypedAddr& Addr) { return Addr.TypeName.ToString(); });
	}
#endif
	return FString::Printf(TEXT("(%s)"), *Result);
};
#endif

namespace Hub
{
#if WITH_EDITOR
	static bool bTraceAllMessages = false;
	FAutoConsoleVariableRef CVar_DebugAllMessages(TEXT("GMP.TraceAllMessages"), bTraceAllMessages, TEXT("TraceAllMessages"));

	struct FDebugMessageInfo
	{
		FWeakObjectPtr Obj;
		FString CallInfo;
		TArray<FGMPKey> Records;
	};

	constexpr int32 BIT_OFFSET = 8;               // 8
	constexpr int32 BIT_LIMIT = 1 << BIT_OFFSET;  // 128
	constexpr int32 BIT_MASK = BIT_LIMIT - 1;     // 127

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
	auto& GetHistoryCalls() { return DebugHistoryCalls; }

	static TMap<FName, TMap<FSigSource, int32>> EntrySources;
	struct FRecursionDetection
	{
		FRecursionDetection(FName InKey, FSigSource InSigSrc)
			: CurSigSrc(InSigSrc)
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

	FMessageHub::CallbackMapType& GMPResponses(const UObject* WorldContextObj = nullptr) { return WorldLocalObject<FMessageHub::CallbackMapType>(WorldContextObj); }

}  // namespace Hub

FGMPKey FMessageBody::GetNextSequenceID()
{
	static volatile int64 Seq = 0;
	if (FPlatformAtomics::InterlockedAdd(&Seq, 1))
		return FGMPKey(Seq);
	return FGMPKey(FPlatformAtomics::InterlockedAdd(&Seq, 1));
}

FMessageBody* FMessageHub::GetCurrentMessageBody() const
{
	return MessageBodyStack.Num() ? MessageBodyStack.Last() : nullptr;
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

void FMessageHub::PushMsgBody(FMessageBody* Body)
{
	MessageBodyStack.Push(Body);
}

FMessageBody* FMessageHub::PopMsgBody()
{
	return MessageBodyStack.Pop();
}

FGMPKey FMessageHub::RequestMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponeSig&& OnRsp, const FArrayTypeNames* SingleshotTypes)
{
	if (OnRsp && CallbackMarks.Contains(MessageKey) && ensureAlwaysMsgf(!Hub::GMPResponses().Contains(OnRsp.GetId()), TEXT("duplicate sequence %zu!"), OnRsp.GetId()))
	{
		Hub::GMPResponses().Emplace(OnRsp.GetId(), MoveTemp(OnRsp));

		FMessageBody Msg(Param, MessageKey, InSigSrc, OnRsp.GetId());

		PushMsgBody(&Msg);
		ON_SCOPE_EXIT { PopMsgBody(); };
		{
			auto SignalPtr = static_cast<FGMPMsgSignal*>(Ptr);

#if WITH_EDITOR
			if (GIsEditor)
			{
				Hub::FRecursionDetection Detector(MessageKey, InSigSrc);
				GMP_CNOTE_ONCE(Detector, TEXT("Recursion Detected! :%s"), *InSigSrc.GetNameSafe());

				auto IDs = SignalPtr->FireWithSigSource(InSigSrc, Msg);
				Hub::GetHistoryCalls().FindOrAdd(MessageKey).AppendCallInfo(InSigSrc, Msg, MoveTemp(IDs));
			}
			else
#endif
			{
				SignalPtr->FireWithSigSource(InSigSrc, Msg);
			}
		}
		return Msg.SequenceId;
	}
	return {};
}

void FMessageHub::ResponseMessageImpl(bool bNativeCall, FGMPKey RequestSequence, FTypedAddresses& Params, const FArrayTypeNames* SingleshotTypes, FSigSource InSigSrc)
{
	FResponeSig Val;
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

		if (!ensure(SingleshotTypes) || ensureAlwaysMsgf(FMessageHub::IsSingleshotCompatible(true, *Val.GetRec().ToString(), *SingleshotTypes, OldParams, bNativeCall), TEXT("RequestMessage Singleshot Mismatch")))
#endif
		{
			FMessageBody Msg(Params, Val.GetRec(), InSigSrc, RequestSequence);
			Val(Msg);
		}
	}
}

FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPMessageSig&& Slot, int32 Times)
{
	if (!MessageSignals.Contains(MessageKey))
		MessageSignals.Add(MessageKey).Store = FGMPMsgSignal::MakeSignals();

	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		if (auto Elem = Ptr->Connect(Listener.GetObj(), std::move(Slot), InSigSrc))
		{
			GMP_LOG(TEXT("FMessageHub::ListenMessage Key[%s] Listener[%s] Watched[%s]"), *MessageKey.ToString(), *GetNameSafe(Listener.GetObj()), *InSigSrc.GetNameSafe());

			Elem->SetLeftTimes(Times);
			if (auto Inc = Listener.GetInc())
			{
				Ptr->BindSignalConnection(Inc->GMPSignalHandle, Elem->GetGMPKey());
			}

			return Elem->GetGMPKey();
		}
	}
	return {};
}

FGMPKey FMessageHub::ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPMessageSig&& Slot, int32 Times)
{
	if (!MessageSignals.Contains(MessageKey))
		MessageSignals.Add(MessageKey).Store = FGMPMsgSignal::MakeSignals();

	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		if (auto Elem = Ptr->Connect(Listener, std::move(Slot), InSigSrc))
		{
			GMP_LOG(TEXT("FMessageHub::ListenMessage Key[%s] Handle[%p] Watched[%s]"), *MessageKey.ToString(), Listener, *InSigSrc.GetNameSafe());

			Elem->SetLeftTimes(Times);
			return Elem->GetGMPKey();
		}
	}
	return {};
}

void FMessageHub::UnListenMessageImpl(const FName& MessageKey, FGMPKey InKey)
{
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		CallbackMarks.Remove(MessageKey);
		if (InKey)
		{
			GMP_LOG(TEXT("FMessageHub::UnListenMessageImpl Key[%s] UnListen ID[%s]"), *MessageKey.ToString(), *InKey.ToString());
			Ptr->Disconnect(InKey);
		}
	}
}

void FMessageHub::UnListenMessageImpl(const FName& MessageKey, const UObject* Listener)
{
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		CallbackMarks.Remove(MessageKey);
		if (Listener)
		{
			GMP_LOG(TEXT("FMessageHub::UnListenMessageImpl Key[%s] UnListen Obj[%s]"), *MessageKey.ToString(), *GetNameSafe(Listener));
			Ptr->Disconnect(Listener);
		}
	}
}

void FMessageHub::UnListenMessageImpl(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc)
{
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		CallbackMarks.Remove(MessageKey);
		if (Listener)
		{
			GMP_LOG(TEXT("FMessageHub::UnListenMessageImpl Key[%s] UnListen Obj[%s] Src[%p]"), *MessageKey.ToString(), *GetNameSafe(Listener), InSigSrc.GetAddrValue());
			Ptr->Disconnect(Listener, InSigSrc);
		}
	}
}

FGMPKey FMessageHub::NotifyMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Params)
{
	FMessageBody Msg(Params, MessageKey, InSigSrc);
	auto Seq = Msg.SequenceId;
	{
		PushMsgBody(&Msg);
		ON_SCOPE_EXIT { PopMsgBody(); };
		auto SignalPtr = static_cast<FGMPMsgSignal*>(Ptr);
#if WITH_EDITOR
		if (GIsEditor)
		{
			Hub::FRecursionDetection Detector(MessageKey, InSigSrc);

			auto IDs = SignalPtr->FireWithSigSource(InSigSrc, Msg);
			Hub::GetHistoryCalls().FindOrAdd(MessageKey).AppendCallInfo(InSigSrc, Msg, MoveTemp(IDs));
		}
		else
#endif
		{
			SignalPtr->FireWithSigSource(InSigSrc, Msg);
		}
	}
	return Seq;
}

bool FMessageHub::IsAlive(const FName& MessageKey, FGMPKey Key) const
{
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		return Key ? Ptr->IsAlive(Key) : true;
	}
	return false;
}

FGMPKey FMessageHub::IsAlive(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc) const
{
	const FGMPMsgSignal* Ptr = IsValid(Listener) ? FindSig<FGMPMsgSignal>(MessageSignals, MessageKey) : nullptr;
	return Ptr ? Ptr->IsAlive(Listener, InSigSrc) : 0u;
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

					OutArray.Add(Elem->GetHandler());
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
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
	{
		return Hub::GetListeners(Ptr->Store.Get(), InSigSrc, OutArray, MaxCnt);
	}
	return false;
}

bool FMessageHub::GetCallInfos(const UObject* Listener, FName MessageKey, TArray<FString>& OutArray, int32 MaxCnt)
{
	if (auto Ptr = FindSig<FGMPMsgSignal>(MessageSignals, MessageKey))
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

#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
static FMessageHub::FOnUpdateMessageTagDelegate OnUpdateMessageTagDelegate;
FMessageHub::FOnUpdateMessageTagDelegate& FMessageHub::OnUpdateMessageTag()
{
	return OnUpdateMessageTagDelegate;
}
#endif

namespace Hub
{
	using FArrType = const FArrayTypeNames&;
	using FuncType = bool(FArrType&, FArrType&);
	static auto Skip(FArrType& l, FArrType& r) { return true; };
	static auto LhsNoMore(FArrType& l, FArrType& r) { return l.Num() <= r.Num(); };
	static auto RhsNoMore(FArrType& l, FArrType& r) { return l.Num() >= r.Num(); };

	static void AssingIfPossible(const FName& l, const FName& r) {}
	static void AssingIfPossible(FName& l, const FName& r) { l = r; }

	struct FTagDefinition
	{
		const FArrayTypeNames* ParameterTypes = nullptr;
		const FArrayTypeNames* ResponseTypes = nullptr;
	};

	static bool DoesSignatureCompatible(bool bSend, const FName& MessageId, const FTagDefinition& TypeDefinition, FTagDefinition& OutDefinition, bool bNativeCall)
	{
		static auto IsSameType = [](auto& lhs, auto& rhs, bool bPreCond = true) {
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
				return false;
			}
			return true;
		};

		static auto ProcessTypes = [](bool bSend, const FName& MessageId, auto& Sends, auto& Recvs, auto& InTypes, auto*& OutTypes) {
			auto PtrSend = Sends.Find(MessageId);
			auto PtrRecv = Recvs.Find(MessageId);

			if (!bSend)
			{
				if (PtrSend && !IsSameType(InTypes, *PtrSend, LhsNoMore(InTypes, *PtrSend)))
				{
					OutTypes = PtrSend;
					return ensureAlwaysMsgf(false, TEXT("Revcs more than Sends"));
				}

				bool ParamMore = true;
				if (PtrRecv)
				{
					ParamMore = InTypes.Num() > PtrRecv->Num();
					if (!IsSameType(InTypes, *PtrRecv))
					{
						OutTypes = PtrRecv;
						return ensureAlwaysMsgf(false, TEXT("Revcs mismatch"));
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
					return ensureAlwaysMsgf(false, TEXT("Sends less than Revcs"));
				}

				bool ParamLess = true;
				if (PtrSend)
				{
					ParamLess = InTypes.Num() < PtrSend->Num();
					if (!IsSameType(InTypes, *PtrSend))
					{
						OutTypes = PtrSend;
						return ensureAlwaysMsgf(false, TEXT("Sends mismatch"));
					}
				}

				if (ParamLess)
				{
					PtrSend = &Sends.Emplace(MessageId, InTypes);
				}
			}

			if (PtrSend && PtrRecv && !IsSameType(*PtrRecv, *PtrSend, LhsNoMore(*PtrRecv, *PtrSend)))
				return ensureAlways(false);

			if (!OutTypes)
				OutTypes = bSend ? PtrSend : PtrRecv;
			return true;
		};

		if (TypeDefinition.ResponseTypes)
		{
			if (!ProcessTypes(bSend, MessageId, GetSends<true>(), GetRecvs<true>(), *TypeDefinition.ResponseTypes, OutDefinition.ResponseTypes))
				return false;
		}
		else
		{
			OutDefinition.ResponseTypes = bSend ? GetSends<true>().Find(MessageId) : GetRecvs<true>().Find(MessageId);
		}

		if (TypeDefinition.ParameterTypes)
		{
			if (!ProcessTypes(bSend, MessageId, GetSends<false>(), GetRecvs<false>(), *TypeDefinition.ParameterTypes, OutDefinition.ParameterTypes))
				return false;
		}
		else
		{
			OutDefinition.ParameterTypes = bSend ? GetSends<false>().Find(MessageId) : GetRecvs<false>().Find(MessageId);
		}

#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
		if (GIsEditor && bNativeCall)
		{
#if 0
			ensureMsgf(IsRunningCommandlet() || OnUpdateMessageTagDelegate.IsBound(), TEXT("listen or notify message too early, please use GMP::OnGMPTagReady() instead"));
			if (OutDefinition.ParameterTypes)
				OnUpdateMessageTagDelegate.ExecuteIfBound(MessageId.ToString(), OutDefinition.ParameterTypes, OutDefinition.ResponseTypes);
#else
			if (!IsRunningCommandlet() && OutDefinition.ParameterTypes)
			{
				struct FDelayInitMsgData
				{
					FString MsgId;
					FArrayTypeNames ReqParams;
					FArrayTypeNames RspNames;
				};
				static TArray<FDelayInitMsgData> DelayInits;
				if (!OnUpdateMessageTagDelegate.IsBound())
				{
					auto& Ref = DelayInits.AddDefaulted_GetRef();
					Ref.MsgId = MessageId.ToString();
					if (OutDefinition.ParameterTypes)
						Ref.ReqParams = *OutDefinition.ParameterTypes;
					if (OutDefinition.ResponseTypes)
						Ref.RspNames = *OutDefinition.ResponseTypes;
				}
				else
				{
					if (DelayInits.Num() > 0)
					{
						for (auto& Elm : DelayInits)
						{
							OnUpdateMessageTagDelegate.Execute(Elm.MsgId, &Elm.ReqParams, &Elm.RspNames);
						}
						DelayInits.Reset();
					}
					OnUpdateMessageTagDelegate.Execute(MessageId.ToString(), OutDefinition.ParameterTypes, OutDefinition.ResponseTypes);
				}
			}
#endif
		}
#endif
		return true;
	}
}  // namespace Hub

bool FMessageHub::IsSignatureCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, bool bNativeCall)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	Hub::FTagDefinition TagDefinition;
	TagDefinition.ParameterTypes = &TypeNames;

	Hub::FTagDefinition OutTagDefinition;
	ON_SCOPE_EXIT { OldTypes = OutTagDefinition.ParameterTypes; };
	return Hub::DoesSignatureCompatible(bCall, MessageId, TagDefinition, OutTagDefinition, bNativeCall);
#else
	return true;
#endif
}

bool FMessageHub::IsSingleshotCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, bool bNativeCall)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	Hub::FTagDefinition TagDefinition;
	TagDefinition.ResponseTypes = &TypeNames;

	Hub::FTagDefinition OutTagDefinition;
	ON_SCOPE_EXIT { OldTypes = OutTagDefinition.ParameterTypes; };
	return Hub::DoesSignatureCompatible(bCall, MessageId, TagDefinition, OutTagDefinition, bNativeCall);
#else
	return true;
#endif
}

bool FMessageBody::IsSignatureCompatible(bool bCall, const FArrayTypeNames*& OldParams, bool bNativeCall)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
#if GMP_WITH_TYPENAME
	FArrayTypeNames TypeNames;
	TypeNames.Reserve(Params.Num());
	for (auto& Param : Params)
		TypeNames.Add(Param.TypeName);
	return FMessageHub::IsSignatureCompatible(bCall, MessageId, TypeNames, OldParams, bNativeCall);
#else
	auto TypeNames = GetMessageTypes(nullptr);
	return ensure(TypeNames) && FMessageHub::IsSignatureCompatible(bCall, MessageId, *TypeNames, OldParams, bNativeCall);
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
	// if (TrueOnFirstCall([] {}))
	{
		// Register for PreloadMap so cleanup can occur on map transitions
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
			// Register in editor for PreBeginPlay so cleanup can occur when we start a PIE session
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
}  // namespace
