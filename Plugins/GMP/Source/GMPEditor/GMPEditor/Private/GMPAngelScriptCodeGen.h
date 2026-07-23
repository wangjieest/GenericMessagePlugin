//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"

// Generates strongly-typed AngelScript declarations (GMPMessages.as) from the live MessageTag signature table,
// so .as scripts get compile-time type checking + IntelliSense for GMP listen/notify. Regenerated on signature changes.
class FGMPAngelScriptCodeGen
{
public:
	// Writes GMPMessages.as under the resolved script root. Returns true if the file was written (content changed).
	static bool Generate(FString* OutText = nullptr);

	// Subscribe to MessageTag signature/add/remove changes and regenerate (debounced). Idempotent.
	static void Register();
	static void Unregister();

	// Builds the full .as text in-memory (no file IO); exposed for tests/preview.
	static FString BuildScriptText();
};
#endif