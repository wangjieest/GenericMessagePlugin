//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPReflection.h"
#include "GMPSerializer.h"
#include "GMPUnion.h"
#include "GMPValueOneOf.h"
#include "Misc/FileHelper.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/UnrealTypeTraits.h"

class IHttpResponse;
namespace GMP
{
namespace Json
{
	namespace Serializer
	{
		struct GMP_API FNumericFormatter
		{
			enum ENumericFmt
			{
				None,
				BoolAsBoolean = 1 << 0,
				EnumAsStr = 1 << 1,

				Int64AsStr = 1 << 2,
				Uint64AsStr = 1 << 3,
				IntegerAsStr = Int64AsStr | Uint64AsStr,
				OverflowAsStr = 1 << 4,

				Default = BoolAsBoolean | EnumAsStr,
			};

		protected:
			TGuardValue<ENumericFmt> GuardVal;
			static ENumericFmt NumericFmtType;

		public:
			FNumericFormatter(ENumericFmt InType);
			static const auto& GetType() { return NumericFmtType; }
		};

		struct GMP_API FDataTimeFormatter
		{
			enum EFmtType
			{
				Object,
				Unixtimestamp,
				UnixtimestampStr,
				FutureNow,
				TickCountVal,
				TickCountStr,
				// 2DIGIT ":" 2DIGIT ":" 2DIGIT
				// ; 00:00 : 00 - 23 : 59 : 59
				// {DayStr}, {%02d:GetDay()} {MonthStr} {GetYear()} {%H.%M.%S} GMT
				HttpDate,
				// %Y-%m-%dT%H:%M:%S.%sZ
				Iso8601,
				// %Y.%m.%d-%H.%M.%S
				DateTime,
			};

		protected:
			TGuardValue<EFmtType> GuardVal;
			static EFmtType DataTimeFormatType;

		public:
			FDataTimeFormatter(EFmtType InType);
			static const auto& GetType() { return DataTimeFormatType; }
		};
		struct GMP_API FGuidFormatter
		{
		protected:
			TGuardValue<TOptional<EGuidFormats>> GuardVal;
			static TOptional<EGuidFormats> GuidFormatsType;

		public:
			FGuidFormatter(TOptional<EGuidFormats> InType);
			static const auto& GetType() { return GuidFormatsType; }
		};
		struct GMP_API FIDFormatter
		{
		protected:
			TGuardValue<bool> GuardVal;
			static bool bConvertID;

		public:
			FIDFormatter(bool bConvertID = false);

			static const auto& GetType() { return bConvertID; }
		};

		struct GMP_API FCaseFormatter
		{
		protected:
			TGuardValue<bool> GuardVal;
			FIDFormatter IDFormatter;
			static bool bConvertCase;

		public:
			FCaseFormatter(bool bConvertCase = false, bool bConvertID = false);

			static bool StandardizeCase(TCHAR* StringIn, int32 Len);
			static const auto& GetType() { return bConvertCase; }
		};
		struct FCaseLower : public FCaseFormatter
		{
			FCaseLower()
				: FCaseFormatter(true)
			{
			}
		};

		struct GMP_API FArchiveEncoding
		{
			enum EEncodingType
			{
				UTF8,
				UTF16,
			};

		protected:
			EEncodingType GuardVal;
			static EEncodingType EncodingType;

		public:
			FArchiveEncoding(EEncodingType InType);

			static const auto& GetType() { return EncodingType; }
		};
	}  // namespace Serializer

	GMP_API void PropToJsonImpl(FArchive& Ar, FProperty* Prop, const void* ContainerAddr);
	GMP_API void PropToJsonImpl(FString& Out, FProperty* Prop, const void* ContainerAddr);
	GMP_API void PropToJsonImpl(TArray<uint8>& Out, FProperty* Prop, const void* ContainerAddr);
	template<typename T>
	void PropToJson(T& Out, FProperty* Prop, const uint8* ValueAddr)
	{
		PropToJsonImpl(Out, Prop, (const void*)(ValueAddr - Prop->GetOffset_ReplaceWith_ContainerPtrToValuePtr()));
	}
	FORCEINLINE auto PropToJsonStr(FProperty* Prop, const uint8* ValueAddr)
	{
		FString Ret;
		PropToJson(Ret, Prop, ValueAddr);
		return Ret;
	}
	FORCEINLINE auto PropToJsonBuf(FProperty* Prop, const uint8* ValueAddr, bool bLowCase = true)
	{
		Serializer::FCaseFormatter CaseFmt(bLowCase);
		TArray<uint8> Ret;
		PropToJson(Ret, Prop, ValueAddr);
		return Ret;
	}

	template<typename T, typename DataType>
	std::enable_if_t<GMP::TClassToPropTag<DataType>::value> ToJson(T& Out, const DataType& Data)
	{
		PropToJson(Out, GMP::TClass2Prop<DataType>::GetProperty(), (const uint8*)std::addressof(Data));
	}
	template<typename DataType>
	std::enable_if_t<!std::is_same<DataType, FString>::value, FString> ToJsonStr(const DataType& Data, bool bPretty = false)
	{
		FString Ret;
		ToJson(Ret, Data);
		return Ret;
	}
	template<typename DataType>
	auto ToJsonBuf(const DataType& Data, bool bLowCase = true)
	{
		Serializer::FCaseFormatter CaseFmt(bLowCase);
		TArray<uint8> Ret;
		ToJson(Ret, Data);
		return Ret;
	}

	template<typename DataType>
	bool ToJsonFile(const DataType& Data, const TCHAR* Filename, bool bLowCase = true)
	{
		Serializer::FCaseFormatter CaseFmt(bLowCase);
		TArray<uint8> Ret;
		ToJson(Ret, Data);
		return FFileHelper::SaveArrayToFile(Ret, Filename);
	}

	template<typename T>
	void UStructToJson(T& Out, UScriptStruct* Struct, const uint8* ValueAddr)
	{
		PropToJson(Out, GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), ValueAddr);
	}
	template<typename T, typename DataType>
	void UStructToJson(T& Out, const DataType& Data)
	{
		UStructToJson(Out, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
	}
	template<typename DataType>
	auto UStructToJsonStr(const DataType& Data, bool bPretty = false)
	{
		FString Ret;
		UStructToJson(Ret, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
		return Ret;
	}
	template<typename DataType>
	auto UStructToJsonBuf(const DataType& Data, bool bLowCase = true)
	{
		Serializer::FCaseFormatter CaseFmt(bLowCase);
		TArray<uint8> Ret;
		UStructToJson(Ret, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
		return Ret;
	}
	template<typename DataType>
	bool UStructToJsonFile(const DataType& Data, const TCHAR* Filename, bool bLowCase = true)
	{
		Serializer::FCaseFormatter CaseFmt(bLowCase);
		TArray<uint8> Ret;
		UStructToJson(Ret, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
		return FFileHelper::SaveArrayToFile(Ret, Filename);
	}

	namespace Deserializer
	{
		struct GMP_API FInsituFormatter
		{
		protected:
			TGuardValue<bool> GuardVal;
			static bool bTryInsituParse;

		public:
			FInsituFormatter(bool bInInsituParse = true);

			static const auto& GetType() { return bTryInsituParse; }
		};
	}  // namespace Deserializer

	GMP_API bool PropFromJsonImpl(FArchive& Ar, FProperty* Prop, void* ContainerAddr);
	GMP_API bool PropFromJsonImpl(const FString& In, FProperty* Prop, void* ContainerAddr);
	GMP_API bool PropFromJsonImpl(TArrayView<const uint8> In, FProperty* Prop, void* ContainerAddr);
	inline bool PropFromJsonImpl(const TArray<uint8>& In, FProperty* Prop, void* ContainerAddr) { return PropFromJsonImpl(TArrayView<const uint8>(In), Prop, ContainerAddr); }

	GMP_API bool PropFromJsonImpl(FString& In, FProperty* Prop, void* ContainerAddr);
	GMP_API bool PropFromJsonImpl(TArray<uint8>& In, FProperty* Prop, void* ContainerAddr);

	GMP_API bool PropFromJsonImpl(FString&& In, FProperty* Prop, void* ContainerAddr);
	GMP_API bool PropFromJsonImpl(TArray<uint8>&& In, FProperty* Prop, void* ContainerAddr);
	GMP_API bool PropFromJsonImpl(TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Rsp, FProperty* Prop, void* ContainerAddr);
	template<typename T>
	bool PropFromJson(T&& In, FProperty* Prop, uint8* OutValueAddr)
	{
		return PropFromJsonImpl(Forward<T>(In), Prop, OutValueAddr - Prop->GetOffset_ReplaceWith_ContainerPtrToValuePtr());
	}

	template<typename T, typename DataType>
	std::enable_if_t<GMP::TClassToPropTag<DataType>::value, bool> FromJson(T&& In, DataType& OutData)
	{
		return PropFromJsonImpl(Forward<T>(In), GMP::TClass2Prop<DataType>::GetProperty(), std::addressof(OutData));
	}
	template<typename DataType>
	std::enable_if_t<GMP::TClassToPropTag<DataType>::value, bool> FromJsonFile(const TCHAR* Filename, DataType& OutData)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(Filename));
		return ensure(Reader) && PropFromJsonImpl(*Reader, GMP::TClass2Prop<DataType>::GetProperty(), std::addressof(OutData));
	}

	template<typename T>
	bool UStructFromJson(T&& In, UScriptStruct* Struct, void* OutValueAddr)
	{
		return PropFromJson(Forward<T>(In), GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), static_cast<uint8*>(OutValueAddr));
	}
	template<typename T, typename DataType>
	bool UStructFromJson(T&& In, DataType& OutData)
	{
		return UStructFromJson(Forward<T>(In), GMP::TypeTraits::StaticStruct<DataType>(), (uint8*)std::addressof(OutData));
	}
	template<typename DataType>
	bool UStructFromJsonFile(const TCHAR* Filename, DataType& OutData)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(Filename));
		return ensure(Reader) && UStructFromJson(*Reader, GMP::TypeTraits::StaticStruct<DataType>(), (uint8*)std::addressof(OutData));
	}

	namespace Serializer
	{
		class FJsonBuilderImpl;
		struct GMP_API FJsonBuilderBase : public FNoncopyable
		{
		public:
			struct FStrIndexPair
			{
				int32 First = 0;
				int32 Last = 0;

				explicit operator bool() const { return Last >= First && First >= 0; }

				FStrIndexPair& Append(FStrIndexPair Other)
				{
					if (Last <= 0)
					{
						*this = Other;
					}
					else
					{
						First = FMath::Min(First, Other.First);
						Last = FMath::Max(Last, Other.Last);
					}
					return *this;
				}
			};

			~FJsonBuilderBase();

			int32 StartObject();
			int32 EndObject();

			int32 StartArray();
			int32 EndArray();

			FStrIndexPair Key(FStringView k);
			FStrIndexPair Key(FAnsiStringView k);

			FStrIndexPair Value(bool b);
			FStrIndexPair Value(int64 i);
			FStrIndexPair Value(uint64 u);
			FStrIndexPair Value(double d);
			FORCEINLINE FStrIndexPair Value(float f) { return Value(double(f)); }
			FORCEINLINE FStrIndexPair Value(int32 i) { return Value(int64(i)); }
			FORCEINLINE FStrIndexPair Value(uint32 u) { return Value(uint64(u)); }
			FStrIndexPair Value(FStringView s);
			FStrIndexPair Value(const TCHAR* s) { return Value(FStringView{s}); }
			FStrIndexPair Value(const char* s) { return Value(FAnsiStringView{s}); }
			FStrIndexPair RawValue(FStringView s);
			FStrIndexPair RawValue(FAnsiStringView s);
			template<size_t N>
			FORCEINLINE FStrIndexPair Value(const TCHAR (&s)[N])
			{
				return Value(FStringView(s, N));
			}
			FStrIndexPair Value(FAnsiStringView s);
			template<size_t N>
			FORCEINLINE FStrIndexPair Value(const ANSICHAR (&s)[N])
			{
				return Value(FAnsiStringView(s, N));
			}

			template<typename T, typename S>
			FORCEINLINE FStrIndexPair Value(TArrayView<T, S> List)
			{
				FStrIndexPair Ret;
				Ret.First = StartArray();
				for (auto& Elm : List)
				{
					Value(Elm);
				}
				Ret.Last = EndArray();
				return Ret;
			}

			template<typename DataType>
			std::enable_if_t<!std::is_arithmetic<DataType>::value && !!GMP::TClassToPropTag<DataType>::value, FStrIndexPair> Value(const DataType& Data)
			{
				return PropValue(GMP::TClass2Prop<DataType>::GetProperty(), reinterpret_cast<const uint8*>(std::addressof(Data)));
			}
			template<typename DataType>
			FStrIndexPair StructValue(const DataType& InData, UScriptStruct* StructType = GMP::TypeTraits::StaticStruct<DataType>())
			{
				check(StructType->IsChildOf(GMP::TypeTraits::StaticStruct<DataType>()));
				return PropValue(GMP::Class2Prop::TTraitsStructBase::GetProperty(StructType), reinterpret_cast<const uint8*>(std::addressof(InData)));
			}

			template<typename DataType>
			FStrIndexPair AddKeyValue(FStringView k, const DataType& Data)
			{
				auto KeyView = Key(k);
				if (!KeyView)
					return KeyView;
				auto ValView = Value(Data);
				if (!ValView)
					return ValView;
				return KeyView.Append(ValView);
			}

			template<typename DataType>
			bool AddKeyValue(FAnsiStringView k, const DataType& Data)
			{
				if (!Key(k))
					return false;
				if (!Value(Data))
					return false;
				return true;
			}

			template<typename F>
			FJsonBuilderBase& ScopeArray(const F& Func)
			{
				ensure(StartArray() >= 0);
				Func();
				ensure(EndArray() >= 0);
				return *this;
			}
			template<typename F>
			FJsonBuilderBase& ScopeObject(const F& Func)
			{
				StartObject();
				Func();
				EndObject();
				return *this;
			}

			bool IsComplete() const;
			inline operator TArray<uint8>&&() { return MoveTemp(GetJsonArrayImpl()); }
			bool SaveArrayToFile(const TCHAR* Filename, uint32 WriteFlags = 0);
			inline operator FString() const
			{
				FString Str;
				auto& Ref = GetJsonArrayImpl();
				if (Ref.Num() > 0)
				{
					auto Size = FUTF8ToTCHAR_Convert::ConvertedLength((const char*)&Ref[0], Ref.Num());
					Str.GetCharArray().Reserve(Size + 1);
					Str.GetCharArray().AddUninitialized(Size);
					Str.GetCharArray().Add('\0');
					FUTF8ToTCHAR_Convert::Convert(&Str[0], Size, (const char*)&Ref[0], Ref.Num());
				}
				return Str;
			}

			void Reset(TArray<uint8> Context);

			// Join two object or array
			FJsonBuilderBase& Join(const FJsonBuilderBase& Other);

			template<typename DataType>
			FJsonBuilderBase& JoinStruct(const DataType& Other)
			{
				return JoinStructImpl(GMP::TypeTraits::StaticStruct<DataType>(), &Other);
			}

			FString GetIndexedString(FStrIndexPair IndexPair) const;
			FAnsiStringView AsStrView(bool bEnsureCompleted = true) const;

		protected:
			FJsonBuilderBase();
			FJsonBuilderBase& JoinStructImpl(const UScriptStruct* Struct, const void* Data);
			TArray<uint8>& GetJsonArrayImpl(bool bEnsureCompleted = true);
			const TArray<uint8>& GetJsonArrayImpl(bool bEnsureCompleted = true) const;
			FStrIndexPair PropValue(FProperty* ValueProp, const uint8* ValueAddr);
			TUniquePtr<FJsonBuilderImpl> Impl;
		};
	}  // namespace Serializer

	struct FJsonBuilder : public Serializer::FJsonBuilderBase
	{
		FJsonBuilder() {}
		inline operator TArray<uint8>&&() { return MoveTemp(GetJsonArrayImpl()); }
	};

	struct FJsonObjBuilder : public Serializer::FJsonBuilderBase
	{
		FJsonObjBuilder() { StartObject(); }
		inline operator TArray<uint8>&&()
		{
			EndObject();
			return MoveTemp(GetJsonArrayImpl());
		}
		template<typename F>
		FJsonObjBuilder& MakeObject(const F& Func)
		{
			Func();
			return *this;
		}
	};
	struct FJsonArrBuilder : public Serializer::FJsonBuilderBase
	{
		FJsonArrBuilder() { StartArray(); }
		inline operator TArray<uint8>&&()
		{
			EndArray();
			return MoveTemp(GetJsonArrayImpl());
		}
		template<typename F>
		FJsonArrBuilder& MakeArray(const F& Func)
		{
			Func();
			return *this;
		}
	};
}  // namespace Json
}  // namespace GMP
