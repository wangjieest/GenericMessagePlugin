//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// JsonDom parse implementation — the sole place that includes rapidjson.
// Two modes (selected in JsonEncoding.h):
//   - default (inline header-only): JSONDOM_IMPL_INLINE == inline; JsonSerializer.h auto-includes
//     this file, so any TU that includes the header gets the impl (rapidjson comes with it).
//   - JSONDOM_ISOLATED_IMPL: JSONDOM_IMPL_INLINE is empty; a single host TU includes this file once,
//     keeping rapidjson out of every other TU.
// The platform layer (CoreMinimal.h / UECompat.h) must supply TCHAR/FString/TArray/TSharedPtr and
// JSONDOM_ENCODING_UTF8 before this file is reached.
#pragma once

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

	JSONDOM_IMPL_INLINE TSharedPtr<FJsonValue> FromRapid(const FRapidValue& RV)
	{
		if (RV.IsObject())
		{
			auto Obj = MakeShared<FJsonObject>();
			for (auto It = RV.MemberBegin(); It != RV.MemberEnd(); ++It)
			{
				FString Key(It->name.GetString());   // FString(const TCHAR*): native, no transcode
				Obj->Values.Set(Key, FromRapid(It->value));
			}
			auto V = MakeShared<FJsonValue>();
			V->Type = EJson::Object;
			V->ObjectVal = Obj;
			return V;
		}
		if (RV.IsArray())
		{
			auto V = MakeShared<FJsonValue>();
			V->Type = EJson::Array;
			for (auto It = RV.Begin(); It != RV.End(); ++It) V->ArrayVal.Add(FromRapid(*It));
			return V;
		}
		if (RV.IsString()) return FJsonValue::MakeString(FString(RV.GetString()));
		if (RV.IsBool()) return FJsonValue::MakeBool(RV.GetBool());
		if (RV.IsNull()) return FJsonValue::MakeNull();
		if (RV.IsNumber()) return FJsonValue::MakeNumber(RV.GetDouble());
		return FJsonValue::MakeNull();
	}

JSONDOM_IMPL_INLINE bool FJsonSerializer::Deserialize(const TSharedRef<FJsonStringReader>& Reader, TSharedPtr<FJsonObject>& OutObject)
{
	FRapidDocument Doc;
	Doc.Parse(*Reader->Content);   // native TCHAR buffer; encoding matches document instantiation
	if (Doc.HasParseError() || !Doc.IsObject()) return false;
	TSharedPtr<FJsonValue> Root = FromRapid(Doc);
	if (!Root || Root->Type != EJson::Object || !Root->ObjectVal) return false;
	OutObject = Root->ObjectVal;
	return true;
}

JSONDOM_IMPL_INLINE bool FJsonSerializer::Deserialize(const TSharedRef<FJsonStringReader>& Reader, TSharedPtr<FJsonValue>& OutValue)
{
	FRapidDocument Doc;
	Doc.Parse(*Reader->Content);
	if (Doc.HasParseError()) return false;
	OutValue = FromRapid(Doc);
	return OutValue.IsValid();
}

JSONDOM_IMPL_INLINE bool FJsonSerializer::DeserializeArray(const TSharedRef<FJsonStringReader>& Reader, TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	FRapidDocument Doc;
	Doc.Parse(*Reader->Content);
	if (Doc.HasParseError() || !Doc.IsArray()) return false;
	TSharedPtr<FJsonValue> Root = FromRapid(Doc);
	if (!Root || Root->Type != EJson::Array) return false;
	OutArray = Root->ArrayVal;
	return true;
}

}  // namespace JSONDOM_NAMESPACE
