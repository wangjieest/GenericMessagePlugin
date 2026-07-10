//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// FJsonSerializer + reader/writer factories mirroring UE Serialization/JsonSerializer.h.
// Parse uses rapidjson instantiated with FJsonEncoding, so it consumes the platform's native
// TCHAR buffer (UTF-16 under UE, UTF-8 under standalone) with no transcoding. The write path is
// hand-rolled over TCHAR to control byte output (insertion order, UE number format, escaping).
#pragma once

// Content guard (two identical copies exist: SigilCore/ThirdParty + GMP); first-seen wins.
#ifndef UNREAL_JSONDOM_SERIALIZER_H
#define UNREAL_JSONDOM_SERIALIZER_H

#include "JsonDom.h"
#include "JsonEncoding.h"

namespace JSONDOM_NAMESPACE
{
// ---- Reader: thin wrapper carrying source text (matches UE TJsonReaderFactory::Create shape) ----
class FJsonStringReader
{
public:
	FString Content;
	explicit FJsonStringReader(const FString& In) : Content(In) {}
};
template <typename T = TCHAR> using TJsonReader = FJsonStringReader;

struct TJsonReaderFactoryHelper
{
	static TSharedRef<FJsonStringReader> Create(const FString& Content)
	{
		return MakeShared<FJsonStringReader>(Content);
	}
};
template <typename T = TCHAR> using TJsonReaderFactory = TJsonReaderFactoryHelper;

// ---- Print policies (tag types selecting condensed vs pretty output) ----
template <typename CharT = TCHAR>
struct TCondensedJsonPrintPolicy { static constexpr bool bPretty = false; };
template <typename CharT = TCHAR>
struct TPrettyJsonPrintPolicy { static constexpr bool bPretty = true; };

namespace detail
{
	inline void AppendEscaped(FString& Out, const FString& S)
	{
		Out += TEXT("\"");
		const TCHAR* P = *S;
		const int32_t N = (int32_t)S.Len();
		for (int32_t i = 0; i < N; ++i)
		{
			TCHAR c = P[i];
			switch (c)
			{
				case TCHAR('"'):  Out += TEXT("\\\""); break;
				case TCHAR('\\'): Out += TEXT("\\\\"); break;
				case TCHAR('\b'): Out += TEXT("\\b");  break;
				case TCHAR('\f'): Out += TEXT("\\f");  break;
				case TCHAR('\n'): Out += TEXT("\\n");  break;
				case TCHAR('\r'): Out += TEXT("\\r");  break;
				case TCHAR('\t'): Out += TEXT("\\t");  break;
				default:
					if ((uint32_t)c < 0x20u)
					{
						Out += FString::Printf(TEXT("\\u%04x"), (uint32_t)c);
					}
					else
					{
						Out.AppendChar(c);
					}
					break;
			}
		}
		Out += TEXT("\"");
	}

	inline void WriteIndent(FString& Out, bool bPretty, int Depth)
	{
		if (!bPretty) return;
		Out.AppendChar(TCHAR('\n'));
		for (int i = 0; i < Depth; ++i) Out.AppendChar(TCHAR('\t'));
	}

	inline void WriteValue(FString& Out, const TSharedPtr<FJsonValue>& V, bool bPretty, int Depth)
	{
		if (!V) { Out += TEXT("null"); return; }
		switch (V->Type)
		{
			case EJson::Null:
			case EJson::None:
				Out += TEXT("null");
				break;
			case EJson::String:
				AppendEscaped(Out, V->StringVal);
				break;
			case EJson::Number:
				Out += NumberToJsonString(V->NumberVal);
				break;
			case EJson::Boolean:
				Out += V->BoolVal ? TEXT("true") : TEXT("false");
				break;
			case EJson::Array:
			{
				Out.AppendChar(TCHAR('['));
				const auto& Arr = V->ArrayVal;
				for (int32_t i = 0; i < Arr.Num(); ++i)
				{
					if (i) Out.AppendChar(TCHAR(','));
					WriteIndent(Out, bPretty, Depth + 1);
					WriteValue(Out, Arr[i], bPretty, Depth + 1);
				}
				if (Arr.Num() > 0) WriteIndent(Out, bPretty, Depth);
				Out.AppendChar(TCHAR(']'));
				break;
			}
			case EJson::Object:
			{
				Out.AppendChar(TCHAR('{'));
				const FJsonObject* Obj = V->ObjectVal.Get();
				bool bFirst = true;
				if (Obj)
				{
					for (const auto& Pair : Obj->Values)
					{
						if (!bFirst) Out.AppendChar(TCHAR(','));
						bFirst = false;
						WriteIndent(Out, bPretty, Depth + 1);
						AppendEscaped(Out, Pair.Key);
						Out.AppendChar(TCHAR(':'));
						if (bPretty) Out.AppendChar(TCHAR(' '));
						WriteValue(Out, Pair.Value, bPretty, Depth + 1);
					}
					if (!bFirst) WriteIndent(Out, bPretty, Depth);
				}
				Out.AppendChar(TCHAR('}'));
				break;
			}
		}
	}
} // namespace detail

// ---- Writer: accumulates into a target FString; mirrors UE TJsonWriter usage shape ----
template <typename CharT = TCHAR, typename Policy = TCondensedJsonPrintPolicy<TCHAR>>
class TJsonWriter
{
public:
	explicit TJsonWriter(FString* InOut) : Out(InOut) {}
	FString* Out;
	bool bPretty = Policy::bPretty;
};

template <typename CharT = TCHAR, typename Policy = TCondensedJsonPrintPolicy<TCHAR>>
struct TJsonWriterFactory
{
	static TSharedRef<TJsonWriter<CharT, Policy>> Create(FString* Out)
	{
		return MakeShared<TJsonWriter<CharT, Policy>>(Out);
	}
};

struct FJsonSerializer
{
	// Read side: declared only; implemented in JsonDom.inl, the sole TU that includes rapidjson.
	static bool Deserialize(const TSharedRef<FJsonStringReader>& Reader, TSharedPtr<FJsonObject>& OutObject);
	// UE overload: deserialize any top-level JSON (object/array/scalar) into a single FJsonValue.
	static bool Deserialize(const TSharedRef<FJsonStringReader>& Reader, TSharedPtr<FJsonValue>& OutValue);
	static bool DeserializeArray(const TSharedRef<FJsonStringReader>& Reader, TArray<TSharedPtr<FJsonValue>>& OutArray);

	template <typename CharT, typename Policy>
	static bool Serialize(const TSharedPtr<FJsonObject>& Object, const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		auto V = MakeShared<FJsonValueObject>(Object);
		detail::WriteValue(*Writer->Out, V, Writer->bPretty, 0);
		return true;
	}

	template <typename CharT, typename Policy>
	static bool Serialize(const TArray<TSharedPtr<FJsonValue>>& Array, const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		auto V = MakeShared<FJsonValue>();
		V->Type = EJson::Array; V->ArrayVal = Array;
		detail::WriteValue(*Writer->Out, V, Writer->bPretty, 0);
		return true;
	}

	// UE overload: serialize an arbitrary top-level FJsonValue (Identifier unused for a bare value).
	template <typename CharT, typename Policy>
	static bool Serialize(const TSharedPtr<FJsonValue>& Value, const FString& /*Identifier*/,
		const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		detail::WriteValue(*Writer->Out, Value, Writer->bPretty, 0);
		return true;
	}
};

} // namespace jsondom

// Default (inline header-only) mode: pull in the parse impl so including this header is enough.
// JSONDOM_ISOLATED_IMPL opts out (a single host TU includes JsonDom.inl instead). GMP_WITH_JSONDOM==0
// disables JsonDom entirely (keeps rapidjson out even in header-only mode).
#if !defined(JSONDOM_ISOLATED_IMPL) && (!defined(GMP_WITH_JSONDOM) || GMP_WITH_JSONDOM)
#include "JsonDom/JsonDom.inl"
#endif

#endif // UNREAL_JSONDOM_SERIALIZER_H
