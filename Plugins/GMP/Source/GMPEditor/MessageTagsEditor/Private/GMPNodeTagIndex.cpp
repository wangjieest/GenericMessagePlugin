// Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPNodeTagIndex.h"

#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/Blueprint.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "UnrealCompatibility.h"

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

namespace GMPNodeTagIndexPrivate
{
	// AssetRegistrySearchable stores FMessageTag::MsgTag under this key; value contains the tag name (ExportText form).
	static const FName MsgTagKey(TEXT("MsgTag"));

	static void FindBlueprintsOwningTag(FName TagName, TArray<FSoftObjectPath>& OutPaths)
	{
		if (TagName.IsNone())
		{
			return;
		}
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		FARFilter Filter;
		Filter.bRecursiveClasses = true;
#if UE_5_01_OR_LATER
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
#else
		Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
#endif

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		const FString TagStr = TagName.ToString();
		for (const FAssetData& Asset : Assets)
		{
			const FString Value = Asset.GetTagValueRef<FString>(MsgTagKey);
			if (!Value.IsEmpty() && Value.Contains(TagStr))
			{
				OutPaths.AddUnique(Asset.ToSoftObjectPath());
			}
		}
	}
}

TArray<FGMPNodeTagIndex::FJumpResult> FGMPNodeTagIndex::OpenAndJumpByTag(FName TagName, bool bIsListen, int32 Index)
{
	using namespace GMPNodeTagIndexPrivate;
	TArray<FJumpResult> Results;
	if (TagName.IsNone() || !GEditor)
	{
		return Results;
	}

	FGMPNodeTagIndex& Self = Get();

	TArray<UEdGraphNode*> Nodes;
	Self.GetNodes(TagName, bIsListen, Nodes);

	// Nodes only appear in the in-memory index after their owning Blueprint is loaded; lazily load owners on miss.
	if (Nodes.Num() == 0)
	{
		TArray<FSoftObjectPath> Owners;
		FindBlueprintsOwningTag(TagName, Owners);
		UAssetEditorSubsystem* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		for (const FSoftObjectPath& Path : Owners)
		{
			if (UObject* Asset = Path.TryLoad())
			{
				if (AssetEditor)
				{
					AssetEditor->OpenEditorForAsset(Asset);
				}
			}
		}
		Nodes.Reset();
		Self.GetNodes(TagName, bIsListen, Nodes);
	}

	if (Nodes.Num() == 0)
	{
		return Results;
	}

	auto Focus = [&Results, bIsListen](UEdGraphNode* Node)
	{
		if (!IsValid(Node))
		{
			return;
		}
		JumpToNode(Node);
		FJumpResult R;
		R.bListen = bIsListen;
		R.NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		if (const UObject* Outer = Node->GetOutermostObject())
		{
			R.AssetPath = Outer->GetPathName();
		}
		Results.Add(MoveTemp(R));
	};

	if (Index >= 0 && Nodes.IsValidIndex(Index))
	{
		Focus(Nodes[Index]);
	}
	else
	{
		Focus(Nodes[0]);
	}
	return Results;
}
