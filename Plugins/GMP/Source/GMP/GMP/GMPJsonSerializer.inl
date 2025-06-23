//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPJsonUtils.h"
#include "GMPSerializer.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/UnrealTypeTraits.h"
#include "UObject/Package.h"
#if UE_5_05_OR_LATER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#include <limits>

#if GMP_USE_STD_VARIANT
#include <variant>
#else
#include "Misc/Variant.h"
#endif

#if defined(STRUCTUTILS_API)
#if UE_5_05_OR_LATER
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif
#endif

namespace GMP
{
namespace Json
{
#if WITH_EDITOR
#define GMP_ENSURE_JSON(v) ensure(v)
#else
#define GMP_ENSURE_JSON(v) (v)
#endif
	namespace Detail
	{
		template<typename, typename, typename = void>
		struct HasToJson : std::false_type
		{
		};

		template<typename, typename, typename, typename = void>
		struct HasFromJson : std::false_type
		{
		};
		struct FMonoState
		{
		};

#if GMP_USE_STD_VARIANT
		template<typename... TArgs>
		using TValueType = std::variant<FMonoState, bool, int32, uint32, int64, uint64, float, double, TArgs...>;
		template<typename V = TValueType<>, typename T>
		FORCEINLINE auto ToValueType(const T& In)
		{
			return V(In);
		}
		template<typename T, typename V>
		FORCEINLINE bool IsValueType(const V& In)
		{
			return std::holds_alternative<T>(In);
		}
		template<typename F, typename V>
		FORCEINLINE void VisitValueType(const F& Op, const V& Var)
		{
			std::visit(Op, Var);
		}
#else
		template<typename... TArgs>
		using TValueType = TVariant<FMonoState, bool, int32, uint32, int64, uint64, float, double, TArgs...>;
		template<typename V = TValueType<>, typename T>
		FORCEINLINE auto ToValueType(const T& In)
		{
			return V(TInPlaceType<T>{}, In);
		}
		template<typename T, typename V>
		FORCEINLINE bool IsValueType(const V& In)
		{
			return In.GetIndex() == V::template IndexOfType<T>();
		}
		template<typename F, typename V>
		FORCEINLINE void VisitValueType(const F& Op, const V& Var)
		{
			Visit(Op, Var);
		}
#endif
		namespace JsonValueHelper
		{
			template<typename JsonType>
			struct TJsonValueHelper
			{
				static bool IsStringType(const JsonType& Val);
				static StringView AsStringView(const JsonType& Val);
				static bool IsObjectType(const JsonType& Val);
				static const JsonType* FindMember(const JsonType& Val, const FName& Name);
				static bool ForEachObjectPair(const JsonType& Val, TFunctionRef<bool(const StringView&, const JsonType&)>);
				static int32 IterateObjectPair(const JsonType& Val, int32 Idx, TFunctionRef<void(const StringView&, const JsonType&)>);
				static bool IsArrayType(const JsonType& Val);
				static int32 ArraySize(const JsonType& Val);
				static const JsonType& ArrayElm(const JsonType& Val, int32 Idx);
				static bool IsNumberType(const JsonType& Val);
				static TValueType<StringView, const JsonType*> DispatchValue(const JsonType& Val);
				template<typename T>
				static T ToNumber(const JsonType& Val);
			};

			template<typename JsonType>
			FORCEINLINE bool IsStringType(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::IsStringType(Val);
			}
			template<typename JsonType>
			FORCEINLINE StringView AsStringView(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::AsStringView(Val);
			}
			template<typename JsonType>
			FORCEINLINE bool IsObjectType(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::IsObjectType(Val);
			}
			template<typename JsonType>
			FORCEINLINE const JsonType* FindMember(const JsonType& Val, const FName& Name)
			{
				return TJsonValueHelper<JsonType>::FindMember(Val, Name);
			}
			template<typename JsonType, typename... TArgc>
			FORCEINLINE bool ForEachObjectPair(const JsonType& Val, TArgc&&... Argc)
			{
				return TJsonValueHelper<JsonType>::ForEachObjectPair(Val, std::forward<TArgc>(Argc)...);
			}

			template<typename JsonType>
			bool IsArrayType(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::IsArrayType(Val);
			}
			template<typename JsonType>
			FORCEINLINE int32 ArraySize(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::ArraySize(Val);
			}
			template<typename JsonType>
			FORCEINLINE const JsonType& ArrayElm(const JsonType& Val, int32 Idx)
			{
				return TJsonValueHelper<JsonType>::ArrayElm(Val, Idx);
			}

			template<typename JsonType>
			FORCEINLINE bool IsNumberType(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::IsNumberType(Val);
			}
			template<typename T, typename JsonType>
			FORCEINLINE T ToNumber(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::template ToNumber<T>(Val);
			}
			template<typename JsonType>
			FORCEINLINE TValueType<StringView, const JsonType*> DispatchValue(const JsonType& Val)
			{
				return TJsonValueHelper<JsonType>::DispatchValue(Val);
			}
		}  // namespace JsonValueHelper
		namespace JsonUtils = JsonValueHelper;

		template<typename WriterType>
		bool WriteToJson(WriterType& Writer, FProperty* Prop, const void* Value);
		template<typename JsonType>
		bool ReadFromJson(const JsonType& JsonVal, FProperty* Prop, void* Value);
		namespace Internal
		{
			using namespace JsonUtils;
			template<typename WriterType>
			void ToJson(WriterType& Writer, bool Data)
			{
				GMP_ENSURE_JSON(Writer.Bool(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, int8 Data)
			{
				GMP_ENSURE_JSON(Writer.Int(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, uint8 Data)
			{
				GMP_ENSURE_JSON(Writer.Int(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, int16 Data)
			{
				GMP_ENSURE_JSON(Writer.Int(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, uint16 Data)
			{
				GMP_ENSURE_JSON(Writer.Int(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, int32 Data)
			{
				GMP_ENSURE_JSON(Writer.Int(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, uint32 Data)
			{
				GMP_ENSURE_JSON(Writer.Uint(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, int64 Data)
			{
				GMP_ENSURE_JSON(Writer.Int64(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, uint64 Data)
			{
				GMP_ENSURE_JSON(Writer.Uint64(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, float Data)
			{
				GMP_ENSURE_JSON(Writer.Float(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, double Data)
			{
				GMP_ENSURE_JSON(Writer.Double(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FString& Data)
			{
				GMP_ENSURE_JSON(Writer.String(*Data, Data.Len()));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const TCHAR* Data)
			{
				GMP_ENSURE_JSON(Writer.String(Data));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FStringView& Data)
			{
				GMP_ENSURE_JSON(Writer.String(Data.GetData(), Data.Len()));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FName& Data)
			{
				TCHAR NameStr[FName::StringBufferSize];
				auto Len = Data.ToString(NameStr);
				GMP_ENSURE_JSON(Writer.String(*NameStr, Len));
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FText& Data)
			{
				ToJson(Writer, Data.ToString());
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FGuid& Value)
			{
				FString JsonStr = Serializer::FGuidFormatter::GetType().IsSet() ? Value.ToString(Serializer::FGuidFormatter::GetType().GetValue()) : Value.ToString();
				ToJson(Writer, JsonStr);
			}
			template<typename WriterType>
			void ToJson(WriterType& Writer, const FDateTime& Value)
			{
				using EFmtType = Serializer::FDataTimeFormatter::EFmtType;
				switch (Serializer::FDataTimeFormatter::GetType())
				{
					case EFmtType::Unixtimestamp:
					{
						return ToJson(Writer, Value.ToUnixTimestamp());
					}
					case EFmtType::UnixtimestampStr:
					{
						return ToJson(Writer, LexToString(Value.ToUnixTimestamp()));
					}
					case EFmtType::FutureNow:
					{
						return ToJson(Writer, GMP::Serializer::Str_FutureNow);
					}
					case EFmtType::TickCountVal:
					{
						return ToJson(Writer, Value.GetTicks());
					}
					case EFmtType::TickCountStr:
					{
						return ToJson(Writer, LexToString(Value.GetTicks()));
					}
					case EFmtType::HttpDate:
					{
						return ToJson(Writer, Value.ToHttpDate());
					}
					case EFmtType::Iso8601:
					{
						return ToJson(Writer, Value.ToIso8601());
					}
					case EFmtType::DateTime:
					{
						return ToJson(Writer, Value.ToString());
					}

					default:
						break;
				}
				GMP_ENSURE_JSON(Writer.StartObject());
				GMP_ENSURE_JSON(Writer.Key(GMP::Serializer::Str_Ticks));
				ToJson(Writer, LexToString(Value.GetTicks()));
				GMP_ENSURE_JSON(Writer.EndObject());
			}

			template<typename WriterType, typename T, typename A>
			void ToJson(WriterType& Writer, const TArray<T, A>& Container)
			{
				GMP_ENSURE_JSON(Writer.StartArray());
				for (const auto& Item : Container)
					ToJson(Writer, Item);
				GMP_ENSURE_JSON(Writer.EndArray());
			}
			template<typename WriterType, typename T, typename K, typename A>
			void ToJson(WriterType& Writer, const TSet<T, K, A>& Container)
			{
				GMP_ENSURE_JSON(Writer.StartArray());
				for (const auto& Item : Container)
					ToJson(Writer, Item);
				GMP_ENSURE_JSON(Writer.EndArray());
			}

			template<typename WriterType, typename K, typename V>
			void ToJson(WriterType& Writer, const TPair<K, V>& Pair)
			{
				GMP_ENSURE_JSON(Writer.Key(Pair.Key));
				ToJson(Writer, Pair.Value);
			}
			template<typename WriterType, typename T, typename K, typename A>
			void ToJson(WriterType& Writer, const TMap<T, K, A>& Container)
			{
				GMP_ENSURE_JSON(Writer.StartObject());
				for (const auto& Pair : Container)
				{
					GMP_ENSURE_JSON(Writer.Key(Pair.Key));
					return ToJson(Writer, Pair.Value);
				}
				GMP_ENSURE_JSON(Writer.EndObject());
			}
			template<typename WriterType, typename DataType>
			std::enable_if_t<HasToJson<WriterType, DataType>::Value> ToJson(WriterType& Writer, const DataType& Data)
			{
				Data.ToJson(Writer);
			}

			inline FStringView GetAuthoredNameForField(FProperty* Prop, FStringBuilderBase& StrBuilder, bool bIsUserdefinedStruct)
			{
				Prop->GetFName().ToString(StrBuilder);
				FStringView NameView(StrBuilder.GetData(), StrBuilder.Len());
				auto RetLen = StrBuilder.Len();
				if (bIsUserdefinedStruct)
				{
					const int32 GuidStrLen = 32;
					const int32 MinimalPostfixlen = GuidStrLen + 3;
					if (NameView.Len() > MinimalPostfixlen)
					{
						auto TestView = NameView.LeftChop(GuidStrLen + 1);
						int32 FirstCharToRemove = -1;
						const bool bCharFound = TestView.FindLastChar(TCHAR('_'), FirstCharToRemove);
						if (bCharFound && (FirstCharToRemove > 0))
						{
							NameView = TestView.Mid(0, FirstCharToRemove);
							RetLen = FirstCharToRemove;
						}
					}
				}
				Serializer::FCaseFormatter::StandardizeCase(StrBuilder.GetData(), RetLen);
				return NameView;
			}

			template<typename WriterType>
			bool ToJsonImpl(WriterType& Writer, UStruct* Struct, const void* StructAddr)
			{
				if (Struct->GetFName() == GMP::Serializer::NAME_DateTime || Struct->GetFName() == GMP::Serializer::NAME_MemResVersion)
				{
					ToJson(Writer, *reinterpret_cast<const FDateTime*>(StructAddr));
				}
				else if (Struct->GetFName() == GMP::Serializer::NAME_Guid)
				{
					ToJson(Writer, *reinterpret_cast<const FGuid*>(StructAddr));
				}
				else if (Struct->IsChildOf(GMP::Reflection::DynamicStruct<FGMPStructUnion>()))
				{
					GMP_ENSURE_JSON(Writer.StartObject());
					auto GMPStruct = reinterpret_cast<const FGMPStructUnion*>(StructAddr);
					GMP_ENSURE_JSON(Writer.Key(FGMPStructUnion::GetTypePropertyName()));
					ToJson(Writer, GMPStruct->GetTypeName().ToString());

					if (GMPStruct->GetArrayNum() > 0 && ensure(GMPStruct->GetType()))
					{
						GMP_ENSURE_JSON(Writer.Key(FGMPStructUnion::GetDataPropertyName()));
						GMP_ENSURE_JSON(Writer.StartArray());
						for (auto i = 0; i < GMPStruct->GetArrayNum(); ++i)
						{
							ToJsonImpl(Writer, GMPStruct->GetType(), (const void*)GMPStruct->GetDynData(i));
						}
						GMP_ENSURE_JSON(Writer.EndArray());
					}
					GMP_ENSURE_JSON(Writer.EndObject());
				}
#if defined(STRUCTUTILS_API)
				else if (Struct->IsChildOf(GMP::Reflection::DynamicStruct<FInstancedStruct>()))
				{
					GMP_ENSURE_JSON(Writer.StartObject());
					auto InstancedStruct = reinterpret_cast<const FInstancedStruct*>(StructAddr);
					GMP_ENSURE_JSON(Writer.Key(TEXT("ScriptStruct")));
					auto ScriptStruct = InstancedStruct->GetScriptStruct();
					auto StructMemory = InstancedStruct->GetMemory();
					if (!ScriptStruct || !StructMemory)
					{
						GMP_ENSURE_JSON(Writer.String(TEXT("")));
					}
					else
					{
						ToJson(Writer, ScriptStruct->GetFName().ToString());
						GMP_ENSURE_JSON(Writer.Key(TEXT("StructMemory")));
						ToJsonImpl(Writer, (UScriptStruct*)ScriptStruct, (const void*)StructMemory);
					}
					GMP_ENSURE_JSON(Writer.EndObject());
				}
#endif
				else
				{
					GMP_ENSURE_JSON(Writer.StartObject());
					const bool bIsUserdefinedStruct = Struct->IsA(UUserDefinedStruct::StaticClass());
					for (TFieldIterator<FProperty> It(Struct); It; ++It)
					{
						if (It->HasAnyPropertyFlags(/*Writer.IgnoreFlags |*/ CPF_Deprecated | CPF_Transient | CPF_SkipSerialization | CPF_EditorOnly))
							continue;

						FProperty* SubProp = *It;
						TStringBuilder<256> StrBuiler;
						auto Name = GetAuthoredNameForField(SubProp, StrBuiler, bIsUserdefinedStruct);
						GMP_ENSURE_JSON(Writer.Key(Name.GetData(), Name.Len()));
						WriteToJson(Writer, SubProp, StructAddr);
					}

					GMP_ENSURE_JSON(Writer.EndObject());
				}
				return true;
			}

			//////////////////////////////////////////////////////////////////////////
			template<typename JsonType>
			bool FromJsonImpl(const JsonType& JsonVal, UStruct* Struct, void* OutValue);
#if WITH_GMPVALUE_ONEOF
			template<typename JsonType>
			bool FromJson(const JsonType& JsonVal, FGMPValueOneOf& OutValueHolder);
#endif
			template<typename JsonType>
			bool FromJson(const JsonType& JsonVal, FGMPStructUnion& OutGMPStruct)
			{
				do
				{
					if (!JsonUtils::IsObjectType(JsonVal))
						break;
					static FName TypePropName = FGMPStructUnion::GetTypePropertyName();
					auto Val = JsonUtils::FindMember(JsonVal, TypePropName);
					if (!Val)
						break;

					if (!JsonUtils::IsStringType(*Val))
						break;

					FString ValStr = JsonUtils::AsStringView(*Val);
					UScriptStruct* InnerStruct = GMP::Reflection::DynamicStruct(ValStr);
					if (!ensure(InnerStruct))
					{
						GMP_WARNING(TEXT("unnable to resolve type from %s"), *ValStr);
						break;
					}

					static FName DataPropName = FGMPStructUnion::GetDataPropertyName();
					Val = JsonUtils::FindMember(JsonVal, DataPropName);
					if (!Val)
						break;
					if (!JsonUtils::IsArrayType(*Val))
						break;
					int32 Cnt = JsonUtils::ArraySize(*Val);
					OutGMPStruct = FGMPStructUnion(InnerStruct, Cnt);
					for (auto i = 0; i < Cnt; ++i)
					{
						FromJsonImpl(JsonUtils::ArrayElm(*Val, i), InnerStruct, OutGMPStruct.GetDynData(i));
					}
					return true;
				} while (false);
				OutGMPStruct = FGMPStructUnion();
				return false;
			}

			inline bool FromJson(const FString& DateString, FDateTime& OutDateTime)
			{
				do
				{
					if (DateString == GMP::Serializer::Str_Min)
					{
						// min representable value for our date struct. Actual date may vary by platform (this is used for sorting)
						OutDateTime = FDateTime::MinValue();
						break;
					}
					if (DateString == GMP::Serializer::Str_Max)
					{
						// max representable value for our date struct. Actual date may vary by platform (this is used for sorting)
						OutDateTime = FDateTime::MaxValue();
						break;
					}
					if (DateString == GMP::Serializer::Str_FutureNow)
					{
						// this value's not really meaningful from json serialization (since we don't know timezone) but handle it anyway since we're handling the other keywords
						OutDateTime = FDateTime::UtcNow();
						break;
					}

					static auto LexTryParseString = [](FDateTime& OutDateTime, const TCHAR* Buffer) {
						int64 UnixTimeStamp = 0;
						bool bRet = ::LexTryParseString(UnixTimeStamp, Buffer);
						if (bRet && UnixTimeStamp > 0)
						{
							OutDateTime = FDateTime::FromUnixTimestamp(UnixTimeStamp);
							return true;
						}
						return false;
					};

					if (LexTryParseString(OutDateTime, *DateString))
						break;
					if (FDateTime::ParseIso8601(*DateString, OutDateTime))
						break;
					if (FDateTime::Parse(DateString, OutDateTime))
						break;
					if (FDateTime::ParseHttpDate(DateString, OutDateTime))
						break;
					return false;
				} while (false);
				return true;
			}

			template<typename JsonType>
			bool FromJson(const JsonType& JsonVal, FDateTime& OutDateTime)
			{
				if (JsonUtils::IsObjectType(JsonVal))
				{
					static FName Name_Ticks = GMP::Serializer::Str_Ticks;
					auto Val = JsonUtils::FindMember(JsonVal, Name_Ticks);
					if (Val && JsonUtils::IsStringType(*Val))
					{
						auto StrView = JsonUtils::AsStringView(*Val);
						OutDateTime = FDateTime(StrView.IsTCHAR() ? FCString::Atoi64(StrView.ToTCHAR()) : FCStringAnsi::Atoi64(StrView.ToANSICHAR()));
						return true;
					}
				}
				else if (JsonUtils::IsNumberType(JsonVal))
				{
					int64 IntVal = JsonUtils::ToNumber<int64>(JsonVal);
					IntVal = FMath::Max(IntVal, 0ll);
					if (IntVal < INT_MAX)
					{
						OutDateTime = FDateTime::FromUnixTimestamp(IntVal);
					}
					else
					{
						OutDateTime = FDateTime(IntVal);
					}
					return true;
				}
				else if (JsonUtils::IsStringType(JsonVal))
				{
					FString DateString = JsonUtils::AsStringView(JsonVal);
					return FromJson(DateString, OutDateTime);
				}
				return false;
			}

			template<typename JsonType>
			bool FromJson(const JsonType& JsonVal, FText& TextOut)
			{
				bool bRet = false;
				do
				{
					if (!JsonUtils::IsObjectType(JsonVal))
						break;

					// get the prioritized culture name list
					FCultureRef CurrentCulture = FInternationalization::Get().GetCurrentCulture();
					TArray<FString> CultureList = CurrentCulture->GetPrioritizedParentCultureNames();

					bRet = JsonUtils::ForEachObjectPair(JsonVal, [&](const StringView& InName, const JsonType& InVal) -> bool {
						if (CultureList.Contains(InName) && JsonUtils::IsStringType(InVal))
						{
							TextOut = FText::FromString(JsonUtils::AsStringView(InVal));
							bRet = true;
							return true;
						}
						return false;
					});
					if (bRet)
						break;

					for (const FString& LocaleToMatch : CultureList)
					{
						int32 SeparatorPos;
						// only consider base language entries in culture chain (i.e. "en")
						if (!LocaleToMatch.FindChar('-', SeparatorPos))
						{
							bRet = JsonUtils::ForEachObjectPair(JsonVal, [&](const StringView& InName, const JsonType& InVal) -> bool {
								// only consider coupled entries now (base ones would have been matched on first path) (i.e. "en-US")
								if (JsonUtils::IsStringType(InVal))
								{
									FString Str = JsonUtils::AsStringView(InVal);
									if (FCString::Strncmp(*Str, *LocaleToMatch, LocaleToMatch.Len()))
									{
										TextOut = FText::FromString(MoveTemp(Str));
										return true;
									}
								}
								return false;
							});
							if (bRet)
								break;
						}
					}

				} while (false);
				return bRet;
			}

			inline bool FromJsonStr(FString& DateString, UStruct* Struct, void* OutValue)
			{
				do
				{
					if (Struct->GetFName() == GMP::Serializer::NAME_LinearColor)
					{
						FLinearColor& ColorOut = *(FLinearColor*)OutValue;
						ColorOut = FColor::FromHex(*DateString);
						break;
					}
					if (Struct->GetFName() == GMP::Serializer::NAME_DateTime || Struct->GetFName() == GMP::Serializer::NAME_MemResVersion)
					{
						FDateTime& DateTimeOut = *(FDateTime*)OutValue;
						FromJson(DateString, DateTimeOut);
						break;
					}
					if (Struct->GetFName() == GMP::Serializer::NAME_Color)
					{
						FColor& ColorOut = *(FColor*)OutValue;
						ColorOut = FColor::FromHex(*DateString);
						break;
					}
					if (Struct->GetFName() == GMP::Serializer::NAME_Guid)
					{
						FGuid& GuidOut = *(FGuid*)OutValue;
						ensure(FGuid::Parse(*DateString, GuidOut));
						break;
					}
					if (Struct->GetFName() == GMP::Serializer::NAME_Text)
					{
						FText& TextOut = *(FText*)OutValue;
						TextOut = FText::FromString(MoveTemp(DateString));
						break;
					}

					auto ScriptStruct = Cast<UScriptStruct>(Struct);
					if (ScriptStruct && ScriptStruct->GetCppStructOps() && ScriptStruct->GetCppStructOps()->HasImportTextItem())
					{
						UScriptStruct::ICppStructOps* TheCppStructOps = ScriptStruct->GetCppStructOps();
						const TCHAR* ImportTextPtr = *DateString;
						if (TheCppStructOps->ImportTextItem(ImportTextPtr, OutValue, PPF_None, nullptr, (FOutputDevice*)GWarn))
							break;
					}
					return false;
				} while (false);
				return true;
			}

			template<typename JsonType>
			bool FromJsonImpl(const JsonType& JsonVal, UStruct* Struct, void* OutValue)
			{
				if (!JsonUtils::IsObjectType(JsonVal))
					return false;
				if (Struct->IsChildOf(GMP::Reflection::DynamicStruct<FGMPStructUnion>()))
				{
					return FromJson(JsonVal, *reinterpret_cast<FGMPStructUnion*>(OutValue));
				}
#if WITH_GMPVALUE_ONEOF
				else if (Struct->IsChildOf(GMP::Reflection::DynamicStruct<FGMPValueOneOf>()))
				{
					return FromJson(JsonVal, *reinterpret_cast<FGMPValueOneOf*>(OutValue));
				}
#endif
#if defined(STRUCTUTILS_API)
				else if (Struct->IsChildOf(GMP::Reflection::DynamicStruct<FInstancedStruct>()))
				{
					auto& OutStruct = *reinterpret_cast<FInstancedStruct*>(OutValue);
					OutStruct.Reset();
					do
					{
						static FName TypePropName("ScriptStruct");
						auto Val = JsonUtils::FindMember(JsonVal, TypePropName);
						if (!Val || !JsonUtils::IsStringType(*Val))
							break;

						FString ValStr = JsonUtils::AsStringView(*Val);
						UScriptStruct* ScriptStruct = GMP::Reflection::DynamicStruct(ValStr);
						if (!ensure(ScriptStruct))
						{
							GMP_WARNING(TEXT("unnable to resolve type from %s"), *ValStr);
							break;
						}

						static FName DataPropName("StructMemory");
						Val = JsonUtils::FindMember(JsonVal, DataPropName);
						if (!Val || !JsonUtils::IsObjectType(*Val))
							break;
						struct FInstancedStructFriend : public FInstancedStruct
						{
							using FInstancedStruct::SetStructData;
						};
						auto StructMemory = (uint8*)FMemory::Malloc(ScriptStruct->GetStructureSize(), ScriptStruct->GetMinAlignment());
						ScriptStruct->InitializeDefaultValue(StructMemory);
						FromJsonImpl(*Val, ScriptStruct, StructMemory);
						static_cast<FInstancedStructFriend&>(OutStruct).SetStructData(ScriptStruct, StructMemory);
						return true;
					} while (false);
					return false;
				}
#endif
				else if (Struct->GetFName() == GMP::Serializer::NAME_DateTime)
				{
					return FromJson(JsonVal, *reinterpret_cast<FDateTime*>(OutValue));
				}
				else if (Struct->GetFName() == GMP::Serializer::NAME_Text)
				{
					return FromJson(JsonVal, *reinterpret_cast<FText*>(OutValue));
				}
				else
				{
					if (const bool bIsUserdefinedStruct = Struct->IsA(UUserDefinedStruct::StaticClass()))
					{
						for (TFieldIterator<FProperty> It(Struct); It; ++It)
						{
							if (It->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient | CPF_SkipSerialization | CPF_EditorOnly))
								continue;

							FProperty* SubProp = *It;
							auto OriginalName = GMP::Serializer::GetAuthoredFNameForField(SubProp->GetFName());
							if (auto Val = JsonUtils::FindMember(JsonVal, OriginalName))
							{
								ReadFromJson(*Val, SubProp, OutValue);
							}
						}
					}
					else
					{
						JsonUtils::ForEachObjectPair(JsonVal, [&](const StringView& InName, const JsonType& InVal) -> bool {
							FName Name = InName.ToFName();
							if (!Name.IsNone())
							{
								FProperty* SubProp = Struct->FindPropertyByName(Name);
								if (SubProp)
								{
									ReadFromJson(InVal, SubProp, OutValue);
								}
							}
							return false;
						});
					}
				}
				return true;
			}
			//////////////////////////////////////////////////////////////////////////
			struct FValueVisitorBase
			{
				static FString ExportText(FProperty* Prop, const void* Value)
				{
					FString StringValue;
#if UE_5_02_OR_LATER
					Prop->ExportTextItem_Direct(StringValue, Value, NULL, NULL, PPF_None);
#else
					Prop->ExportTextItem(StringValue, Value, NULL, NULL, PPF_None);
#endif
					return StringValue;
				}
				static void ImportText(const TCHAR* Str, FProperty* Prop, void* Addr, int32 ArrIdx)
				{
#if UE_5_02_OR_LATER
					Prop->ImportText_Direct(Str, Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), nullptr, PPF_None);
#else
					Prop->ImportText(Str, Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), PPF_None, nullptr);
#endif
				}

				static bool CanHoldWithDouble(uint64 u)
				{
					volatile double d = static_cast<double>(u);
					return (d >= 0.0) && (d < static_cast<double>((std::numeric_limits<uint64>::max)())) && (u == static_cast<uint64>(d));
				}
				static bool CanHoldWithDouble(int64 i)
				{
					volatile double d = static_cast<double>(i);
					return (d >= static_cast<double>((std::numeric_limits<int64>::min)())) && (d < static_cast<double>((std::numeric_limits<int64>::max)())) && (i == static_cast<int64>(d));
				}
			};

			template<typename P>
			struct TValueVisitorDefault : public FValueVisitorBase
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, P* Prop, const void* Addr, int32 ArrIdx)
				{
					ToJson(Writer, FValueVisitorBase::ExportText(Prop, std::add_const_t<P*>(Prop)->template ContainerPtrToValuePtr<void>(Addr, ArrIdx)));
				}
				static FORCEINLINE void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx) { FValueVisitorBase::ImportText(Val.ToFStringData(), Prop, Addr, ArrIdx); }
				static FORCEINLINE void ReadVisit(const FMonoState& Val, P* Prop, void* Addr, int32 ArrIdx) {}
				template<typename T>
				static FORCEINLINE std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
				}
				template<typename JsonType>
				static FORCEINLINE void ReadVisit(const JsonType* JsonPtr, P* Prop, void* Addr, int32 ArrIdx)
				{
				}
			};

			template<typename P>
			struct TValueVisitor : public TValueVisitorDefault<FProperty>
			{
				using TValueVisitorDefault<FProperty>::ReadVisit;
			};

			template<>
			struct TValueVisitor<FBoolProperty> : public TValueVisitorDefault<FBoolProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FBoolProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					auto BoolVal = Prop->GetPropertyValue(Value);
					using ENumericFmt = Serializer::FNumericFormatter::ENumericFmt;
					if (EnumHasAnyFlags(Serializer::FNumericFormatter::GetType(), ENumericFmt::BoolAsBoolean))
					{
						ToJson(Writer, BoolVal);
					}
					else
					{
						ToJson(Writer, BoolVal ? 0 : 1);
					}
				}
				using TValueVisitorDefault<FBoolProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FBoolProperty* Prop, void* Addr, int32 ArrIdx)
				{
					ensure(Val == 0 || Val == 1);
					Prop->SetPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), !!Val);
				}

				static void ReadVisit(const StringView& Val, FBoolProperty* Prop, void* Addr, int32 ArrIdx)
				{
					Prop->SetPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), Val.IsTCHAR() ? FCString::ToBool(Val.ToTCHAR()) : FCStringAnsi::ToBool(Val.ToANSICHAR()));
				}
			};
			template<>
			struct TValueVisitor<FEnumProperty> : public TValueVisitorDefault<FEnumProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FEnumProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					auto IntVal = Prop->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
					using ENumericFmt = Serializer::FNumericFormatter::ENumericFmt;
					if (EnumHasAnyFlags(Serializer::FNumericFormatter::GetType(), ENumericFmt::EnumAsStr))
					{
						UEnum* EnumDef = Prop->GetEnum();
						FString StringValue = EnumDef->GetNameStringByValue(IntVal);
						ToJson(Writer, StringValue);
					}
					else
					{
						ToJson(Writer, IntVal);
					}
				}

				using TValueVisitorDefault<FEnumProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FEnumProperty* Prop, void* Addr, int32 ArrIdx)
				{
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), (int64)Val);
				}

				static void ReadVisit(const StringView& Val, FEnumProperty* Prop, void* Addr, int32 ArrIdx)
				{
					const UEnum* Enum = Prop->GetEnum();
					check(Enum);
					int64 IntValue = Enum->GetValueByNameString(Val);
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), IntValue);
				}
			};
			template<>
			struct TValueVisitor<FNumericProperty> : public TValueVisitorDefault<FNumericProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FNumericProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					using ENumericFmt = Serializer::FNumericFormatter::ENumericFmt;
					auto FmtType = Serializer::FNumericFormatter::GetType();
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto IntVal = Prop->GetSignedIntPropertyValue(Value);
						if (EnumHasAnyFlags(FmtType, ENumericFmt::EnumAsStr))
						{
							FString StringValue = EnumDef->GetNameStringByValue(IntVal);
							ToJson(Writer, StringValue);
						}
						else
						{
							ToJson(Writer, IntVal);
						}
						return;
					}

					do
					{
						if (Prop->IsFloatingPoint())
						{
							const bool bIsDouble = Prop->IsA<FDoubleProperty>();
							if (bIsDouble)
							{
								double d = CastFieldChecked<FDoubleProperty>(Prop)->GetPropertyValue(Value);
								ToJson(Writer, d);
							}
							else
							{
								float f = CastFieldChecked<FFloatProperty>(Prop)->GetPropertyValue(Value);
								ToJson(Writer, f);
							}
						}
						else if (Prop->IsA<FUInt64Property>())
						{
							if (EnumHasAnyFlags(FmtType, ENumericFmt::UInt64AsStr))
								break;
							uint64 UIntVal = Prop->GetUnsignedIntPropertyValue(Value);
							if (EnumHasAnyFlags(FmtType, ENumericFmt::OverflowAsStr) && !FValueVisitorBase::CanHoldWithDouble(UIntVal))
								break;
							ToJson(Writer, UIntVal);
						}
						else
						{
							const bool bIs64Bits = Prop->IsA<FInt64Property>();
							if (bIs64Bits && EnumHasAnyFlags(FmtType, ENumericFmt::Int64AsStr))
								break;
							int64 IntVal = Prop->GetSignedIntPropertyValue(Value);
							if (bIs64Bits && EnumHasAnyFlags(FmtType, ENumericFmt::OverflowAsStr) && !FValueVisitorBase::CanHoldWithDouble(IntVal))
								break;
							ToJson(Writer, IntVal);
						}
						return;
					} while (false);
					ToJson(Writer, Prop->GetNumericPropertyValueToString(Value));
				}

				using TValueVisitorDefault<FNumericProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FNumericProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (Prop->IsFloatingPoint())
					{
						if (auto FloatProp = CastField<FFloatProperty>(Prop))
							FloatProp->SetPropertyValue(Value, (float)Val);
						else
							Prop->SetFloatingPointPropertyValue(Value, (double)Val);
					}
					else
					{
						Prop->SetIntPropertyValue(Value, (int64)Val);
					}
				}

				static void ReadVisit(const StringView& Val, FNumericProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto EnumVal = EnumDef->GetValueByNameString(Val);
						Prop->SetIntPropertyValue(Value, EnumVal);
					}
					else
					{
						Prop->SetNumericPropertyValueFromString(Value, Val.ToFStringData());
					}
				}
			};

			template<typename P>
			struct TNumericValueVisitor
			{
				using NumericType = typename P::TCppType;
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, P* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					auto Val = Prop->GetPropertyValue(Value);
					GMP_IF_CONSTEXPR(std::is_same<NumericType, uint64>::value || std::is_same<NumericType, int64>::value)
					{
						auto Vall = std::conditional_t<std::is_same<NumericType, uint64>::value, uint64, int64>(Val);
						using ENumericFmt = Serializer::FNumericFormatter::ENumericFmt;
						auto FmtType = Serializer::FNumericFormatter::GetType();
						if (EnumHasAnyFlags(FmtType, std::is_same<NumericType, uint64>::value ? ENumericFmt::UInt64AsStr : ENumericFmt::Int64AsStr) ||
							(EnumHasAnyFlags(FmtType, ENumericFmt::OverflowAsStr) && !TValueVisitor<FNumericProperty>::CanHoldWithDouble(Vall)))
						{
							ToJson(Writer, Prop->GetNumericPropertyValueToString(Value));
							return;
						}
					}
					ToJson(Writer, Val);
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					Prop->SetPropertyValue(Value, Val);
				}
				static void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto* ValuePtr = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					LexFromString(*ValuePtr, Val.ToFStringData());
				}
				static FORCEINLINE void ReadVisit(const FMonoState& Val, P* Prop, void* Addr, int32 ArrIdx) {}
				template<typename JsonType>
				static FORCEINLINE void ReadVisit(const JsonType* JsonPtr, P* Prop, void* Addr, int32 ArrIdx)
				{
				}
			};
			template<>
			struct TValueVisitor<FFloatProperty> : public TNumericValueVisitor<FFloatProperty>
			{
			};
			template<>
			struct TValueVisitor<FDoubleProperty> : public TNumericValueVisitor<FDoubleProperty>
			{
			};
			template<>
			struct TValueVisitor<FInt8Property> : public TNumericValueVisitor<FInt8Property>
			{
			};
			template<>
			struct TValueVisitor<FInt16Property> : public TNumericValueVisitor<FInt16Property>
			{
			};
			template<>
			struct TValueVisitor<FIntProperty> : public TNumericValueVisitor<FIntProperty>
			{
			};
			template<>
			struct TValueVisitor<FInt64Property> : public TNumericValueVisitor<FInt64Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt16Property> : public TNumericValueVisitor<FUInt16Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt32Property> : public TNumericValueVisitor<FUInt32Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt64Property> : public TNumericValueVisitor<FUInt64Property>
			{
			};
			template<>
			struct TValueVisitor<FByteProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FByteProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					auto Val = Prop->GetPropertyValue(Value);
					using ENumericFmt = Serializer::FNumericFormatter::ENumericFmt;
					UEnum* EnumDef = Prop->GetIntPropertyEnum();  // TEnumAsByte
					if (EnumDef && EnumHasAnyFlags(Serializer::FNumericFormatter::GetType(), ENumericFmt::EnumAsStr))
					{
						ToJson(Writer, EnumDef->GetNameStringByValue(Val));
					}
					else
						ToJson(Writer, Val);
				}
				static FORCEINLINE void ReadVisit(const FMonoState& Val, FByteProperty* Prop, void* Addr, int32 ArrIdx) {}
				static FORCEINLINE void ReadVisit(bool Val, FByteProperty* Prop, void* Addr, int32 ArrIdx) {}
				template<typename JsonType>
				static FORCEINLINE void ReadVisit(const JsonType* JsonPtr, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<uint8>(Addr, ArrIdx);
					if (ensureAlways(Val >= 0 && Val <= (std::numeric_limits<uint8>::max)()))
						Prop->SetPropertyValue(Value, (uint8)Val);
				}

				static void ReadVisit(const StringView& Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = Prop->template ContainerPtrToValuePtr<uint8>(Addr, ArrIdx);
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())  // TEnumAsByte
					{
						auto EnumVal = EnumDef->GetValueByNameString(Val);
						if (ensureAlways(EnumVal >= 0 && EnumVal <= (std::numeric_limits<uint8>::max)()))
							Prop->SetPropertyValue(ValuePtr, (uint8)EnumVal);
					}
					else
					{
						LexFromString(*ValuePtr, Val.ToFStringData());
					}
				}
			};
			template<>
			struct TValueVisitor<FStrProperty> : public TValueVisitorDefault<FStrProperty>
			{
				using TValueVisitorDefault<FStrProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FStrProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FString* Str = Prop->template ContainerPtrToValuePtr<FString>(Addr, ArrIdx);
					*Str = LexToString(Val);
				}
				static void ReadVisit(const StringView& Val, FStrProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FString* Str = Prop->template ContainerPtrToValuePtr<FString>(Addr, ArrIdx);
					*Str = FString(Val);
				}
			};
			template<>
			struct TValueVisitor<FNameProperty> : public TValueVisitorDefault<FNameProperty>
			{
				using TValueVisitorDefault<FNameProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FNameProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FName* Name = Prop->template ContainerPtrToValuePtr<FName>(Addr, ArrIdx);
					*Name = Val.ToFName(FNAME_Add);
				}
			};
			template<>
			struct TValueVisitor<FTextProperty> : public TValueVisitorDefault<FTextProperty>
			{
				using TValueVisitorDefault<FTextProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FTextProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FText* Text = Prop->template ContainerPtrToValuePtr<FText>(Addr, ArrIdx);
					*Text = FText::FromString(Val);
				}

				template<typename JsonType>
				static void ReadVisit(const JsonType* JsonPtr, FTextProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FromJson(*JsonPtr, *Prop->template ContainerPtrToValuePtr<FText>(Addr, ArrIdx));
				}
			};

			template<>
			struct TValueVisitor<FSoftObjectProperty> : public TValueVisitorDefault<FSoftObjectProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSoftObjectProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					UObject* Obj = Prop->GetObjectPropertyValue(Value);
					FString StringValue = GIsEditor ? UWorld::RemovePIEPrefix(GetPathNameSafe(Obj)) : GetPathNameSafe(Obj);
					ToJson(Writer, StringValue);
				}
				using TValueVisitorDefault<FSoftObjectProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FSoftObjectProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FSoftObjectPath* OutValue = (FSoftObjectPath*)Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (GIsEditor && GWorld)
					{
#if UE_5_00_OR_LATER
						OutValue->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->GetPIEInstanceID()));
#else
						OutValue->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->PIEInstanceID));
#endif
					}
					else
					{
						if (Val.IsTCHAR())
							OutValue->SetPath(FStringView(Val));
						else
							OutValue->SetPath(FAnsiStringView(Val));
					}
				}
			};

			template<>
			struct TValueVisitor<FStructProperty> : public TValueVisitorDefault<FStructProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FStructProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					ToJsonImpl(Writer, Prop->Struct, Value);
				}

				using TValueVisitorDefault<FStructProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FStructProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Struct = Prop->Struct;
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					FString DataString = Val;
					if (!FromJsonStr(DataString, Struct, OutValue))
						FValueVisitorBase::ImportText(*DataString, Prop, Addr, ArrIdx);
				}
				template<typename JsonType>
				static void ReadVisit(const JsonType* JsonPtr, FStructProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& JsonVal = *JsonPtr;
					auto Struct = Prop->Struct;
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					FromJsonImpl(JsonVal, Struct, OutValue);
				}
			};
			template<>
			struct TValueVisitor<FArrayProperty> : public TValueVisitorDefault<FArrayProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FArrayProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					GMP_ENSURE_JSON(Writer.StartArray());
					FScriptArrayHelper Helper(Prop, Value);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						WriteToJson(Writer, Prop->Inner, Helper.GetRawPtr(i));
					}
					GMP_ENSURE_JSON(Writer.EndArray());
				}
				using TValueVisitorDefault<FArrayProperty>::ReadVisit;
				template<typename JsonType>
				static void ReadVisit(const JsonType* JsonPtr, FArrayProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& JsonVal = *JsonPtr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					if (GMP_ENSURE_JSON(JsonUtils::IsArrayType(JsonVal)))
					{
						auto ItemsToRead = FMath::Max((int32)JsonUtils::ArraySize(JsonVal), 0);
						FScriptArrayHelper Helper(Prop, OutValue);
						Helper.Resize(ItemsToRead);
						for (auto i = 0; i < Helper.Num(); ++i)
						{
							ReadFromJson(JsonUtils::ArrayElm(JsonVal, i), Prop->Inner, Helper.GetRawPtr(i));
						}
					}
					else
					{
						FScriptArrayHelper Helper(Prop, OutValue);
						Helper.Resize(1);
						ReadFromJson(JsonVal, Prop->Inner, Helper.GetRawPtr(0));
					};
				}
			};
			template<>
			struct TValueVisitor<FSetProperty> : public TValueVisitorDefault<FSetProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSetProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					GMP_ENSURE_JSON(Writer.StartArray());
					FScriptSetHelper Helper(Prop, Value);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						if (Helper.IsValidIndex(i))
						{
							WriteToJson(Writer, Prop->ElementProp, Helper.GetElementPtr(i));
						}
					}
					GMP_ENSURE_JSON(Writer.EndArray());
				}
				using TValueVisitorDefault<FSetProperty>::ReadVisit;
				template<typename JsonType>
				static void ReadVisit(const JsonType* JsonPtr, FSetProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& JsonVal = *JsonPtr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					if (GMP_ENSURE_JSON(JsonUtils::IsArrayType(JsonVal)))
					{
						FScriptSetHelper Helper(Prop, OutValue);
						for (auto i = 0; i < JsonUtils::ArraySize(JsonVal); ++i)
						{
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							ReadFromJson(JsonUtils::ArrayElm(JsonVal, i), Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						}
						Helper.Rehash();
					}
					else
					{
						FScriptSetHelper Helper(Prop, OutValue);
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						ReadFromJson(JsonVal, Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						Helper.Rehash();
					}
				}
			};
			template<>
			struct TValueVisitor<FMapProperty> : public TValueVisitorDefault<FMapProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FMapProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					GMP_ENSURE_JSON(Writer.StartObject());
					FScriptMapHelper Helper(Prop, Value);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						if (Helper.IsValidIndex(i))
						{
							FString StrVal = FValueVisitorBase::ExportText(Prop->KeyProp, Helper.GetKeyPtr(i));
							GMP_ENSURE_JSON(Writer.Key(*StrVal, StrVal.Len()));
							WriteToJson(Writer, Prop->ValueProp, Helper.GetValuePtr(i));
						}
					}
					GMP_ENSURE_JSON(Writer.EndObject());
				}
				using TValueVisitorDefault<FMapProperty>::ReadVisit;
				template<typename JsonType>
				static void ReadVisit(JsonType* JsonPtr, FMapProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& JsonVal = *JsonPtr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					FScriptMapHelper Helper(Prop, OutValue);
					if (GMP_ENSURE_JSON(JsonUtils::IsObjectType(JsonVal)))
					{
						JsonUtils::ForEachObjectPair(JsonVal, [&](const StringView& InName, const JsonType& InVal) -> bool {
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							TValueVisitor<FProperty>::ReadVisit(InName, Prop->KeyProp, Helper.GetKeyPtr(NewIndex), 0);
							ReadFromJson(InVal, Prop->ValueProp, Helper.GetValuePtr(NewIndex));
							return false;
						});
						Helper.Rehash();
					}
				}
			};

			template<typename P>
			struct TValueDispatcher
			{
				template<typename WriterType>
				static bool Write(WriterType& Writer, P* Prop, const void* Value)
				{
					if (Prop->ArrayDim <= 1)
					{
						TValueVisitor<P>::WriteVisit(Writer, Prop, Value, 0);
					}
					else
					{
						GMP_ENSURE_JSON(Writer.StartArray());
						for (auto i = 0; i < Prop->ArrayDim; ++i)
						{
							TValueVisitor<P>::WriteVisit(Writer, Prop, Value, i);
						}
						GMP_ENSURE_JSON(Writer.EndArray());
					}
					return true;
				}

				template<typename JsonType>
				static bool Read(const JsonType& JsonVal, P* Prop, void* Addr)
				{
					int32 i = 0;
					auto Visitor = [&](auto&& Elm) { TValueVisitor<P>::ReadVisit(std::forward<decltype(Elm)>(Elm), Prop, Addr, i); };
					if (JsonUtils::IsArrayType(JsonVal) && !CastField<FArrayProperty>(Prop) && !CastField<FSetProperty>(Prop))
					{
						auto ItemsToRead = FMath::Clamp((int32)JsonVal.Size(), 0, Prop->ArrayDim);
						for (; i < ItemsToRead; ++i)
						{
#if GMP_USE_STD_VARIANT
							std::visit(Visitor, JsonUtils::DispatchValue(JsonUtils::ArrayElm(JsonVal, i)));
#else
							Visit(Visitor, JsonUtils::DispatchValue(JsonUtils::ArrayElm(JsonVal, i)));
#endif
						}
					}
					else
					{
#if GMP_USE_STD_VARIANT
						std::visit(Visitor, JsonUtils::DispatchValue(JsonVal));
#else
						Visit(Visitor, JsonUtils::DispatchValue(JsonVal));
#endif
					}
					return true;
				}
			};
		}  // namespace Internal
		template<typename WriterType>
		bool WriteToJson(WriterType& Writer, FProperty* Prop, const void* Value)
		{
			return GMP::Serializer::Traits::ForeachProp([](auto& OutVal, auto* InProp, const void* InVal) -> bool { return Internal::TValueDispatcher<std::decay_t<decltype(*InProp)>>::Write(OutVal, InProp, InVal); }, Writer, Prop, Value);
		}
		template<typename JsonType>
		bool ReadFromJson(const JsonType& JsonVal, FProperty* Prop, void* Value)
		{
			return GMP::Serializer::Traits::ForeachProp([](auto& InVal, auto* InProp, void* OutVal) -> bool { return Internal::TValueDispatcher<std::decay_t<decltype(*InProp)>>::Read(InVal, InProp, OutVal); }, JsonVal, Prop, Value);
		}
	}  // namespace Detail

	template<typename WriterType, typename DataType>
	std::enable_if_t<GMP::TClassToPropTag<DataType>::value, bool> WriterToJson(WriterType& Writer, const DataType& Data)
	{
		return Detail::WriteToJson(Writer, TClass2Prop<DataType>::GetProperty(), &Data);
	}

	template<typename InputStream, typename DataType>
	std::enable_if_t<GMP::TClassToPropTag<DataType>::value, bool> ReadFromJson(InputStream& Stream, DataType& Data)
	{
		return Detail::ReadFromJson(Stream, GMP::TClass2Prop<DataType>::GetProperty(), &Data);
	}
}  // namespace Json
}  // namespace GMP
