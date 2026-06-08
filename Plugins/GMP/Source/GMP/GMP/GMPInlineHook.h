//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

// Minimal cross-platform inline hook: x64 (Windows/macOS) + arm64 (macOS Apple Silicon)
// Editor-only: used to hook BlueprintGraph module functions.
namespace GMPHook
{
	struct FHookEntry
	{
		void* Target = nullptr;
		void* Trampoline = nullptr;
		uint8 SavedBytes[32] = {};
		uint32 SavedSize = 0;
	};

	GMP_API void* Install(void* Target, void* Hook);
	GMP_API void Uninstall(void* Target);

	template<typename F>
	FORCEINLINE F InstallHook(F Target, F Hook)
	{
		return reinterpret_cast<F>(Install(reinterpret_cast<void*>(Target), reinterpret_cast<void*>(Hook)));
	}

	template<typename F>
	FORCEINLINE void UninstallHook(F Target)
	{
		Uninstall(reinterpret_cast<void*>(Target));
	}
}

#endif // WITH_EDITOR
