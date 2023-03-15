//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPClass2Prop.h"
#include "Templates/Invoke.h"
#include "UObject/Class.h"

class UPackageMap;
namespace GMP
{
namespace Serializer
{
	constexpr auto MaxNetArrayNum = 1024u;

	template<typename T>
	constexpr bool WithNetAr = TStructOpsTypeTraits<T>::WithNetSerializer && !!Class2Prop::TTraitsStruct<T>::value;
	template<typename T>
	struct TArrayElementType
	{
		using ElementType = void;
		using AllocatorType = void;
		enum
		{
			IsDefaultAllocator = 0,
			IsPodType = 0,
		};
	};

	template<typename T, typename InAllocator>
	struct TArrayElementType<TArray<T, InAllocator>>
	{
		static_assert(!TIsTArray<T>::Value, "not supported");
		using ElementType = std::remove_cv_t<T>;
		using AllocatorType = InAllocator;

		enum
		{
			IsDefaultAllocator = TypeTraits::IsSameV<AllocatorType, FDefaultAllocator>,
			IsPodType = TIsPODType<ElementType>::Value,
		};
	};

	template<typename T>
	struct TCustomNetSerializer
	{
		enum
		{
			tag = 1,
			value = 0,
		};
		// static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, T& Data) {}
	};

	template<typename T>
	struct TTraitsPODArray : public TArrayElementType<T>
	{
		enum
		{
			tag = 2,
			value = TIsTArray<T>::Value && TArrayElementType<T>::IsPodType && TArrayElementType<T>::IsDefaultAllocator ? tag : 0,
		};
	};

	template<typename T>
	struct TTraitsArrayNetAr : public TArrayElementType<T>
	{
		enum
		{
			tag = 3,
			value = TIsTArray<T>::Value && WithNetAr<typename TArrayElementType<T>::ElementType> ? tag : 0,
		};
	};

	template<typename T>
	struct TTraitsArrayNormal : public TArrayElementType<T>
	{
		enum
		{
			tag = 4,
			value = TIsTArray<T>::Value && !WithNetAr<typename TArrayElementType<T>::ElementType> ? tag : 0,
		};
	};

	template<typename T>
	struct TTraitsWithoutNetAr
	{
		enum
		{
			tag = 5,
			value = !WithNetAr<std::decay_t<T>> ? tag : 0,
		};
	};

	template<typename T, int Tag = TypeTraits::TDisjunction<TCustomNetSerializer<T>, TTraitsPODArray<T>, TTraitsArrayNetAr<T>, TTraitsArrayNormal<T>, TTraitsWithoutNetAr<T>>::value>
	struct TNetSerializer
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, T& Data) { Prop->NetSerializeItem(Ar, Map, (void*)&Data); }
	};

	template<typename T>
	struct TNetSerializer<T, TCustomNetSerializer<void>::tag>
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, T& Data) { TCustomNetSerializer<T>::NetSerialize(Map, Ar, Prop, Data); }
	};

	template<typename T, typename A>
	struct TNetSerializer<TArray<T, A>, TTraitsPODArray<void>::tag>
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, TArray<T, A>& Array)
		{
			bool bOutSuccess = true;
			int32 ArrayNum = SafeNetSerializeTArray_HeaderOnly<MaxNetArrayNum>(Ar, Array, bOutSuccess);
			if (bOutSuccess)
			{
				Ar.SerializeBits(Array.GetData(), ArrayNum * sizeof(T) * 8);
			}
			bOutSuccess &= !Ar.IsError();
			ensure(bOutSuccess);
		}
	};

	template<typename T, typename A>
	struct TNetSerializer<TArray<T, A>, TTraitsArrayNetAr<void>::tag>
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, TArray<T, A>& Array) { SafeNetSerializeTArray_WithNetSerialize<MaxNetArrayNum>(Ar, Array, Map); }
	};

	template<typename T, typename A>
	struct TNetSerializer<TArray<T, A>, TTraitsArrayNormal<void>::tag>
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, TArray<T, A>& Array)
		{
			static_assert(TTraitsArrayNormal<TArray<T, A>>::value, "err");
			bool bOutSuccess = true;
			int32 ArrayNum = SafeNetSerializeTArray_HeaderOnly<MaxNetArrayNum>(Ar, Array, bOutSuccess);
			if (bOutSuccess)
			{
				auto ItemProp = CastFieldChecked<FArrayProperty>(Prop)->Inner;

				for (int32 idx = 0; idx < ArrayNum && Ar.IsError() == false; ++idx)
					TNetSerializer<T>::NetSerialize(Map, Ar, ItemProp, Array[idx]);
			}
			bOutSuccess |= Ar.IsError();
			ensure(bOutSuccess);
		}
	};

	template<typename T>
	struct TNetSerializer<T, TTraitsWithoutNetAr<void>::tag>
	{
		static void NetSerialize(UPackageMap* Map, FArchive& Ar, FProperty* Prop, T& Data) { Ar << Data; }
	};

#if 1
	template<size_t... Is, typename... TArgs>
	void NetSerializeImpl(UPackageMap* Map, FArchive& Ar, const TArray<FProperty*>& Props, std::index_sequence<Is...>, TArgs&... Args)
	{
		int Temp[] = {0, (TNetSerializer<TArgs>::NetSerialize(Map, Ar, Props[Is], Args), 0)...};
		(void)(Temp);
	}
#else
	// dynamic methods
	template<size_t... Is, typename... TArgs>
	void NetSerializeImpl(UPackageMap* Map, FArchive& Ar, const TArray<FProperty*>& Props, std::index_sequence<Is...>, TArgs&... Args)
	{
		int Temp[] = {0, (UGMPBPLib::NetSerializeProperty(Ar, Props[Is], &Args, Map), 0)...};
		(void)(Temp);
	}
#endif
	template<typename... TArgs>
	FORCEINLINE void NetSerializeWithProps(UPackageMap* Map, FArchive& Ar, const TArray<FProperty*>& Props, TArgs&... Args)
	{
		NetSerializeImpl(Map, Ar, Props, std::make_index_sequence<sizeof...(TArgs)>{}, Args...);
	}

	template<typename... TArgs>
	FORCEINLINE void NetSerialize(FArchive& Ar, TArgs&... Args)
	{
		using MyTraits = Class2Prop::TPropertiesTraits<std::decay_t<TArgs>...>;
		NetSerializeImpl(Ar, MyTraits::GetProperties(), std::make_index_sequence<sizeof...(TArgs)>{}, Args...);
	}

	//////////////////////////////////////////////////////////////////////////
	template<typename A, typename = void>
	struct TArgsSerializer
	{
		template<size_t... Is, typename... TArgs>
		static void SerializeArgs(A& ArgsStorage, std::index_sequence<Is...>, TArgs&... Args);
	};

	template<typename A, typename... TArgs, typename F, size_t... Is, typename... TPayloads>
	FORCEINLINE void SerializedInvokeImpl(A& ArgsStorage, std::tuple<TArgs...>& Tup, const F& Callable, std::index_sequence<Is...> Indexes, TPayloads&&... PlayLoads)
	{
		TArgsSerializer<std::decay_t<A>>::template SerializeArgs(ArgsStorage, Indexes, std::get<Is>(Tup)...);
		Invoke(Callable, std::get<Is>(Tup)..., Forward<TPayloads>(PlayLoads)...);
	}

	template<typename A, typename F, typename... TPayloads>
	void SerializedInvoke(A& ArgsStorage, F&& Callable, TPayloads&&... PlayLoads)
	{
		using TSig = TypeTraits::TSigTraits<F>;
		using TupleType = typename TSig::DecayTuple;
		static_assert(sizeof...(TPayloads) <= TSig::TupleSize, "err");
		TypeTraits::TRemoveTupleLastType<TupleType, sizeof...(TPayloads)> TupleData;
		SerializedInvokeImpl(ArgsStorage, TupleData, Callable, std::make_index_sequence<TSig::TupleSize - sizeof...(TPayloads)>{}, Forward<TPayloads>(PlayLoads)...);
	}

	//////////////////////////////////////////////////////////////////////////
	template<typename T, typename V = void>
	struct TCustomParameterSerializer
	{
		enum
		{
			value = false,
		};
#if 0
	static void ParameterSerialize(FStringView Str, T& Data) { if (!Str.IsEmpty()) TClass2Prop<T>::GetProperty()->ImportText(Str.GetData(), std::addressof(Data), 0, nullptr); }
#endif
	};

	template<typename T, typename V = void>
	struct TParameterSerializer
	{
		static void ParameterSerialize(FStringView Str, T& Data)
		{
			if (!Str.IsEmpty())
				TClass2Prop<T>::GetProperty()->ImportText(Str.GetData(), std::addressof(Data), 0, nullptr);
		}
	};

	template<typename T>
	struct TParameterSerializer<T, std::enable_if_t<TCustomParameterSerializer<T>::value>> : public TCustomParameterSerializer<T>
	{
	};

	template<typename T>
	struct TParameterSerializer<TOptional<T>, void>
	{
		static void ParameterSerialize(const FString& Str, TOptional<T>& Data)
		{
			if (Str.IsEmpty())
			{
				Data.Reset();
				return;
			}

			T Val{};
			TParameterSerializer<T>::ParameterSerialize(Str, Val);
			Data = MoveTemp(Val);
		}
	};

	template<typename T, ESPMode Mode>
	struct TParameterSerializer<TSharedRef<T, Mode>, void>
	{
		static void ParameterSerialize(const FString& Str, TSharedRef<T, Mode>& Data) { TParameterSerializer<T>::ParameterSerialize(Str, *Data); }
	};
	template<typename T, ESPMode Mode>
	struct TParameterSerializer<TSharedPtr<T, Mode>, void>
	{
		static void ParameterSerialize(const FString& Str, TSharedPtr<T, Mode>& Data)
		{
			if (Str.IsEmpty())
			{
				Data.Reset();
				return;
			}

			if (Data.IsValid())
				TParameterSerializer<T>::ParameterSerialize(Str, *Data);
		}
	};

	template<>
	struct TArgsSerializer<TArray<FString>, void>
	{
		template<size_t... Is, typename... TArgs>
		static void SerializeArgs(const TArray<FString>& ArgsStorage, std::index_sequence<Is...>, TArgs&... Args)
		{
			const FString EmptyString;
			int Temp[] = {0, (TParameterSerializer<TArgs>::ParameterSerialize(ArgsStorage.IsValidIndex(Is) ? ArgsStorage[Is] : EmptyString, Args), 0)...};
			(void)(Temp);
		}
	};

	//////////////////////////////////////////////////////////////////////////
	template<typename T, typename V = void>
	struct TCustomArchiveSerializer
	{
		enum
		{
			value = false,
		};
#if 0
	static void ArchiveSerialize(FArchive& Ar, T& Data, int32 Index = -1) { TClass2Prop<T>::GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), std::addressof(Data)); }
#endif
	};

	template<typename T, typename V = void>
	struct TArchiveSerializer
	{
		static void ArchiveSerialize(FArchive& Ar, T& Data, int32 Index = -1) { TClass2Prop<T>::GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), std::addressof(Data)); }
	};

	template<typename T>
	struct TArchiveSerializer<T, std::enable_if_t<TCustomArchiveSerializer<T>::value>> : public TCustomArchiveSerializer<T>
	{
	};

	template<typename A>
	struct TArgsSerializer<A, std::enable_if_t<std::is_base_of<FArchive, A>::value>>
	{
		template<size_t... Is, typename... TArgs>
		static void SerializeArgs(FArchive& ArgsStorage, std::index_sequence<Is...>, TArgs&... Args)
		{
			int Temp[] = {0, (TArchiveSerializer<TArgs>::ArchiveSerialize(ArgsStorage, Args, static_cast<int32>(Is)), 0)...};
			(void)(Temp);
		}
	};

	template<typename T, typename Ar = FArchive, typename = void>
	struct TProxySerializeDetect : public std::false_type
	{
	};
	template<typename T, typename Ar>
	struct TProxySerializeDetect<T, Ar, VoidType<decltype(std::declval<T&>().Serialize(std::declval<Ar&>()))>> : public std::true_type
	{
	};
	template<typename T, typename ArType = FArchive, typename = void>
	struct TSerializerProxy
	{
		static decltype(auto) Serialize(ArType& Ar, T& Value) { return Ar << Value; }
	};
	template<typename T, typename ArType>
	struct TSerializerProxy<T, ArType, std::enable_if_t<TProxySerializeDetect<T, FStructuredArchiveSlot>::value>>
	{
		static decltype(auto) Serialize(ArType& Ar, T& Value) { return Value.Serialize(FStructuredArchiveFromArchive(Ar).GetSlot()); }
	};
	template<typename T, typename ArType>
	struct TSerializerProxy<T, ArType, std::enable_if_t<!TProxySerializeDetect<T, FStructuredArchiveSlot>::value && TProxySerializeDetect<T, ArType>::value>>
	{
		static decltype(auto) Serialize(ArType& Ar, T& Value) { return Value.Serialize(Ar); }
	};

	template<typename T, typename A, typename ArType>
	struct TSerializerProxy<TArray<T, A>, ArType, std::enable_if_t<TProxySerializeDetect<T, ArType>::value>>
	{
		static decltype(auto) Serialize(ArType& Ar, TArray<T, A>& Array)
		{
			Array.CountBytes(Ar);
			using SizeType = typename A::SizeType;
			constexpr SizeType MaxNetArraySerialize = (16 * 1024 * 1024) / sizeof(T);
			SizeType SerializeNum = Ar.IsLoading() ? 0 : Array.Num();
			Ar << SerializeNum;

			if (SerializeNum == 0)
			{
				if (Ar.IsLoading())
				{
					Array.Empty();
				}
				return Ar;
			}

			GMP_CHECK(SerializeNum >= 0);
			if (!Ar.IsError() && SerializeNum > 0 && ensure(!Ar.IsNetArchive() || SerializeNum <= MaxNetArraySerialize))
			{
				struct FArray : public TArray<T, A>
				{
					using TArray<T, A>::ArrayNum;
				};

				if (sizeof(T) == 1 || TCanBulkSerialize<T>::Value)
				{
					static_cast<FArray&>(Array).ArrayNum = SerializeNum;
					if (Ar.IsLoading())
						Array.SetNumUninitialized(Array.Num());
					Ar.Serialize(Array.GetData(), Array.Num() * sizeof(T));
				}
				else if (Ar.IsLoading())
				{
					Array.Empty(SerializeNum);

					for (SizeType i = 0; i < SerializeNum; i++)
					{
						TSerializerProxy<T, ArType>::Serialize(Ar, *::new (Array) T);
					}
				}
				else
				{
					static_cast<FArray&>(Array).ArrayNum = SerializeNum;

					for (SizeType i = 0; i < Array.Num(); i++)
					{
						TSerializerProxy<T, ArType>::Serialize(Ar, Array[i]);
					}
				}
			}
			else
			{
				Ar.SetError();
			}
			return Ar;
		}
	};
}  // namespace Serializer
}  // namespace GMP
