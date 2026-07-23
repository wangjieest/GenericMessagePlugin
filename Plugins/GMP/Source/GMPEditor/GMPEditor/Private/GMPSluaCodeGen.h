//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"

// Editor codegen for the sluaunreal backend, from the live MessageTag signature table:
//  - SA: GMPMessages.lua  -- EmmyLua annotations for static checking + IntelliSense (lua IDE / CI lua-lint).
//  - SB: GMPSluaBinds.gen.cpp -- per-tag strongly-typed lua_CFunctions (compile-time push/check, no runtime lookup),
//        wrapped in #if GMP_SLUA_STATIC_BIND so the whole file compiles out when the Build.cs switch is off.
class FGMPSluaCodeGen
{
public:
	// Writes both GMPMessages.lua (SA) and GMPSluaBinds.gen.cpp (SB). Returns true if anything changed on disk.
	static bool Generate();

	static FString BuildLuaAnnotations();  // SA
	static FString BuildCppBinds();        // SB

	// Subscribe to tag signature changes -> regenerate (debounced-free; write is skipped when content is unchanged).
	static void Register();
	static void Unregister();
};
#endif
