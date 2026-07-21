//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if defined(ANGELSCRIPTRUNTIME_API)
#include "GMPCore.h"

#include "AngelscriptBinds.h"
#include "AngelscriptEngine.h"
#include "angelscript.h"

#include "ClassGenerator/ASClass.h"
#include "StartAngelscriptHeaders.h"
#include "source/as_context.h"
#include "EndAngelscriptHeaders.h"

GMP_EXTERNAL_SIGSOURCE(asIScriptContext)
namespace AngelScriptSupport
{
// Holds the script callback (a funcdef handle / delegate) for the lifetime of a GMP listen; released on unbind/GC.
struct FAsCallbackHolder
{
	asIScriptFunction* Func = nullptr;
	explicit FAsCallbackHolder(asIScriptFunction* InFunc)
		: Func(InFunc)
	{
		if (Func)
			Func->AddRef();
	}
	FAsCallbackHolder(const FAsCallbackHolder&) = delete;
	FAsCallbackHolder& operator=(const FAsCallbackHolder&) = delete;
	FAsCallbackHolder(FAsCallbackHolder&& In) noexcept
		: Func(In.Func)
	{
		In.Func = nullptr;
	}
	~FAsCallbackHolder()
	{
		if (Func)
			Func->Release();
	}
};

// Pushes GMP message params onto the script context and invokes the callback. Params are normalized to (paddrs, count,
// key, typename-source) by both dispatch paths (raw paddrs+extra, or FMessageBody), mirroring the Lua/Puerts adapters.
inline void GMP_As_InvokeListenCallback(const FGMPTypedAddr* Paddrs, int32 NumArgs, FName KeyName, const FName* InRawTypeNames, const TArray<FName>* InMetaTypes, const FAsCallbackHolder& Holder)
{
	if (!ensure(Holder.Func))
		return;

	asIScriptEngine* Engine = Holder.Func->GetEngine();
	if (!ensure(Engine))
		return;

	auto GetTypeName = [&](int32 Idx) -> FName {
#if GMP_WITH_TYPENAME
		(void)InRawTypeNames;
		(void)InMetaTypes;
		return Paddrs[Idx].TypeName;
#else
		if (InMetaTypes && InMetaTypes->IsValidIndex(Idx))
			return (*InMetaTypes)[Idx];
		return InRawTypeNames ? InRawTypeNames[Idx] : NAME_None;
#endif
	};

#if GMP_WITH_DYNAMIC_CALL_CHECK
	GMP::FArrayTypeNames ArgNames;
	ArgNames.Reserve(NumArgs);
	for (auto Idx = 0; Idx < NumArgs; ++Idx)
		ArgNames.Add(GetTypeName(Idx));
	const GMP::FArrayTypeNames* OldParams = nullptr;
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("AngelScript"));
	if (!ensure(GMP::FMessageHub::IsSignatureCompatible(false, KeyName, ArgNames, OldParams)))
	{
		GMP_WARNING(TEXT("SignatureMismatch On AngelScript Listen %s"), *KeyName.ToString());
		return;
	}
#endif

	asIScriptContext* Ctx = Engine->RequestContext();
	if (!ensure(Ctx))
		return;
	ON_SCOPE_EXIT { Engine->ReturnContext(Ctx); };

	if (Ctx->Prepare(Holder.Func) < 0)
		return;

	for (auto Idx = 0; Idx < NumArgs; ++Idx)
		Ctx->SetArgAddress(Idx, Paddrs[Idx].ToAddr());

	const int R = Ctx->Execute();
	if (R != asEXECUTION_FINISHED)
		GMP_WARNING(TEXT("AngelScript callback did not finish for %s (code %d)"), *KeyName.ToString(), R);
}

// GMP.ListenObjectMessage(WatchedObj, MsgKey, WeakObj, callback [,Times]) -> key
inline int64 As_ListenObjectMessage(UObject* WatchedObject, const FString& MsgKeyStr, UObject* WeakObj, asIScriptFunction* Callback, int32 LeftTimes = -1)
{
	asIScriptContext* Ctx = asGetActiveContext();
	if (!ensure(Ctx && Callback))
		return 0;

	const FName MsgKey = *MsgKeyStr;
	if (!ensure(!MsgKey.IsNone()))
		return 0;

	FAsCallbackHolder Holder(Callback);

	uint64 RetKey = 0;
#if GMP_WITH_DIRECT_SIGNAL
	RetKey = FGMPHelper::ScriptListenMessageRaw(
		WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(Ctx),
		MsgKey,
		WeakObj,
		[Holder{std::move(Holder)}](const FGMPTypedAddr* paddrs, const GMP::FGMPExtra* extra) { GMP_As_InvokeListenCallback(paddrs, extra->Size, extra->Key, extra->TypeNames, nullptr, Holder); },
		LeftTimes);
#else
	RetKey = FGMPHelper::ScriptListenMessage(
		WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(Ctx),
		MsgKey,
		WeakObj,
		[Holder{std::move(Holder)}, WeakObj](GMP::FMessageBody& MsgBody) {
			const auto Addrs = MsgBody.GetParams();
			GMP_As_InvokeListenCallback(Addrs.GetData(), Addrs.Num(), MsgBody.MessageKey(), nullptr, MsgBody.GetMessageTypes(WeakObj), Holder);
		},
		LeftTimes);
#endif
	return (int64)RetKey;
}

// GMP.UnbindObjectMessage(MsgKey, ListenedObj) / GMP.UnbindObjectMessage(MsgKey, Key)
inline void As_UnbindObjectMessage(const FString& MsgKeyStr, UObject* ListenedObj, int64 Key = 0)
{
	const FName MsgKey = *MsgKeyStr;
	if (ListenedObj)
		FGMPHelper::ScriptUnbindMessage(MsgKey, ListenedObj);
	else
		FGMPHelper::ScriptUnbindMessage(MsgKey, FGMPKey((uint64)Key));
}

// GMP.NotifyObjectMessage(Sender, MsgKey, Params) -> bool
inline bool As_NotifyObjectMessage(UObject* Sender, const FString& MsgKeyStr, const TArray<FGMPTypedAddr>& Params)
{
	const FName MsgKey = *MsgKeyStr;
	auto Types = GMP::FMessageBody::GetMessageTypes(Sender, MsgKey);
	if (!ensure(Types && Params.Num() >= Types->Num()))
	{
		GMP_WARNING(TEXT("GetMessageTypes is null or arg count mismatch on AngelScript Notify %s"), *MsgKey.ToString());
		return false;
	}

	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("AngelScript"));
	GMP::FTypedAddresses ParamsCopy(Params);
	return FGMPHelper::ScriptNotifyMessage(MsgKey, ParamsCopy, Sender);
}

// Generic thunk for the weak-typed ListenObjectMessage: funcdef handle args must go through GetArgObject (a plain
// BindGlobalFunction with an asIScriptFunction* param would receive the stack slot address, not the handle value).
inline void As_ListenObjectMessage_Generic(asIScriptGeneric* Gen)
{
	UObject* WatchedObj = static_cast<UObject*>(Gen->GetArgObject(0));
	const FString* MsgKey = static_cast<const FString*>(Gen->GetArgAddress(1));
	UObject* WeakObj = static_cast<UObject*>(Gen->GetArgObject(2));
	asIScriptFunction* Callback = static_cast<asIScriptFunction*>(Gen->GetArgObject(3));
	int32 Times = (int32)Gen->GetArgDWord(4);
	int64 RetKey = MsgKey ? As_ListenObjectMessage(WatchedObj, *MsgKey, WeakObj, Callback, Times) : 0;
	Gen->SetReturnQWord((asQWORD)RetKey);
}

inline int64 As_ListenObjectMessageMethod(UObject* WeakObj, const FString& MsgKeyStr, const FString& MethodName);  // ABI-B, defined below

// funcdef must be registered before the globals that consume it, hence EOrder::Late relative to core type binds.
AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_GMP(FAngelscriptBinds::EOrder::Late, [] {
	FAngelscriptEngine::Get().GetScriptEngine()->RegisterFuncdef("void FGMPMessageCallback()");

	FAngelscriptBinds::FNamespace ns("GMP");
	// funcdef param must use the generic calling convention to receive the handle value correctly.
	FAngelscriptBinds::BindGlobalGenericFunction("int64 ListenObjectMessage(UObject WatchedObj, const FString&in MsgKey, UObject WeakObj, FGMPMessageCallback@ Callback, int Times = -1)", &As_ListenObjectMessage_Generic);
	FAngelscriptBinds::BindGlobalFunction("void UnbindObjectMessage(const FString&in MsgKey, UObject ListenedObj, int64 Key = 0)", FUNC_TRIVIAL(As_UnbindObjectMessage));
	FAngelscriptBinds::BindGlobalFunction("bool NotifyObjectMessage(UObject Sender, const FString&in MsgKey, const TArray<FGMPTypedAddr>&in Params)", FUNC_TRIVIAL(As_NotifyObjectMessage));
	// ABI-B: listen with a named AS method on WeakObj -> fire dispatches via JitFunction_ParmsEntry (bypasses ProcessEvent).
	FAngelscriptBinds::BindGlobalFunction("int64 ListenObjectMessageMethod(UObject WeakObj, const FString&in MsgKey, const FString&in MethodName)", FUNC_TRIVIAL(As_ListenObjectMessageMethod));
});

// ---- ABI-B: named-method fast path. When the listener is a UObject with a named AS method (a UASFunction),
// GMP fire fills a UFunction parms struct from paddrs and invokes JitFunction_ParmsEntry directly (Shipping/StaticJIT),
// fully bypassing ProcessEvent/context; falls back to ProcessEvent when JIT is unavailable (editor/dev).

// Copies message paddrs into a UASFunction parms buffer (native UE layout, per UFunction property offsets) and dispatches.
inline void GMP_As_InvokeNamedMethod(UObject* Object, UASFunction* Fn, const FGMPTypedAddr* Paddrs, int32 NumArgs)
{
	if (!ensure(Object && Fn))
		return;

	void* Parms = FMemory_Alloca_Aligned(FMath::Max<int32>(Fn->ParmsSize, 1), Fn->GetMinAlignment());
	FMemory::Memzero(Parms, Fn->ParmsSize);

	int32 Idx = 0;
	for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm) && !(It->PropertyFlags & CPF_ReturnParm); ++It, ++Idx)
	{
		if (Idx >= NumArgs)
			break;
		It->CopyCompleteValue(It->ContainerPtrToValuePtr<void>(Parms), Paddrs[Idx].ToAddr());
	}

	// Shipping/StaticJIT: direct JIT parms-entry (bypasses ProcessEvent + interpreter context).
	if (asJITFunction_ParmsEntry Jit = Fn->JitFunction_ParmsEntry)
	{
		FAngelscriptGameThreadScopeWorldContext WorldContext(Object);
		FScriptExecution Execution(FAngelscriptEngine::GameThreadTLD);
		Jit(Execution, Object, Parms);
	}
	else  // editor/dev: no JIT -> standard UFunction dispatch (UASFunction native exec handles the context path).
	{
		Object->ProcessEvent(Fn, Parms);
	}

	for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It)
		It->DestroyValue_InContainer(Parms);
}

// GMP.ListenObjectMessageMethod(WeakObj, MsgKey, MethodName): listen with a named AS method (ABI-B fast path).
inline int64 As_ListenObjectMessageMethod(UObject* WeakObj, const FString& MsgKeyStr, const FString& MethodName)
{
	asIScriptContext* Ctx = asGetActiveContext();
	const FName MsgKey = *MsgKeyStr;
	UASFunction* Fn = WeakObj ? Cast<UASFunction>(WeakObj->FindFunction(*MethodName)) : nullptr;
	if (!ensure(Fn && !MsgKey.IsNone()))
		return 0;

	uint64 RetKey = 0;
	TWeakObjectPtr<UObject> WeakThis(WeakObj);
	TWeakObjectPtr<UASFunction> WeakFn(Fn);
#if GMP_WITH_DIRECT_SIGNAL
	RetKey = FGMPHelper::ScriptListenMessageRaw(
		WeakObj ? FGMPSigSource(WeakObj) : FGMPSigSource(Ctx), MsgKey, WeakObj,
		[WeakThis, WeakFn](const FGMPTypedAddr* paddrs, const GMP::FGMPExtra* extra) {
			if (UObject* Obj = WeakThis.Get())
				GMP_As_InvokeNamedMethod(Obj, WeakFn.Get(), paddrs, extra->Size);
		});
#else
	RetKey = FGMPHelper::ScriptListenMessage(
		WeakObj ? FGMPSigSource(WeakObj) : FGMPSigSource(Ctx), MsgKey, WeakObj,
		[WeakThis, WeakFn](GMP::FMessageBody& MsgBody) {
			const auto Addrs = MsgBody.GetParams();
			if (UObject* Obj = WeakThis.Get())
				GMP_As_InvokeNamedMethod(Obj, WeakFn.Get(), Addrs.GetData(), Addrs.Num());
		});
#endif
	return (int64)RetKey;
}

// ---- Strongly-typed per-tag binds (L1 runtime half). Each tag gets Listen_<id>/Notify_<id> registered via
// BindGlobalGenericFunction with a concrete funcdef, so .as gets compile-time checking; the generic callback pulls
// the funcdef handle / typed args from asIScriptGeneric and forwards to the weak-typed runtime above.

// Per-tag context carried as the bound function's UserData (FAngelscriptBinds::OnBind -> ScriptFunction->SetUserData).
struct FGMPTypedTagCtx
{
	FName Key;
	TArray<FName> ParamTypes;
};

// GMP type name (FName) -> AngelScript type spelling. UE-AngelScript value types ARE the UE native types, so the
// mapping mirrors the editor-side codegen; unknown non-container names fall back to UObject.
inline FString GMP_AsTypeName(FName GmpType)
{
	static const TMap<FName, FString> Scalars = {
		{TEXT("bool"), TEXT("bool")}, {TEXT("int8"), TEXT("int8")}, {TEXT("uint8"), TEXT("uint8")},
		{TEXT("int16"), TEXT("int16")}, {TEXT("uint16"), TEXT("uint16")},
		{TEXT("int32"), TEXT("int")}, {TEXT("int"), TEXT("int")}, {TEXT("uint32"), TEXT("uint")}, {TEXT("uint"), TEXT("uint")},
		{TEXT("int64"), TEXT("int64")}, {TEXT("uint64"), TEXT("uint64")}, {TEXT("float"), TEXT("float")}, {TEXT("double"), TEXT("double")},
		{TEXT("Name"), TEXT("FName")}, {TEXT("Text"), TEXT("FText")}, {TEXT("String"), TEXT("FString")},
		{TEXT("Object"), TEXT("UObject")}, {TEXT("Class"), TEXT("UClass")},
	};
	if (const FString* Found = Scalars.Find(GmpType))
		return *Found;

	const FString S = GmpType.ToString();
	if (S.StartsWith(TEXT("TArray<")) && S.EndsWith(TEXT(">")))
		return FString::Printf(TEXT("TArray<%s>"), *GMP_AsTypeName(*S.Mid(7, S.Len() - 8)));
	if (UClass* Cls = UClass::TryFindTypeSlow<UClass>(S))
		return Cls->GetPrefixCPP() + Cls->GetName();
	if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(S))
		return FString(Struct->GetPrefixCPP()) + Struct->GetName();
	return TEXT("UObject");
}

// Generic thunk for Notify_<id>(UObject Sender, <typed args...>): arg 0 = Sender, args 1..N = message params.
// UE-AngelScript arg memory is native UE layout, so FromAddr wraps it zero-copy (no per-type transcoding like Lua/JS).
inline void As_TypedNotify_Generic(asIScriptGeneric* Gen)
{
	auto* Ctx = static_cast<FGMPTypedTagCtx*>(Gen->GetFunction()->GetUserData());
	if (!ensure(Ctx))
		return;
	UObject* Sender = static_cast<UObject*>(Gen->GetArgObject(0));

	GMP::FTypedAddresses Params;
	Params.Reserve(Ctx->ParamTypes.Num());
	for (int32 i = 0; i < Ctx->ParamTypes.Num(); ++i)
	{
		FProperty* Prop = nullptr;
		if (!ensure(GMPReflection::PropertyFromString(Ctx->ParamTypes[i].ToString(), Prop) && Prop))
			return;
		Params.Add(FGMPTypedAddr::FromAddr(Gen->GetAddressOfArg(i + 1), Prop));
	}

	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("AngelScript"));
	FGMPHelper::ScriptNotifyMessage(Ctx->Key, Params, Sender);
}

// Generic thunk for Listen_<id>(UObject WeakObj, FOn_<id>@ cb, int Times): args (0)=WeakObj (1)=funcdef handle (2)=Times.
inline void As_TypedListen_Generic(asIScriptGeneric* Gen)
{
	auto* Ctx = static_cast<FGMPTypedTagCtx*>(Gen->GetFunction()->GetUserData());
	UObject* WeakObj = static_cast<UObject*>(Gen->GetArgObject(0));
	asIScriptFunction* Callback = static_cast<asIScriptFunction*>(Gen->GetArgObject(1));  // as_generic.cpp: GetArgObject supports funcdef
	int32 Times = Gen->GetArgCount() > 2 ? (int32)Gen->GetArgDWord(2) : -1;
	int64 RetKey = (Ctx && Callback) ? As_ListenObjectMessage(nullptr, Ctx->Key.ToString(), WeakObj, Callback, Times) : 0;
	Gen->SetReturnQWord((asQWORD)RetKey);
}

// Registers Listen_<id>/Notify_<id> for every tag in the runtime signature table (UGMPMeta). Call once the AS engine
// is up (and again on signature changes). Type names map via GMP_AsTypeName, matching the editor-side .as stub codegen.
inline void GMP_RegisterTypedBinds(const UObject* WorldContext)
{
	asIScriptEngine* Engine = FAngelscriptEngine::Get().GetScriptEngine();
	if (!ensure(Engine))
		return;

	GMP::EnumerateMessageTagMetas(WorldContext, [&](FName Tag, const TArray<FName>& ParamTypes, const TArray<FName>&) {
		FString Id = Tag.ToString();
		for (TCHAR& C : Id)
			if (!((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_'))
				C = '_';

		FString FuncdefDecl, ParamDecl;
		for (int32 i = 0; i < ParamTypes.Num(); ++i)
		{
			const FString T = GMP_AsTypeName(ParamTypes[i]);
			FuncdefDecl += FString::Printf(TEXT("%s%s A%d"), i ? TEXT(", ") : TEXT(""), *T, i);
			ParamDecl += FString::Printf(TEXT(", %s A%d"), *T, i);
		}

		Engine->RegisterFuncdef(TCHAR_TO_UTF8(*FString::Printf(TEXT("void FOn_%s(%s)"), *Id, *FuncdefDecl)));

		FGMPTypedTagCtx* Ctx = new FGMPTypedTagCtx{Tag, ParamTypes};  // leaked intentionally: lives with the engine binding
		FAngelscriptBinds::BindGlobalGenericFunction(TCHAR_TO_UTF8(*FString::Printf(TEXT("int64 Listen_%s(UObject WeakObj, FOn_%s@ cb, int Times = -1)"), *Id, *Id)), &As_TypedListen_Generic, Ctx);
		FAngelscriptBinds::BindGlobalGenericFunction(TCHAR_TO_UTF8(*FString::Printf(TEXT("void Notify_%s(UObject Sender%s)"), *Id, *ParamDecl)), &As_TypedNotify_Generic, Ctx);
	});
}
}  // namespace AngelScriptSupport
#endif

// how to use:
// 1. add "GMP" to PrivateDependencyModuleNames in AngelscriptRuntime.Build.cs (and PrivateIncludePaths to GMP/Source/GMP/Shared if unseen)
// 2. just include this header into an AngelscriptRuntime-module TU; the AS_FORCE_LINK FBind auto-registers the weak-typed GMP namespace globals
// 3. after the AS engine is created, call AngelScriptSupport::GMP_RegisterTypedBinds(WorldContext) to add per-tag strongly-typed
//    Listen_<id>/Notify_<id> (signatures from the runtime UGMPMeta table baked at cook). In editor, re-call on tag signature changes.
// 4. the editor codegen (FGMPAngelScriptCodeGen) writes matching GMPMessages.as declaration stubs for compile-time checking + IntelliSense.

#if 0
// GMP.as (AngelScript)
namespace GMP
{
    // weak-typed (always available):
    // ListenObjectMessage(WatchedObj, MsgKey, WeakObj, callback [,Times]) -> key; WeakObj may be null
    // UnbindObjectMessage(MsgKey, ListenedObj) / UnbindObjectMessage(MsgKey, null, Key)
    // NotifyObjectMessage(Sender, MsgKey, Params) -> bool
    // strongly-typed per tag (from GMP_RegisterTypedBinds; declared in GMPMessages.as):
    // funcdef void FOn_Player_Hurt(int Damage, AActor Causer);
    // int64 Listen_Player_Hurt(UObject WeakObj, FOn_Player_Hurt@ cb, int Times = -1);
    // void  Notify_Player_Hurt(UObject Sender, int Damage, AActor Causer);
}

// example.as
int64 Key = GMP::Listen_Player_Hurt(this, function(int Damage, AActor Causer) { Print("hurt " + Damage); });
GMP::Notify_Player_Hurt(this, 42, causer);
GMP::UnbindObjectMessage("Player.Hurt", this);
#endif