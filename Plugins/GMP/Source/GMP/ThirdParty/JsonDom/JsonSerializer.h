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

// The condensed/pretty writer now lives in JsonArena.h (arena_detail::WriteNode); serialization walks
// the arena tree directly. No separate legacy DOM writer remains.

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
	// Read side: declared only; implemented in JsonDom.inl, the sole TU that includes rapidjson. Parses
	static bool Deserialize(const TSharedRef<FJsonStringReader>& Reader, FJsonValuePtr& OutValue);
	static bool DeserializeArray(const TSharedRef<FJsonStringReader>& Reader, FJsonArrayView& OutArray);

	template <typename CharT, typename Policy>
	static bool Serialize(const FJsonObjectPtr& Object, const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		arena_detail::WriteNode(*Writer->Out, Object.Node, Writer->bPretty, 0);
		return true;
	}

	template <typename CharT, typename Policy>
	static bool Serialize(const TArray<FJsonValuePtr>& Array, const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		FString& Out = *Writer->Out;
		const bool bPretty = Writer->bPretty;
		Out.AppendChar(TCHAR('['));
		for (int32_t i = 0; i < (int32_t)Array.Num(); ++i)
		{
			if (i) Out.AppendChar(TCHAR(','));
			arena_detail::WriteIndent(Out, bPretty, 1);
			arena_detail::WriteNode(Out, Array[i].Node, bPretty, 1);
		}
		if (Array.Num() > 0) arena_detail::WriteIndent(Out, bPretty, 0);
		Out.AppendChar(TCHAR(']'));
		return true;
	}

	// UE overload: serialize an arbitrary top-level value handle (Identifier unused for a bare value).
	template <typename CharT, typename Policy>
	static bool Serialize(const FJsonValuePtr& Value, const FString& /*Identifier*/,
		const TSharedRef<TJsonWriter<CharT, Policy>>& Writer)
	{
		arena_detail::WriteNode(*Writer->Out, Value.Node, Writer->bPretty, 0);
		return true;
	}
};

} // namespace jsondom

// Default (inline header-only) mode: pull in the parse impl so including this header is enough.
#if !defined(JSONDOM_ISOLATED_IMPL) && (!defined(GMP_WITH_JSONDOM) || GMP_WITH_JSONDOM)
#include "JsonDom/JsonDom.inl"
#endif

#endif // UNREAL_JSONDOM_SERIALIZER_H
