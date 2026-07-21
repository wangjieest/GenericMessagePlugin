//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"

// Editor codegen for the Puerts (TS/JS) backend, from the live MessageTag signature table:
// addon direct-call model (user-decided): Notify_<id> is a plain v8 FunctionTemplate C++ callback, NOT a UFunction, so it
// runs as a direct C++ call (faster than UFunction FastCall, no FFrame). The TS transformer rewrites
// NotifyObjectMessage("Tag", ...) -> Notify_<id>(...) at emit time.
class FGMPPuertsCodeGen
{
public:
	// Writes both Typing/gmp/index.d.ts (PA) and GMPPuertsBinds.gen.cpp (PB). Returns true if anything changed on disk.
	static bool Generate();

	static FString BuildDts();       // PA
	static FString BuildCppBinds();  // PB

	static void Register();
	static void Unregister();
};
#endif
