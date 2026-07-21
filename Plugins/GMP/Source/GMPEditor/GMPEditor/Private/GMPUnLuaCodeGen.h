//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"

// Editor codegen for the UnLua backend (shares the codegen base with slua/AngelScript):
//  - SA: GMPMessages.lua -- EmmyLua annotations (same file/format as slua; any lua IDE consumes it).
//  - SB: GMPUnLuaBinds.gen.cpp -- per-tag strongly-typed lua_CFunctions using UnLua::Get<T> (compile-time, no runtime
//        CreateTypeInterface lookup), wrapped in #if GMP_UNLUA_STATIC_BIND.
class FGMPUnLuaCodeGen
{
public:
	static bool Generate();
	static FString BuildLuaAnnotations();  // shared base
	static FString BuildCppBinds();        // SB (UnLua)
	static void Register();
	static void Unregister();
};
#endif
