//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPJsonSerializer.h"

#include "GMPJsonSerializer.inl"
#include "Interfaces/IHttpResponse.h"

#define RAPIDJSON_WRITE_DEFAULT_FLAGS (kWriteNanAndInfFlag | (WITH_EDITOR ? kWriteValidateEncodingFlag : kWriteNoFlags))
#include "rapidjson/document.h"
#include "rapidjson/encodedstream.h"
#include "rapidjson/encodings.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace GMP
{
namespace Json
{

	StringView::StringView(uint32 InLen, const TCHAR* InData)
		: Data(InData)
		, Length(InLen)
	{
	}

	StringView::StringView(uint32 InLen, const void* InData)
		: Data(InData)
		, Length(-int64(InLen))
	{
	}
	FName StringView::ToFName(EFindName Flag) const
	{
		FName Name;
		if (Len() > 0)
		{
			if (IsTCHAR())
			{
				Name = FName(Len(), ToTCHAR(), Flag);
			}
			else
			{
#if 0
					// FNAME ANSICHAR codepage is UTF7
					Name = FName(Len(), ToANSICHAR(), Flag)
#else
				TCHAR NameBuf[NAME_SIZE];
				auto ReqiredSize = FUTF8ToTCHAR_Convert::ConvertedLength(ToANSICHAR(), Len());
				auto Size = FMath::Min(ReqiredSize, static_cast<int32>(NAME_SIZE));
				FUTF8ToTCHAR_Convert::Convert(NameBuf, Size, ToANSICHAR(), Len());
				Name = FName(Size, NameBuf, Flag);
				if (ReqiredSize > NAME_SIZE)
				{
					NameBuf[Size - 1] = '\0';
					UE_LOG(LogGMP, Error, TEXT("stringView too long to convert to a properly fname %s"), NameBuf);
				}

#endif
			}

#if WITH_EDITOR
			UE_CLOG(Name.IsNone(), LogGMP, Warning, TEXT("fromjson keyname mismatch : %s"), *ToFString());
#endif
		}
		return Name;
	}

	FString StringView::ToFString() const
	{
		FString Str = IsTCHAR() ? FString(Len(), ToTCHAR()) : GMP::Serializer::AsFString(ToANSICHAR(), Len());
		return Str;
	}

	StringView::FStringViewData::FStringViewData(const StringView& InStrView)
	{
		if (InStrView.IsTCHAR())
		{
			CharData = InStrView.ToTCHAR();
		}
		else
		{
			StrData = InStrView.ToFString();
			CharData = GetData(StrData);
		}
		GMP_CHECK(CharData);
	}

	static auto FriendGMPValueOneOf = [](const FGMPValueOneOf& In) -> decltype(auto) {
		struct FGMPValueOneOfFriend : public FGMPValueOneOf
		{
			using FGMPValueOneOf::Value;
			using FGMPValueOneOf::Flags;
		};
		return const_cast<FGMPValueOneOfFriend&>(static_cast<const FGMPValueOneOfFriend&>(In));
	};

	static bool bUseInsituParse = true;
	namespace Detail
	{
#ifndef GMP_RAPIDJSON_ALLOCATOR_UNREAL
#define GMP_RAPIDJSON_ALLOCATOR_UNREAL 1
#endif
#if GMP_RAPIDJSON_ALLOCATOR_UNREAL
		class FStackAllocator
		{
		public:
			static const bool kNeedFree = true;
			void* Malloc(size_t InSize)
			{
				//  behavior of malloc(0) is implementation defined. // standardize to returning NULL.
				return InSize ? FMemory::Malloc(InSize) : nullptr;
			}
			void* Realloc(void* OriginalPtr, size_t OriginalSize, size_t NewSize)
			{
				(void)OriginalSize;
				if (NewSize == 0)
				{
					FMemory::Free(OriginalPtr);
					return nullptr;
				}
				return FMemory::Realloc(OriginalPtr, NewSize);
			}
			static void Free(void* Ptr) { FMemory::Free(Ptr); }

			bool operator==(const FStackAllocator&) const { return true; }
			bool operator!=(const FStackAllocator&) const { return false; }
		};
		using FDefaultAllocator = rapidjson::MemoryPoolAllocator<FStackAllocator>;
#else
		using FStackAllocator = RAPIDJSON_DEFAULT_STACK_ALLOCATOR;
		using FDefaultAllocator = RAPIDJSON_DEFAULT_ALLOCATOR;
#endif
		template<typename Encoding, typename Allocator = FDefaultAllocator, typename StackAllocator = FStackAllocator>
		using TGenericDocument = rapidjson::GenericDocument<Encoding, Allocator, StackAllocator>;

		namespace JsonValueHelper
		{
			template<typename Encoding, typename Allocator>
			struct TJsonValueHelper<rapidjson::GenericValue<Encoding, Allocator>>
			{
				using GenericValue = rapidjson::GenericValue<Encoding, Allocator>;

				static bool IsStringType(const GenericValue& Val) { return Val.IsString(); }
				static StringView AsStringView(const GenericValue& Val)
				{
					GMP_CHECK(IsStringType(Val));
					return StringView(static_cast<uint32>(Val.GetStringLength()), Val.GetString());
				}
				static bool IsObjectType(const GenericValue& Val) { return Val.IsObject(); }
				static const GenericValue* FindMember(const GenericValue& Val, const FName& Name)
				{
					GMP_CHECK(IsObjectType(Val));
					if (Name.IsNone())
						return &Val;

					for (auto& Pair : Val.GetObject())
					{
						if (Name == AsStringView(Pair.name).ToFName())
							return &Pair.value;
					}
					return nullptr;
				}
				static bool ForEachObjectPair(const GenericValue& Val, TFunctionRef<bool(const StringView&, const GenericValue&)> Op)
				{
					GMP_CHECK(IsObjectType(Val));
					for (auto& Pair : Val.GetObject())
					{
						if (Op(AsStringView(Pair.name), Pair.value))
							return true;
					}
					return false;
				}
				static int32 IterateObjectPair(const GenericValue& Val, int32 Idx, TFunctionRef<void(const StringView&, const GenericValue&)> Op)
				{
					if (GMP_ENSURE_JSON(Idx < 0 || !IsObjectType(Val)))
						return 0;

					auto&& Obj = Val.GetObject();
					if (Idx >= (int32)Obj.MemberCount())
						return 0;

					auto It = Obj.MemberBegin() + Idx;
					Op(AsStringView(It->name), It->value);
					return ++Idx < (int32)Obj.MemberCount() ? Idx : INDEX_NONE;
				}
				static bool IsArrayType(const GenericValue& Val) { return Val.IsArray(); }
				static int32 ArraySize(const GenericValue& Val) { return IsArrayType(Val) ? (int32)Val.Size() : 0; }
				static const GenericValue& ArrayElm(const GenericValue& Val, int32 Idx)
				{
					GMP_CHECK(IsArrayType(Val));
					return Val[Idx];
				}
				static bool IsNumberType(const GenericValue& Val) { return Val.IsNumber(); }
				static TValueType<StringView, const GenericValue*> DispatchValue(const GenericValue& Val)
				{
					if (Val.IsString())
					{
						return AsStringView(Val);
					}
					else if (Val.IsBool())
						return Val.GetBool();
					else if (Val.IsDouble())
					{
						if (Val.IsLosslessFloat())
							return float(Val.GetDouble());
						return Val.GetDouble();
					}
					else if (Val.IsInt())
					{
						return Val.GetInt();
					}
					else if (Val.IsUint())
					{
						return Val.GetUint();
					}
					else if (Val.IsInt64())
					{
						return Val.GetInt64();
					}
					else if (Val.IsUint64())
					{
						return Val.GetUint64();
					}
					else if (Val.IsObject() || Val.IsArray())
					{
						return &Val;
					}
					return std::monostate{};
				}

				static TValueType<> AsNumber(const GenericValue& Val)
				{
					if (Val.IsBool())
					{
						return Val.GetBool();
					}
					else if (Val.IsDouble())
					{
						if (Val.IsLosslessFloat())
							return float(Val.GetDouble());
						return Val.GetDouble();
					}
					else if (Val.IsInt())
					{
						return Val.GetInt();
					}
					else if (Val.IsUint())
					{
						return Val.GetUint();
					}
					else if (Val.IsInt64())
					{
						return Val.GetInt64();
					}
					else if (Val.IsUint64())
					{
						return Val.GetUint64();
					}

					return std::monostate{};
				}
				template<typename V>
				static V VisitVal(const std::monostate& Val)
				{
					return {};
				}
				template<typename V, typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value, V> VisitVal(const T& Val)
				{
					return V(Val);
				}
				template<typename T>
				static T ToNumber(const GenericValue& Val)
				{
					GMP_ENSURE_JSON(IsNumberType(Val));
					T Ret;
					std::visit([&](const auto& Item) { Ret = VisitVal<T>(Item); }, AsNumber(Val));
					return Ret;
				}
			};
		}  // namespace JsonValueHelper
#if WITH_GMPVALUE_ONEOF
		namespace Internal
		{
			template<typename JsonType>
			bool FromJson(const JsonType& JsonVal, FGMPValueOneOf& OutValueHolder)
			{
				using EncodingType = typename JsonType::EncodingType;
				using AllocatorType = typename JsonType::AllocatorType;
				using CharType = typename JsonType::Ch;

				auto Ref = MakeShared<TGenericDocument<EncodingType, AllocatorType>, ESPMode::ThreadSafe>();
				Ref->CopyFrom(JsonVal, Ref->GetAllocator());
				auto& Holder = GMP::Json::FriendGMPValueOneOf(OutValueHolder);
				Holder.Flags = sizeof(CharType);
				Holder.Value = MoveTemp(Ref);
				return true;
			}

			using WValueType = rapidjson::GenericValue<rapidjson::UTF16LE<TCHAR>, FStackAllocator>;
			template bool FromJson<WValueType>(const WValueType& JsonVal, FGMPValueOneOf& OutValueHolder);
			using ValueType = rapidjson::GenericValue<rapidjson::UTF8<uint8>, FStackAllocator>;
			template bool FromJson<ValueType>(const ValueType& JsonVal, FGMPValueOneOf& OutValueHolder);
		}  // namespace Internal
#endif
		struct FDefaultJsonFlags
		{
			Serializer::FDataTimeFormatter::EFmtType DataTimeFormatType = Serializer::FDataTimeFormatter::EFmtType::UnixtimestampStr;
			Serializer::FNumericFormatter::ENumericFmt NumericFmtType = Serializer::FNumericFormatter::ENumericFmt::Default;
			Serializer::FArchiveEncoding::EEncodingType EncodingType = Serializer::FArchiveEncoding::EEncodingType::UTF8;
			TOptional<EGuidFormats> GuidFormatsType;
			bool bConvertID = false;
			bool bConvertCase = false;
			bool bTryInsituParse = false;
		};
		static FDefaultJsonFlags DefaultJsonFlags;

		struct FJsonFlags : public TThreadSingleton<FJsonFlags>
		{
			FDefaultJsonFlags Flags;
			FJsonFlags()
				: Flags(DefaultJsonFlags)
			{
			}
		};
	}  // namespace Detail

	namespace Serializer
	{

		const FDataTimeFormatter::EFmtType FDataTimeFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.DataTimeFormatType;
		}
		FDataTimeFormatter::FDataTimeFormatter(EFmtType InType)
			: GuardVal(Detail::FJsonFlags::Get().Flags.DataTimeFormatType, InType)
		{
		}
		const TOptional<EGuidFormats>& FGuidFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.GuidFormatsType;
		}
		FGuidFormatter::FGuidFormatter(TOptional<EGuidFormats> InType)
			: GuardVal(Detail::FJsonFlags::Get().Flags.GuidFormatsType, InType)
		{
		}

		const bool FIDFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.bConvertID;
		}
		FIDFormatter::FIDFormatter(bool bInConvertID)
			: GuardVal(Detail::FJsonFlags::Get().Flags.bConvertID, bInConvertID)
		{
		}

		const bool FCaseFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.bConvertCase;
		}
		FCaseFormatter::FCaseFormatter(bool bInConvertCase, bool bInConvertID)
			: GuardVal(Detail::FJsonFlags::Get().Flags.bConvertCase, bInConvertCase)
			, IDFormatter(bInConvertID)
		{
		}
		bool FCaseFormatter::StandardizeCase(TCHAR* StringIn, int32 Len)
		{
			if (Len > 0 && GetType())
			{
				// our json classes/variable start lower case
				StringIn[0] = FChar::ToLower(StringIn[0]);
				if (FIDFormatter::GetType())
				{
					// Id is standard instead of ID, some of our fnames use ID
					TCHAR* Find = StringIn + 1;
					while (auto Found = FCString::Strstr(Find, TEXT("ID")))
					{
						Found[1] = TEXT('d');
						Find = Found + 1;
					}
				}
				return true;
			}
			return false;
		}

		const FNumericFormatter::ENumericFmt FNumericFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.NumericFmtType;
		}
		FNumericFormatter::FNumericFormatter(ENumericFmt InType)
			: GuardVal(Detail::FJsonFlags::Get().Flags.NumericFmtType, InType)
		{
		}

		const FArchiveEncoding::EEncodingType FArchiveEncoding::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.EncodingType;
		}
		FArchiveEncoding::FArchiveEncoding(EEncodingType InType)
			: GuardVal(Detail::FJsonFlags::Get().Flags.EncodingType, InType)
		{
		}

		template<typename StreamType, typename CharType = typename StreamType::ElementType>
		class TOutputWrapper : public FNoncopyable
		{
		public:
			using Ch = CharType;
			TOutputWrapper(StreamType& InStream)
				: Stream(InStream)
			{
			}

			void Flush() {}
			void Put(Ch C);
			bool PutN(const Ch* Str, size_t Len);
			friend void PutUnsafe(TOutputWrapper& Wrapper, Ch C) { Wrapper.Put(C); }

		private:
			StreamType& Stream;
		};

		template<>
		inline void TOutputWrapper<FString, TCHAR>::Put(TCHAR C)
		{
			Stream.AppendChar(C);
		}
		template<>
		inline void TOutputWrapper<TArray<uint8>, uint8>::Put(uint8 C)
		{
			Stream.Add(C);
		}
		template<>
		inline bool TOutputWrapper<FString, TCHAR>::PutN(const TCHAR* Str, size_t Len)
		{
			Stream.AppendChars(Str, Len);
			return true;
		}
		template<>
		inline bool TOutputWrapper<TArray<uint8>, uint8>::PutN(const uint8* Str, size_t Len)
		{
			Stream.Append(Str, Len);
			return true;
		}

	}  // namespace Serializer

	template<typename CharType = uint8>
	struct TArchiveStream
	{
		using Ch = CharType;
		TArchiveStream(FArchive& InAr)
			: Ar(InAr)
		{
		}
		void Flush() { Ar.Flush(); }
		void Put(Ch C)
		{
			GMP_CHECK(!GIsEditor || Ar.IsSaving());
			Ar.Serialize(&C, sizeof(C));
		}
		friend void PutUnsafe(TArchiveStream& AS, Ch C) { AS.Put(C); }

		Ch Take()
		{
			GMP_CHECK(!GIsEditor || Ar.IsLoading());
			if (Ar.AtEnd())
				return '\0';
			Ch C;
			Ar.Serialize(&C, sizeof(C));
			return C;
		}
		size_t Tell() const { return Ar.Tell(); }
		const Ch* Peek4() const
		{
			GMP_CHECK(!GIsEditor || Ar.IsLoading());
			auto Pos = Tell();
			Ar.Serialize(DetectBuf, sizeof(DetectBuf));
			Ar.Seek(Pos);
			return DetectBuf;
		}
		Ch Peek() const
		{
			GMP_CHECK(!GIsEditor || Ar.IsLoading());
			Ch C;
			auto Pos = Tell();
			Ar.Serialize(&C, sizeof(C));
			Ar.Seek(Pos);
			return C;
		}

	protected:
		FArchive& Ar;
		mutable Ch DetectBuf[4];
	};

	void PropToJsonImpl(FString& Out, FProperty* Prop, const void* ContainerAddr)
	{
		using namespace rapidjson;
		Serializer::TOutputWrapper<FString> Output{Out};
		using WriterType = Writer<decltype(Output), UTF16LE<TCHAR>, UTF16LE<TCHAR>>;
		WriterType Wrtier{Output};
		Detail::WriteToJson(Wrtier, Prop, ContainerAddr);
	}
	void PropToJsonImpl(TArray<uint8>& Out, FProperty* Prop, const void* ContainerAddr)
	{
		using namespace rapidjson;
		Serializer::TOutputWrapper<TArray<uint8>> Output{Out};
		using WriterType = Writer<decltype(Output), UTF16LE<TCHAR>, UTF8<uint8>>;
		WriterType Wrtier{Output};
		Detail::WriteToJson(Wrtier, Prop, ContainerAddr);
	}

	void PropToJsonImpl(FArchive& Ar, FProperty* Prop, const void* ContainerAddr)
	{
		GMP_CHECK(Ar.IsSaving());

		using namespace rapidjson;
		if (Serializer::FArchiveEncoding::GetType() == Serializer::FArchiveEncoding::EEncodingType::UTF16)
		{
			TArchiveStream<TCHAR> Output{Ar};
			using WriterType = Writer<decltype(Output), UTF16LE<TCHAR>, UTF16LE<TCHAR>>;
			WriterType Wrtier{Output};
			Detail::WriteToJson(Wrtier, Prop, ContainerAddr);
		}
		else
		{
			TArchiveStream<uint8> Output{Ar};
			using WriterType = Writer<decltype(Output), UTF16LE<TCHAR>, UTF8<uint8>>;
			WriterType Wrtier{Output};
			Detail::WriteToJson(Wrtier, Prop, ContainerAddr);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	namespace Deserializer
	{
		const bool FInsituFormatter::GetType()
		{
			return Detail::FJsonFlags::Get().Flags.bTryInsituParse;
		}
		FInsituFormatter::FInsituFormatter(bool bInInsituParse /*= true*/)
			: GuardVal(Detail::FJsonFlags::Get().Flags.bTryInsituParse, bInInsituParse)
		{
		}
	}  // namespace Deserializer

	bool PropFromJsonImpl(const FString& In, FProperty* Prop, void* ContainerAddr)
	{
		if (In.Len() == 0)
			return false;
		using namespace rapidjson;
		Detail::TGenericDocument<UTF16LE<TCHAR>> Document;
		Document.Parse<kParseStopWhenDoneFlag | kParseCommentsFlag | kParseTrailingCommasFlag>(*In, In.Len());
		if (Document.HasParseError())
			return false;
		Detail::ReadFromJson(static_cast<decltype(Document)::ValueType&>(Document), Prop, ContainerAddr);
		return true;
	}
	bool PropFromJsonImpl(TArrayView<const uint8> In, FProperty* Prop, void* ContainerAddr)
	{
		if (In.Num() == 0)
			return false;
		using namespace rapidjson;
		Detail::TGenericDocument<UTF8<uint8>> Document;
		Document.Parse<kParseStopWhenDoneFlag | kParseCommentsFlag | kParseTrailingCommasFlag>(In.GetData(), In.Num());
		if (Document.HasParseError())
			return false;
		Detail::ReadFromJson(static_cast<decltype(Document)::ValueType&>(Document), Prop, ContainerAddr);
		return true;
	}

	bool PropFromJsonImpl(FString&& In, FProperty* Prop, void* ContainerAddr)
	{
		if (In.Len() == 0)
			return false;
		using namespace rapidjson;
		Detail::TGenericDocument<UTF16LE<TCHAR>> Document;
		GenericInsituStringStream<UTF16LE<TCHAR>> s(GetData(In), GetData(In) + In.Len());
		Document.ParseStream<kParseStopWhenDoneFlag | kParseCommentsFlag | kParseTrailingCommasFlag | kParseInsituFlag>(s);
		if (Document.HasParseError())
			return false;
		Detail::ReadFromJson(static_cast<decltype(Document)::ValueType&>(Document), Prop, ContainerAddr);
		return true;
	}
	bool PropFromJsonImpl(TArray<uint8>&& In, FProperty* Prop, void* ContainerAddr)
	{
		if (In.Num() == 0)
			return false;
		using namespace rapidjson;
		Detail::TGenericDocument<UTF8<uint8>> Document;
		GenericInsituStringStream<UTF8<uint8>> s(In.GetData(), In.GetData() + In.Num());
		Document.ParseStream<kParseStopWhenDoneFlag | kParseCommentsFlag | kParseTrailingCommasFlag | kParseInsituFlag>(s);
		if (Document.HasParseError())
			return false;
		Detail::ReadFromJson(static_cast<decltype(Document)::ValueType&>(Document), Prop, ContainerAddr);
		return true;
	}

	bool PropFromJsonImpl(FArchive& Ar, FProperty* Prop, void* ContainerAddr)
	{
		GMP_CHECK(Ar.IsLoading());

		using namespace rapidjson;
		Detail::TGenericDocument<UTF16BE<TCHAR>> Document;
		TArchiveStream<uint8> RawInput{Ar};
		AutoUTFInputStream<unsigned, TArchiveStream<uint8>> Input{RawInput};
		Document.ParseStream<kParseStopWhenDoneFlag | kParseCommentsFlag | kParseTrailingCommasFlag, AutoUTF<unsigned>>(Input);
		if (Document.HasParseError())
			return false;
		Detail::ReadFromJson(static_cast<decltype(Document)::ValueType&>(Document), Prop, ContainerAddr);
		return true;
	}

	bool PropFromJsonImpl(TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Rsp, FProperty* Prop, void* ContainerAddr)
	{
		return PropFromJsonImpl(MoveTemp(const_cast<TArray<uint8>&>(Rsp->GetContent())), Prop, ContainerAddr);
	}

	bool PropFromJsonImpl(FString& In, FProperty* Prop, void* ContainerAddr)
	{
		if (bUseInsituParse && Deserializer::FInsituFormatter::GetType())
		{
			return PropFromJsonImpl((FString&&)In, Prop, ContainerAddr);
		}
		else
		{
			return PropFromJsonImpl((const FString&)In, Prop, ContainerAddr);
		}
	}

	bool PropFromJsonImpl(TArray<uint8>& In, FProperty* Prop, void* ContainerAddr)
	{
		if (bUseInsituParse && Deserializer::FInsituFormatter::GetType())
		{
			return PropFromJsonImpl((TArray<uint8>&&)In, Prop, ContainerAddr);
		}
		else
		{
			return PropFromJsonImpl((const TArray<uint8>&)In, Prop, ContainerAddr);
		}
	}

	namespace Serializer
	{
		using FStrIndexPair = FJsonBuilderBase::FStrIndexPair;

		enum EJsonScopeType : uint8
		{
			None,
			Object,
			Array,
			Key,
		};

		class FJsonBuilderImpl
		{
		public:
			TArray<uint8> Out;
			Serializer::TOutputWrapper<TArray<uint8>> Output{Out};
			using WriterType = rapidjson::Writer<Serializer::TOutputWrapper<TArray<uint8>>, rapidjson::UTF16LE<TCHAR>, rapidjson::UTF8<uint8>, Detail::FStackAllocator>;
			WriterType Writer{Output};
			WriterType* operator->() { return &Writer; }
#define GMP_ENABLE_JSON_VALIDATOR WITH_EDITOR
#if GMP_ENABLE_JSON_VALIDATOR
			TArray<EJsonScopeType, TInlineAllocator<32>> ScopeStack{EJsonScopeType::None};
#endif
			struct FWriterType : public WriterType
			{
				using WriterType::hasRoot_;
				using WriterType::level_stack_;

				bool HasLevel() const { return level_stack_.GetSize() > 0; }
				int32 ValueCount() const { return level_stack_.GetSize() > 0 ? level_stack_.template Top<Level>()->valueCount : -1; }
			};

			bool IsEmpty() const { return Out.Num() == 0; }
			bool IsComplete() const
			{
				auto& W = static_cast<const FWriterType&>(Writer);
				const bool bRet = !W.HasLevel();
#if GMP_ENABLE_JSON_VALIDATOR
				ensureAlways(bRet == (ScopeStack.Num() == 1 && ScopeStack[0] == EJsonScopeType::None));
#endif
				return bRet;
			}

			~FJsonBuilderImpl() { ensureAlways(IsComplete()); }
			int32 StartIndex() const
			{
				auto& W = static_cast<const FWriterType&>(Writer);
				return Out.Num() + 2 - W.ValueCount() <= 0 ? 1 : 0;
			}
			int32 CurIndex() const { return FMath::Max(0, Out.Num() - 1); }

			int32 Prefix(rapidjson::Type Type)
			{
				struct FWriterFriendType : public WriterType
				{
					using WriterType::Prefix;
				};
				auto CurIdx = CurIndex();
				static_cast<FWriterFriendType*>(&Writer)->Prefix(Type);
				return CurIdx;
			}
			FStrIndexPair Key(FStringView s)
			{
				auto CurIdx = StartIndex();
				GMP_ENSURE_JSON(Writer.Key(s.GetData(), s.Len()));
				return FStrIndexPair{CurIdx, CurIndex()};
			}
			FStrIndexPair WriteString(FAnsiStringView k)
			{
				static const uint8 hexDigits[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
				static const uint8 escape[256] = {
#define Z16 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
					//0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
					'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'b', 't', 'n', 'u', 'f',  'r', 'u', 'u',  // 00
					'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',  'u', 'u', 'u',  // 10
					0,   0,   '"', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    // 20
					Z16, Z16,                                                                         // 30~4F
					0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '\\', 0,   0,   0,    // 50
					Z16, Z16, Z16, Z16, Z16, Z16, Z16, Z16, Z16, Z16                                  // 60~FF
#undef Z16
				};

				auto CurIdx = Prefix(rapidjson::kStringType);
				Output.Put('\"');
				rapidjson::GenericStringStream<rapidjson::UTF8<uint8>> is((const uint8*)k.GetData());
				while (is.Tell() < k.Len())
				{
					const uint8 c = is.Take();
					if (escape[c])
					{
						Output.Put('\\');
						Output.Put(escape[c]);
						if (escape[c] == 'u')
						{
							Output.Put('0');
							Output.Put('0');
							Output.Put(hexDigits[c >> 4]);
							Output.Put(hexDigits[c & 0xF]);
						}
					}
					else
					{
						Output.Put(c);
					}
				}
				Output.Put('\"');
				return FStrIndexPair{CurIdx, CurIndex()};
			}

			FStrIndexPair RawValue(FAnsiStringView s)
			{
				auto CurIdx = Prefix(rapidjson::kStringType);
				for (auto i = 0; i < s.Len(); ++i)
					Output.Put(s[i]);
				return FStrIndexPair{CurIdx, CurIndex()};
			}

			void Reset(TArray<uint8>&& Context)
			{
#if GMP_ENABLE_JSON_VALIDATOR
				ScopeStack = {EJsonScopeType::None};
#endif
				Swap(Out, Context);
				Writer.Reset(Output);
			}
		};

		FJsonBuilderBase::FJsonBuilderBase()
			: Impl(new FJsonBuilderImpl)
		{
		}
		FJsonBuilderBase::~FJsonBuilderBase() = default;

		int32 FJsonBuilderBase::StartObject()
		{
#if GMP_ENABLE_JSON_VALIDATOR
			if (Impl->ScopeStack.Last() != EJsonScopeType::None)
			{
				auto Poped = Impl->ScopeStack.Pop();
				ensureAlways(Poped == EJsonScopeType::Array || Poped == EJsonScopeType::Key);
			}
			Impl->ScopeStack.Push(EJsonScopeType::Object);
#endif
			(*Impl)->StartObject();
			return Impl->CurIndex();
		}
		int32 FJsonBuilderBase::EndObject()
		{
#if GMP_ENABLE_JSON_VALIDATOR
			ensureAlways(Impl->ScopeStack.Pop() == EJsonScopeType::Object);
#endif
			(*Impl)->EndObject();
			return Impl->CurIndex();
		}
		int32 FJsonBuilderBase::StartArray()
		{
#if GMP_ENABLE_JSON_VALIDATOR
			if (Impl->ScopeStack.Last() != EJsonScopeType::None)
			{
				auto Poped = Impl->ScopeStack.Pop();
				ensureAlways(Poped == EJsonScopeType::Array || Poped == EJsonScopeType::Key);
			}

			Impl->ScopeStack.Push(EJsonScopeType::Array);
#endif
			(*Impl)->StartArray();
			return Impl->CurIndex();
		}
		int32 FJsonBuilderBase::EndArray()
		{
#if GMP_ENABLE_JSON_VALIDATOR
			ensureAlways(Impl->ScopeStack.Pop() == EJsonScopeType::Array);
#endif
			(*Impl)->EndArray();
			return Impl->CurIndex();
		}

		FStrIndexPair FJsonBuilderBase::Key(FStringView k)
		{
#if GMP_ENABLE_JSON_VALIDATOR
			ensureAlways(Impl->ScopeStack.Last() == EJsonScopeType::Object);
			Impl->ScopeStack.Push(EJsonScopeType::Key);
#endif
			return Impl->Key(k);
		}
		FStrIndexPair FJsonBuilderBase::Key(FAnsiStringView k)
		{
#if GMP_ENABLE_JSON_VALIDATOR
			ensureAlways(Impl->ScopeStack.Last() == EJsonScopeType::Object);
			Impl->ScopeStack.Push(EJsonScopeType::Key);
#endif
			return Impl->WriteString(k);
		}

#if GMP_ENABLE_JSON_VALIDATOR
#define GMP_VALIDATE_JSON_VALUE() ensureAlways(Impl->ScopeStack.Last() == EJsonScopeType::None || Impl->ScopeStack.Last() == EJsonScopeType::Array || Impl->ScopeStack.Pop() == EJsonScopeType::Key)
#else
#define GMP_VALIDATE_JSON_VALUE()
#endif
		FStrIndexPair FJsonBuilderBase::Value(FStringView s)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->String(s.GetData(), s.Len());
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}
		FStrIndexPair FJsonBuilderBase::Value(FAnsiStringView s)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			return Impl->WriteString(s);
		}
		FStrIndexPair FJsonBuilderBase::Value(bool b)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->Bool(b);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}
		FStrIndexPair FJsonBuilderBase::Value(int64 i)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->Int64(i);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}
		FStrIndexPair FJsonBuilderBase::Value(uint64 u)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->Uint64(u);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}
		FStrIndexPair FJsonBuilderBase::Value(double d)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->Double(d);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}
		FStrIndexPair FJsonBuilderBase::PropValue(FProperty* ValueProp, const uint8* ValueAddr)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			Detail::WriteToJson(Impl->Writer, ValueProp, ValueAddr);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}

		const TArray<uint8>& FJsonBuilderBase::GetJsonArrayImpl(bool bEnsureCompleted) const
		{
			auto This = const_cast<FJsonBuilderBase*>(this);
			return This->GetJsonArrayImpl(bEnsureCompleted);
		}

		TArray<uint8>& FJsonBuilderBase::GetJsonArrayImpl(bool bEnsureCompleted)
		{
			if (!ensureAlways(!bEnsureCompleted || IsComplete()))
			{
				static TArray<uint8> Dummy;
				return Dummy;
			}
			return Impl->Out;
		}

		bool FJsonBuilderBase::IsComplete() const
		{
			return Impl->IsComplete();
		}
		bool FJsonBuilderBase::SaveArrayToFile(const TCHAR* Filename, uint32 WriteFlags)
		{
			return FFileHelper::SaveArrayToFile(GetJsonArrayImpl(), Filename, &IFileManager::Get(), WriteFlags);
		}
		FStrIndexPair FJsonBuilderBase::RawValue(FStringView s)
		{
			GMP_VALIDATE_JSON_VALUE();
			auto CurIdx = Impl->StartIndex();
			(*Impl)->RawValue(s.GetData(), s.Len(), rapidjson::kStringType);
			return FStrIndexPair{CurIdx, Impl->CurIndex()};
		}

		FStrIndexPair FJsonBuilderBase::RawValue(FAnsiStringView s)
		{
			GMP_VALIDATE_JSON_VALUE();
			return Impl->RawValue(s);
		}

		void FJsonBuilderBase::Reset(TArray<uint8> Context)
		{
			Impl->Reset(MoveTemp(Context));
		}

		FString FJsonBuilderBase::GetIndexedString(FStrIndexPair IndexPair) const
		{
			FString Str;
			if (IndexPair && Impl->Out.IsValidIndex(IndexPair.First) && Impl->Out.IsValidIndex(IndexPair.Last))
			{
				auto Data = (const char*)Impl->Out.GetData() + IndexPair.First;
				auto Len = IndexPair.Last - IndexPair.First + 1;
				auto Size = FUTF8ToTCHAR_Convert::ConvertedLength(Data, Len);
				Str.GetCharArray().Reserve(Size + 1);
				Str.GetCharArray().AddUninitialized(Size);
				Str.GetCharArray().Add('\0');
				FUTF8ToTCHAR_Convert::Convert(&Str[0], Size, Data, Len);
			}

			return Str;
		}

		FJsonBuilderBase& FJsonBuilderBase::Join(const FJsonBuilderBase& Other)
		{
			if (this == &Other || Other.Impl->IsEmpty())
			{
				return *this;
			}

			if (ensureAlways(IsComplete() && Other.IsComplete()))
			{
				if (Impl->Out.Num() > 2)
				{
					ensureAlways((Impl->Out.Last() == '}' || Impl->Out.Last() == ']') && Impl->Out.Last() == Other.Impl->Out.Last());
					auto Pos = Impl->Out.Num() - 1;
					Impl->Out.Pop();
					Impl->Out.Append(Other.Impl->Out);
					Impl->Out[Pos] = ',';
				}
				else
				{
					Reset(Other.GetJsonArrayImpl());
				}
			}
			return *this;
		}

		FJsonBuilderBase& FJsonBuilderBase::JoinStructImpl(const UScriptStruct* Struct, const void* Data)
		{
			if (ensureAlways(IsComplete()))
			{
				auto ValueProp = GMP::Class2Prop::TTraitsStructBase::GetProperty(const_cast<UScriptStruct*>(Struct));
				if (Impl->Out.Num() > 2)
				{
					ensureAlways(Impl->Out.Last() == '}' || Impl->Out.Last() == ']');
					auto Pos = Impl->Out.Num() - 1;
					Impl->Out.Pop();                                                                     // } -->
					Detail::WriteToJson(Impl->Writer, ValueProp, reinterpret_cast<const uint8*>(Data));  // { -->
					Impl->Out[Pos] = ',';                                                                // ,
				}
				else
				{
					Reset({});
					PropValue(ValueProp, reinterpret_cast<const uint8*>(Data));
				}
			}
			return *this;
		}

		FAnsiStringView FJsonBuilderBase::AsStrView(bool bEnsureCompleted) const
		{
			auto& Arr = GetJsonArrayImpl(bEnsureCompleted);
			return FAnsiStringView((const char*)Arr.GetData(), Arr.Num());
		}

	}  // namespace Serializer
}  // namespace Json
}  // namespace GMP

int32 UGMPJsonUtils::IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue)
{
	int32 RetIdx = INDEX_NONE;
	do
	{
		auto OneOfPtr = &GMP::Json::FriendGMPValueOneOf(In);

		if (!OneOfPtr->IsValid())
			break;

#if WITH_GMPVALUE_ONEOF
		if (OneOfPtr->Flags == sizeof(uint8))
		{
			using DocType = GMP::Json::Detail::TGenericDocument<rapidjson::UTF8<uint8>>;
			auto Ptr = StaticCastSharedPtr<DocType>(OneOfPtr->Value);
			using ValueType = DocType::ValueType;
			RetIdx = GMP::Json::Detail::JsonValueHelper::TJsonValueHelper<ValueType>::IterateObjectPair(static_cast<ValueType&>(*Ptr), Idx, [&](const GMP::Json::StringView& Key, const ValueType& JsonValue) {
				OutKey = Key;
				GMP::Json::ReadFromJson(JsonValue, OutValue);
			});
		}
		else if (OneOfPtr->Flags == sizeof(TCHAR))
		{
			using DocType = GMP::Json::Detail::TGenericDocument<rapidjson::UTF16LE<TCHAR>>;
			auto Ptr = StaticCastSharedPtr<DocType>(OneOfPtr->Value);
			using ValueType = DocType::ValueType;
			RetIdx = GMP::Json::Detail::JsonValueHelper::TJsonValueHelper<ValueType>::IterateObjectPair(static_cast<ValueType&>(*Ptr), Idx, [&](const GMP::Json::StringView& Key, const ValueType& JsonValue) {
				OutKey = Key;
				GMP::Json::ReadFromJson(JsonValue, OutValue);
			});
		}
		else
		{
			bool bUnreachable = false;
			(void)GMP_ENSURE_JSON(bUnreachable);
		}
#endif
	} while (false);
	return RetIdx;
}

bool UGMPJsonUtils::AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey)
{
	bool bRet = false;
	do
	{
		auto OneOfPtr = &GMP::Json::FriendGMPValueOneOf(In);

		if (!OneOfPtr->IsValid())
			break;

#if WITH_GMPVALUE_ONEOF
		if (OneOfPtr->Flags == sizeof(uint8))
		{
			using DocType = GMP::Json::Detail::TGenericDocument<rapidjson::UTF8<uint8>>;
			auto Ptr = StaticCastSharedPtr<DocType>(OneOfPtr->Value);
			bRet = GMP::Json::Detail::ReadFromJson(*GMP::Json::Detail::JsonUtils::FindMember(static_cast<DocType::ValueType&>(*Ptr), SubKey), const_cast<FProperty*>(Prop), Out);
		}
		else if (OneOfPtr->Flags == sizeof(TCHAR))
		{
			using DocType = GMP::Json::Detail::TGenericDocument<rapidjson::UTF16LE<TCHAR>>;
			auto Ptr = StaticCastSharedPtr<DocType>(OneOfPtr->Value);
			bRet = GMP::Json::Detail::ReadFromJson(*GMP::Json::Detail::JsonUtils::FindMember(static_cast<DocType::ValueType&>(*Ptr), SubKey), const_cast<FProperty*>(Prop), Out);
		}
		else
		{
			bool bUnreachable = false;
			(void)GMP_ENSURE_JSON(bUnreachable);
		}
#endif
	} while (false);
	return bRet;
}

void UGMPJsonUtils::ClearOneOf(FGMPValueOneOf& OneOf)
{
	OneOf.Clear();
}

DEFINE_FUNCTION(UGMPJsonUtils::execAsStruct)
{
	P_GET_STRUCT_REF(FGMPValueOneOf, OneOf);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	void* OutData = Stack.MostRecentPropertyAddress;
	FProperty* OutProp = Stack.MostRecentProperty;
	P_GET_PROPERTY(FNameProperty, SubKey);
	P_GET_UBOOL(bConsumeOneOf);
	P_FINISH

	P_NATIVE_BEGIN
	*(bool*)RESULT_PARAM = AsValueImpl(OneOf, OutProp, OutData, SubKey);
	if (bConsumeOneOf)
		OneOf.Clear();
	P_NATIVE_END
}
