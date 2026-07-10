//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// JsonDom — a JSON DOM whose public type names and API mirror UE's Dom/JsonObject.h +
// Dom/JsonValue.h so call sites written against UE Json compile unchanged. Backed by rapidjson.
//
// Encoding: the DOM speaks FString/TCHAR only. Default is UTF-16 (UE native form); a standalone
// build swaps TCHAR/FString to UTF-8 via its Compat layer. The DOM code itself never references any
// build-mode macro — the platform layer (CoreMinimal.h or UECompat.h) supplies TCHAR/FString/TEXT.
#pragma once

#ifndef UNREAL_JSONDOM_H
#define UNREAL_JSONDOM_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <string>   // FKeyStore = std::basic_string<TCHAR>

#include "JsonEncoding.h"   // FJsonEncoding + requires TCHAR/FString/TEXT from platform layer

namespace JSONDOM_NAMESPACE
{
// Container/pointer types are supplied by the platform layer (UE CoreMinimal or UECompat) so the
// DOM's public signatures use the SAME TArray/TSharedPtr the call sites use. The DOM never defines
// its own — it lifts the global ones. MakeShared is likewise the platform's.
using ::TArray;
using ::TSharedPtr;
using ::TSharedRef;

enum class EJson : uint8_t { None, Null, String, Number, Boolean, Array, Object };

class FJsonObject;

// Format a double the UE way: integral values print without a fractional part; otherwise a
// compact round-trippable form. Uses FString::Printf so output is TCHAR (correct on both sides).
inline FString NumberToJsonString(double N)
{
	double Int = 0.0;
	const bool bIntegral = (N == (double)(long long)N);
	(void)Int;
	if (bIntegral)
	{
		return FString::Printf(TEXT("%lld"), (long long)N);
	}
	return FString::Printf(TEXT("%.17g"), N);
}

// ---------------------------------------------------------------------------
// FJsonValue — variant node mirroring UE FJsonValue's As*/TryGet* type coercion.
// ---------------------------------------------------------------------------
class FJsonValue
{
public:
	EJson Type = EJson::Null;

	bool BoolVal = false;
	double NumberVal = 0.0;
	FString StringVal;
	TSharedPtr<FJsonObject> ObjectVal;
	TArray<TSharedPtr<FJsonValue>> ArrayVal;

	FJsonValue() = default;
	virtual ~FJsonValue() = default;

	TSharedPtr<FJsonObject> AsObject() const { return ObjectVal; }
	const TArray<TSharedPtr<FJsonValue>>& AsArray() const { return ArrayVal; }

	FString AsString() const
	{
		if (Type == EJson::String) return StringVal;
		if (Type == EJson::Number) return NumberToJsonString(NumberVal);
		if (Type == EJson::Boolean) return BoolVal ? FString(TEXT("true")) : FString(TEXT("false"));
		return FString();
	}
	double AsNumber() const
	{
		if (Type == EJson::Number) return NumberVal;
		if (Type == EJson::String) return FCString::Atod(*StringVal);
		return 0.0;
	}
	bool AsBool() const
	{
		if (Type == EJson::Boolean) return BoolVal;
		if (Type == EJson::Number) return NumberVal != 0.0;
		return false;
	}

	bool TryGetString(FString& Out) const
	{
		if (Type == EJson::String) { Out = StringVal; return true; }
		if (Type == EJson::Number) { Out = AsString(); return true; }
		if (Type == EJson::Boolean) { Out = AsString(); return true; }
		return false;
	}
	bool TryGetNumber(double& Out) const
	{
		if (Type == EJson::Number) { Out = NumberVal; return true; }
		if (Type == EJson::String) { Out = FCString::Atod(*StringVal); return true; }
		return false;
	}
	bool TryGetBool(bool& Out) const
	{
		if (Type == EJson::Boolean) { Out = BoolVal; return true; }
		return false;
	}
	bool TryGetArray(const TArray<TSharedPtr<FJsonValue>>*& Out) const
	{
		if (Type != EJson::Array) return false;
		Out = &ArrayVal;
		return true;
	}
	bool TryGetObject(const TSharedPtr<FJsonObject>*& Out) const
	{
		if (Type != EJson::Object || !ObjectVal) return false;
		Out = &ObjectVal;
		return true;
	}

	bool IsNull() const { return Type == EJson::Null || Type == EJson::None; }

	static TSharedPtr<FJsonValue> MakeNull()                 { auto V = MakeShared<FJsonValue>(); V->Type = EJson::Null;    return V; }
	static TSharedPtr<FJsonValue> MakeString(const FString& S) { auto V = MakeShared<FJsonValue>(); V->Type = EJson::String; V->StringVal = S; return V; }
	static TSharedPtr<FJsonValue> MakeNumber(double N)         { auto V = MakeShared<FJsonValue>(); V->Type = EJson::Number; V->NumberVal = N; return V; }
	static TSharedPtr<FJsonValue> MakeBool(bool B)            { auto V = MakeShared<FJsonValue>(); V->Type = EJson::Boolean; V->BoolVal = B; return V; }
};

// UE-named concrete subclasses (call sites do MakeShared<FJsonValueObject>(Obj), etc.)
class FJsonValueObject : public FJsonValue
{
public:
	explicit FJsonValueObject(TSharedPtr<FJsonObject> InObj) { Type = EJson::Object; ObjectVal = InObj; }
};
class FJsonValueString : public FJsonValue
{
public:
	explicit FJsonValueString(const FString& S) { Type = EJson::String; StringVal = S; }
};
class FJsonValueNumber : public FJsonValue
{
public:
	explicit FJsonValueNumber(double N) { Type = EJson::Number; NumberVal = N; }
};
class FJsonValueBoolean : public FJsonValue
{
public:
	explicit FJsonValueBoolean(bool B) { Type = EJson::Boolean; BoolVal = B; }
};
class FJsonValueArray : public FJsonValue
{
public:
	explicit FJsonValueArray(const TArray<TSharedPtr<FJsonValue>>& A) { Type = EJson::Array; ArrayVal = A; }
	FJsonValueArray() { Type = EJson::Array; }
};
class FJsonValueNull : public FJsonValue
{
public:
	FJsonValueNull() { Type = EJson::Null; }
};

// ---------------------------------------------------------------------------
// FJsonObject — insertion-ordered key->value store with UE FJsonObject accessors.
// Keys are FString; the underlying map keys on the raw character buffer so lookups are encoding-safe.
// ---------------------------------------------------------------------------
class FJsonObject
{
public:
	struct FOrderedValues
	{
		// Map keyed by a std::basic_string<TCHAR> derived from FString, preserving exact code units.
		using FKeyStore = std::basic_string<TCHAR>;
		static FKeyStore ToStore(const FString& K) { return FKeyStore(*K, (size_t)K.Len()); }

		std::vector<FString> Order;
		std::unordered_map<FKeyStore, TSharedPtr<FJsonValue>> Map;

		struct FEntry { const FString& Key; const TSharedPtr<FJsonValue>& Value; };
		struct FIterator
		{
			const FOrderedValues* Owner;
			size_t Idx;
			mutable TSharedPtr<FJsonValue> Cached;
			FEntry operator*() const
			{
				const FString& K = Owner->Order[Idx];
				Cached = Owner->Map.at(ToStore(K));
				return FEntry{ K, Cached };
			}
			FIterator& operator++() { ++Idx; return *this; }
			bool operator!=(const FIterator& O) const { return Idx != O.Idx; }
		};
		FIterator begin() const { return FIterator{ this, 0, nullptr }; }
		FIterator end() const { return FIterator{ this, Order.size(), nullptr }; }

		void Set(const FString& K, const TSharedPtr<FJsonValue>& V)
		{
			FKeyStore Ks = ToStore(K);
			if (Map.find(Ks) == Map.end()) Order.push_back(K);
			Map[Ks] = V;
		}
		const TSharedPtr<FJsonValue>* Find(const FString& K) const
		{
			auto It = Map.find(ToStore(K));
			return It != Map.end() ? &It->second : nullptr;
		}
		bool Contains(const FString& K) const { return Map.find(ToStore(K)) != Map.end(); }
		int32_t Num() const { return (int32_t)Order.size(); }

		// Snapshot the keys in insertion order (UE FJsonObject::Values.GetKeys shape).
		void GetKeys(TArray<FString>& Out) const { for (const FString& K : Order) Out.Add(K); }

		// Remove a key (UE-style); returns whether it existed.
		bool Remove(const FString& K)
		{
			FKeyStore Ks = ToStore(K);
			auto It = Map.find(Ks);
			if (It == Map.end()) return false;
			Map.erase(It);
			for (auto OIt = Order.begin(); OIt != Order.end(); ++OIt)
				if (*OIt == K) { Order.erase(OIt); break; }
			return true;
		}
	};

	FOrderedValues Values;

	TSharedPtr<FJsonValue> TryGetField(const FString& Key) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return V ? *V : TSharedPtr<FJsonValue>();
	}
	bool HasField(const FString& Key) const { return Values.Contains(Key); }
	bool HasTypedField(const FString& Key, EJson InType) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return V && *V && (*V)->Type == InType;
	}

	bool TryGetObjectField(const FString& Key, const TSharedPtr<FJsonObject>*& Out) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		if (!V || !(*V) || (*V)->Type != EJson::Object || !(*V)->ObjectVal) return false;
		Out = &(*V)->ObjectVal;
		return true;
	}
	bool TryGetArrayField(const FString& Key, const TArray<TSharedPtr<FJsonValue>>*& Out) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		if (!V || !(*V) || (*V)->Type != EJson::Array) return false;
		Out = &(*V)->ArrayVal;
		return true;
	}
	bool TryGetStringField(const FString& Key, FString& Out) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		if (!V || !(*V) || (*V)->Type == EJson::Null) return false;
		return (*V)->TryGetString(Out);
	}
	// UE FJsonObject::TryGetNumberField is templated over the numeric out type (int32/int64/float/double).
	template <typename NumberT>
	bool TryGetNumberField(const FString& Key, NumberT& Out) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		if (!V || !(*V) || (*V)->Type != EJson::Number) return false;
		Out = (NumberT)(*V)->NumberVal;
		return true;
	}
	bool TryGetBoolField(const FString& Key, bool& Out) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		if (!V || !(*V) || (*V)->Type != EJson::Boolean) return false;
		Out = (*V)->BoolVal;
		return true;
	}

	FString GetStringField(const FString& Key) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return (V && *V) ? (*V)->AsString() : FString();
	}
	double GetNumberField(const FString& Key) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return (V && *V) ? (*V)->AsNumber() : 0.0;
	}
	int32_t GetIntegerField(const FString& Key) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return (V && *V) ? (int32_t)(*V)->AsNumber() : 0;
	}
	bool GetBoolField(const FString& Key) const
	{
		const TSharedPtr<FJsonValue>* V = Values.Find(Key);
		return (V && *V) ? (*V)->AsBool() : false;
	}

	void SetField(const FString& Key, const TSharedPtr<FJsonValue>& V) { Values.Set(Key, V); }
	void RemoveField(const FString& Key) { Values.Remove(Key); }
	void SetStringField(const FString& Key, const FString& S) { Values.Set(Key, FJsonValue::MakeString(S)); }
	void SetNumberField(const FString& Key, double N) { Values.Set(Key, FJsonValue::MakeNumber(N)); }
	void SetBoolField(const FString& Key, bool B) { Values.Set(Key, FJsonValue::MakeBool(B)); }
	void SetObjectField(const FString& Key, const TSharedPtr<FJsonObject>& O)
	{
		Values.Set(Key, MakeShared<FJsonValueObject>(O));
	}
	void SetArrayField(const FString& Key, const TArray<TSharedPtr<FJsonValue>>& A)
	{
		Values.Set(Key, MakeShared<FJsonValueArray>(A));
	}
};

} // namespace jsondom

// The `sj::` alias every call site uses; not lifted to global (would collide with UE's ::FJsonObject).
#ifndef UNREAL_SJ_ALIAS_DEFINED
#define UNREAL_SJ_ALIAS_DEFINED 1
namespace JSONDOM_ALIAS = JSONDOM_NAMESPACE;
#endif

#endif // UNREAL_JSONDOM_H
