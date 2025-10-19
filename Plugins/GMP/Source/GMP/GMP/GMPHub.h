//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GMPLocalSharedStorage.h"

#include "Delegates/Delegate.h"
#include "GMPSignals.inl"
#include "GMPSignalsInc.h"
#include "GMPStruct.h"
#include "GMP/GMPPropHolder.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ScriptMacros.h"

#include "GMPHub.generated.h"

#ifndef GMP_REDUCE_IGMPSIGNALS_CAST
#define GMP_REDUCE_IGMPSIGNALS_CAST 0
#endif

class UGMPBPLib;
struct FGMPStructUnion;

namespace GMP
{
class FMessageHub;
using FGMPMessageSig = TGMPFunction<void(FMessageBody&)>;

struct FResponseRec
{
	int64 GetId() const { return Id; }
	FName GetRec() const { return Rec; }

protected:
	FGMPKey Id;
	FName Rec;
};
struct FResponseSig final : public TAttachedCallableStore<FResponseRec, GMP_FUNCTION_PREDEFINED_ALIGN_SIZE>
{
	FResponseSig() = default;
	FResponseSig(FResponseSig&& Val) = default;
	FResponseSig& operator=(FResponseSig&& Val) = default;

	template<typename Functor, GMP_SFINAE_DISABLE_FUNCTIONREF(Functor)>
	FResponseSig(Functor&& Val, FName InRec = NAME_None, uint64 InId = 0u)
		: TAttachedCallableStore(std::forward<Functor>(Val))
	{
		static_assert(TypeTraits::IsSameV<void(FMessageBody&), TypeTraits::TSigFuncType<Functor>>, "sig mismatch");
		Rec = InRec;
		Id = InId;
	}

	void operator()(FMessageBody& Body) const
	{
		CheckCallable();
		return reinterpret_cast<void (*)(void*, FMessageBody&)>(GetCallable())(GetObjectAddress(), Body);
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

	template<typename... TArgs>
	void ResponseAndCear(TArgs&&... Args) const
	{
		Response(Forward<TArgs&&>(Args)...);
		MsgHub = nullptr;
	}

	operator bool() const { return MsgHub != nullptr; }
	FGMPResponder(GMP::FMessageHub* InMsgHub, FName InMsgId, uint64 InSeq)
		: MsgHub(InMsgHub)
		, MsgId(InMsgId)
		, Sequence(InSeq)
	{
	}
	FGMPResponder() = default;

protected:
	mutable GMP::FMessageHub* MsgHub = nullptr;
	UPROPERTY()
	FName MsgId;
	UPROPERTY()
	uint64 Sequence = 0;
};

GMP_MSG_OF(FSimpleDelegate)

namespace GMP
{
using FGMPSignalMap = TMap<FName, FSignalBase>;
template<bool bAdd>
FSignalBase* GetSig(FGMPSignalMap& Map, FName Name);
extern template GMP_API FSignalBase* GetSig<true>(FGMPSignalMap& Map, FName Name);
extern template GMP_API FSignalBase* GetSig<false>(FGMPSignalMap& Map, FName Name);
template<typename T>
FORCEINLINE auto FindSig(T&& Map, FName Name)
{
	return Map.Find(Name);
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

#if GMP_DELEGATE_INVOKABLE
#if !UE_4_26_OR_LATER
	template<typename R, typename... TArgs, size_t... Is>
	FORCEINLINE_DEBUGGABLE void InvokeImpl(const TBaseDelegate<R, TArgs...>& Func, FMessageBody& Body, std::tuple<TArgs...>*, std::index_sequence<Is...>*)
	{
		static_assert(sizeof...(TArgs) == sizeof...(Is), "mismatch");
#if GMP_WITH_DYNAMIC_CALL_CHECK
		GMP_CHECK_SLOW(Body.GetParamCount() >= sizeof...(TArgs));
#endif
		Func.ExecuteIfBound(Body.GetParamVerify<TArgs>(Is)...);
	}
#else
	template<typename R, typename... TArgs, size_t... Is>
	FORCEINLINE_DEBUGGABLE void InvokeImpl(const TDelegate<R(TArgs...)>& Func, FMessageBody& Body, std::tuple<TArgs...>*, std::index_sequence<Is...>*)
	{
		static_assert(sizeof...(TArgs) == sizeof...(Is), "mismatch");
#if GMP_WITH_DYNAMIC_CALL_CHECK
		GMP_CHECK_SLOW(Body.GetParamCount() >= sizeof...(TArgs));
#endif
		Func.ExecuteIfBound(Body.GetParamVerify<TArgs>(Is)...);
	}
#endif
#endif

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
		FGMPResponder Info{InMsgHub, Body.MessageKey(), (uint64)(int64)Body.Sequence()};
		Func(Body.GetParamVerify<TArgs>(Is)..., Info);
	}

	template<typename Tup, size_t... Is>
	FTypedAddresses MakeParamFromTuple(Tup& InTup, const std::index_sequence<Is...>&)
	{
		return FTypedAddresses{FGMPTypedAddr::MakeMsg(std::get<Is>(InTup))...};
	}

	template<typename Tup, size_t... Is>
	FGMPPropStackRefArray AsPropRefArrayFromTuple(Tup& InTup, const std::index_sequence<Is...>&)
	{
		return FGMPPropStackRefArray{FGMPPropStackRef::MakePropStackRef(std::get<Is>(InTup))...};
	}
	template<typename StructType>
	FORCEINLINE FGMPPropStackRefArray AsPropRefArrayFromStruct(const StructType& InData)
	{
		return FGMPPropStackRef::MakePropStackRefArray(InData);
	}
	FORCEINLINE FGMPPropStackRefArray AsPropRefArrayFromStruct(const void* InAddr, const UScriptStruct* InStruct)
	{
		return FGMPPropStackRef::MakePropStackRefArray(InAddr, InStruct);
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
			auto Func = [=](ForwardParam<TArgs>... Args) { return (Listener->*Op)(static_cast<TArgs>(Args)...); };
			return MyTraits::MakeCallback(InMsgHub, std::move(Func), std::conditional_t<bIsSingleShot, std::true_type, std::false_type>());
		}
		template<typename T, typename R, typename F, typename... TArgs>
		static decltype(auto) MakeCallback(FMessageHub* InMsgHub, T* Listener, R (F::*Op)(TArgs...) const)
		{
			GMP_CHECK_SLOW(Listener);
			auto Func = [=](ForwardParam<TArgs>... Args) { return (Listener->*Op)(static_cast<TArgs>(Args)...); };
			return MyTraits::MakeCallback(InMsgHub, std::move(Func), std::conditional_t<bIsSingleShot, std::true_type, std::false_type>());
		}
		static decltype(auto) MakeNames() { return FMessageBody::MakeStaticNames((Tuple*)nullptr, std::make_index_sequence<TupleSize - (bIsSingleShot ? 1 : 0)>()); }
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
		static FGMPPropStackRefArray AsPropRefArray(Tup& InTup)
		{
			return AsPropRefArrayFromTuple(InTup, std::make_index_sequence<std::tuple_size<Tup>::value>());
		}
		template<typename Tup>
		static decltype(auto) MakeNames(Tup& InTup)
		{
			return FMessageBody::MakeStaticNames((Tup*)nullptr, std::make_index_sequence<std::tuple_size<Tup>::value>());
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
		static auto AsPropRefArray(Tup& InTup)
		{
			const auto TupleSize = std::tuple_size<Tup>::value;
			static_assert(TupleSize > 0, "err");
			return AsPropRefArrayFromTuple(InTup, std::make_index_sequence<TupleSize - 1>());
		}
		template<typename Tup>
		static decltype(auto) MakeNames(Tup& InTup)
		{
			const auto TupleSize = std::tuple_size<Tup>::value;
			static_assert(TupleSize > 0, "err");
			return FMessageBody::MakeStaticNames((Tup*)nullptr, std::make_index_sequence<TupleSize - 1>());
		}

		template<typename F>
		static FResponseSig MakeSingleShotImpl(const FName& SingleShotId, F&& OnRsp);

		template<typename Tup>
		static FResponseSig MakeSingleShot(const FName& SingleShotId, Tup* InTup)
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
	struct TSendArgumentsTraits<LastType, std::enable_if_t<TypeTraits::TIsCallable<LastType>::value || TypeTraits::TIsUnrealDelegate<LastType>::value>> : public DefaultLessTraits
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
			if (!ensureAlwaysMsgf(DoesSignatureCompatible(true, Body.MessageKey(), ArgNames, OldParams, bNative), TEXT("FMessageHub::ApplyMessageBoy SignatureMismatch Key:[%s]"), *Body.MessageKey().ToString()))
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
	struct GMP_API FTagTypeSetter
	{
		FTagTypeSetter(const TCHAR* Type);
		~FTagTypeSetter();
		static TOptional<const TCHAR*> GetType();
	};

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
	static FORCEINLINE std::enable_if_t<IsCollectionBase<T>, FSigCollection*> ToSigListener(T* InObj)
	{
		static_assert(!std::is_base_of<UObject, T>::value, "UObject types should inherit from IGMPSignalsHandle!");
		return InObj;
	}

	template<typename T>
	static FORCEINLINE std::enable_if_t<!IsCollectionBase<T>, FSigListener> ToSigListener(T* InObj)
	{
		static_assert(std::is_base_of<UObject, T>::value, "Only UObject based or GMPSignals::FSigCollection based are supported.");
		return {InObj};
	}

	// Listen
	FGMPKey ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigListener Listener, FGMPMessageSig&& Func, FGMPListenOptions Options = {});
	FGMPKey ListenMessageImpl(const FName& MessageKey, FSigSource InSigSrc, FSigCollection* Listener, FGMPMessageSig&& Func, FGMPListenOptions Options = {});

	// Unbind
	void UnbindMessageImpl(const FName& MessageKey, FGMPKey InKey);
	void UnbindMessageImpl(const FName& MessageKey, const UObject* Listener = nullptr);
	void UnbindMessageImpl(const FName& MessageKey, const UObject* Listener, FSigSource InSigSrc);
	// Notify
	FGMPKey NotifyMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param);
	// Request
	FGMPKey RequestMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponseSig&& Sig, const FArrayTypeNames* RspTypes = nullptr);
	// Respone
	void ResponseMessageImpl(FGMPKey RequestSequence, FTypedAddresses& Param, const FArrayTypeNames* RspTypes = nullptr, FSigSource InSigSrc = FSigSource::NullSigSrc, const TCHAR* Tag = nullptr);

private:
	//////////////////////////////////////////////////////////////////////////
	bool IsAlive(const FSignalBase* Ptr) const;
	// Send
	FORCEINLINE FGMPKey SendObjectMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, std::nullptr_t) { return NotifyMessageImpl(Ptr, MessageKey, InSigSrc, Param); }
	FORCEINLINE FGMPKey SendObjectMessageImpl(FSignalBase* Ptr, const FName& MessageKey, FSigSource InSigSrc, FTypedAddresses& Param, FResponseSig&& OnRsp) { return RequestMessageImpl(Ptr, MessageKey, InSigSrc, Param, std::move(OnRsp)); }
#if GMP_WITH_MSG_HOLDER
	void StoreObjectMessageImpl(FSignalBase* Ptr, FSigSource InSigSrc, const FGMPPropStackRefArray& Params, int32 Flags = 0);
	int32 RemoveObjectMessageImpl(FSignalBase* Ptr, FSigSource InSigSrc);
	static FTypedAddresses AsTypedAddresses(const FGMPStructUnion* InData);
#endif

	template<bool bWarn>
	FORCEINLINE_DEBUGGABLE bool ScriptNotifyMessageImpl(const FMSGKEY& MessageKey, FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		//GMP_DEBUG_LOG(TEXT("%sNotifyMessage Key:[%s] SigSource:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *InSigSrc.GetNameSafe());
		if (!VerifyScriptMessage(MessageKey, Param, InSigSrc))
			return false;

		TraceMessageKey(MessageKey, InSigSrc);
		if (auto Ptr = FindSig(MessageSignals, MessageKey))
		{
			return !!NotifyMessageImpl(Ptr, MessageKey, InSigSrc, Param);
		}
#if WITH_EDITOR
		GMP_IF_CONSTEXPR(bWarn)
		{
			GMP_CWARNING(ShouldWarningNoListeners(), TEXT("no listeners when %s(MSGKEY(\"%s\"))"), *FString(__func__), *MessageKey.ToString());
		}
#endif
		return false;
	}

	template<int32 Flags, typename... TArgs>
	FGMPKey SendObjectMessageWrapper(const FMSGKEYFind& MessageKey, FSigSource InSigSrc, TArgs&&... Args)
	{
		FGMPKey Ret;
#if !WITH_EDITOR
		if (!MessageKey)
			return 0;
#endif
		using SendTraits = Hub::TSendArgumentsTraits<TypeTraits::TGetLastType<TArgs...>>;
		using TupleType = std::tuple<Class2Name::InterfaceParamConvert<TArgs>...>;
		auto TupRef = TupleType(Args...);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = SendTraits::MakeNames(TupRef);
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams, GetNativeTagType()))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Send %s"), *MessageKey.ToString());
			return Ret;
		}
#endif
		TraceMessageKey(MessageKey, InSigSrc);

		auto Ptr = GetSig<(!!Flags && !SendTraits::bIsSingleShot)>(MessageSignals, MessageKey);
#if GMP_WITH_MSG_HOLDER
		bool bIsAlive = IsAlive(Ptr);
#endif
		GMP_IF_CONSTEXPR(SendTraits::bIsSingleShot)
		{
			if (!ensure(Ptr))
			{
				GMP_WARNING(TEXT("response for %s does not existed!"), *MessageKey.ToString());
			}
		}
		if (Ptr)
		{
			auto Arr = SendTraits::MakeParam(TupRef);
			Ret = SendObjectMessageImpl(Ptr, MessageKey, InSigSrc, Arr, SendTraits::MakeSingleShot(MessageKey, &TupRef));
		}
#if WITH_EDITOR
		else
		{
			GMP_CWARNING(Flags == 0 && ShouldWarningNoListeners(), TEXT("no listeners when %s(MSGKEY(\"%s\"))"), *FString(__func__), *MessageKey.ToString());
		}
#endif
#if GMP_WITH_MSG_HOLDER
		GMP_IF_CONSTEXPR(Flags != 0 && !SendTraits::bIsSingleShot)
		{
			GMP_IF_CONSTEXPR(Flags == 1)
			{
				if (!bIsAlive)
					StoreObjectMessageImpl(Ptr, InSigSrc, SendTraits::AsPropRefArray(TupRef), Flags);
			}
			else
			{
				StoreObjectMessageImpl(Ptr, InSigSrc, SendTraits::AsPropRefArray(TupRef), Flags);
			}
		}
#endif
		return Ret;
	}

public:
#if GMP_WITH_DYNAMIC_CALL_CHECK && WITH_EDITOR
	// Let MessageTagsEditorModule to add MessageTag at runtime
	using FOnUpdateMessageTagDelegate = TDelegate<void(const FString&, const FArrayTypeNames*, const FArrayTypeNames*, const TCHAR*)>;
	static void InitMessageTagBinding(FOnUpdateMessageTagDelegate&& InBinding);
#endif

	template<typename... TArgs>
	FORCEINLINE FGMPKey SendMessage(const FMSGKEYFind& MessageKey, TArgs&&... Args)
	{
		return SendObjectMessage(MessageKey, nullptr, std::forward<TArgs>(Args)...);
	}

	template<typename... TArgs>
	FORCEINLINE FGMPKey SendObjectMessage(const FMSGKEYFind& MessageKey, FSigSource InSigSrc, TArgs&&... Args)
	{
		return SendObjectMessageWrapper<0>(MessageKey, InSigSrc, Forward<TArgs>(Args)...);
	}

#if GMP_WITH_MSG_HOLDER
	template<typename... TArgs>
	FORCEINLINE FGMPKey StoreObjectMessage(const FMSGKEYFind& MessageKey, FSigSource InSigSrc, TArgs&&... Args)
	{
		return SendObjectMessageWrapper<-1>(MessageKey, InSigSrc, Forward<TArgs>(Args)...);
	}
	template<typename... TArgs>
	FORCEINLINE FGMPKey OnceObjectMessage(const FMSGKEY& MessageKey, FSigSource InSigSrc, TArgs&&... Args)
	{
		return SendObjectMessageWrapper<1>(FMSGKEYFind(MessageKey), InSigSrc, Forward<TArgs>(Args)...);
	}
	FORCEINLINE int32 RemoveStoredObjectMessage(const FMSGKEY& MessageKey, FSigSource InSigSrc)
	{
		if(auto Ptr = FindSig(MessageSignals, MessageKey))
		{
			return RemoveObjectMessageImpl(Ptr, InSigSrc);
		}
		return 0;
	}
#endif

	template<typename T, typename F>
	FORCEINLINE FGMPKey ListenMessage(const FMSGKEY& MessageId, T* Listener, F&& Func, FGMPListenOptions Options = {})
	{
		return ListenObjectMessage(MessageId, nullptr, Listener, std::forward<F>(Func), Options);
	}

	template<typename T, typename F>
	FGMPKey ListenObjectMessage(const FMSGKEY& MessageId, FSigSource InSigSrc, T* Listener, F&& Func, FGMPListenOptions Options = {})
	{
		auto&& MessageKey = ToMessageKey(MessageId);
		using ListenTraits = Hub::TListenArgumentsTraits<F>;
#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = ListenTraits::MakeNames();
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(false, MessageKey, ArgNames, OldParams, GetNativeTagType()))
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

		return ListenMessageImpl(MessageKey, InSigSrc, ToSigListener(Listener), ListenTraits::MakeCallback(this, Listener, std::forward<F>(Func)), Options);
	}

	FORCEINLINE void UnbindMessage(const FMSGKEYFind& MessageKey, FGMPKey InKey)
	{
		if (MessageKey)
			UnbindMessageImpl(MessageKey, InKey);
	}

	FORCEINLINE void UnbindMessage(const FMSGKEYFind& MessageKey, const UObject* Listener)
	{
		if (MessageKey)
			UnbindMessageImpl(MessageKey, Listener);
	}

	FORCEINLINE void UnbindMessage(const FMSGKEYFind& MessageKey, const UObject* Listener, FSigSource InSigSrc)
	{
		if (MessageKey)
			UnbindMessageImpl(MessageKey, Listener, InSigSrc);
	}

	bool IsAlive(const FName& MessageId, FGMPKey Key = 0) const;
	FGMPKey IsAlive(const FName& MessageId, const UObject* Listener, FSigSource InSigSrc = FSigSource::NullSigSrc) const;
	bool IsValidHub() const;
	bool IsResponseOn(FGMPKey Key) const;

	static const TCHAR* GetNativeTagType();
	static const TCHAR* GetScriptTagType();
	static const TCHAR* GetBlueprintTagType();

	static bool IsSignatureCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, const TCHAR* TagType = nullptr);
	static bool IsSingleshotCompatible(bool bCall, const FName& MessageId, const FArrayTypeNames& TypeNames, const FArrayTypeNames*& OldTypes, const TCHAR* TagType = nullptr);

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
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams, GetNativeTagType()))
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
#if WITH_EDITOR
		GMP_CWARNING(ShouldWarningNoListeners(), TEXT("no listeners when %s(MSGKEY(\"%s\"))"), *FString(__func__), *MessageKey.ToString());
#endif
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
		ResponseMessageImpl(RequestSequence, Arr, RspTypes, FSigSource::NullSigSrc, FMessageHub::GetNativeTagType());
	}

public:  // for script binding
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, const UObject* Listener, FGMPMessageSig&& Func, FGMPListenOptions Options = {})
	{
		//GMP_DEBUG_LOG(TEXT("%sListenMessage Key:[%s] Listener:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *GetNameSafe(Listener));
		return ListenMessageImpl(MessageKey, WatchedObj, Listener, std::move(Func), Options);
	}

	template<typename T, typename R>
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, T* Listener, R (T::*const MemFunc)(FMessageBody&), FGMPListenOptions Options = {})
	{
		return ScriptListenMessage(WatchedObj, MessageKey, Listener, [=](FMessageBody& Body) { (Listener->*MemFunc)(Body); }, Options);
	}

	template<typename T, typename R>
	FGMPKey ScriptListenMessage(FSigSource WatchedObj, const FMSGKEY& MessageKey, const T* Listener, R (T::*const MemFunc)(FMessageBody&) const, FGMPListenOptions Options = {})
	{
		return ScriptListenMessage(WatchedObj, MessageKey, Listener, [=](FMessageBody& Body) { (Listener->*MemFunc)(Body); }, Options);
	}

	FORCENOINLINE void ScriptUnbindMessage(const FMSGKEYFind& MessageKey, const UObject* Listener)
	{
		//GMP_DEBUG_LOG(TEXT("%sUnbindMessage Key:[%s] Listener:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *GetNameSafe(Listener));
		UnbindMessage(MessageKey, Listener);
	}

	FORCENOINLINE void ScriptUnbindMessage(const FMSGKEYFind& MessageKey, FGMPKey InKey)
	{
		//GMP_DEBUG_LOG(TEXT("%sUnbindMessage Key:[%s] Listener:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *InKey.ToString());
		UnbindMessage(MessageKey, InKey);
	}

	bool VerifyScriptMessage(const FMSGKEY& MessageKey, FTypedAddresses& Param, FSigSource InSigSrc)
	{
#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (!ensureWorld(InSigSrc.TryGetUObject(), !MessageKey.IsNone()))
			return false;

		FArrayTypeNames ArgNames;
		ArgNames.Reserve(Param.Num());
		for (auto& a : Param)
			ArgNames.Add(a.TypeName);

#if GMP_TRACE_MSG_STACK
			//GMP::FMessageHub::FGMPTracker MsgTracker(MessageKey, FString(__func__));
#endif
		const FArrayTypeNames* OldParams = nullptr;
		if (!IsSignatureCompatible(true, MessageKey, ArgNames, OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("%sNotifyMessage SignatureMismatch Key:[%s] SigSource:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *InSigSrc.GetNameSafe());
			return false;
		}
#endif
		return true;
	}
	bool ScriptNotifyMessage(const FMSGKEY& MessageKey, FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		//
		return ScriptNotifyMessageImpl<true>(MessageKey, Param, InSigSrc);
	}
#if GMP_WITH_MSG_HOLDER
	bool ScriptStoreMessage(const FMSGKEY& MessageKey, FGMPPropStackRefArray& Params, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		FTypedAddresses Arr;
		bool Ret = ScriptNotifyMessageImpl<false>(MessageKey, FGMPTypedAddr::FromHolderArray(Arr, Params), InSigSrc);
		StoreObjectMessageImpl(GetSig<true>(MessageSignals, MessageKey), InSigSrc, MoveTemp(Params));
		return Ret;
	}
#endif
	FGMPKey ScriptRequestMessage(const FMSGKEY& MessageKey, FTypedAddresses& Param, FGMPMessageSig&& OnRsp, FSigSource InSigSrc = FSigSource::NullSigSrc)
	{
		//GMP_DEBUG_LOG(TEXT("%sRequestMessage Key:[%s] SigSource:%s"), FTagTypeSetter::GetType().Get(TEXT("Script")), *MessageKey.ToString(), *InSigSrc.GetNameSafe());
		if (!VerifyScriptMessage(MessageKey, Param, InSigSrc))
			return {};
		TraceMessageKey(MessageKey, InSigSrc);

		if (auto Ptr = FindSig(MessageSignals, MessageKey))
		{
			return SendObjectMessageImpl(Ptr, MessageKey, InSigSrc, Param, std::move(OnRsp));
		}
#if WITH_EDITOR
		GMP_CWARNING(ShouldWarningNoListeners(), TEXT("no listeners when %s(MSGKEY(\"%s\"))"), *FString(__func__), *MessageKey.ToString());
#endif
		return FGMPKey{};
	}

	void ScriptResponseMessage(FGMPKey RspId, FTypedAddresses& Param, FSigSource InSigSrc = FSigSource::NullSigSrc, const FArrayTypeNames* RspTypes = nullptr) { ResponseMessageImpl(RspId, Param, RspTypes, InSigSrc); }

	FGMPKey ScriptListenMessageCallback(const FMSGKEY& MessageKey, const UObject* Listener, FGMPMessageSig&& Func, FGMPListenOptions Options = {})
	{
		GMP_CNOTE(!CallbackMarks.Contains(MessageKey), GIsEditor, TEXT("ScriptListenMessageCallback callback none!"));
		CallbackMarks.Add(MessageKey);
		return ListenMessageImpl(MessageKey, FSigSource::NullSigSrc, Listener, std::move(Func), Options);
	}

public:
#if WITH_EDITOR
	bool GetListeners(FSigSource InSigSrc, FName MessageKey, TArray<FWeakObjectPtr>& OutArray, int32 MaxCnt = 0);
	bool GetCallInfos(const UObject* Listener, FName MessageKey, TArray<FString>& OutArray, int32 MaxCnt = 0);
#endif

	using CallbackMapType = TMap<uint64, FResponseSig>;
	~FMessageHub();
	FMessageHub();

private:
	FGMPSignalMap MessageSignals;

	TSet<FName> CallbackMarks;
	void PushMsgBody(FMessageBody* Body);
	FMessageBody* PopMsgBody();
	TArray<FMessageBody*, TInlineAllocator<8>> MessageBodyStack;

#if GMP_TRACE_MSG_STACK
private:
	friend class MSGKEY_TYPE;

	void TraceMessageKey(const FName& MessageKey, FSigSource InSigSrc);
#else
	FORCEINLINE void TraceMessageKey(const FName& MessageKey, FSigSource InSigSrc) {}
#endif
	static bool ShouldWarningNoListeners();
};

namespace Hub
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	inline auto MakeNullSingleShotSig(const FName& SingleShotId)
	{
		return FResponseSig([](FMessageBody& Body) { GMP_ERROR(TEXT("ResponeMessage Mismatch")); }, SingleShotId, 0u);
	}
#endif
	template<typename F>
	FResponseSig DefaultLessTraits::MakeSingleShotImpl(const FName& SingleShotId, F&& OnRsp)
	{
		using SingleshotTraits = TListenArgumentsTraits<F>;
		static_assert(!SingleshotTraits::bIsSingleShot && SingleshotTraits::TupleSize > 0, "err");

#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& RspTypes = SingleshotTraits::MakeNames();
		const FArrayTypeNames* OldParams = nullptr;
		if (!ensureAlwaysMsgf(FMessageHub::IsSingleshotCompatible(false, *SingleShotId.ToString(), RspTypes, OldParams, FMessageHub::GetNativeTagType()), TEXT("RequestMessage Singleshot Mismatch")))
		{
			return MakeNullSingleShotSig(SingleShotId);
		}
#endif

		return FResponseSig([OnRsp{std::forward<F>(OnRsp)}](FMessageBody& Body) { Hub::Invoke<typename SingleshotTraits::Tuple>(OnRsp, Body); }, SingleShotId, FMessageBody::GetNextSequenceID());
	}
}  // namespace Hub
}  // namespace GMP

template<typename... TArgs>
void FGMPResponder::Response(TArgs&&... Args) const
{
	if (MsgHub && GMP_CNOTE(MsgHub->IsValidHub(), GIsEditor, TEXT("Invalid MsgHub!")))
		MsgHub->ResponseMessage(Sequence, std::forward<TArgs>(Args)...);
}

UCLASS()
class GMP_API UGMPManager : public UObject
{
	GENERATED_BODY()
public:
	auto& GetHub() { return MessageHub; }
	auto& GetHub() const { return MessageHub; }

protected:
	GMP::FMessageHub MessageHub;
};
