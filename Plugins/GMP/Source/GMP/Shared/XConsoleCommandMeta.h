//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

// Per-parameter or per-variable metadata (key-value pairs, aligned with UPROPERTY meta keys)
struct GMP_API FXConsoleParamMeta
{
	FString Name;
	TMap<FName, FString> MetaMap;

	bool HasMeta(FName Key) const { return MetaMap.Contains(Key); }
	FString GetMeta(FName Key, const FString& Default = FString()) const { auto* V = MetaMap.Find(Key); return V ? *V : Default; }
	double GetMetaDouble(FName Key, double Default = 0.0) const { auto* V = MetaMap.Find(Key); return V ? FCString::Atod(**V) : Default; }
	int32 GetMetaInt(FName Key, int32 Default = 0) const { auto* V = MetaMap.Find(Key); return V ? FCString::Atoi(**V) : Default; }
	bool GetMetaBool(FName Key, bool Default = false) const { auto* V = MetaMap.Find(Key); return V ? V->Equals(TEXT("true"), ESearchCase::IgnoreCase) || *V == TEXT("1") : Default; }
};

// Complete metadata for a command or variable
struct GMP_API FXConsoleObjectMeta
{
	FXConsoleParamMeta SelfMeta;           // Command/Variable level meta (DisplayName, Tooltip, Category, etc.)
	TArray<FXConsoleParamMeta> Params;     // Per-parameter meta (Command only)
};

// Builder for declaring meta — suffix style, old code unchanged
//
// Usage:
//   static FXConsoleCommandLambda XVar(TEXT("game.damage"), [](float D, int32 Id, UWorld*) { ... });
//   static auto XVar_Meta = FXConsoleMeta(TEXT("game.damage"))
//       .DisplayName(TEXT("Deal Damage"))
//       .Param(0, TEXT("Damage")).ClampMin(0).ClampMax(1000)
//       .Param(1, TEXT("PlayerId")).DisplayName(TEXT("Player ID"));
//
//   static TXConsoleVariable<float> XVar_Speed(TEXT("game.speed"), 1.0f);
//   static auto XVar_Speed_Meta = FXConsoleMeta(TEXT("game.speed"))
//       .ClampMin(0.1).ClampMax(10).DisplayName(TEXT("Game Speed"));
//
struct GMP_API FXConsoleMeta
{
	FXConsoleMeta(const TCHAR* InName)
		: Z_XMETA_A(*this), Z_XMETA_B(*this), CmdName(InName) {}
	~FXConsoleMeta();

	// Command/Variable level
	FXConsoleMeta& DisplayName(const TCHAR* V) { Meta.SelfMeta.MetaMap.Add(TEXT("DisplayName"), V); return *this; }
	FXConsoleMeta& Tooltip(const TCHAR* V) { Meta.SelfMeta.MetaMap.Add(TEXT("Tooltip"), V); return *this; }
	FXConsoleMeta& Category(const TCHAR* V) { Meta.SelfMeta.MetaMap.Add(TEXT("Category"), V); return *this; }
	FXConsoleMeta& DisplayPriority(int32 V) { Meta.SelfMeta.MetaMap.Add(TEXT("DisplayPriority"), FString::FromInt(V)); return *this; }
	FXConsoleMeta& Hidden(bool b = true) { Meta.SelfMeta.MetaMap.Add(TEXT("Hidden"), b ? TEXT("true") : TEXT("false")); return *this; }
	FXConsoleMeta& AdvancedDisplay(bool b = true) { Meta.SelfMeta.MetaMap.Add(TEXT("AdvancedDisplay"), b ? TEXT("true") : TEXT("false")); return *this; }

	// Variable level (also applies to self)
	FXConsoleMeta& ClampMin(double V) { Meta.SelfMeta.MetaMap.Add(TEXT("ClampMin"), FString::SanitizeFloat(V)); return *this; }
	FXConsoleMeta& ClampMax(double V) { Meta.SelfMeta.MetaMap.Add(TEXT("ClampMax"), FString::SanitizeFloat(V)); return *this; }
	FXConsoleMeta& UIMin(double V) { Meta.SelfMeta.MetaMap.Add(TEXT("UIMin"), FString::SanitizeFloat(V)); return *this; }
	FXConsoleMeta& UIMax(double V) { Meta.SelfMeta.MetaMap.Add(TEXT("UIMax"), FString::SanitizeFloat(V)); return *this; }

	// SetMeta overloads — used by Z_XMETA_A/B macros and direct calls
	FXConsoleMeta& SetMeta(FName Key, const FString& Value) { Meta.SelfMeta.MetaMap.Add(Key, Value); return *this; }
	FXConsoleMeta& SetMeta(FStringView Key, const TCHAR* Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), Value); return *this; }
	FXConsoleMeta& SetMeta(FStringView Key, double Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), FString::SanitizeFloat(Value)); return *this; }
	FXConsoleMeta& SetMeta(FStringView Key, int32 Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), FString::FromInt(Value)); return *this; }
	FXConsoleMeta& SetMeta(FStringView Key, bool Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), Value ? TEXT("true") : TEXT("false")); return *this; }
	FXConsoleMeta& SetMeta(FStringView Key) { Meta.SelfMeta.MetaMap.Add(FName(Key), TEXT("true")); return *this; }
	FXConsoleMeta& SetMeta(FAnsiStringView Key, const TCHAR* Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), Value); return *this; }
	FXConsoleMeta& SetMeta(FAnsiStringView Key, double Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), FString::SanitizeFloat(Value)); return *this; }
	FXConsoleMeta& SetMeta(FAnsiStringView Key, int32 Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), FString::FromInt(Value)); return *this; }
	FXConsoleMeta& SetMeta(FAnsiStringView Key, bool Value) { Meta.SelfMeta.MetaMap.Add(FName(Key), Value ? TEXT("true") : TEXT("false")); return *this; }
	FXConsoleMeta& SetMeta(FAnsiStringView Key) { Meta.SelfMeta.MetaMap.Add(FName(Key), TEXT("true")); return *this; }

	// Ping-pong reference members — same name as Z_XMETA_A/B macros
	FXConsoleMeta& Z_XMETA_A;
	FXConsoleMeta& Z_XMETA_B;

	// Parameter builder — chain to Param() for per-parameter meta
	struct FParamBuilder
	{
		FParamBuilder& DisplayName(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("DisplayName"), V); return *this; }
		FParamBuilder& Tooltip(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("Tooltip"), V); return *this; }
		FParamBuilder& DefaultValue(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("DefaultValue"), V); return *this; }
		FParamBuilder& ClampMin(double V) { GetCurrent().MetaMap.Add(TEXT("ClampMin"), FString::SanitizeFloat(V)); return *this; }
		FParamBuilder& ClampMax(double V) { GetCurrent().MetaMap.Add(TEXT("ClampMax"), FString::SanitizeFloat(V)); return *this; }
		FParamBuilder& UIMin(double V) { GetCurrent().MetaMap.Add(TEXT("UIMin"), FString::SanitizeFloat(V)); return *this; }
		FParamBuilder& UIMax(double V) { GetCurrent().MetaMap.Add(TEXT("UIMax"), FString::SanitizeFloat(V)); return *this; }
		FParamBuilder& Delta(double V) { GetCurrent().MetaMap.Add(TEXT("Delta"), FString::SanitizeFloat(V)); return *this; }
		FParamBuilder& Units(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("Units"), V); return *this; }
		FParamBuilder& AllowedClasses(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("AllowedClasses"), V); return *this; }
		FParamBuilder& DisallowedClasses(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("DisallowedClasses"), V); return *this; }
		FParamBuilder& MustImplement(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("MustImplement"), V); return *this; }
		FParamBuilder& AllowAbstract(bool b = true) { GetCurrent().MetaMap.Add(TEXT("AllowAbstract"), b ? TEXT("true") : TEXT("false")); return *this; }
		FParamBuilder& Bitmask(bool b = true) { GetCurrent().MetaMap.Add(TEXT("Bitmask"), b ? TEXT("true") : TEXT("false")); return *this; }
		FParamBuilder& BitmaskEnum(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("BitmaskEnum"), V); return *this; }
		FParamBuilder& ValidEnumValues(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("ValidEnumValues"), V); return *this; }
		FParamBuilder& MultiLine(bool b = true) { GetCurrent().MetaMap.Add(TEXT("MultiLine"), b ? TEXT("true") : TEXT("false")); return *this; }
		FParamBuilder& MaxLength(int32 V) { GetCurrent().MetaMap.Add(TEXT("MaxLength"), FString::FromInt(V)); return *this; }
		FParamBuilder& FilePathFilter(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("FilePathFilter"), V); return *this; }
		FParamBuilder& EditCondition(const TCHAR* V) { GetCurrent().MetaMap.Add(TEXT("EditCondition"), V); return *this; }
		FParamBuilder& SetMeta(FName Key, const FString& Value) { GetCurrent().MetaMap.Add(Key, Value); return *this; }

		// Smart-assert style: operator() chaining for params
		FParamBuilder& operator()(const TCHAR* Key, const TCHAR* Value) { GetCurrent().MetaMap.Add(FName(Key), Value); return *this; }
		FParamBuilder& operator()(const TCHAR* Key, double Value) { GetCurrent().MetaMap.Add(FName(Key), FString::SanitizeFloat(Value)); return *this; }
		FParamBuilder& operator()(const TCHAR* Key, int32 Value) { GetCurrent().MetaMap.Add(FName(Key), FString::FromInt(Value)); return *this; }
		FParamBuilder& operator()(const TCHAR* Key, bool Value) { GetCurrent().MetaMap.Add(FName(Key), Value ? TEXT("true") : TEXT("false")); return *this; }

		// Chain to next param
		FParamBuilder& Param(int32 Index, const TCHAR* Name = nullptr);

	private:
		friend struct FXConsoleMeta;
		FParamBuilder(FXConsoleMeta& InOwner, int32 InIndex) : Owner(InOwner), ParamIndex(InIndex) {}
		FXConsoleParamMeta& GetCurrent() { return Owner.Meta.Params[ParamIndex]; }

		FXConsoleMeta& Owner;
		int32 ParamIndex;
	};

	FParamBuilder& Param(int32 Index, const TCHAR* Name = nullptr);

	struct FMetaKV
	{
		FName Key;
		FString Value;
		FMetaKV(const TCHAR* K) : Key(K), Value(TEXT("true")) {}
		FMetaKV(const TCHAR* K, const TCHAR* V) : Key(K), Value(V) {}
		FMetaKV(const TCHAR* K, const FString& V) : Key(K), Value(V) {}
		FMetaKV(const TCHAR* K, double V) : Key(K), Value(FString::SanitizeFloat(V)) {}
		FMetaKV(const TCHAR* K, int32 V) : Key(K), Value(FString::FromInt(V)) {}
		FMetaKV(const TCHAR* K, bool V) : Key(K), Value(V ? TEXT("true") : TEXT("false")) {}
	};
	struct FMetaEnd {};

	FXConsoleMeta& operator>>(const FMetaKV& KV) { Meta.SelfMeta.MetaMap.Add(KV.Key, KV.Value); return *this; }
	FXConsoleMeta& operator>>(const FMetaEnd&) { return *this; }

private:
	const TCHAR* CmdName;
	FXConsoleObjectMeta Meta;
};

// ============================================================
// Usage:
//   static FXConsoleCommandLambda XVar(TEXT("game.damage"), [](float D, UWorld*) { ... })
//       XMeta(XVar, DisplayName, TEXT("Deal Damage"))(ClampMin, 0)(ClampMax, 1000);
//
//   static TXConsoleVariable<float> XVar_Speed(TEXT("game.speed"), 1.0f)
//       XMeta(XVar_Speed, DisplayName, TEXT("Speed"))(ClampMin, 0.1)(ClampMax, 10);
// ============================================================

#ifndef GMP_XCONSOLE_META
#define GMP_XCONSOLE_META !UE_BUILD_SHIPPING
#endif

enum class EXConsoleVarType : uint8
{
	None,
	Bool,
	Int32,
	Float,
	String,
};

template<typename T> struct TXConsoleVarType { static constexpr EXConsoleVarType Value = EXConsoleVarType::None; };
template<> struct TXConsoleVarType<bool>    { static constexpr EXConsoleVarType Value = EXConsoleVarType::Bool; };
template<> struct TXConsoleVarType<int32>   { static constexpr EXConsoleVarType Value = EXConsoleVarType::Int32; };
template<> struct TXConsoleVarType<float>   { static constexpr EXConsoleVarType Value = EXConsoleVarType::Float; };
template<> struct TXConsoleVarType<FString> { static constexpr EXConsoleVarType Value = EXConsoleVarType::String; };

#if GMP_XCONSOLE_META
struct FXConsoleMetaBase
{
	FXConsoleMeta Meta() { return FXConsoleMeta(CachedName).Tooltip(CachedHelp); }
	EXConsoleVarType GetVarType() const { return VarType; }
protected:
	FXConsoleMetaBase(const TCHAR* InName, const TCHAR* InHelp, EXConsoleVarType InVarType = EXConsoleVarType::None)
		: CachedName(InName)
		, CachedHelp(InHelp)
		, VarType(InVarType)
	{
	}

private:
	const TCHAR* CachedName;
	const TCHAR* CachedHelp;
	EXConsoleVarType VarType;
};
#else
struct FXConsoleMetaBase
{
	FXConsoleMetaBase(const TCHAR* InName, const TCHAR* InHelp, EXConsoleVarType InVarType = EXConsoleVarType::None)
	{
	}
};
#endif

// Overloaded factory — accepts both name string and FXConsoleMetaBase-derived variable
#if GMP_XCONSOLE_META
inline FXConsoleMeta MakeXConsoleMeta(const TCHAR* Name) { return FXConsoleMeta(Name); }
inline FXConsoleMeta MakeXConsoleMeta(FXConsoleMetaBase& Base) { return Base.Meta(); }
#endif

#define Z_XMETA_CONCAT_(a, b) a##b
#define Z_XMETA_UID_(prefix, line) Z_XMETA_CONCAT_(prefix, line)

// Z_XMETA_A/B: expand to SetMeta(TEXT("k"), v).Z_XMETA_B/A
// Same name as reference members on FXConsoleMeta/FXConsoleMetaNoop:
//   With (): macro expands → SetMeta(...).Z_XMETA_B
//   Without (): member reference (terminal)
#define Z_XMETA_A(k, ...) SetMeta(TEXT(#k) __VA_OPT__(,) __VA_ARGS__).Z_XMETA_B
#define Z_XMETA_B(k, ...) SetMeta(TEXT(#k) __VA_OPT__(,) __VA_ARGS__).Z_XMETA_A

#if GMP_XCONSOLE_META

#define XMetaCmd(VarName, ...) ; static auto VarName##_xm_ = MakeXConsoleMeta(VarName) __VA_OPT__(.Z_XMETA_A(__VA_ARGS__))
#define XMetaVar(CvarName, ...) static auto Z_XMETA_UID_(xmv_, __LINE__) = MakeXConsoleMeta(CvarName) __VA_OPT__(.Z_XMETA_A(__VA_ARGS__))

#else

// Shipping: noop — SetMeta swallows args, Z_XMETA_A/B are self-references (terminal)
struct FXConsoleMetaNoop
{
	template<typename... T> const FXConsoleMetaNoop& SetMeta(T&&...) const { return *this; }
	const FXConsoleMetaNoop& Z_XMETA_A;
	const FXConsoleMetaNoop& Z_XMETA_B;
	FXConsoleMetaNoop() : Z_XMETA_A(*this), Z_XMETA_B(*this) {}
};

#define XMetaCmd(VarName, ...) ; [[maybe_unused]] static const auto& VarName##_xm_ = FXConsoleMetaNoop() __VA_OPT__(.Z_XMETA_A(__VA_ARGS__))
#define XMetaVar(CvarName, ...) [[maybe_unused]] static const auto& Z_XMETA_UID_(xmv_, __LINE__) = FXConsoleMetaNoop() __VA_OPT__(.Z_XMETA_A(__VA_ARGS__))

#endif // GMP_XCONSOLE_META
