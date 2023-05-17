//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Delegates/Delegate.h"
#include "GMPSignals.inl"
#include "GMPSignalsInc.h"
#include "GMPStruct.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ScriptMacros.h"

#include "GMPHub.generated.h"

#ifndef GMP_REDUCE_IGMPSIGNALS_CAST
#define GMP_REDUCE_IGMPSIGNALS_CAST 1
#endif

class UGMPBPLib;

namespace GMP
{
class FMessageHub;
using FGMPMessageSig = GMP::TGMPFunction<void(GMP::FMessageBody&)>;

struct FResponeRec
{
	uint64 GetId() const { return Id; }
	FName GetRec() const { return Rec; }

protected:
	FGMPKey Id;
	FName Rec;
};
struct FResponeSig final : public TAttachedCallableStore<FResponeRec, GMP_FUNCTION_PREDEFINED_ALIGN_SIZE>
{
	FResponeSig() = default;
	FResponeSig(FResponeSig&& Val) = default;
	FResponeSig& operator=(FResponeSig&& Val) = default;

	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	FResponeSig(Functor&& Val, FName InRec = NAME_None, uint64 InId = 0u)
		: TAttachedCallableStore(std::forward<Functor>(Val))
	{
		static_assert(TypeTraits::IsSameV<void(GMP::FMessageBody&), TypeTraits::TSigFuncType<Functor>>, "sig mismatch");
		Rec = InRec;
		Id = InId;
	}

	void operator()(GMP::FMessageBody& Body) const
	{
		CheckCallable();
		return reinterpret_cast<void (*)(void*, GMP::FMessageBody&)>(GetCallable())(GetObjectAddress(), Body);
	}
};
}  // namespace GMP

USTRUCT(NotBlueprintable, NotBlueprintType)
struct FGMPResponder
{
	GENERATED_BODY()
public:
	template<typename... TArgs>
	void Response(TArgs&&... Args) const;

	class GMP::FMessageHub* MsgHub;
	UPROPERTY()
	FName MsgId;
	UPROPERTY()
	uint64 Sequence;
};

GMP_MSG_OF(FSimpleDelegate)

namespace GMP
{
using FGMPSignalMap = TMap<FName, FSignalBase>;
template<typename T = FSignalBase>
FORCEINLINE auto FindSig(FGMPSignalMap& Map, FName Name)
{
	return static_cast<T*>(Map.Find(Name));
}
template<typename T = FSignalBase>
FORCEINLINE auto FindSig(const FGMPSignalMap& Map, FName Name)
{
	return static_cast<const T*>(Map.Find(Name));
}

namespace Hub
{
	template<typename F, typename... TArgs, size_t... Is>
	FORCEINLINE_DEBUGGABLE void InvokeImpl(const F& Func, FMessageBody& Body, std::tuple<TArgs...>*, std::index_sequence<Is...>*)
	{
		static_assert(sizeof...(TArgs) == sizeof...(Is), "mismatch");
#if GMP_WITH_DYNAMIC_CALL_CHECK
		GMP_CHECK_SLOW(Body.GetParamCount() >= sizeof...(TArgs));
#endif
		Func(Body.GetParamVerify<TArgs>(Is)...);
	}
	template<typename Tup, typename F>
	FORCEINLINE void Invoke(const F& Func, FMessageBody& Body, Tup* In = nullptr)
	{
		InvokeImpl(Func, Body, In, (std::make_index_sequence<std::tuple_size<Tup>::value>*)nullptr);
	}

	template<typename F, typename... TArgs, size_t... Is>
	FORCEINLINE_DEBUGGABLE void InvokeWithSingleShotInfo(FMessageHub* InMsgHub, const F& Func, FMessageBody& Body, std::tuple<TArgs...>*, std::index_sequence<Is...>*)
	{
		static_assert(sizeof...(TArgs) == sizeof...(Is), "mismatch");
		GMP_CHECK_SLOW(Body.GetParamCount() >= sizeof...(TArgs));
		FGMPResponder Info{InMsgHub, Body.MessageKey(), Body.Sequence()};
		Func(Body.GetParamVerify<TArgs>(Is)..., Info);
	}

	template<typename Tup, size_t... Is>
	FTypedAddresses MakeParamFromTuple(Tup& InTup, const std::index_sequence<Is...>&)
	{
		return FTypedAddresses{FGMPTypedAddr::MakeMsg(std::get<Is>(InTup))...};
	}

	template<typename Tup, size_t... Is>
	static decltype(auto) MakeNamesImpl(Tup* InTup, const std::index_sequence<Is...>&)
	{
		return FMessageBody::MakeStaticNamesImpl<std::decay_t<std::tuple_element_t<Is, Tup>>...>();
	}

	template<typename FuncType>
	struct TMessageTraits
	{
		using TSig = TypeTraits::TSigTraits<FuncType>;
		using TFuncType = typename TSig::TFuncType;
		using Tuple = typename TSig::Tuple;
		using LastType = typename TSig::LastType;

		static_assert(!(TSig::TupleSize == 1 && TypeTraits::IsSameV<std::decay_t<LastType>, FMessageBody>), "err");

		using AttachedFunctorType = TGMPFunction<TFuncType>;

		template<typename F>
		static FGMPMessageSig MakeCallback(FMessageHub* InMsgHub, F&& Func, std::true_type)
		{
			using SeqIndex = std::make_index_sequence<TSig::TupleSize - 1>;
			using ReducedTuple = TypeTraits::TTupleRemoveLastType<Tuple>;
			static_assert(std::tuple_size<ReducedTuple>::value == (TSig::TupleSize - 1), "err");
			return [Func{std::move(Func)}, InMsgHub](FMessageBody& Body) { Hub::InvokeWithSingleShotInfo(InMsgHub, static_cast<const AttachedFunctorType&>(Func), Body, (ReducedTuple*)nullptr, (SeqIndex*)nullptr); };
		}

		template<typename F>
		static FGMPMessageSig MakeCallback(FMessageHub*, F&& Func, std::false_type)
		{
			return [Func{std::move(Func)}](FMessageBody& Body) { Hub::Invoke<Tuple>(static_cast<const AttachedFunctorType&>(Func), Body); };
		}
	};

	template<typename FuncType, typename = void>
	struct TListenArgumentsTraits
	{
		using MyTraits = TMessageTraits<std::decay_t<FuncType>>;
		using Tuple = typename MyTraits::Tuple;
		enum
		{
			bIsSingleShot = TypeTraits::IsSameV<FGMPResponder&, std::remove_cv_t<typename MyTraits::LastType>>,
			TupleSize = std::tuple_size<Tuple>::value
		};

		template<typename T, typename F>
		static decltype(auto) MakeCallback(FMessageHub* InMsgHub, T* Listener, F&& Func)
		{
			return MyTraits::MakeCallback(InMsgHub, std::forward<F>(Func), std::conditional_t<bIsSingleShot, std::true_type, std::false_type>());
		}
		template<typename T, typename R, typename F, typename... TArgs>
		static decltype(auto) MakeCallback(FMessageHub* InMsgHub, T* Listener, R (F::*Op)(TArgs...))
		{
			GMP_CHECK_SLOW(Listener);
			auto Func = [=](ForwardParam<TArgs>... Args) { (Listener->*Op)(static_cast<TArgs>(Args)...); };
			return MyTraits::MakeCallback(InMsgHub, std::move(Func), std::conditional_t<bIsSingleShot, std::true_type, std::false_type>());
		}
		template<typename T, typename R, typename F, typename... TArgs>
		static decltype(auto) MakeCallback(FMessageHub* InMsgHub, T* Listener, R (F::*Op)(TArgs...) const)
		{
			GMP_CHECK_SLOW(Listener);
			auto Func = [=](ForwardParam<TArgs>... Args) { (Listener->*Op)(static_cast<TArgs>(Args)...); };
			return MyTraits::MakeCallback(InMsgHub, std::move(Func), std::conditional_t<bIsSingleShot, std::true_type, std::false_type>());
		}
		static decltype(auto) MakeNames() { return MakeNamesImpl((Tuple*)nullptr, std::make_index_sequence<TupleSize - (bIsSingleShot ? 1 : 0)>()); }
	};

	struct DefaultTraits
	{
		enum
		{
			bIsSingleShot = false
		};
		template<typename Tup>
		static FTypedAddresses MakeParam(Tup& InTup)
		{
			return MakeParamFromTuple(InTup, std::make_index_sequence<std::tuple_size<Tup>::value>());
		}

		template<typename Tup>
		static decltype(auto) MakeNames(Tup& InTup)
		{
			return MakeNamesImpl((Tup*)nullptr, std::make_index_sequence<std::tuple_size<Tup>::value>());
		}

		FORCEINLINE static auto MakeSingleShot(const FName&, const void*) { return nullptr; }
	};
	struct DefaultLessTraits
	{
		enum
		{
			bIsSingleShot = true
		};
		template<typename Tup>
		static FTypedAddresses MakeParam(Tup& InTup)
		{
			const auto TupleSize = std::tuple_size<Tup>::value;
			static_assert(TupleSize > 0, "err");
			return MakeParamFromTuple(InTup, std::make_index_sequence<TupleSize - 1>());
		}

		template<typename Tup>
		static decltype(auto) MakeNames(Tup& InTup)
		{
			const auto TupleSize = std::tuple_size<Tup>::value;
			static_assert(TupleSize > 0, "err");
			return MakeNamesImpl((Tup*)nullptr, std::make_index_sequence<TupleSize - 1>());
		}

		template<typename F>
		static FResponeSig MakeSingleShotImpl(const FName& SingleShotId, F&& OnRsp);

		template<typename Tup>
		static FResponeSig MakeSingleShot(const FName& SingleShotId, Tup* InTup)
		{
			const auto TupleSize = std::tuple_size<Tup>::value;
			using LastType = std::tuple_element_t<TupleSize - 1, Tup>;
			return MakeSingleShotImpl(SingleShotId, std::forward<LastType>(std::get<TupleSize - 1>(*InTup)));
		}
	};

	template<typename LastType, typename Enable = void>
	struct TSendArgumentsTraits : public DefaultTraits
	{
	};
	template<typename LastType>
	struct TSendArgumentsTraits<LastType, std::enable_if_t<TypeTraits::TIsCallable<LastType>::value || TypeTraits::TIsBaseDelegate<LastType>::value>> : public DefaultLessTraits
	{
	};

	template<typename F>
	static bool ApplyMessageBoy(FMessageBody& Body, const F& Lambda, bool bNative = true)
	{
		do
		{
			using ListenTraits = Hub::TListenArgumentsTraits<F>;
#if GMP_WITH_DYNAMIC_CALL_CHECK
			const auto& ArgNames = ListenTraits::MakeNames();
			const FArrayTypeNames* OldParams = nullptr;
			if (!ensureAlwaysMsgf(DoesSignatureCompatible(true, Body.MessageKey(), ArgNames, OldParams, bNative), TEXT("FMessageHub::ApplyMessageBoy SignatureMismatch ID:[%s]"), *Body.MessageKey().ToString()))
				break;
#else
			if (!ensure(ListenTraits::TupleSize <= Body.GetParamCount()))
				break;
#endif
			ListenTraits::MyTraits::Apply(Lambda, Body);
			return true;

		} while (false);
		return false;
	}
}  // namespace Hub

class FMessageUtils;
class GMP_API FMessageHub
{
public:
	friend class FMessageUtils;
	friend struct FGMPResponder;

	FMessageBody* GetCurrentMessageBody() const;

private:
	struct FSigListener
	{
		template<typename T>
		FSigListener(T* In)
			: Obj(In)
		{
			TrySetData(In);
		}

		FORCEINLINE const class IGMPSignalHandle* GetInc() const
		{
#if GMP_REDUCE_IGMPSIGNALS_CAST
			return Inc;
#else
			return Cast<IGMPSignalHandle>(Obj);
#endif
		}

		FORCEINLINE const UObject* GetObj() const { return Obj; }

	protected:
		const UObject* Obj = nullptr;
#if GMP_REDUCE_IGMPSIGNALS_CAST
		template<typename T>
		std::enable_if_t<!std::is_base_of<IGMPSignalHandle, std::decay_t<T>>::value> TrySetData(T* In)
		{
		}
		void TrySetData(const class IGMPSignalHandle* In) { Inc = In; }
		const class IGMPSignalHandle* Inc = nullptr;
#else
		template<typename T>
		void TrySetData(T* In)
		{
		}

#endif
	};

	template<typename T>
	static FORCEINLINE std::enable_if_t<IsCollectionBase<T>, FSigCollection*> ToSigListenner(T* InObj)
	{
		static_assert(!std::is_base_of<UObject, T>::value, "UObject types should inherit from IGMPSignalsHandle!");
		return InObj;
	}

	template<typename T>
	static FORCEINLINE std::enable_if_t<!IsCollectionBase<T>, FSigListener> ToSigListenner(T* InObj)
	{
		static_assert(std::is_base_of<UObject, T>::value, "Only UObject-based or GMPSignals::FSignalsCollection-based are supported.");
		return {InObj};
	}

	// Listen
	FGMPKey ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPMessageSig&& Func, int32 Times = -1);
	FGMPKey ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPMessageSig&& Func, int32 Times = -1);

	// Unlisten
	void UnListenMessageImpl(const FName& MessageKey, FGMPKey InKey);
	void UnListenMessageImpl(const FName& MessageKey, const UObject* Listener = nullptr);
	void UnListenMessageImpl(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc);
	// Notify
	FGMPKey NotifyMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param);

	// Request
	FGMPKey RequestMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponeSig&& Sig, const FArrayTypeNames* RspTypes = nullptr);
	// Respone
	void ResponseMessageImpl(bool bNativeCall, FGMPKey RequestSequence, FTypedAddresses& Param, const FArrayTypeNames* RspTypes = nullptr, FSigSource InSigSrc = FSigSource::NullSigSrc);

private:
	//////////////////////////////////////////////////////////////////////////
	// Send
	FORCEINLINE FGMPKey SendObjectMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, std::nullptr_t) { return NotifyMessageImpl(Ptr, MessageKey, InSigSrc, Param); }
	FORCEINLINE FGMPKey SendObjectMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponeSig&& OnRsp) { return RequestMessageImpl(Ptr, MessageKey, InSigSrc, Param, std::move(OnRsp)); }

public:
#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
	// Let MessageTagsEditorModule to add MessageTag at runtime
	using FOnUpdateMessageTagDelegate = TDelegate<void(const FString&, const FArrayTypeNames*, const FArrayTypeNames*)>;
	static FOnUpdateMessageTagDelegate& OnUpdateMessageTag();
#endif

	template<typename... TArgs>
	FORCEINLINE uint64 SendMessage(const FMSGKEYFind& MessageKey, TArgs&&... Args)
	{
		return SendObjectMessage(MessageKey, nullptr, std::forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	uint64 SendObjectMessage(const FMSGKEYFind& MessageKey, FSigSource InSigSrc, TArgs&&... Args)
	{
#if !WITH_EDITOR
		if (!MessageKey)
			return 0;
#endif
		using SendTraits = Hub::TSendArgumentsTraits<TypeTraits::TGetLastType<TArgs...>>;
		auto TupRef = std::tuple<Class2Name::InterfaceParamConvert<TArgs>...>(Args...);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = SendTraits::MakeNames(TupRef);
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Send %s"), *MessageKey.ToString());
			return 0;
		}
#endif

		TraceMessageKey(MessageKey, InSigSrc);

		if (auto Ptr = FindSig(MessageSignals, MessageKey))
		{
			auto Arr = SendTraits::MakeParam(TupRef);
			return SendObjectMessageImpl(Ptr, MessageKey, InSigSrc, Arr, SendTraits::MakeSingleShot(MessageKey, &TupRef));
		}
		return 0;
	}

	template<typename T, typename F>
	FORCEINLINE FGMPKey ListenMessage(const FMSGKEY& MessageId, T* Listener, F&& Func, int32 Times = -1)
	{
		return ListenObjectMessage(MessageId, nullptr, Listener, std::forward<F>(Func), Times);
	}

	template<typename T, typename F>
	FGMPKey ListenObjectMessage(const FMSGKEY& MessageId, FSigSource InSigSrc, T* Listener, F&& Func, int32 Times = -1)
	{
		auto&& MessageKey = ToMessageKey(MessageId);
		using ListenTraits = Hub::TListenArgumentsTraits<F>;
#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = ListenTraits::MakeNames();
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(false, MessageKey, ArgNames, OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Listen %s"), *MessageKey.ToString());
			return 0;
		}

#endif
		GMP_IF_CONSTEXPR(ListenTraits::bIsSingleShot)
		{
			ensureAlways(GIsEditor || !CallbackMarks.Contains(MessageKey));
			CallbackMarks.Add(MessageKey);
		}

		return ListenMessageImpl(MessageKey, InSigSrc, ToSigListenner(Listener), ListenTraits::MakeCallback(this, Listener, std::forward<F>(Func)), Times);
	}

	FORCEINLINE void UnListenMessage(const FMSGKEYFind& MessageKey, FGMPKey InKey)
	{
		if (MessageKey)
			UnListenMessageImpl(MessageKey, InKey);
	}

	FORCEINLINE void UnListenMessage(const FMSGKEYFind& MessageKey, const UObject* Listener)
	{
		if (MessageKey)
			UnListenMessageImpl(MessageKey, Listener);
	}

	FORCEINLINE void UnListenMessage(const FMSGKEYFind& MessageKey, const UObject* Listener, FSigSource InSigSrc)
	{
		if (MessageKey)
			UnListenMessageImpl(MessageKey, Listener, InSigSrc);
	}

	bool IsAlive(const FName& MessageId, FGMPKey Key = 0) const;
	FGMPKey IsAlive(const FName& MessageId, const UObject* Listener, FSigSource InSigSrc = FSigSource::NullSigSrc) const;
	bool IsValidHub() const;
	bool IsResponseOn(FGMPKey Key) const;

	static bool IsSignatureCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, bool bNativeCall = true);
	static bool IsSingleshotCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, bool bNativeCall = true);

public:
	template<typename F, typename... TArgs>
	FGMPKey RequestMessage(const FMSGKEYFind& MessageKey, FSigSource InSigSrc, F&& OnRsp, TArgs&&... Args)
	{
#if !WITH_EDITOR
		if (!MessageKey)
			return {};
#endif

#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = FMessageBody::MakeStaticNamesImpl<std::decay_t<TArgs>...>();
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Request %s"), *MessageKey.ToString());
			return 0;
		}
#endif
		TraceMessageKey(MessageKey, InSigSrc);

		if (auto Ptr = FindSig(MessageSignals, MessageKey))
		{
			FTypedAddresses Arr{FGMPTypedAddr::MakeMsg(Args)...};

#if GMP_WITH_DYNAMIC_CALL_CHECK
			const FArrayTypeNames* RspTypes = &Hub::TListenArgumentsTraits<F>::MakeNames();
#else
			const FArrayTypeNames* RspTypes = nullptr;
#endif
			return RequestMessageImpl(Ptr, MessageKey, InSigSrc, Arr, Hub::DefaultLessTraits::MakeSingleShotImpl(MessageKey, std::forward<F>(OnRsp)), RspTypes);
		}
		return {};
	}

	template<typename... TArgs>
	void ResponseMessage(FGMPKey RequestSequence, TArgs&&... Args)
	{
		FTypedAddresses Arr{FGMPTypedAddr::MakeMsg(Args)...};
		const FArrayTypeNames* RspTypes = nullptr;
#if GMP_WITH_DYNAMIC_CALL_CHECK
		RspTypes = &FMessageBody::MakeStaticNamesImpl<std::decay_t<TArgs>...>();
#endif
		ResponseMessageImpl(true, RequestSequence, Arr, RspTypes);
	}

public:  // for script binding
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, const UObject* Listener, FGMPMessageSig&& Func, int32 Times = -1)
	{
		GMP_DEBUG_LOG(TEXT("ScriptListenMessage ID:[%s] Listener:%s"), *MessageKey.ToString(), *GetNameSafe(Listener));
		return ListenMessageImpl(MessageKey, WatchedObj, Listener, std::move(Func), Times);
	}

	template<typename T, typename R>
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, T* Listener, R (T::*const MemFunc)(FMessageBody&), int32 Times = -1)
	{
		auto Func = [=](FMessageBody& Body) { (Listener->*MemFunc)(Body); };
		return ScriptListenMessage(WatchedObj, MessageKey, Listener, std::move(Func), Times);
	}

	template<typename T, typename R>
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, const T* Listener, R (T::*const MemFunc)(FMessageBody&) const, int32 Times = -1)
	{
		auto Func = [=](FMessageBody& Body) { (Listener->*MemFunc)(Body); };
		return ScriptListenMessage(WatchedObj, MessageKey, Listener, std::move(Func), Times);
	}

	FORCENOINLINE void ScriptUnListenMessage(const FMSGKEYFind& MessageKey, const UObject* Listener)
	{
		GMP_DEBUG_LOG(TEXT("ScriptUnListenMessage ID:[%s] Listener:%s"), *MessageKey.ToString(), *GetNameSafe(Listener));
		UnListenMessage(MessageKey, Listener);
	}

	FORCENOINLINE void ScriptUnListenMessage(const FMSGKEYFind& MessageKey, FGMPKey InKey)
	{
		GMP_DEBUG_LOG(TEXT("ScriptUnListenMessage ID:[%s] Listener:%s"), *MessageKey.ToString(), *InKey.ToString());
		UnListenMessage(MessageKey, InKey);
	}

	bool ScriptNotifyMessage(const FMSGKEY& MessageKey, FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		GMP_DEBUG_LOG(TEXT("ScriptNotifyMessage ID:[%s] SigSource:%s"), *MessageKey.ToString(), *InSigSrc.GetNameSafe());

#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (!ensureWorld(InSigSrc.TryGetUObject(), !MessageKey.IsNone()))
			return false;

		FArrayTypeNames ArgNames;
		ArgNames.Reserve(Param.Num());
		for (auto& a : Param)
			ArgNames.Add(a.TypeName);

		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams, false))
		{
			ensureAlwaysMsgf(false, TEXT("ScriptNotifyMessage SignatureMismatch ID:[%s] SigSource:%s"), *MessageKey.ToString(), *InSigSrc.GetNameSafe());
			return false;
		}
#endif

		TraceMessageKey(MessageKey, InSigSrc);

		auto Ptr = FindSig(MessageSignals, MessageKey);
		return Ptr ? !!NotifyMessageImpl(Ptr, MessageKey, InSigSrc, Param) : true;
	}

#if 1
	FGMPKey ScriptListenMessageCallback(const FMSGKEY& MessageKey, const UObject* Listener, FGMPMessageSig&& Func, int32 Times = -1)
	{
		GMP_CNOTE(!CallbackMarks.Contains(MessageKey), GIsEditor, TEXT("ScriptListenMessageCallback callback none!"));
		CallbackMarks.Add(MessageKey);
		return ListenMessageImpl(MessageKey, FSigSource::NullSigSrc, Listener, std::move(Func), Times);
	}

	FGMPKey ScriptRequestMessage(const FMSGKEY& MessageKey, FTypedAddresses& Param, FGMPMessageSig&& OnRsp, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		auto Ptr = FindSig(MessageSignals, MessageKey);
		return Ptr ? SendObjectMessageImpl(Ptr, MessageKey, InSigSrc, Param, std::move(OnRsp)) : FGMPKey{};
	}
	void ScriptResponeMessage(FGMPKey RspId, FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc, const FArrayTypeNames* RspTypes = nullptr) { ResponseMessageImpl(false, RspId, Param, RspTypes, InSigSrc); }
#endif

public:
#if WITH_EDITOR
	bool GetListeners(FSigSource InSigSrc, FName MessageKey, TArray<FWeakObjectPtr>& OutArray, int32 MaxCnt = 0);
	bool GetCallInfos(const UObject* Listener, FName MessageKey, TArray<FString>& OutArray, int32 MaxCnt = 0);
#endif
	using CallbackMapType = TMap<uint64, FResponeSig>;
	~FMessageHub();
	FMessageHub();

private:
	FGMPSignalMap MessageSignals;

	TSet<FName> CallbackMarks;

	void PushMsgBody(FMessageBody* Body);
	FMessageBody* PopMsgBody();
	TArray<FMessageBody*, TInlineAllocator<8>> MessageBodyStack;

#if GMP_DEBUGGAME
	void TraceMessageKey(const FName& MessageKey, FSigSource InSigSrc);
#else
	FORCEINLINE void TraceMessageKey(const FName& MessageKey, FSigSource InSigSrc) {}
#endif
};

namespace Hub
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	inline auto MakeNullSingleshotSig(const FName& SingleShotId)
	{
		return FResponeSig([](FMessageBody& Body) { UE_LOG(LogGMP, Error, TEXT("ResponeMessage Mismatch")); }, SingleShotId, 0u);
	}
#endif
	template<typename F>
	FResponeSig DefaultLessTraits::MakeSingleShotImpl(const FName& SingleShotId, F&& OnRsp)
	{
		using SingleshotTraits = TListenArgumentsTraits<F>;
		static_assert(!SingleshotTraits::bIsSingleShot && SingleshotTraits::TupleSize > 0, "err");

#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& RspTypes = SingleshotTraits::MakeNames();
		const FArrayTypeNames* OldParams = nullptr;
		if (!ensureAlwaysMsgf(FMessageHub::IsSingleshotCompatible(false, *SingleShotId.ToString(), RspTypes, OldParams), TEXT("RequestMessage Singleshot Mismatch")))
		{
			return MakeNullSingleshotSig(SingleShotId);
		}
#endif

		return FResponeSig([OnRsp{std::forward<F>(OnRsp)}](FMessageBody& Body) { Hub::Invoke<typename SingleshotTraits::Tuple>(OnRsp, Body); }, SingleShotId, FMessageBody::GetNextSequenceID());
	}
}  // namespace Hub
}  // namespace GMP

template<typename... TArgs>
void FGMPResponder::Response(TArgs&&... Args) const
{
	if (GMP_CNOTE(MsgHub->IsValidHub(), GIsEditor, TEXT("Invalid MsgHub!")))
		MsgHub->ResponseMessage(Sequence, std::forward<TArgs>(Args)...);
}

UCLASS()
class GMP_API UGMPManager : public UObject
{
	GENERATED_BODY()
public:
	UGMPManager();

	auto& GetHub() { return MessageHub; }
	auto& GetHub() const { return MessageHub; }

protected:
	GMP::FMessageHub MessageHub;
};
