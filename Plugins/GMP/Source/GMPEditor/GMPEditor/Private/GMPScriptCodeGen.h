//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"
#include "MessageTagsManager.h"

// Shared codegen base for script-backend annotation/binding generators (AngelScript / slua / UnLua).
// Backend-agnostic: tag enumeration, identifier sanitize, a reusable GMP-type-name mapper (scalar table + container
// recursion + reflection fallback), atomic write-if-changed, and OnMessageTagSignatureChanged subscription.
// Each backend only supplies its scalar type table and per-tag emit; output file count is up to the backend.
namespace GMPScriptCodeGen
{
// One registered message tag with a non-empty typed signature.
struct FTagSig
{
	FString TagStr;                     // full dotted tag, e.g. "Player.Hurt"
	FString Id;                         // sanitized identifier, e.g. "Player_Hurt"
	TArray<FMessageParameter> Params;   // {Name, Type(FName)}
};

// Enumerate all dictionary tags that carry a parameter signature.
TArray<FTagSig> CollectTags();

// Replace non [0-9A-Za-z_] with '_'.
FString SanitizeIdent(const FString& In);

// Map a GMP type name to a backend spelling: scalar table first, then TArray<>/TSubclassOf<> container recursion,
// then reflection (OnClass/OnStruct). Sets bOutKnown=false and returns UnknownFallback if unmappable.
FString MapType(
	const FString& GmpType,
	const TMap<FString, FString>& Scalars,
	const TCHAR* UnknownFallback,
	TFunctionRef<FString(UClass*)> OnClass,
	TFunctionRef<FString(UScriptStruct*)> OnStruct,
	bool& bOutKnown);

// Write only if content differs; returns true if the file changed.
bool WriteIfChanged(const FString& Path, const FString& Text);

// Subscribe GenFn to signature changes (no-op if Handle already valid). Pairs with UnregisterGen.
void RegisterGen(FDelegateHandle& Handle, TFunction<void()> GenFn);
void UnregisterGen(FDelegateHandle& Handle);

// EmmyLua annotation text for all typed tags (Listen_<id> + callback signature). Shared by UnLua and slua (any lua IDE).
FString BuildLuaAnnotations();
}  // namespace GMPScriptCodeGen
#endif
