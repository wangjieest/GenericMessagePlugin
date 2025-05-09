//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

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

GMP_EXTERNAL_SIGSOURCE(lua_State)

// lua_function ListenObjectMessage(watchedobj, msgkey, tableobj, tablefuncstr   [,times]) // recommended for member function
// lua_function ListenObjectMessage(watchedobj, msgkey, nil,      globalfunction [,times]) // recommended for global function
// lua_function ListenObjectMessage(watchedobj, msgkey, weakobj,  globalfuncstr  [,times])
// lua_function ListenObjectMessage(watchedobj, msgkey, weakobj,  memberfunction [,times])
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
#if WITH_EDITOR
		if (!ensureAlways(lua_gettop(L) <= GMP_Listen_Index::Times))
			break;
#endif
		int luaCurType = LUA_TNONE;
		int32 LeftTimes = -1;
		if (lua_gettop(L) == GMP_Listen_Index::Times)
		{
#if WITH_EDITOR
			luaCurType = lua_type(L, GMP_Listen_Index::Times);
			if (!ensureAlways(luaCurType == LUA_TNUMBER))
				break;
#endif
			LeftTimes = UnLua::Get(L, GMP_Listen_Index::Times, UnLua::TType<int32>{});
			ensureAlways(LeftTimes != 0);
			lua_pop(L, 1);
		}
		else if (!ensure(lua_gettop(L) == GMP_Listen_Index::Function))
		{
			break;
		}
		// should be string or function type
		auto OrignalFuncType = lua_type(L, GMP_Listen_Index::Function);
		if (!ensure(OrignalFuncType == LUA_TSTRING || OrignalFuncType == LUA_TFUNCTION))
		{
			break;
		}

		UObject* WatchedObject = UnLua::GetUObject(L, GMP_Listen_Index::WatchedObj);
		FName MsgKey = GMP::ToMessageKey(UnLua::Get(L, GMP_Listen_Index::MessageKey, UnLua::TType<const char*>{}));
		UObject* WeakObj = UnLua::GetUObject(L, GMP_Listen_Index::WeakObject);

		UObject* TableObj = nullptr;
		if (OrignalFuncType == LUA_TSTRING)
		{
			auto Str = lua_tostring(L, GMP_Listen_Index::Function);
			lua_pop(L, 1);
			if (WeakObj && lua_istable(L, GMP_Listen_Index::WeakObject))
			{
				// member function
				TableObj = WeakObj;
				lua_getfield(L, GMP_Listen_Index::WeakObject, Str);
				ensure(lua_isfunction(L, GMP_Listen_Index::Function));
			}

			if (!lua_isfunction(L, GMP_Listen_Index::Function))
			{
				// global function
				lua_getglobal(L, Str);
				lua_replace(L, GMP_Listen_Index::Function);
			}
		}
		else if (WeakObj)
		{
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
			// slow verify member function in table
			int32 TopIdx = lua_gettop(L);
			lua_pushnil(L);
			while (lua_next(L, GMP_Listen_Index::WeakObject))
			{
				if (lua_rawequal(L, -1, GMP_Listen_Index::Function))
				{
					lua_pop(L, lua_gettop(L) - TopIdx);
					TableObj = WeakObj;
					break;
				}
				lua_pop(L, 1);
			}
			GMP_CHECK(lua_gettop(L) == TopIdx);
			if (!ensureAlwaysMsgf(TableObj, TEXT("must us member function if weakObj exist or using nil for global function")))
				break;
#else
			TableObj = WeakObj;
#endif
		}

#if WITH_EDITOR
		luaCurType = lua_type(L, GMP_Listen_Index::WatchedObj);
		if (!ensureAlways(luaCurType == LUA_TTABLE || luaCurType == LUA_TNIL || luaCurType == LUA_TUSERDATA))
			break;
		luaCurType = lua_type(L, GMP_Listen_Index::MessageKey);
		if (!ensureAlways(luaCurType == LUA_TSTRING || luaCurType == LUA_TUSERDATA))
			break;
		luaCurType = lua_type(L, GMP_Listen_Index::WeakObject);
		if (!ensureAlways(luaCurType == LUA_TTABLE || luaCurType == LUA_TNIL || luaCurType == LUA_TUSERDATA))
			break;
#endif
		if (!ensure(lua_gettop(L) == GMP_Listen_Index::Function && lua_isfunction(L, GMP_Listen_Index::Function)))
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
			WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(L),
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
				if (!ensureMsgf(Types, TEXT("unable to verify sig from %s"), *Body.MessageKey().ToString()))
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

				lua_settop(L, 0);
				if (bSucc)
				{
					lua_pushcfunction(L, UnLua::ReportLuaCallError);
					const int32 errfunc = lua_gettop(L);
					lua_rawgeti(L, LUA_REGISTRYINDEX, LubCb.FuncRef);
					if (!lua_isfunction(L, -1))
					{
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
					GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("Unlua"));
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
	lua_settop(L, 0);
	lua_pushnumber(L, RetNum);
	return 0;
}

// lua_function UnbindObjectMessage(msgkey, ListenedObj)
// lua_function UnbindObjectMessage(msgkey, Key)
// lua_function UnbindObjectMessage(msgkey, ListenedObj, Key)
inline int Lua_UnbindObjectMessage(lua_State* L)
{
	int32 NumArgs = lua_gettop(L);
	do
	{
		if (!ensure(NumArgs >= 2))
			break;
		FName MsgKey = GMP::ToMessageKey(UnLua::Get(L, 1, UnLua::TType<const char*>{}));
		UObject* ListenedObj = UnLua::GetUObject(L, 2);
		lua_Number LuaNum = lua_tonumber(L, NumArgs >= 3 ? 3 : 2);
		uint64 Key = 0;
		FMemory::Memcpy(&Key, &LuaNum, sizeof(LuaNum));

#if WITH_EDITOR
		int luaCurType = LUA_TNONE;
		luaCurType = lua_type(L, 1);
		if (!ensureAlways(luaCurType == LUA_TSTRING || luaCurType == LUA_TUSERDATA))
			break;
		luaCurType = lua_type(L, 2);
		if (!ensureAlways(luaCurType == LUA_TNUMBER || luaCurType == LUA_TTABLE || luaCurType == LUA_TUSERDATA))
			break;
#endif

		if (ListenedObj)
			FGMPHelper::ScriptUnbindMessage(MsgKey, ListenedObj);
		else
			FGMPHelper::ScriptUnbindMessage(MsgKey, Key);

	} while (false);

	lua_settop(L, 0);
	return 0;
}
inline int Lua_UnListenObjectMessage(lua_State* L)
{
	return Lua_UnbindObjectMessage(L);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4750)  // warning C4750: function with _alloca() inlined into a loop
#endif

// lua_function NotifyObjectMessage(obj, msgkey, parameters...)
inline int Lua_NotifyObjectMessage(lua_State* L)
{
	int32 NumArgs = lua_gettop(L);
	do
	{
		if (!ensure(NumArgs >= 2))
			break;
		UObject* Sender = UnLua::GetUObject(L, 1);
		FName MsgKey = GMP::ToMessageKey(UnLua::Get(L, 2, UnLua::TType<const char*>{}));

#if WITH_EDITOR
		int luaCurType = LUA_TNONE;
		luaCurType = lua_type(L, 1);
		if (!ensureAlways(luaCurType == LUA_TTABLE || luaCurType == LUA_TUSERDATA))
			break;
		luaCurType = lua_type(L, 2);
		if (!ensureAlways(luaCurType == LUA_TSTRING || luaCurType == LUA_TUSERDATA))
			break;
#endif

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
#if WITH_EDITOR
		else
		{
		}
#endif
#endif

		if (bSucc)
		{
			GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("Unlua"));
			FGMPHelper::ScriptNotifyMessage(MsgKey, Params, Sender);
		}
	} while (false);
	lua_settop(L, 0);
	return 0;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Self:ListenWorldMessage(msgkey, func, times)
// Self:ListenObjectMessage(Obj, msgkey, func, times)
inline auto Obj_ListenObjectMessage(lua_State* L) -> int
{
#if !UE_BUILD_SHIPPING
	luaL_checktype(L, 1, LUA_TTABLE);  // self
#endif
	UObject* Obj = nullptr;
	if (lua_type(L, 2) != LUA_TSTRING)
	{
		if (!(lua_type(L, 2) == LUA_TTABLE || lua_type(L, 2) == LUA_TNIL))
			luaL_error(L, "first parameter should be object or nil");
		Obj = UnLua::GetUObject(L, 2, false);
		lua_remove(L, 2);
#if !UE_BUILD_SHIPPING
		luaL_checktype(L, 2, LUA_TSTRING);  // string
#endif
	}
	lua_rotate(L, 1, 1);

	auto nargs = lua_gettop(L);
#if !UE_BUILD_SHIPPING
	if (nargs < 3)
		luaL_error(L, "too few parameter");
#endif

	int times = nargs >= 4 ? luaL_checkinteger(L, 4) : -1;
	ensure(times != 0);
	lua_settop(L, 3);

	auto Self = UnLua::GetUObject(L, 2, false);
	auto FuncType = lua_type(L, 3);
	if (FuncType == LUA_TSTRING)
	{
		auto Str = lua_tostring(L, 3);
		lua_pop(L, 1);
		lua_getfield(L, 2, Str);
		if (!ensure(lua_isfunction(L, 3)))
			luaL_error(L, "should be member function %s", Str);
	}
	else if (FuncType == LUA_TFUNCTION)
	{
#if WITH_EDITOR
		bool bIsMemberFunc = false;
		int32 TopIdx = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, 2))
		{
			if (lua_rawequal(L, -1, 3))
			{
				lua_pop(L, lua_gettop(L) - TopIdx);
				bIsMemberFunc = true;
				break;
			}
			lua_pop(L, 1);
		}
		GMP_CHECK(lua_gettop(L) == TopIdx);
		if (!ensure(bIsMemberFunc))
			return luaL_error(L, "should be member function");
#endif
	}
	else
	{
		return luaL_error(L, "Invalid function type");
	}

	Obj = Obj ? Obj : Self->GetWorld();
	UnLua::PushUObject(L, Obj);
	lua_insert(L, 2);
	lua_pushinteger(L, times);

#if UE_BUILD_SHIPPING
	return Lua_ListenObjectMessage(L);
#else
	lua_pushcfunction(L, Lua_ListenObjectMessage);
	lua_insert(L, 1);
	if (lua_pcall(L, 5, 1, 0) != LUA_OK)
	{
		return luaL_error(L, "Failed to call ListenObjectMessage: %s", lua_tostring(L, -1));
	}
	return 1;
#endif
}

// Self:UnbindObjectMessage(msgkey, ListenedObj, Key or 0)
// Self:UnbindObjectMessage(msgkey, Key)
inline auto Obj_UnbindObjectMessage(lua_State* L) -> int
{
	auto nargs = lua_gettop(L);
#if !UE_BUILD_SHIPPING
	if (nargs < 3)
		luaL_error(L, "too few parameter");
#endif

#if !UE_BUILD_SHIPPING
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TSTRING);
#endif
	lua_rotate(L, 1, 1);

#if UE_BUILD_SHIPPING
	return Lua_UnbindObjectMessage(L);
#else
	lua_pushcfunction(L, Lua_UnbindObjectMessage);
	lua_insert(L, 1);
	if (lua_pcall(L, nargs, 0, 0) != LUA_OK)
	{
		return luaL_error(L, "Failed to call UnbindObjectMessage: %s", lua_tostring(L, -1));
	}
	return 0;
#endif
}

// Self:NotifyObjectMessage(msgkey, ...)
inline auto Obj_NotifyObjectMessage(lua_State* L) -> int
{
#if !UE_BUILD_SHIPPING
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TSTRING);
#endif
#if UE_BUILD_SHIPPING
	return Lua_NotifyObjectMessage(L);
#else
	int nargs = lua_gettop(L);
	// call Lua_NotifyObjectMessage(msgkey, self, ...)
	lua_pushcfunction(L, Lua_NotifyObjectMessage);
	lua_insert(L, 1);
	if (lua_pcall(L, nargs, 0, 0) != 0)
	{
		return luaL_error(L, "Failed to call NotifyObjectMessage: %s", lua_tostring(L, -1));
	}

	return 0;
#endif
}

// lua_function MixinObject(obj)
inline int GMP_MixinObject(lua_State* L)
{
	do
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_pushvalue(L, 1);

		lua_pushstring(L, "ListenObjectMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "ListenWorldMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "NotifyObjectMessage");
		lua_pushcfunction(L, Obj_NotifyObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "UnbindObjectMessage");
		lua_pushcfunction(L, Obj_UnbindObjectMessage);
		lua_settable(L, -3);
	} while (false);
	return 1;
}

inline int GMP_MixinMeta(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	const char* MIXIN_TABLE_KEY = "GMPMixinTable";
	lua_pushstring(L, MIXIN_TABLE_KEY);
	lua_gettable(L, LUA_REGISTRYINDEX);

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);

		lua_pushstring(L, "ListenObjectMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "ListenWorldMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "NotifyObjectMessage");
		lua_pushcfunction(L, Obj_NotifyObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, "UnbindObjectMessage");
		lua_pushcfunction(L, Obj_UnbindObjectMessage);
		lua_settable(L, -3);

		lua_pushstring(L, MIXIN_TABLE_KEY);
		lua_pushvalue(L, -2);
		lua_settable(L, LUA_REGISTRYINDEX);
	}

	if (!lua_getmetatable(L, 1))
	{
		lua_newtable(L);
	}

	int mt_idx = lua_gettop(L);
	lua_newtable(L);
	lua_pushstring(L, "__index");

	lua_pushvalue(L, -3);
	lua_pushvalue(L, mt_idx);

	auto CClosure = [](lua_State* L) -> int {
		lua_pushvalue(L, 2);
		lua_gettable(L, 1);
		if (!lua_isnil(L, -1))
		{
			return 1;
		}
		lua_pop(L, 1);

		lua_pushvalue(L, 2);
		lua_gettable(L, lua_upvalueindex(1));
		if (!lua_isnil(L, -1))
		{
			return 1;
		}
		lua_pop(L, 1);

		lua_pushvalue(L, lua_upvalueindex(2));
		lua_pushstring(L, "__index");
		lua_gettable(L, -2);

		if (lua_isnil(L, -1))
		{
			return 1;
		}
		else if (lua_istable(L, -1))
		{
			lua_pushvalue(L, 2);
			lua_gettable(L, -2);
			return 1;
		}
		else if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1);
			lua_pushvalue(L, 2);
			lua_call(L, 2, 1);
			return 1;
		}
		lua_pushnil(L);
		return 1;
	};
	lua_pushcclosure(L, CClosure, 2);

	lua_settable(L, -3);
	lua_setmetatable(L, 1);
	lua_pop(L, 2);
	lua_pushvalue(L, 1);
	return 1;
}

inline void GMP_UnregisterToLua(lua_State* L)
{
	if (ensure(L))
	{
		FGMPSigSource::RemoveSource(L);
	}
}


inline void GMP_AutoMixin()
{
	static FDelegateHandle Handle;
	if (Handle.IsValid())
		return;
	Handle = FUnLuaDelegates::OnObjectBinded.AddLambda([](UObjectBaseUtility* Obj) {
		if (!Obj || Obj->IsA<UClass>())
			return;
		UE_LOG(LogTemp, Log, TEXT("GMP_AutoMixin for %s"), *Obj->GetName());

		lua_State* L = UnLua::GetState();
		luaL_checktype(L, -1, LUA_TTABLE);

		lua_pushstring(L, "ListenObjectMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_rawset(L, -3);

		lua_pushstring(L, "ListenWorldMessage");
		lua_pushcfunction(L, Obj_ListenObjectMessage);
		lua_rawset(L, -3);

		lua_pushstring(L, "NotifyObjectMessage");
		lua_pushcfunction(L, Obj_NotifyObjectMessage);
		lua_rawset(L, -3);

		lua_pushstring(L, "UnbindObjectMessage");
		lua_pushcfunction(L, Obj_UnbindObjectMessage);
		lua_rawset(L, -3);
	});
}

inline void GMP_RegisterToLua(lua_State* L)
{
	GMP_AutoMixin();
	if (ensure(L))
	{
#if 1
#define LUA_REG_GMP_FUNC(NAME) lua_register(L, #NAME, Lua_##NAME);
#else
#define LUA_REG_GMP_FUNC(NAME)        \
	lua_pushstring(L, #NAME);         \
	lua_pushcfunction(L, Lua_##NAME); \
	lua_rawset(L, -3);
#endif
		LUA_REG_GMP_FUNC(NotifyObjectMessage);
		LUA_REG_GMP_FUNC(ListenObjectMessage);
		LUA_REG_GMP_FUNC(UnbindObjectMessage);
		LUA_REG_GMP_FUNC(UnListenObjectMessage);
	}
}

#if 1  // via ExportFunction
inline void GMP_ExportToLuaEx()
{
	struct FExportedGMPFunction : public UnLua::IExportedFunction
	{
		FExportedGMPFunction()
		{
			UE_LOG(LogTemp, Log, TEXT("ExportGMP"));
			UnLua::ExportFunction(this);
			FUnLuaDelegates::OnPreLuaContextCleanup.AddLambda([](bool) {
				if (lua_State* L = UnLua::GetState())
					GMP_UnregisterToLua(L);
			});
		}

		virtual void Register(lua_State* L) override { GMP_RegisterToLua(L); }
		virtual int32 Invoke(lua_State* L) override { return 0; }

#if WITH_EDITOR
		virtual FString GetName() const override { return TEXT("GMP"); }
		virtual void GenerateIntelliSense(FString& Buffer) const override {}
#endif
	};
	static FExportedGMPFunction ExportedGMPFunction;
}

#elif 0  // via Delegates in LuaEnv
inline void GMP_ExportToLuaEx()
{
	UE_LOG(LogTemp, Log, TEXT("ExportGMP"));
	if (lua_State* L = UnLua::GetState())
	{
		GMP_RegisterToLua(L);
	}
	else
	{
		UnLua::FLuaEnv::OnCreated.AddStatic([](UnLua::FLuaEnv& LuaEnv) {
			LuaEnv.AddBuiltInLoader(TEXT("NotifyObjectMessage"), Lua_NotifyObjectMessage);
			LuaEnv.AddBuiltInLoader(TEXT("ListenObjectMessage"), Lua_ListenObjectMessage);
			LuaEnv.AddBuiltInLoader(TEXT("UnbindObjectMessage"), Lua_UnbindObjectMessage);
			LuaEnv.AddBuiltInLoader(TEXT("UnListenObjectMessage"), Lua_UnListenObjectMessage);
		});
	}

	UnLua::FLuaEnv::OnDestroyed.AddStatic([](UnLua::FLuaEnv& LuaEnv) { GMP_UnregisterToLua(LuaEnv.GetMainState()); });
}

#else  // via FUnLuaDelegates Callbacks
inline void GMP_ExportToLuaEx()
{
	UE_LOG(LogTemp, Log, TEXT("ExportGMP"));
	if (lua_State* L = UnLua::GetState())
	{
		GMP_RegisterToLua(L);
	}
	else
	{
		FUnLuaDelegates::OnLuaStateCreated.AddLambda([](lua_State* InL) {
			if (ensure(InL))
				GMP_RegisterToLua(InL);
			else if (lua_State* L = UnLua::GetState())
				GMP_RegisterToLua(L);
		});
	}

	FUnLuaDelegates::OnPreLuaContextCleanup.AddLambda([](bool) {
		if (lua_State* L = UnLua::GetState())
			GMP_UnregisterToLua(L);
	});
}
#endif

struct GMP_ExportToLuaExObj
{
	GMP_ExportToLuaExObj() { GMP_ExportToLuaEx(); }
} ExportToLuaExObj;

#endif  // defined(UNLUA_API)

// how to use:
// 1. add "GMP" to PrivateDependencyModuleNames in Unlua.Build.cs
// 2. just included this header file into LuaCore.cpp in unlua module
// 3. add GMP_RegisterToLua and GMP_UnregisterToLua to a proper code place
//  a. // via ExportFunction
//  b. // via Delegates in LuaEnv
//  c. // via FUnLuaDelegates Callbacks
//  or add manually codes with the lifecycle of lua_State
// 4. EmmyLua Annotations as below and enjoy

#if 0
/*
---GMP.lua 
---EmmyLua Annotations

---@class GMP
local GMP = {}

--- lua_function ListenObjectMessage(watchedobj, msgkey, nil,      globalfunction [,times]) --- must be nil if using global function
--- lua_function ListenObjectMessage(watchedobj, msgkey, weakobj,  globalfuncstr  [,times]) --- or global function name string
--- lua_function ListenObjectMessage(watchedobj, msgkey, tableobj, tablefuncstr   [,times]) --- otherwise
--- lua_function ListenObjectMessage(watchedobj, msgkey, weakobj,  tablefunction  [,times]) --- treat as member function

---@override func(watchedobj:Object, msgkey:string, weakobj:Object, function|string):integer
---@override func(watchedobj:Object, msgkey:string, weakobj:Object, function|string, times:integer=-1):integer
---@generic T : Object
---@param watchedobj T
---@param msgkey string
---@param weakobj table|T
---@param luafunction function|string
---@param times integer
---@return integer
function GMP.ListenObjectMessage(watchedobj, msgkey, weakobj, luafunction, times)
	return ListenObjectMessage(watchedobj, msgkey, weakobj, luafunction, times or -1)
end

--- lua_function UnbindObjectMessage(msgkey, ListenedObj)
--- lua_function UnbindObjectMessage(msgkey, ListenedObj, Key)

---@generic T : Object
---@override func(string, T):void
---@override func(string, integer):void
---@override func(string, T, integer):void
---@param msgkey string
---@param listenedobj integer|T
---@param key integer
function GMP.UnbindObjectMessage(msgkey, listenedobj, key)
	return UnbindObjectMessage(msgkey, listenedobj, key)
end

--- lua_function NotifyObjectMessage(obj, msgkey, ...):void

---@generic T : Object|table
---@override func(T, string, ...:any):void
---@param obj T
---@param msgkey string
---@vararg any
function GMP.NotifyObjectMessage(obj, msgkey, ...)
	return NotifyObjectMessage(obj, msgkey, ...)
end


--- lua_function self:ListenWorldMessage(msgkey, memfunction [,times]):integer
---@override func(msgkey:string, function|string):integer
---@override func(msgkey:string, function|string, times:integer=-1):integer
---@param msgkey string
---@param memfunction function|string
---@param times integer
---@return integer
function GMP.ObjListenWorldMessage(self, msgkey, memfunction, times)
	local World = self:GetWorld()
	return GMP.ListenObjectMessage(World, msgkey, self, func, times or -1)
end

--- lua_function self:ListenObjectMessage(obj, msgkey, memfunction [,times]):integer
---@generic T : Object
---@override func(T, msgkey:string, function|string):integer
---@override func(T, msgkey:string, function|string, times:integer=-1):integer
---@param msgkey string
---@param memfunction function|string
---@param times integer
---@return integer
function GMP.ObjListenObjectMessage(self, watchedobj, msgkey, func, times)
	return GMP.ListenObjectMessage(watchedobj or self:GetWorld(), msgkey, self, func, times or -1)
end

--- lua_function self:NotifyObjectMessage(msgkey, ...):void
---@override func(string, ...:any):void
---@param msgkey string
---@vararg any
function GMP.ObjNotifyObjectMessage(self, msgkey, ...)
	return GMP.NotifyObjectMessage(self, msgkey, ...)
end


--- lua_function self:UnbindObjectMessage(msgkey [, key]):void
---@override func(string):void
---@override func(string, integer):void
---@param msgkey string
---@param key integer
function GMP.ObjUnbindObjectMessage(self, msgkey, key)
	return GMP.UnbindObjectMessage(msgkey, self,  key or 0)
end

function GMP.Mixin(tableobj)
	tableobj.ListenWorldMessage  = GMP.ObjListenWorldMessage
	tableobj.ListenObjectMessage = GMP.ObjListenObjectMessage
	tableobj.NotifyObjectMessage = GMP.ObjNotifyObjectMessage
	tableobj.UnbindObjectMessage = GMP.ObjUnbindObjectMessage
	return tableobj
end
*/
#endif
