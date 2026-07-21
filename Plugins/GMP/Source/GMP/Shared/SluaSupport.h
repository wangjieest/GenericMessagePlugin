//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if defined(SLUA_UNREAL_API)
#include "GMPCore.h"

#include "luaconf.h"
namespace NS_SLUA { typedef struct lua_State lua_State; }
GMP_EXTERNAL_SIGSOURCE(NS_SLUA::lua_State)

#include "LuaObject.h"
#include "LuaState.h"
#include "LuaVar.h"
#include "GMPLuaRewrite.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
namespace SluaSupport
{
using namespace NS_SLUA;

// Holds the lua callback + optional weak-table ref for a listen's lifetime; unref'd on unbind/dtor.
struct FSluaCb
{
	lua_State* L = nullptr;
	int FuncRef = LUA_NOREF;
	int ObjRef = LUA_NOREF;
	FSluaCb(lua_State* InL, int InFunc, int InObj = LUA_NOREF)
		: L(InL), FuncRef(InFunc), ObjRef(InObj)
	{
	}
	FSluaCb(const FSluaCb&) = delete;
	FSluaCb& operator=(const FSluaCb&) = delete;
	FSluaCb(FSluaCb&& In) noexcept
		: L(In.L), FuncRef(In.FuncRef), ObjRef(In.ObjRef)
	{
		In.FuncRef = LUA_NOREF;
		In.ObjRef = LUA_NOREF;
	}
	~FSluaCb()
	{
		if (L)
		{
			if (FuncRef != LUA_NOREF)
				luaL_unref(L, LUA_REGISTRYINDEX, FuncRef);
			if (ObjRef != LUA_NOREF)
				luaL_unref(L, LUA_REGISTRYINDEX, ObjRef);
		}
	}
};

// Shared listen dispatch. Params normalized to (paddrs, count, key, typename-source) by both paths (raw paddrs+extra
// or FMessageBody), mirroring the Lua/Puerts adapters. Enhanced vs UnLua: pushes each arg via LuaObject::push(FProperty*)
// which uses slua's cached per-property pusher (a bare function pointer) -- no ITypeInterface object allocation per arg.
inline void GMP_Slua_InvokeListenCallback(const FGMPTypedAddr* Paddrs, int32 NumArgs, FName KeyName, const FName* InRawTypeNames, const TArray<FName>* InMetaTypes, const FSluaCb& Cb)
{
	lua_State* L = Cb.L;
	if (!ensure(L) || Cb.FuncRef == LUA_NOREF)
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
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("Slua"));
	if (!ensure(GMP::FMessageHub::IsSignatureCompatible(false, KeyName, ArgNames, OldParams)))
	{
		GMP_WARNING(TEXT("SignatureMismatch On Slua Listen %s"), *KeyName.ToString());
		return;
	}
#endif

	const int Top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Cb.FuncRef);
	if (!lua_isfunction(L, -1))
	{
		lua_settop(L, Top);
		return;
	}

	int PushedArgs = 0;
	for (auto Idx = 0; Idx < NumArgs; ++Idx)
	{
		FProperty* Prop = nullptr;
		if (!GMPReflection::PropertyFromString(GetTypeName(Idx).ToString(), Prop) || !Prop)
		{
			GMP_ERROR(TEXT("[GMPSlua] cannot get property from [%s]"), *GetTypeName(Idx).ToString());
			lua_settop(L, Top);
			return;
		}
		// slua cached pusher: UE value memory -> lua stack, no per-arg interface object.
		LuaObject::push(L, Prop, reinterpret_cast<uint8*>(Paddrs[Idx].ToAddr()));
		++PushedArgs;
	}

	if (lua_pcall(L, PushedArgs, 0, 0) != LUA_OK)
	{
		GMP_WARNING(TEXT("[GMPSlua] callback error on %s: %hs"), *KeyName.ToString(), lua_tostring(L, -1));
		lua_settop(L, Top);
		return;
	}
	lua_settop(L, Top);
}

// ListenObjectMessage(watchedobj, msgkey, weakobj, function [,times]) -> key
inline int Lua_ListenObjectMessage(lua_State* L)
{
	lua_Number RetNum = 0;
	do
	{
		const int NumArgs = lua_gettop(L);
		if (!ensure(NumArgs >= 4 && lua_isfunction(L, 4)))
			break;

		UObject* WatchedObject = LuaObject::checkValueOpt<UObject*>(L, 1, nullptr);
		const FName MsgKey = UTF8_TO_TCHAR(luaL_checkstring(L, 2));
		UObject* WeakObj = LuaObject::checkValueOpt<UObject*>(L, 3, nullptr);
		int32 LeftTimes = NumArgs >= 5 ? (int32)luaL_checkinteger(L, 5) : -1;
		if (!ensure(!MsgKey.IsNone()))
			break;

		lua_pushvalue(L, 4);
		const int FuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
		FSluaCb Cb(L, FuncRef);

#if GMP_TRACE_SCRIPT_SRC
		{
			luaL_traceback(L, L, nullptr, 1);
			const FString Loc = UTF8_TO_TCHAR(lua_tostring(L, -1));
			lua_pop(L, 1);
			if (!Loc.IsEmpty())
				GMP::TraceScriptMessageSource(MsgKey, Loc, /*bIsListen*/ true);
		}
#endif

		uint64 Key = 0;
#if GMP_WITH_DIRECT_SIGNAL
		Key = FGMPHelper::ScriptListenMessageRaw(
			WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(L),
			MsgKey, WeakObj,
			[Cb{std::move(Cb)}](const FGMPTypedAddr* paddrs, const GMP::FGMPExtra* extra) { GMP_Slua_InvokeListenCallback(paddrs, extra->Size, extra->Key, extra->TypeNames, nullptr, Cb); },
			LeftTimes);
#else
		Key = FGMPHelper::ScriptListenMessage(
			WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(L),
			MsgKey, WeakObj,
			[Cb{std::move(Cb)}, WeakObj](GMP::FMessageBody& MsgBody) {
				const auto Addrs = MsgBody.GetParams();
				GMP_Slua_InvokeListenCallback(Addrs.GetData(), Addrs.Num(), MsgBody.MessageKey(), nullptr, MsgBody.GetMessageTypes(WeakObj), Cb);
			},
			LeftTimes);
#endif
		RetNum = (lua_Number)Key;
	} while (false);
	lua_pushnumber(L, RetNum);
	return 1;
}

// UnbindObjectMessage(msgkey, listenedobj) / UnbindObjectMessage(msgkey, key)
inline int Lua_UnbindObjectMessage(lua_State* L)
{
	if (lua_gettop(L) >= 2)
	{
		const FName MsgKey = UTF8_TO_TCHAR(luaL_checkstring(L, 1));
		if (UObject* ListenedObj = LuaObject::checkValueOpt<UObject*>(L, 2, nullptr))
			FGMPHelper::ScriptUnbindMessage(MsgKey, ListenedObj);
		else
			FGMPHelper::ScriptUnbindMessage(MsgKey, FGMPKey((uint64)luaL_checkinteger(L, 2)));
	}
	return 0;
}

// NotifyObjectMessage(sender, msgkey, ...) -> bool. Enhanced vs UnLua: uses slua's cached per-property checker (bare
// function pointer) to read lua stack -> UE memory, instead of allocating an ITypeInterface per arg.
inline int Lua_NotifyObjectMessage(lua_State* L)
{
	bool bSucc = false;
	do
	{
		const int NumArgs = lua_gettop(L);
		if (!ensure(NumArgs >= 2))
			break;

		UObject* Sender = LuaObject::checkValueOpt<UObject*>(L, 1, nullptr);
		const FName MsgKey = UTF8_TO_TCHAR(luaL_checkstring(L, 2));

		auto Types = GMP::FMessageBody::GetMessageTypes(Sender, MsgKey);
		if (!ensure(Types && NumArgs - 2 >= Types->Num()))
		{
			GMP_WARNING(TEXT("[GMPSlua] GetMessageTypes null or arg count mismatch %s"), *MsgKey.ToString());
			break;
		}

		FGMPPropStackHolderArray PropHolders;
		PropHolders.Reserve(NumArgs);
		bSucc = true;
		for (int32 i = 0; i < Types->Num(); ++i)
		{
			FProperty* Prop = nullptr;
			if (!GMPReflection::PropertyFromString((*Types)[i].ToString(), Prop) || !Prop)
			{
				bSucc = false;
				break;
			}
			auto& Holder = PropHolders.Emplace_GetRef(Prop, FMemory_Alloca_Aligned(Prop->ElementSize, Prop->GetMinAlignment()));
			// slua cached checker: lua stack -> UE memory, no per-arg interface object.
			if (auto Checker = LuaObject::getChecker(Prop))
				Checker(L, Prop, reinterpret_cast<uint8*>(Holder.GetAddr()), 3 + i, /*bForceCopy*/ false);
			else
				bSucc = false;
		}

		if (bSucc)
		{
			GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("Slua"));
			GMP::FTypedAddresses Params;
			Params.Reserve(NumArgs);
			bSucc = FGMPHelper::ScriptNotifyMessage(MsgKey, FGMPTypedAddr::FromHolderArray(Params, PropHolders), Sender);
		}
	} while (false);
	lua_pushboolean(L, bSucc);
	return 1;
}

// Registers the GMP.* globals into a lua_State. Call once per state (e.g. on LuaState created).
#if defined(GMP_SLUA_STATIC_BIND) && GMP_SLUA_STATIC_BIND
void GMP_RegisterStaticBinds(lua_State* L);  // defined in generated GMPSluaBinds.gen.cpp

inline TArray<uint8> GMP_Slua_LoadFile(const char* fn, FString& filepath)
{
	FString Rel = UTF8_TO_TCHAR(fn);
	Rel.ReplaceInline(TEXT("."), TEXT("/"));
	FString Raw;
	const FString Candidates[] = {
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Lua"), Rel + TEXT(".lua")),
		FPaths::Combine(FPaths::ProjectContentDir(), Rel + TEXT(".lua")),
		FPaths::Combine(FPaths::ProjectDir(), Rel + TEXT(".lua")),
	};
	for (const FString& Cand : Candidates)
	{
		const FString Full = FPaths::ConvertRelativePathToFull(Cand);
		if (FFileHelper::LoadFileToString(Raw, *Full))
		{
			filepath = Full;
			const FString Rewritten = GMPLuaRewrite::Rewrite(Raw);
			FTCHARToUTF8 Utf8(*Rewritten);
			TArray<uint8> Out;
			Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			return Out;
		}
	}
	return TArray<uint8>();
}
#endif

inline void GMP_RegisterToSlua(lua_State* L)
{
	if (!ensure(L))
		return;
	LuaObject::addGlobalMethod(L, "ListenObjectMessage", Lua_ListenObjectMessage);
	LuaObject::addGlobalMethod(L, "NotifyObjectMessage", Lua_NotifyObjectMessage);
	LuaObject::addGlobalMethod(L, "UnbindObjectMessage", Lua_UnbindObjectMessage);
	LuaObject::addGlobalMethod(L, "UnListenObjectMessage", Lua_UnbindObjectMessage);
#if defined(GMP_SLUA_STATIC_BIND) && GMP_SLUA_STATIC_BIND
	GMP_RegisterStaticBinds(L);  // per-tag strongly-typed Notify_<id> from generated GMPSluaBinds.gen.cpp
	if (LuaState* State = LuaState::get(L))
		State->setLoadFileDelegate(&GMP_Slua_LoadFile);  // transparent NotifyObjectMessage -> Notify_<id> at load time
#endif
}
}  // namespace SluaSupport
#endif

// how to use:
// 1. add "GMP" to PrivateDependencyModuleNames in slua_unreal.Build.cs (and PrivateIncludePaths to GMP/Source/GMP/Shared if unseen)
// 2. include this header into a slua_unreal-module TU, then call SluaSupport::GMP_RegisterToSlua(L) for each created lua_State
//    (e.g. from a NS_SLUA::LuaState::onInitEvent handler or right after LuaState::init).
// 3. cleanup: on state close, drop lua_State-keyed listens via FGMPSigSource::RemoveSource(L).
//
// enhanced vs UnLua: listen callback pushes args with slua's cached per-property pusher (LuaObject::push(FProperty*)),
// and notify reads args with the cached checker (LuaObject::getChecker) -- both are bare function pointers, avoiding the
// per-arg ITypeInterface object UnLua allocates via CreateTypeInterface.

#if 0
--- GMP.lua usage
-- key = ListenObjectMessage(watchedobj, "MsgKey", weakobj, function(a, b) end [, times])
-- NotifyObjectMessage(sender, "MsgKey", a, b)
-- UnbindObjectMessage("MsgKey", listenedobj)  -- or UnbindObjectMessage("MsgKey", key)
#endif
