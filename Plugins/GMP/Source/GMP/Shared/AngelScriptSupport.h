//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if defined(ANGELSCRIPTRUNTIME_API)
#include "GMPCore.h"

#include "AngelscriptBinds.h"
#include "AngelscriptEngine.h"
#include "angelscript.h"

#include "Preprocessor/AngelscriptPreprocessor.h"
#include "ClassGenerator/ASClass.h"
#include "StartAngelscriptHeaders.h"
#include "source/as_context.h"
#include "source/as_scriptfunction.h"
#include "source/as_datatype.h"
#include "EndAngelscriptHeaders.h"

GMP_EXTERNAL_SIGSOURCE(asIScriptContext)
namespace AngelScriptSupport
{
// A C-struct parms layout mirroring AngelscriptStaticJIT's ParmsEntry offset budget (AngelscriptStaticJIT.cpp:449-654):
// each parm is Align(offset, align) then offset += size, from a fresh 0. Reference/handle parms occupy a pointer slot
// (the Parms slot holds the pointer value); object/primitive parms occupy their value memory. bValid=false disables the
// direct path (e.g. sub-dword primitive-by-reference, which StaticJIT itself refuses via bParmsEntryValid=false).
struct FAsParmsLayout
{
	struct FEntry
	{
		int32 Offset = 0;
		int32 Size = 0;
		bool bPointerSlot = false;  // reference/handle: slot holds a pointer value copied from *(void**)paddr
	};
	TArray<FEntry> Entries;
	int32 TotalSize = 0;
	int32 Align = 1;
	bool bValid = false;
};

// Budgets a parms layout from a script function's parameterTypes, matching StaticJIT's ParmsEntry rules. Handle/reference
// parms use pointer align/size (8); object value types use typeInfo align/size; primitives use their in-memory size.
inline void GMP_As_BuildParmsLayout(asIScriptFunction* Func, FAsParmsLayout& Out)
{
	Out = FAsParmsLayout{};
	asCScriptFunction* Fn = reinterpret_cast<asCScriptFunction*>(Func);
	if (!Fn)
		return;

	const int Count = Fn->parameterTypes.GetLength();
	int32 Offset = 0;
	int32 MaxAlign = 1;
	for (int i = 0; i < Count; ++i)
	{
		const asCDataType& Dt = Fn->parameterTypes[i];
		const bool bPointerSlot = Dt.IsReference() || Dt.IsObjectHandle();
		const int32 ParmAlign = Dt.GetAlignment();
		const int32 ParmSize = bPointerSlot ? (int32)sizeof(void*) : Dt.GetSizeInMemoryBytes();

		// StaticJIT bails on sub-dword primitive-by-reference; so do we (fall back to the context path).
		if (Dt.IsReference() && !Dt.IsObject() && !Dt.IsObjectHandle() && Dt.GetSizeInMemoryBytes() < 4)
			return;
		if (ParmSize <= 0 || ParmAlign <= 0)
			return;

		Offset = Align(Offset, ParmAlign);
		FAsParmsLayout::FEntry E;
		E.Offset = Offset;
		E.Size = ParmSize;
		E.bPointerSlot = bPointerSlot;
		Out.Entries.Add(E);
		Offset += ParmSize;
		MaxAlign = FMath::Max(MaxAlign, ParmAlign);
	}

	Out.TotalSize = Align(Offset, MaxAlign);
	Out.Align = MaxAlign;
	Out.bValid = true;
}

// Holds the script callback (a funcdef handle / delegate) for the lifetime of a GMP listen; released on unbind/GC.
// Caches the direct-call target resolved once at listen time: the delegate is unwrapped to (Object, target func), and the
// ParmsEntry JIT thunk + a mirrored parms layout are cached so fire-time takes the VM-direct path with no context.
struct FAsCallbackHolder
{
	asIScriptFunction* Func = nullptr;
	// Direct-call cache (VM ParmsEntry). Null JitEntry => direct path unavailable, fall back to the context path.
	asJITFunction_ParmsEntry JitEntry = nullptr;
	void* DirectObject = nullptr;  // delegate object, or null for a plain closure
	FAsParmsLayout Layout;

	explicit FAsCallbackHolder(asIScriptFunction* InFunc)
		: Func(InFunc)
	{
		if (Func)
		{
			Func->AddRef();
			ResolveDirectCall();
		}
	}
	FAsCallbackHolder(const FAsCallbackHolder&) = delete;
	FAsCallbackHolder& operator=(const FAsCallbackHolder&) = delete;
	FAsCallbackHolder(FAsCallbackHolder&& In) noexcept
		: Func(In.Func)
		, JitEntry(In.JitEntry)
		, DirectObject(In.DirectObject)
		, Layout(MoveTemp(In.Layout))
	{
		In.Func = nullptr;
		In.JitEntry = nullptr;
		In.DirectObject = nullptr;
	}
	~FAsCallbackHolder()
	{
		if (Func)
			Func->Release();
	}

	// Unwraps a delegate to its underlying function+object (as_context.cpp:2428), then caches the ParmsEntry thunk and
	// a mirrored parms layout. Only script functions carry a ParmsEntry (Shipping/StaticJIT); otherwise JitEntry stays null.
	void ResolveDirectCall()
	{
		asCScriptFunction* Target = reinterpret_cast<asCScriptFunction*>(Func);
		if (Target->funcType == asFUNC_DELEGATE)
		{
			DirectObject = Target->objForDelegate;
			Target = Target->funcForDelegate;
		}
		if (!Target)
			return;

		GMP_As_BuildParmsLayout(reinterpret_cast<asIScriptFunction*>(Target), Layout);
		if (Layout.bValid && Target->jitFunction_ParmsEntry)
			JitEntry = Target->jitFunction_ParmsEntry;
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

	// VM-direct path (Shipping/StaticJIT): copy paddrs into a C-struct parms buffer matching the ParmsEntry layout and
	// invoke the JIT thunk directly, fully bypassing asIScriptContext. Closures pass Object=null (ParmsEntry does not
	// touch Object when objectType==null). Falls back to the context path when no ParmsEntry was resolved at listen time.
	if (Holder.JitEntry && Holder.Layout.bValid && Holder.Layout.Entries.Num() == NumArgs)
	{
		void* Buf = FMemory_Alloca_Aligned(FMath::Max<int32>(Holder.Layout.TotalSize, 1), Holder.Layout.Align);
		FMemory::Memzero(Buf, Holder.Layout.TotalSize);
		for (int32 Idx = 0; Idx < NumArgs; ++Idx)
		{
			const FAsParmsLayout::FEntry& E = Holder.Layout.Entries[Idx];
			void* Src = Paddrs[Idx].ToAddr();
			if (E.bPointerSlot)
				*reinterpret_cast<void**>(reinterpret_cast<uint8*>(Buf) + E.Offset) = *reinterpret_cast<void**>(Src);
			else
				FMemory::Memcpy(reinterpret_cast<uint8*>(Buf) + E.Offset, Src, E.Size);
		}
		// DirectObject is a script-object/delegate pointer (or null for a plain closure), not a UObject world context;
		// the callback has no ambient UObject world, matching the context-path behavior, so pass null here.
		FAngelscriptGameThreadScopeWorldContext WorldContext(nullptr);
		FScriptExecution Execution(FAngelscriptEngine::GameThreadTLD);
		Holder.JitEntry(Execution, Holder.DirectObject, Buf);
		return;
	}

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

// GMP.ListenObjectMessage(WatchedObj, MsgKey, WeakObj, callback [,Times]) -> key. DirectStore (key-固化, direct-signal
// only) is the pre-resolved static store for this key; when non-null listen binds by store and skips the FName/TMap lookup.
inline int64 As_ListenObjectMessage(UObject* WatchedObject, const FString& MsgKeyStr, UObject* WeakObj, asIScriptFunction* Callback, int32 LeftTimes = -1, GMP::FSignalStore* DirectStore = nullptr)
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
	RetKey = FGMPHelper::GetMessageHub()->ScriptListenMessageRawByStore(
		DirectStore,
		WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(Ctx),
		MsgKey,
		WeakObj,
		[Holder{std::move(Holder)}](const FGMPTypedAddr* paddrs, const GMP::FGMPExtra* extra) { GMP_As_InvokeListenCallback(paddrs, extra->Size, extra->Key, extra->TypeNames, nullptr, Holder); },
		LeftTimes);
#else
	(void)DirectStore;
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
// CachedStore is the compile-time-equivalent direct-signal store resolved once at bind time (key-固化 for AngelScript,
// whose per-tag binds are registered at runtime rather than codegen'd), so listen/notify skip the FName/TMap lookup.
struct FGMPTypedTagCtx
{
	FName Key;
	TArray<FName> ParamTypes;
	GMP::FSignalStore* CachedStore = nullptr;
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
#if GMP_WITH_DIRECT_SIGNAL
	if (Ctx->CachedStore)
		FGMPHelper::GetMessageHub()->ScriptNotifyMessageByStore(Ctx->CachedStore, Ctx->Key, Params, Sender);
	else
		FGMPHelper::ScriptNotifyMessage(Ctx->Key, Params, Sender);
#else
	FGMPHelper::ScriptNotifyMessage(Ctx->Key, Params, Sender);
#endif
}

// Generic thunk for Listen_<id>(UObject WatchedObj, UObject WeakObj, FOn_<id>@ cb, int Times):
// args (0)=WatchedObj (1)=WeakObj (2)=funcdef handle (3)=Times. The signature mirrors the weak-typed ListenObjectMessage
// minus the key literal, so the preprocessor无感 rewrite is a pure "drop the key arg + rename to Listen_<id>".
inline void As_TypedListen_Generic(asIScriptGeneric* Gen)
{
	auto* Ctx = static_cast<FGMPTypedTagCtx*>(Gen->GetFunction()->GetUserData());
	UObject* WatchedObj = static_cast<UObject*>(Gen->GetArgObject(0));
	UObject* WeakObj = static_cast<UObject*>(Gen->GetArgObject(1));
	asIScriptFunction* Callback = static_cast<asIScriptFunction*>(Gen->GetArgObject(2));  // as_generic.cpp: GetArgObject supports funcdef
	int32 Times = Gen->GetArgCount() > 3 ? (int32)Gen->GetArgDWord(3) : -1;
	int64 RetKey = (Ctx && Callback) ? As_ListenObjectMessage(WatchedObj, Ctx->Key.ToString(), WeakObj, Callback, Times, Ctx ? Ctx->CachedStore : nullptr) : 0;
	Gen->SetReturnQWord((asQWORD)RetKey);
}

// Sanitizes a tag string into the identifier suffix used by Listen_<id>/Notify_<id> (same rule as the editor .as codegen).
inline FString GMP_As_TagToId(const FString& TagStr)
{
	FString Id = TagStr;
	for (TCHAR& C : Id)
		if (!((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_'))
			C = '_';
	return Id;
}

// key ("Player.Hurt") -> id ("Player_Hurt") map, filled at bind time; the preprocessor无感 rewrite consults it to map a
// literal key in a weak-typed GMP call onto the strongly-typed Listen_<id>/Notify_<id> registered for that tag.
inline TMap<FString, FString>& GMP_As_KeyToId()
{
	static TMap<FString, FString> Map;
	return Map;
}

// Registers Listen_<id>/Notify_<id> for every tag in the runtime signature table (UGMPMeta). Call once the AS engine
// is up (and again on signature changes). Type names map via GMP_AsTypeName, matching the editor-side .as stub codegen.
inline void GMP_RegisterTypedBinds(const UObject* WorldContext)
{
	asIScriptEngine* Engine = FAngelscriptEngine::Get().GetScriptEngine();
	if (!ensure(Engine))
		return;

	GMP::EnumerateMessageTagMetas(WorldContext, [&](FName Tag, const TArray<FName>& ParamTypes, const TArray<FName>&) {
		const FString TagStr = Tag.ToString();
		FString Id = GMP_As_TagToId(TagStr);
		GMP_As_KeyToId().Add(TagStr, Id);

		FString FuncdefDecl, ParamDecl;
		for (int32 i = 0; i < ParamTypes.Num(); ++i)
		{
			const FString T = GMP_AsTypeName(ParamTypes[i]);
			FuncdefDecl += FString::Printf(TEXT("%s%s A%d"), i ? TEXT(", ") : TEXT(""), *T, i);
			ParamDecl += FString::Printf(TEXT(", %s A%d"), *T, i);
		}

		Engine->RegisterFuncdef(TCHAR_TO_UTF8(*FString::Printf(TEXT("void FOn_%s(%s)"), *Id, *FuncdefDecl)));

		FGMPTypedTagCtx* Ctx = new FGMPTypedTagCtx{Tag, ParamTypes};  // leaked intentionally: lives with the engine binding
#if GMP_WITH_DIRECT_SIGNAL
		// key-固化: resolve the direct-signal store once at bind time so runtime listen/notify skip the FName/TMap lookup.
		Ctx->CachedStore = FGMPHelper::GetMessageHub()->GetDirectStoreByKey(Tag);
#endif
		FAngelscriptBinds::BindGlobalGenericFunction(TCHAR_TO_UTF8(*FString::Printf(TEXT("int64 asListen_%s(UObject WatchedObj, UObject WeakObj, FOn_%s@ cb, int Times = -1)"), *Id, *Id)), &As_TypedListen_Generic, Ctx);
		FAngelscriptBinds::BindGlobalGenericFunction(TCHAR_TO_UTF8(*FString::Printf(TEXT("void asNotify_%s(UObject Sender%s)"), *Id, *ParamDecl)), &As_TypedNotify_Generic, Ctx);
	});
}

// ---- Preprocessor无感 rewrite (block③). Hooks AngelScript's official FAngelscriptPreprocessor::OnPostProcessCode (fires
// after all built-in transforms, before the code is handed to the compiler; same pipeline the plugin uses to fold n!"X"
// literals into __STATIC_NAME). Rewrites weak-typed GMP::(Listen|Notify)ObjectMessage(... "key" ...) into the strongly-
// typed GMP::Listen_<id>/Notify_<id> (drops the key literal, renames), so usage scripts stay unchanged yet gain the VM-
// direct (block①) + key-固化 (block②) fast paths. Only calls whose key argument is a plain string literal are rewritten.

// Splits a call's argument-list source (text strictly inside the outer parens) into top-level args, honoring nested
// (), [], <>, string literals and // /* */ comments. Returns the [start,end) char range of each top-level argument.
struct FAsArgSpan { int32 Start; int32 End; };
inline void GMP_As_SplitTopLevelArgs(const FString& Code, int32 OpenParenPos, int32 CloseParenPos, TArray<FAsArgSpan>& Out)
{
	int32 Depth = 0;
	bool bInStr = false, bInLine = false, bInBlock = false;
	int32 ArgStart = OpenParenPos + 1;
	for (int32 Pos = OpenParenPos + 1; Pos < CloseParenPos; ++Pos)
	{
		const TCHAR C = Code[Pos];
		if (bInLine) { if (C == '\n') bInLine = false; continue; }
		if (bInBlock) { if (C == '*' && Pos + 1 < CloseParenPos && Code[Pos + 1] == '/') { bInBlock = false; ++Pos; } continue; }
		if (bInStr)
		{
			if (C == '\\') { ++Pos; continue; }
			if (C == '"') bInStr = false;
			continue;
		}
		if (C == '/' && Pos + 1 < CloseParenPos && Code[Pos + 1] == '/') { bInLine = true; ++Pos; continue; }
		if (C == '/' && Pos + 1 < CloseParenPos && Code[Pos + 1] == '*') { bInBlock = true; ++Pos; continue; }
		if (C == '"') { bInStr = true; continue; }
		if (C == '(' || C == '[' || C == '<') { ++Depth; continue; }
		if (C == ')' || C == ']' || C == '>') { --Depth; continue; }
		if (C == ',' && Depth == 0) { Out.Add({ArgStart, Pos}); ArgStart = Pos + 1; }
	}
	Out.Add({ArgStart, CloseParenPos});
}

// Extracts the single string-literal content if the trimmed span is exactly one "..." literal; else returns false.
inline bool GMP_As_ArgIsStringLiteral(const FString& Code, const FAsArgSpan& Span, FString& OutLiteral)
{
	int32 S = Span.Start, E = Span.End;
	while (S < E && FChar::IsWhitespace(Code[S])) ++S;
	while (E > S && FChar::IsWhitespace(Code[E - 1])) --E;
	if (E - S < 2 || Code[S] != '"' || Code[E - 1] != '"')
		return false;
	for (int32 P = S + 1; P < E - 1; ++P)
	{
		if (Code[P] == '\\') { ++P; continue; }
		if (Code[P] == '"')
			return false;  // more than one literal / concatenation -- not a plain key
	}
	OutLiteral = Code.Mid(S + 1, E - S - 2);
	return true;
}

// Rewrites all GMP::(Listen|Notify)ObjectMessage(...) calls with a literal key into Listen_<id>/Notify_<id>. Scans the
// whole file honoring string/comment lexing so matches inside strings or comments are ignored.
inline void GMP_As_RewriteGMPCalls(FString& Code)
{
	static const FString Prefixes[] = {TEXT("GMP::ListenObjectMessage"), TEXT("GMP::NotifyObjectMessage")};
	static const FString Repl[] = {TEXT("GMP::asListen_"), TEXT("GMP::asNotify_")};
	// The key literal is the 2nd argument for Listen (WatchedObj, "key", ...) and Notify (Sender, "key", ...).
	const int32 KeyArgIndex = 1;

	for (int32 Which = 0; Which < 2; ++Which)
	{
		const FString& Prefix = Prefixes[Which];
		int32 SearchFrom = 0;
		while (true)
		{
			const int32 Found = Code.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Found == INDEX_NONE)
				break;

			// Reject matches inside a string/comment, or where the prefix is part of a longer identifier.
			int32 After = Found + Prefix.Len();
			const TCHAR NextCh = After < Code.Len() ? Code[After] : TEXT('\0');
			int32 ParenPos = After;
			while (ParenPos < Code.Len() && FChar::IsWhitespace(Code[ParenPos])) ++ParenPos;
			if (NextCh == TEXT('_') || FChar::IsAlnum(NextCh) || ParenPos >= Code.Len() || Code[ParenPos] != TEXT('('))
			{
				SearchFrom = After;
				continue;
			}

			// Find the matching close paren from ParenPos (honoring nesting/strings/comments).
			int32 Depth = 0, Close = INDEX_NONE;
			bool bInStr = false, bInLine = false, bInBlock = false;
			for (int32 P = ParenPos; P < Code.Len(); ++P)
			{
				const TCHAR C = Code[P];
				if (bInLine) { if (C == '\n') bInLine = false; continue; }
				if (bInBlock) { if (C == '*' && P + 1 < Code.Len() && Code[P + 1] == '/') { bInBlock = false; ++P; } continue; }
				if (bInStr) { if (C == '\\') { ++P; continue; } if (C == '"') bInStr = false; continue; }
				if (C == '/' && P + 1 < Code.Len() && Code[P + 1] == '/') { bInLine = true; ++P; continue; }
				if (C == '/' && P + 1 < Code.Len() && Code[P + 1] == '*') { bInBlock = true; ++P; continue; }
				if (C == '"') { bInStr = true; continue; }
				if (C == '(') ++Depth;
				else if (C == ')') { if (--Depth == 0) { Close = P; break; } }
			}
			if (Close == INDEX_NONE)
				break;

			TArray<FAsArgSpan> Args;
			GMP_As_SplitTopLevelArgs(Code, ParenPos, Close, Args);
			FString KeyLiteral;
			if (Args.Num() <= KeyArgIndex || !GMP_As_ArgIsStringLiteral(Code, Args[KeyArgIndex], KeyLiteral))
			{
				SearchFrom = Close + 1;
				continue;
			}
			const FString* Id = GMP_As_KeyToId().Find(KeyLiteral);
			if (!Id)
			{
				SearchFrom = Close + 1;
				continue;
			}

			// Build the rewritten call: <Repl><id>( <args except the key literal> ).
			FString NewArgs;
			for (int32 i = 0; i < Args.Num(); ++i)
			{
				if (i == KeyArgIndex)
					continue;
				FString Arg = Code.Mid(Args[i].Start, Args[i].End - Args[i].Start).TrimStartAndEnd();
				NewArgs += (NewArgs.IsEmpty() ? TEXT("") : TEXT(", "));
				NewArgs += Arg;
			}
			const FString NewCall = Repl[Which] + *Id + TEXT("(") + NewArgs + TEXT(")");
			Code = Code.Mid(0, Found) + NewCall + Code.Mid(Close + 1);
			SearchFrom = Found + NewCall.Len();
		}
	}
}

// Installs the OnPostProcessCode hook once. Safe to call from module startup / engine init.
inline void GMP_As_InstallPreprocessorHook()
{
	static bool bInstalled = false;
	if (bInstalled)
		return;
	bInstalled = true;
	FAngelscriptPreprocessor::OnPostProcessCode.AddLambda([](FAngelscriptPreprocessor& PP) {
		if (GMP_As_KeyToId().Num() == 0)
			return;
		for (FAngelscriptPreprocessor::FFile& File : PP.Files)
			GMP_As_RewriteGMPCalls(File.ProcessedCode);
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
// 5. (optional, block③) call AngelScriptSupport::GMP_As_InstallPreprocessorHook() once at startup so usage scripts that write the
//    weak-typed GMP::(Listen|Notify)ObjectMessage(..., "key", ...) are rewritten to the strongly-typed Listen_<id>/Notify_<id>
//    at preprocess time -- source unchanged, yet they gain the VM-direct (block①) + key-固化 (block②) fast paths.

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
    // int64 asListen_Player_Hurt(UObject WatchedObj, UObject WeakObj, FOn_Player_Hurt@ cb, int Times = -1);
    // void  asNotify_Player_Hurt(UObject Sender, int Damage, AActor Causer);
}

// example.as (strongly-typed, explicit):
int64 Key = GMP::asListen_Player_Hurt(null, this, function(int Damage, AActor Causer) { Print("hurt " + Damage); });
GMP::asNotify_Player_Hurt(this, 42, causer);
GMP::UnbindObjectMessage("Player.Hurt", this);

// example.as (无感, block③: weak-typed with a literal key -> rewritten to the strongly-typed calls above at preprocess time):
GMP::ListenObjectMessage(null, "Player.Hurt", this, function(int Damage, AActor Causer) { Print("hurt " + Damage); });
GMP::NotifyObjectMessage(this, "Player.Hurt", 42, causer);
#endif