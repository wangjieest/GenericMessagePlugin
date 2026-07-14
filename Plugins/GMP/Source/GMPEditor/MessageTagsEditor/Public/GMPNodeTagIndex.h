// Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UEdGraphNode;

/** Editor-only index of GMP message-tag Blueprint nodes, split by Listen/Notify, for the tag preview panel to jump to. */
class MESSAGETAGSEDITOR_API FGMPNodeTagIndex
{
public:
	static FGMPNodeTagIndex& Get();

	void RegisterNode(FName TagName, UEdGraphNode* Node, bool bIsListen);
	void UnregisterNode(FName TagName, UEdGraphNode* Node, bool bIsListen);

	/** Returns still-valid nodes for the tag/direction. */
	void GetNodes(FName TagName, bool bIsListen, TArray<UEdGraphNode*>& OutNodes) const;

	/** Focuses the Blueprint editor on the node. */
	static void JumpToNode(UEdGraphNode* Node);

	struct FJumpResult
	{
		FString AssetPath;
		FString NodeTitle;
		bool bListen = false;
	};
	static TArray<FJumpResult> OpenAndJumpByTag(FName TagName, bool bIsListen, int32 Index);

	/** Bumped on every register/unregister so viewers can detect changes (e.g. after lazy load). */
	uint32 GetChangeCount() const { return ChangeCount; }

private:
	uint32 ChangeCount = 0;
	struct FTagNodes
	{
		TArray<TWeakObjectPtr<UEdGraphNode>> ListenNodes;
		TArray<TWeakObjectPtr<UEdGraphNode>> NotifyNodes;
	};
	TMap<FName, FTagNodes> Index;
};
