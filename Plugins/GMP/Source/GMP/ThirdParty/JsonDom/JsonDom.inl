//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// JsonDom parse implementation — the sole place that includes rapidjson.
// Two modes (selected in JsonEncoding.h):
//   - default (inline header-only): entry points are inline; JsonSerializer.h auto-includes this
//     file, so any TU that includes the header gets the impl (rapidjson comes with it).
//   - JSONDOM_ISOLATED_IMPL: a single host TU includes this file once, keeping rapidjson out of
//     every other TU; predefine JSONDOM_API to a host export macro to share it across modules.
// The platform layer (CoreMinimal.h / UECompat.h) must supply TCHAR/FString/TArray/TSharedPtr and
// JSONDOM_ENCODING_UTF8 before this file is reached.
//
// A content guard, not just `#pragma once`: two vendored copies of this package are two files to
// `#pragma once` but one implementation here, and it may be defined only once.
#pragma once

#ifndef UNREAL_JSONDOM_IMPL_INL
#define UNREAL_JSONDOM_IMPL_INL

#include "JsonDom/JsonSerializer.h"
#include "JsonDom/JsonEncoding.h"

#include "rapidjson/document.h"

namespace JSONDOM_NAMESPACE
{
#if JSONDOM_ENCODING_UTF8
	using FRapidEncoding = rapidjson::UTF8<TCHAR>;
#else
	using FRapidEncoding = rapidjson::UTF16LE<TCHAR>;
#endif
	using FRapidDocument = rapidjson::GenericDocument<FRapidEncoding>;
	using FRapidValue = rapidjson::GenericValue<FRapidEncoding>;

	// Build an arena node subtree from a rapidjson value (parse -> arena tree, single doc).
	JSONDOM_IMPL_INLINE FArenaNode* FromRapid(const TSharedPtr<FArenaDoc>& D, const FRapidValue& RV)
	{
		if (RV.IsObject())
		{
			FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Object; N->Obj = D->NewObj();
			for (auto It = RV.MemberBegin(); It != RV.MemberEnd(); ++It)
			{
				const TCHAR* KeyPtr = It->name.GetString();
				const int32_t KeyLen = (int32_t)It->name.GetStringLength();
				N->Obj->Set(KeyPtr, KeyLen, FromRapid(D, It->value));
			}
			return N;
		}
		if (RV.IsArray())
		{
			FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Array;
			const int32_t Cnt = (int32_t)RV.Size();
			FArenaNode** Items = (FArenaNode**)D->Arena.Alloc(sizeof(FArenaNode*) * (size_t)(Cnt > 0 ? Cnt : 1), alignof(FArenaNode*));
			int32_t i = 0;
			for (auto It = RV.Begin(); It != RV.End(); ++It) Items[i++] = FromRapid(D, *It);
			N->Arr.Items = Items; N->Arr.Count = Cnt;
			return N;
		}
		if (RV.IsString())
		{
			FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::String;
			N->Str.Ptr = D->Arena.CopyStr(RV.GetString(), (int32_t)RV.GetStringLength()); N->Str.Len = (int32_t)RV.GetStringLength();
			return N;
		}
		if (RV.IsBool())   { FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Boolean; N->B = RV.GetBool(); return N; }
		if (RV.IsNull())   { FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Null; return N; }
		if (RV.IsNumber()) { FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Number; N->N = RV.GetDouble(); return N; }
		FArenaNode* N = D->NewNode(); N->Type = (uint8_t)EJson::Null; return N;
	}

// Single Deserialize (object/value handle are the same type). Requires an object top level (matches the
// legacy object-overload behavior every call site relies on: `if (!Deserialize(R, Root) || !Root.IsValid())`).
JSONDOM_API bool FJsonSerializer::Deserialize(const TSharedRef<FJsonStringReader>& Reader, FJsonValuePtr& OutValue)
{
	FRapidDocument Doc;
	// Native TCHAR buffer; encoding matches the document instantiation. Full precision because the
	// default fast path is off by up to a few ulp, and a shortest-round-trip number has to come back
	// as the same double.
	Doc.Parse<rapidjson::kParseFullPrecisionFlag>(*Reader->Content);
	if (Doc.HasParseError() || !Doc.IsObject()) return false;
	auto D = MakeShared<FArenaDoc>();
	FArenaNode* Root = FromRapid(D, Doc);
	if (!Root || Root->Type != (uint8_t)EJson::Object) return false;
	D->Root = Root;
	OutValue = FJsonValuePtr(Root, D);
	return true;
}

JSONDOM_API bool FJsonSerializer::DeserializeArray(const TSharedRef<FJsonStringReader>& Reader, FJsonArrayView& OutArray)
{
	FRapidDocument Doc;
	Doc.Parse<rapidjson::kParseFullPrecisionFlag>(*Reader->Content);
	if (Doc.HasParseError() || !Doc.IsArray()) return false;
	auto D = MakeShared<FArenaDoc>();
	FArenaNode* Root = FromRapid(D, Doc);
	if (!Root || Root->Type != (uint8_t)EJson::Array) return false;
	D->Root = Root;
	OutArray = FJsonArrayView(Root->Arr.Items, Root->Arr.Count, D);
	return true;
}

}  // namespace JSONDOM_NAMESPACE

#endif // UNREAL_JSONDOM_IMPL_INL
