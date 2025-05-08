//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPClass2Name.h"
#include "GMPClass2Prop.h"
#include "GMPReflection.h"
#include "GMPSignals.inl"
#include "UnrealCompatibility.h"

#include "GMPStruct.generated.h"

#define GMP_MSG_OF(T)  \
	GMP_RAW_NAME_OF(T) \
	GMP_NULL_PROPERTY_OF(T)

namespace GMP
{
namespace Meta
{
	template<typename T, bool bExactType = true, typename V = void>
	struct TGMPTypeMeta
	{
		using DT = Class2Name::InterfaceTypeConvert<std::decay_t<T>>;
		static auto GetFName()
		{
			static auto TypeName = TClass2Name<DT, bExactType>::GetFName();
#if WITH_EDITOR && 0  // register property for this type?
			Class2Prop::FindOrAddProperty(TypeName, TClass2Prop<DT, bExactType>::GetProperty());
#endif
			return TypeName;
		}
	};

#if GMP_WITH_TYPE_INFO_EXTENSION
	// adl extension
	namespace ADL
	{
		template<typename T>
		using TGMPTraitsTypeMeta = TGMPTypeMeta<Class2Name::InterfaceTypeConvert<std::decay_t<T>>>;
		std::nullptr_t GMPTypeMetaTag(...);
		template<typename T>
		TGMPTraitsTypeMeta<T> GMPTypeMetaAdl(const T&, std::nullptr_t);
	}  // namespace ADL

	template<typename T>
	auto GetGMPTypeMeta()
	{
		using GMP_DECAY_TYPE_T = std::decay_t<T>;
		using ADL::GMPTypeMetaAdl;
		using ADL::GMPTypeMetaTag;
		using FGMPTypeMeta = decltype(GMPTypeMetaAdl(std::declval<GMP_DECAY_TYPE_T>(), GMPTypeMetaTag(std::declval<GMP_DECAY_TYPE_T>())));

		return FGMPTypeMeta{};
	}
#define GMP_TYPE_META(T) decltype(GMP::Meta::GetGMPTypeMeta<T>())
#else
#define GMP_TYPE_META(T) GMP::Meta::TGMPTypeMeta<std::decay_t<T>>
#endif
	template<typename T>
	static T DummyRet = {};

	template<typename T>
	void VerifyGMPDummy(T& In)
	{
#if WITH_EDITOR
//	static const T DummyShadow = {};
//	ensure(In == DummyShadow);
//	In = DummyShadow;
#endif
	}

	template<typename T, typename = void>
	struct TGMPDummyDefault
	{
		static T& GetValueRef()
		{
			auto& Val = DummyRet<T>;
			VerifyGMPDummy(Val);
			return Val;
		}
	};
}  // namespace Meta
}  // namespace GMP

UCLASS()
class UGMPPlaceHolder final : public UObject
{
	GENERATED_BODY()
public:
};

USTRUCT(BlueprintType, meta = (HiddenByDefault = true))
struct GMP_API FGMPTypedAddr
{
	GENERATED_BODY()
public:
	UPROPERTY()
	uint64 Value = 0;

#if GMP_WITH_TYPENAME
	FName TypeName;
#endif

	struct FPropertyValuePair
	{
		FProperty* Prop = nullptr;
		uint8* Addr = nullptr;
		FPropertyValuePair() = default;

		FPropertyValuePair(FProperty* InProp, void* InAddr)
			: Prop(InProp)
			, Addr(static_cast<uint8*>(InAddr))
		{
			Prop->InitializeValue_InContainer(Addr);
		}
		FPropertyValuePair(const FPropertyValuePair&) = delete;
		FPropertyValuePair(FPropertyValuePair&& Rhs)
		{
			Prop = Rhs.Prop;
			Addr = Rhs.Addr;
			Rhs.Prop = nullptr;
			Rhs.Addr = nullptr;
		}

		~FPropertyValuePair()
		{
			if (Prop && Addr)
			{
				Prop->DestroyValue_InContainer(Addr);
			}
		}
	};

private:
#if GMP_WITH_DYNAMIC_TYPE_CHECK
	template<typename V, typename T>
	std::enable_if_t<!GMP::TypeTraits::IsSameV<T, bool>, void> ToType(const V& v, T& t) const
	{
		t = static_cast<T>(v);
	}
	template<typename V, typename T>
	std::enable_if_t<GMP::TypeTraits::IsSameV<T, bool>, void> ToType(const V& v, T& t) const
	{
		t = (v != 0);
	}
#endif
	// clang-format off
	struct DispatchArithmetic {};
	struct DispatchScopedEnum {};
	struct DispatchStruct {};
	struct DispatchInterface {};
	struct DispatchClass {};
	struct DispatchObject {};
	// clang-format on

	template<typename TargetType>
	static inline auto& GetValueRef()
	{
		return GMP::Meta::TGMPDummyDefault<TargetType>::GetValueRef();
	}

#if GMP_WITH_DYNAMIC_TYPE_CHECK
	template<typename T>
	bool ShouldSkipValidate() const
	{
		if (TypeName == NAME_GMPSkipValidate)
		{
			using FGMPTypeMeta = GMP_TYPE_META(T);
			GMP_LOG(TEXT("type validata skiped %s"), *FGMPTypeMeta::GetFName().ToString());
			return true;
		}
		return false;
	}
#endif
	bool MatchEnum(uint32 Bytes) const;

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchArithmetic) const
	{
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		using FGMPTypeMeta = GMP_TYPE_META(TargetType);
		static_assert(!ITS::is_scoped_enum<TargetType>::value, "err");
		const bool bIsEnum = std::is_enum<TargetType>::value;
		const bool bIsInteger = std::is_integral<TargetType>::value;
		using UnderlyingType = typename GMP::Class2Name::TTraitsEnum<TargetType>::underlying_type;
		using FUnderlyingTypeMeta = GMP_TYPE_META(UnderlyingType);
		if (!(TypeName == FGMPTypeMeta::GetFName() || ShouldSkipValidate<TargetType>() || (bIsEnum && TypeName == FUnderlyingTypeMeta::GetFName()) || (bIsInteger && MatchEnum(sizeof(TargetType)))))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString());
			return GetValueRef<TargetType>();
		}
#endif
		return *ToTypedAddr<TargetType>();
	}

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchScopedEnum) const
	{
		static_assert(sizeof(TargetType) == sizeof(uint8), "err");
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		static auto EnumName = GMP_TYPE_META(TargetType)::GetFName();
		if (!(TypeName == EnumName || TypeName == GMP_TYPE_META(uint8)::GetFName() || TypeName == GMP_TYPE_META(int8)::GetFName() || ShouldSkipValidate<TargetType>()))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *EnumName.ToString());
			return GetValueRef<TargetType>();
		}
#endif
		return *ToTypedAddr<TargetType>();
	}

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchInterface) const
	{
		using ScriptIncType = Z_GMP_NATIVE_INC_NAME<std::remove_pointer_t<TargetType>>;
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		using FGMPTypeMeta = GMP_TYPE_META(ScriptIncType);
		if (!(TypeName == FGMPTypeMeta::GetFName() || ShouldSkipValidate<TargetType>()))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString());
			return GetValueRef<TargetType>();
		}
		else
#endif
#if GMP_WITH_NO_CLASS_CHECK
		{
			return ToTypedAddr<ScriptIncType>()->GetNativeAddr();
		}
#else
		{
			auto Ptr = ToTypedAddr<ScriptIncType>();
#if GMP_WITH_DYNAMIC_TYPE_CHECK
			if (ensureMsgf(Ptr, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString()))
#elif GMP_WITH_TYPENAME
			if (ensureMsgf(Ptr, TEXT("type error %s"), *TypeName.ToString()))
#else
			if (ensureMsgf(Ptr, TEXT("type error")))
#endif
			{
				return Ptr->GetNativeAddr();
			}
		}
		return GetValueRef<TargetType>();
#endif
	}

	bool MatchObjectClass(UClass* InClass) const;

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchClass) const
	{
		using FGMPTypeMeta = GMP_TYPE_META(std::remove_pointer_t<TargetType>);
		using FTraitsClassType = GMP::Class2Name::TTraitsClassType<std::remove_pointer_t<std::decay_t<TargetType>>>;
		static_assert(FTraitsClassType::value, "err");
		using ClassType = typename FTraitsClassType::class_type;
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		const bool bIsBase = GMP::TypeTraits::IsSameV<TargetType, UClass*> || GMP::TypeTraits::IsSameV<ClassType, TSubclassOf<UObject>>;
		if (!(bIsBase || TypeName == FGMPTypeMeta::GetFName() || ShouldSkipValidate<TargetType>() || MatchObjectClass(StaticClass<ClassType>())))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString());
			return GetValueRef<TargetType>();
		}
		else
#endif
#if GMP_WITH_NO_CLASS_CHECK || GMP_WITH_EXACT_OBJECT_TYPE
		{
			return *reinterpret_cast<TargetType*>(ToAddr());
		}
#else
		{
			UClass** Ptr = ToTypedAddr<UClass*>();
			if (ensureMsgf(!(*Ptr) || (*Ptr)->IsChildOf<ClassType>(), TEXT("type error %s--%s"), *GetNameSafe(*Ptr), *GMP::TClass2Name<ClassType>::GetFName().ToString()))
				return *reinterpret_cast<TargetType*>(Ptr);
		}
		return GetValueRef<TargetType>();
#endif
	}

	bool MatchObjectType(UClass* TargetClass) const;

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchObject) const
	{
		using ClassType = std::remove_pointer_t<TargetType>;
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		using FGMPTypeMeta = GMP_TYPE_META(ClassType);
		static_assert(!GMP::TypeTraits::IsSameV<UClass, ClassType>, "err");
		if (!(TypeName == FGMPTypeMeta::GetFName() || ShouldSkipValidate<TargetType>() || MatchObjectType(StaticClass<ClassType>())))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString());
			return GetValueRef<TargetType>();
		}
		else
#endif
#if GMP_WITH_NO_CLASS_CHECK || GMP_WITH_EXACT_OBJECT_TYPE
		{
			return *reinterpret_cast<TargetType*>(ToAddr());
		}
#else
		{
			UObject** Ptr = ToTypedAddr<UObject*>();
			if (!(*Ptr) || ensureMsgf((*Ptr)->IsA<ClassType>(), TEXT("type error %s--%s"), *GetNameSafe((*Ptr)->GetClass()), *GMP::TClass2Name<ClassType>::GetFName().ToString()))
				return *reinterpret_cast<TargetType*>(Ptr);
		}
		return GetValueRef<TargetType>();
#endif
	}

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParamImpl(DispatchStruct) const
	{
#if GMP_WITH_DYNAMIC_TYPE_CHECK
		using FGMPTypeMeta = GMP_TYPE_META(TargetType);
		// FIXME: currently we just check if types are exactly the same
		if (!(TypeName == FGMPTypeMeta::GetFName() || ShouldSkipValidate<TargetType>()) || GMP::Class2Name::TTraitsNativeInterface<TargetType>::IsCompatible(TypeName))
		{
			GMP_VALIDATE_MSGF(false, TEXT("type error %s--%s"), *TypeName.ToString(), *FGMPTypeMeta::GetFName().ToString());
			return GetValueRef<TargetType>();
		}
#endif
		return *ToTypedAddr<TargetType>();
	}

public:
	template<typename TargetType>
	FORCEINLINE TargetType& GetParam() const
	{
		static_assert(!std::is_reference<TargetType>::value, "no need ref");

		using DT = std::decay_t<TargetType>;
		const bool IsPtr = std::is_pointer<DT>::value;
		const bool IsInc = IsPtr && TIsIInterface<std::remove_pointer_t<DT>>::Value;
		const bool IsObj = std::is_base_of<UObject, std::remove_pointer_t<DT>>::value;
		using TraitsArithmetic = GMP::Class2Name::TTraitsArithmetic<DT>;
		const bool IsEnumAsByte = TraitsArithmetic::enum_as_byte;
		const bool IsArithmetic = TraitsArithmetic::value;
#if defined(GMP_WITH_ARITHMETIC_CALLBACK) && GMP_WITH_ARITHMETIC_CALLBACK
		static_assert(IsArithmetic || IsEnumAsByte || IsInc || (IsPtr == IsObj), "type not support");
#else
		static_assert(IsInc || (IsPtr == IsObj), "type not support");
#endif
		using TT = typename TraitsArithmetic::type;
		const bool IsCls = GMP::Class2Name::TTraitsClassType<TT>::value;
		static_assert(!IsArithmetic || sizeof(DT) == sizeof(int8) || sizeof(DT) == sizeof(int32) || (UE_4_22_OR_LATER && sizeof(DT) == sizeof(int64)), "type not support by blueprint");
		using DetectType =
			std::conditional_t<ITS::is_scoped_enum<DT>::value || IsEnumAsByte,
							   DispatchScopedEnum,
							   std::conditional_t<IsArithmetic, DispatchArithmetic, std::conditional_t<IsInc, DispatchInterface, std::conditional_t<IsCls, DispatchClass, std::conditional_t<IsObj, DispatchObject, DispatchStruct>>>>>;
		return GetParamImpl<TT>(DetectType());
	}

public:
	static auto FromAddr(const void* Addr)
	{
		return FGMPTypedAddr
		{
			GMP::TypeTraits::HorribleFromAddr<uint64>(Addr),
#if GMP_WITH_TYPENAME
				NAME_GMPSkipValidate,
#endif
		};
	}

	static auto FromAddr(const void* Addr, const FProperty* Prop)
	{
		return FGMPTypedAddr
		{
			GMP::TypeTraits::HorribleFromAddr<uint64>(Addr),
#if GMP_WITH_TYPENAME
				GMP::Reflection::GetPropertyName(Prop),
#endif
		};
	}

	auto InitializeValue(FProperty* Prop)
	{
		GMP_CHECK_SLOW(Prop);
		auto p = ToAddr();
		Prop->InitializeValue_InContainer(p);
#if GMP_WITH_TYPENAME
		TypeName = GMP::Reflection::GetPropertyName(Prop);
#endif
		return p;
	}

	FORCEINLINE void* ToAddr() const { return GMP::TypeTraits::HorribleToAddr<void*>(Value); }
	FORCEINLINE void SetAddr(const void* Addr) { Value = GMP::TypeTraits::HorribleFromAddr<decltype(Value)>(Addr); }
	void SetAddr(const void* Addr, const FProperty* Prop)
	{
		SetAddr(Addr);
#if GMP_WITH_TYPENAME
		TypeName = GMP::Reflection::GetPropertyName(Prop);
#endif
	}
	void SetAddr(const FPropertyValuePair& Pair) { SetAddr(Pair.Addr, Pair.Prop); }
	template<typename P>
	FORCEINLINE P* ToTypedAddr() const
	{
		return reinterpret_cast<P*>(ToAddr());
	}

	template<typename T>
	GMP_FORCEINLINE_DEBUGGABLE static FGMPTypedAddr MakeMsg(T& t)
	{
		using DT = std::decay_t<T>;
		const bool IsObj = std::is_base_of<UObject, std::remove_pointer_t<DT>>::value;
		const bool IsPtr = std::is_pointer<DT>::value;
		const bool IsInc = IsPtr && TIsIInterface<std::remove_pointer_t<DT>>::Value;
		static_assert(/*IsInc || */ (IsPtr == IsObj), "type not support");
		return FGMPTypedAddr
		{
			GMP::TypeTraits::HorribleFromAddr<uint64>(std::addressof(t)),
#if GMP_WITH_TYPENAME
				GMP_TYPE_META(T)::GetFName()
#endif
		};
	}
};

namespace GMP
{
using FTypedAddresses = TArray<FGMPTypedAddr, TInlineAllocator<8>>;
using FArrayTypeNames = TArray<FName, TInlineAllocator<8>>;

struct GMP_API FMessageBody
{
	template<typename... Ts>
	static const FArrayTypeNames& MakeStaticNamesImpl()
	{
		static FArrayTypeNames Ret{GMP_TYPE_META(Ts)::GetFName()...};
		return Ret;
	}

	FORCEINLINE auto GetSigSource() const { return CurSigSrc.TryGetUObject(); }

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE TargetType& GetParam(int ParamIndex)
	{
		using DT = std::decay_t<TargetType>;

		const bool IsRef = std::is_reference<TargetType>::value;
		const bool IsObj = std::is_base_of<UObject, std::remove_pointer_t<DT>>::value;
		const bool IsPtr = std::is_pointer<DT>::value;
		const bool IsInc = IsPtr && TIsIInterface<std::remove_pointer_t<DT>>::Value;

		static_assert(!IsRef, "no need ref");
		static_assert(IsInc || (IsPtr == IsObj), "type not support");

#if GMP_WITH_DYNAMIC_TYPE_CHECK
		using FGMPTypeMeta = GMP_TYPE_META(DT);
		GMP_VALIDATE_MSGF(ParamIndex >= 0 && ParamIndex < Params.Num(), TEXT("Get Type [%s] out of range[%d][%d]"), *FGMPTypeMeta::GetFName().ToString(), ParamIndex, Params.Num());
#else
		GMP_VALIDATE_MSGF(ParamIndex >= 0 && ParamIndex < Params.Num(), TEXT("out of range[%d][%d]"), ParamIndex, Params.Num());
#endif
		return Params[ParamIndex].GetParam<TargetType>();
	}

	template<typename TargetType>
	GMP_FORCEINLINE_DEBUGGABLE auto& GetParamVerify(int ParamIndex)
	{
		static_assert(std::is_reference<TargetType>::value || std::is_pointer<TargetType>::value || TIsPODType<TargetType>::Value, "err");
		return GetParam<std::decay_t<TargetType>>(ParamIndex);
	}

	static const TArray<FName>* GetMessageTypes(const UObject* InObj, const FMSGKEYAny& MsgKey);
	const TArray<FName>* GetMessageTypes(const UObject* InObj) const { return GetMessageTypes(InObj, MessageId); }

	int GetParamCount() const { return Params.Num(); }

	auto MessageKey() const { return MessageId; }
	auto Parameters() const { return TArray<FGMPTypedAddr>(Params); }
	auto Sequence() const { return SequenceId; }
	auto& GetParams() { return Params; }

	bool IsSignatureCompatible(bool bCall, const FArrayTypeNames*& OldTypes);

	TArray<FGMPTypedAddr> MakeFullParameters(uint8 BodyDataMask, int32& ReserveCnt, TArray<FGMPTypedAddr>& InOutAddrs) const
	{
		TArray<FGMPTypedAddr> Ret;
		Ret.Reserve(Params.Num() + 4);

		if (BodyDataMask & (1 << 0))  // 0x1
		{
			static const UObject* StaticSigSource;
			StaticSigSource = GetSigSource();
			Ret.Add(FGMPTypedAddr::MakeMsg(StaticSigSource));
			++ReserveCnt;
		}
		if (BodyDataMask & (1 << 1))  // 0x2
		{
			Ret.Add(FGMPTypedAddr::MakeMsg(MessageId));
			++ReserveCnt;
		}
		if (BodyDataMask & (1 << 2))  // 0x4
		{
			Ret.Add(FGMPTypedAddr::MakeMsg(SequenceId));
			++ReserveCnt;
		}

		if (BodyDataMask & (1 << 3))  // 0x8
		{
			static auto FromArray = [](TArray<FGMPTypedAddr>& Addr, const FTypedAddresses& InParams) {
				Addr.Reset();
				Addr.Append(InParams);
				return FGMPTypedAddr
				{
					TypeTraits::HorribleFromAddr<uint64>(std::addressof(Addr)),
#if GMP_WITH_TYPENAME
						GMP_TYPE_META(TArray<FGMPTypedAddr>)::GetFName(),
#endif
				};
			};
			Ret.Add(FromArray(InOutAddrs, Params));
			++ReserveCnt;
		}

		Ret.Append(Params);
		return Ret;
	}

#if WITH_EDITOR
	FString MessageToString() const;
	void ToString(FString& Out) const
	{
#if UE_4_23_OR_LATER
		Out.Reset();
		Out.Appendf(TEXT("%s->(%s) @%0.2fs "), *CurSigSrc.GetNameSafe(), *MessageToString(), DebugSeconds);
#else
		Out = FString::Printf(TEXT("%s->(%s) @%0.2fs "), *CurSigSrc.GetNameSafe(), *MessageToString(), DebugSeconds);
#endif
	}
#endif

	static FGMPKey GetNextSequenceID();

protected:
	FMessageBody(FTypedAddresses& InParams, FName InName, FSigSource InSigSrc, FGMPKey Id = {})
		: Params(InParams)
		, MessageId(InName)
		, CurSigSrc(InSigSrc)
		, SequenceId(Id ? Id : FMessageBody::GetNextSequenceID())
#if WITH_EDITOR
		, DebugSeconds(GetTimeSeconds())
#endif
	{
	}

	FMessageBody(const FMessageBody&) = delete;
	FMessageBody& operator=(const FMessageBody&) = delete;

	FTypedAddresses& Params;
	FName MessageId;
	FSigSource CurSigSrc;

	FGMPKey SequenceId;
	friend class FMessageHub;
#if WITH_EDITOR
	float GetTimeSeconds();
	float DebugSeconds = 0.f;
#endif
};
}  // namespace GMP
