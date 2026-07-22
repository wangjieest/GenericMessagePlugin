//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if WITH_EDITOR
#include "CoreMinimal.h"

// Editor codegen for the UnrealCSharp (C#) backend, from the live MessageTag signature table.
// Generates GMPTag.cs: per-tag strongly-typed MsgTag<...> markers carrying the parameter types, so the fixed generic API
// GMP.NotifyObjectMessage<A1,A2>(sender, GMPTag.Player_Hurt, a1, a2) / ListenObjectMessage(... (dmg,causer)=>{}) gets
// compile-time type checking + IntelliSense (native C# compiler, no plugin/tsc). The generic Notify/Listen overloads and
// the MsgTag<> definitions themselves are fixed (hand-written in GMPBridge.cs, arity-covered); only the per-tag marker
// fields vary with the tag table, so this codegen only emits GMPTag.cs.
//
// Minimal-intrusion: output lands in the UnrealCSharp Script glob (Script/Interop/GMP/) so Interop.csproj picks it up
// automatically -- no UnrealCSharp .csproj/Build.cs change. Symmetric with slua/UnLua/Puerts editor codegen (reuses
// GMPScriptCodeGen base). Not the runtime path: CSharpSupport.h alone works with string-tag API; this adds strong typing.
class FGMPCSharpCodeGen
{
public:
	// Writes GMPTag.cs. Returns true if content changed on disk.
	static bool Generate();

	static FString BuildTagMarkers();  // GMPTag.cs body

	static void Register();
	static void Unregister();
};
#endif
