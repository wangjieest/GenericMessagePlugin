#pragma once
#if defined(UNLUA_API)
#include "GMPCore.h"
#include "UnLuaDelegates.h"
#include "UnLuaEx.h"

#if 1
UnLua::ITypeInterface* CreateTypeInterface(FProperty* InProp)
{
	return FPropertyDesc::Create(InProp);
}
UnLua::ITypeInterface* CreateTypeInterface(lua_State* L, int32 Idx)
{
	auto& Env = UnLua::FLuaEnv::FindEnvChecked(L);
	return Env.GetPropertyRegistry()->CreateTypeInterface(L, Idx).Get();
}
#else
extern UnLua::ITypeInterface* CreateTypeInterface(FProperty* InProp);
extern UnLua::ITypeInterface* CreateTypeInterface(lua_State* L, int32 Idx);
#endif

// lua_function ListenObjectMessage(watchedobj, msgkey, weakobj, globalfunc [,times])
// lua_function ListenObjectMessage(watchedobj, msgkey, weakobj, globalfuncstr [,times])
// lua_function ListenObjectMessage(watchedobj, msgkey, tableobj, tablefunc [,times])
// lua_function ListenObjectMessage(watchedobj, msgkey, tableobj, tablefuncstr [,times])
inline int Lua_ListenObjectMessage(lua_State* L)
{
	lua_Number RetNum{};
	do
	{
		enum GMP_Listen_Index : int32
		{
			WatchedObj = 1,
			MessageKey,
			WeakObject,
			Function,
			Times,
		};

		int32 LeftTimes = -1;
		if (lua_gettop(L) == GMP_Listen_Index::Times)
		{
			LeftTimes = UnLua::Get(L, GMP_Listen_Index::Times, UnLua::TType<int32>{});
			lua_pop(L, 1);
		}
		else if (!ensure(lua_gettop(L) == GMP_Listen_Index::Function))
		{
			break;
		}

		UObject* WatchedObject = UnLua::GetUObject(L, GMP_Listen_Index::WatchedObj);
		const FName MsgKey = UnLua::Get(L, GMP_Listen_Index::MessageKey, UnLua::TType<FName>{});
		UObject* WeakObj = UnLua::GetUObject(L, GMP_Listen_Index::WeakObject);

		static auto DetectTableFunction = [](lua_State* L, int32 TableIndex, int32 FunctionIndex) {
			if (!lua_isfunction(L, FunctionIndex))
				return false;

			int32 TopIdx = lua_gettop(L);

			lua_pushnil(L);
			bool bRet = false;
			while (lua_next(L, TableIndex))
			{
				if (lua_rawequal(L, -1, FunctionIndex))
				{
					lua_pop(L, lua_gettop(L) - TopIdx);
					bRet = true;
					break;
				}
				lua_pop(L, 1);
			}

#if WITH_EDITOR || (!UE_BUILD_SHIPPING)
			GMP_CHECK(lua_gettop(L) == TopIdx);
#endif
			return bRet;
		};

		UObject* TableObj = nullptr;
		if (lua_type(L, 4) == LUA_TSTRING)
		{
			// string as global function entry
			lua_getglobal(L, lua_tostring(L, GMP_Listen_Index::Function));
			lua_replace(L, GMP_Listen_Index::Function);
		}
#if 1
		else if (WeakObj && DetectTableFunction(L, GMP_Listen_Index::WeakObject, GMP_Listen_Index::Function))
		{
			TableObj = WeakObj;
		}
#endif

		if (!ensure(lua_isfunction(L, GMP_Listen_Index::Function)))
			break;

		int lua_cb = luaL_ref(L, LUA_REGISTRYINDEX);
		struct FLubCb
		{
			int32 FuncRef = INT_MAX;
			FLubCb(int32 In)
				: FuncRef(In)
			{
			}
			FLubCb(const FLubCb&) = delete;
			FLubCb& operator=(const FLubCb&) = delete;
			FLubCb(FLubCb&& Cb)
			{
				FuncRef = Cb.FuncRef;
				Cb.FuncRef = INT_MAX;
			}
			~FLubCb()
			{
				lua_State* L = UnLua::GetState();
				if (L && FuncRef != INT_MAX)
					luaL_unref(L, LUA_REGISTRYINDEX, FuncRef);
			}
		};

		uint64 RetKey = FGMPHelper::ScriptListenMessage(
			WatchedObject,
			MsgKey,
			WeakObj,
			[LubCb{FLubCb(lua_cb)}, WatchedObject, TableObj](GMP::FMessageBody& Body) {
				lua_State* L = UnLua::GetState();
				if (!ensure(L))
					return;

				bool bSucc = true;
				auto& Addrs = Body.GetParams();
				const int32 NumArgs = Addrs.Num();

				TArray<UnLua::ITypeInterface*, TInlineAllocator<8>> Incs;
				auto Types = Body.GetMessageTypes(WatchedObject);

#if !GMP_WITH_TYPENAME
				if (!ensureMsgf(Types,TEXT("unable to verify sig from %s"), *Body.MessageKey().ToString()))
					return;
#endif

				auto GetTypeName = [&](int32 Idx) {
#if GMP_WITH_TYPENAME
					return Addrs[Idx].TypeName;
#else
					return (*Types)[Idx];
#endif
				};

				for (auto i = 0; i < NumArgs; ++i)
				{
					FProperty* Prop = nullptr;
					if (GMPReflection::PropertyFromString(GetTypeName(i).ToString(), Prop) && Prop)
					{
						using namespace UnLua;
						if (auto Inc = CreateTypeInterface(Prop))
						{
							Incs.Add(Inc);
							continue;
						}
					}

					GMP_ERROR(TEXT("cannot get property from [%s]"), *GetTypeName(i).ToString());
					bSucc = false;
					break;
				}

				lua_pop(L, -1);
				if (bSucc)
				{
					lua_pushcfunction(L, UnLua::ReportLuaCallError);
					const int32 errfunc = lua_gettop(L);
					lua_rawgeti(L, LUA_REGISTRYINDEX, LubCb.FuncRef);
					if (!lua_isfunction(L, -1))
					{
						lua_pop(L, -1);
						return;
					}

					if (TableObj)
						UnLua::PushUObject(L, TableObj);

					for (auto i = 0; i < NumArgs; ++i)
					{
						auto& Inc = Incs[i];
#if 1
						// fixme : make unlua happy, unlua treat all integer as same type
						auto IncProp = CastField<FNumericProperty>(Inc->GetUProperty());
						if (IncProp && IncProp->IsInteger())
						{
							auto IntVal = IncProp->GetUnsignedIntPropertyValue(Addrs[i].ToAddr());
							Inc->Read(L, &IntVal, true);
						}
						else
#endif
						{
							Inc->Read(L, Addrs[i].ToAddr(), true);
						}
					}

					const GMP::FArrayTypeNames* OldParams = nullptr;
					if (!Body.IsSignatureCompatible(false, OldParams))
					{
						GMP_WARNING(TEXT("SignatureMismatch On Lua Listen %s"), *Body.MessageKey().ToString());
					}
					ensureAlways(bSucc && (lua_pcall(L, NumArgs + (TableObj ? 1 : 0), 0, errfunc) == LUA_OK));
					lua_remove(L, errfunc);
				}
			},
			LeftTimes);
		static_assert(sizeof(RetKey) == sizeof(RetNum), "err");
		FMemory::Memcpy(&RetNum, &RetKey, sizeof(RetNum));
	} while (false);
	lua_pop(L, -1);
	lua_pushnumber(L, RetNum);
	return 0;
}

// lua_function UnListenObjectMessage(msgkey, ListenedObj)
// lua_function UnListenObjectMessage(msgkey, Key)
// lua_function UnListenObjectMessage(msgkey, ListenedObj, Key)
inline int Lua_UnListenObjectMessage(lua_State* L)
{
	int32 NumArgs = lua_gettop(L);
	if (NumArgs >= 2)
	{
		FName MsgKey = UnLua::Get(L, 1, UnLua::TType<FName>{});
		UObject* ListenedObj = UnLua::GetUObject(L, 2);
		lua_Number LuaNum = lua_tonumber(L, NumArgs >= 3 ? 3 : 2);
		uint64 Key = 0;
		FMemory::Memcpy(&Key, &LuaNum, sizeof(LuaNum));

		if (ListenedObj)
			FGMPHelper::ScriptUnListenMessage(MsgKey, ListenedObj);
		else
			FGMPHelper::ScriptUnListenMessage(MsgKey, Key);
	}
	lua_pop(L, -1);
	return 0;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4750)  // warning C4750: function with _alloca() inlined into a loop
#endif

// lua_function NotifyObjectMessage(obj, msgkey, ...)
inline int Lua_NotifyObjectMessage(lua_State* L)
{
	int32 NumArgs = lua_gettop(L);
	if (ensure(NumArgs >= 2))
	{
		UObject* Sender = UnLua::GetUObject(L, 1);
		FName MsgKey = GMP::ToMessageKey(UnLua::Get(L, 2, UnLua::TType<const char*>{}));

		GMP::FTypedAddresses Params;
		Params.Reserve(NumArgs);

		TArray<FGMPTypedAddr::FPropertyValuePair, TInlineAllocator<8>> PropPairs;
		PropPairs.Reserve(NumArgs);

		bool bSucc = true;
		for (auto i = 3; i <= NumArgs; ++i)
		{
			using namespace UnLua;
			auto Inc = CreateTypeInterface(L, i);
			FProperty* Prop = Inc ? Inc->GetUProperty() : nullptr;
			if (!Inc || !Prop)
			{
				bSucc = false;
				break;
			}
			auto& Ref = PropPairs.Emplace_GetRef(Prop, FMemory_Alloca_Aligned(Prop->ElementSize, Prop->GetMinAlignment()));
			Inc->Write(L, Ref.Addr, i);
			Params.AddDefaulted_GetRef().SetAddr(Ref);
		}

#if GMP_WITH_DYNAMIC_TYPE_CHECK
		if (auto Types = GMP::FMessageBody::GetMessageTypes(Sender, MsgKey))
		{
			for (auto i = 0; i < PropPairs.Num(); ++i)
			{
				if (!GMPReflection::EqualPropertyName(PropPairs[i].Prop, (*Types)[i], false))
				{
					GMP_WARNING(TEXT("SignatureMismatch On Lua Notify %s"), *MsgKey.ToString());
					bSucc = false;
					break;
				}
			}
		}
#endif

		if (bSucc)
			FGMPHelper::ScriptNotifyMessage(MsgKey, Params, Sender);
	}
	lua_pop(L, -1);
	return 0;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

inline void GMP_RegisterToLua(lua_State* L)
{
	lua_register(L, "NotifyObjectMessage", Lua_NotifyObjectMessage);
	lua_register(L, "ListenObjectMessage", Lua_ListenObjectMessage);
	lua_register(L, "UnListenObjectMessage", Lua_UnListenObjectMessage);
}

inline void GMP_ExportToLuaEx()
{
	lua_State* L = UnLua::GetState();
	if (L)
	{
		GMP_RegisterToLua(L);
	}
	else
	{
		FUnLuaDelegates::OnLuaStateCreated.AddStatic(GMP_RegisterToLua);
	}
}

struct GMP_ExportToLuaExObj
{
	GMP_ExportToLuaExObj() { GMP_ExportToLuaEx(); }
} ExportToLuaExObj;
#endif  // defined(UNLUA_API)

// how to use:
// 1. add "GMP" to PrivateDependencyModuleNames in Unlua.Build.cs
// 2. just included this header file into LuaCore.cpp in unlua module
