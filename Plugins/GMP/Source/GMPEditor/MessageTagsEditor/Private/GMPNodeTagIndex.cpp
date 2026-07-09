// Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPNodeTagIndex.h"

#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetEditorUtilities.h"

FGMPNodeTagIndex& FGMPNodeTagIndex::Get()
{
	static FGMPNodeTagIndex Instance;
	return Instance;
}

void FGMPNodeTagIndex::RegisterNode(FName TagName, UEdGraphNode* Node, bool bIsListen)
{
	if (TagName.IsNone() || !IsValid(Node))
	{
		return;
	}
	FTagNodes& Nodes = Index.FindOrAdd(TagName);
	auto& Arr = bIsListen ? Nodes.ListenNodes : Nodes.NotifyNodes;
	Arr.AddUnique(TWeakObjectPtr<UEdGraphNode>(Node));
	++ChangeCount;
}

void FGMPNodeTagIndex::UnregisterNode(FName TagName, UEdGraphNode* Node, bool bIsListen)
{
	if (FTagNodes* Nodes = Index.Find(TagName))
	{
		auto& Arr = bIsListen ? Nodes->ListenNodes : Nodes->NotifyNodes;
		Arr.RemoveAllSwap([Node](const TWeakObjectPtr<UEdGraphNode>& W) { return !W.IsValid() || W.Get() == Node; });
		++ChangeCount;
	}
}

void FGMPNodeTagIndex::GetNodes(FName TagName, bool bIsListen, TArray<UEdGraphNode*>& OutNodes) const
{
	if (const FTagNodes* Nodes = Index.Find(TagName))
	{
		const auto& Arr = bIsListen ? Nodes->ListenNodes : Nodes->NotifyNodes;
		for (const TWeakObjectPtr<UEdGraphNode>& W : Arr)
		{
			if (UEdGraphNode* N = W.Get())
			{
				OutNodes.Add(N);
			}
		}
	}
}

void FGMPNodeTagIndex::JumpToNode(UEdGraphNode* Node)
{
	if (IsValid(Node))
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
	}
}
