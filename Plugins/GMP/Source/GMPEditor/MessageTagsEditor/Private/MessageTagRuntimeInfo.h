// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MessageTagContainer.h"

#if WITH_EDITOR

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EdGraph/EdGraphNode.h"
#include "Framework/Application/SlateApplication.h"
#include "GMP/GMPHub.h"
#include "GMP/GMPMessageKey.h"
#include "GMP/GMPUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Shared helpers for the tag-pin runtime info UI (badge + hover panel). Header-only so both SMessageTagGraphPin and SMessageTagRuntimeBadge can reuse them. Editor-only.
namespace MessageTagRuntimeInfo
{
	// Owning-node role: +1 Notify, -1 Listen, 0 unknown/non-GMP. Uses class FName to avoid a cross-module include of GMPEditor node types.
	inline int32 GetNodeRole(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return 0;
		}
		const FName ClassName = Node->GetClass()->GetFName();
		if (ClassName == TEXT("K2Node_NotifyMessage"))
		{
			return +1;
		}
		if (ClassName == TEXT("K2Node_ListenMessage"))
		{
			return -1;
		}
		return 0;
	}

	// World source that GetListeners/GetCallInfos filter by. Use the PIE GameInstance (not the UWorld itself): FSigSource::GetSigSourceWorld() bails when the object's GetWorld()==itself, which is true for a UWorld, defeating the world-level fallback match. A GameInstance resolves to the PIE world correctly. Null outside PIE = list everything.
	inline GMP::FSigSource WorldFilterSigSource()
	{
		if (GEditor && GEditor->PlayWorld)
		{
			if (UGameInstance* GI = GEditor->PlayWorld->GetGameInstance())
			{
				return GMP::FSigSource(GI);
			}
			return GMP::FSigSource(GEditor->PlayWorld);
		}
		return GMP::FSigSource();
	}

	inline void JumpToListener(TWeakObjectPtr<UObject> WeakObj)
	{
		UObject* Target = WeakObj.Get();
		if (!IsValid(Target))
		{
			return;
		}
		if (UBlueprint* BP = Cast<UBlueprint>(Target->GetClass()->ClassGeneratedBy))
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(BP);
		}
		else if (GEditor)
		{
			TArray<UObject*> Objs = {Target};
			GEditor->SyncBrowserToObjects(Objs);
		}
	}

	inline void AddInfoRow(TSharedRef<SVerticalBox> List, const FString& Text, const FLinearColor& Color, const FMargin& Pad)
	{
		List->AddSlot().AutoHeight().Padding(Pad)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(FSlateColor(Color))
			.Text(FText::FromString(Text))
		];
	}

	inline bool CanJumpTo(const UObject* Obj)
	{
		return IsValid(Obj) && !Obj->HasAnyFlags(RF_Transient) && Cast<UBlueprint>(Obj->GetClass()->ClassGeneratedBy) != nullptr;
	}

	// A clickable jump-to-blueprint row (used for listeners); closes the tooltip on click.
	inline void AddListenerRow(TSharedRef<SVerticalBox> List, const FString& Display, TWeakObjectPtr<UObject> WeakObj, bool bClickable, float LeftPad = 12.f)
	{
		if (!bClickable)
		{
			AddInfoRow(List, Display, FLinearColor(1, 1, 1, 0.5f), FMargin(LeftPad, 1, 0, 1));
			return;
		}
		List->AddSlot().AutoHeight().Padding(FMargin(LeftPad, 1, 0, 1))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([WeakObj] {
				FSlateApplication::Get().CloseToolTip();
				JumpToListener(WeakObj);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f)))
				.Text(FText::FromString(Display))
			]
		];
	}

	// Splits "file:line" out of a runtime trigger Loc; unwraps Lua's [string "<path>"] chunk form.
	inline bool ParseLoc(const FString& Loc, FString& OutFile, int32& OutLine)
	{
		OutFile = Loc;
		OutLine = 0;
		int32 ColonIdx = INDEX_NONE;
		bool bJumpable = false;
		if (Loc.FindLastChar(TEXT(':'), ColonIdx) && ColonIdx > 0 && ColonIdx < Loc.Len() - 1 && Loc.Mid(ColonIdx + 1).IsNumeric())
		{
			OutFile = Loc.Left(ColonIdx);
			OutLine = FCString::Atoi(*Loc.Mid(ColonIdx + 1));
			bJumpable = true;
		}
		if (OutFile.StartsWith(TEXT("[string \"")) && OutFile.EndsWith(TEXT("\"]")))
		{
			OutFile = OutFile.Mid(9, OutFile.Len() - 11);
		}
		return bJumpable;
	}

	// Collects runtime triggers grouped by sig source (listener host). First-level world filter = current PIE instances; second-level = sig bucket. bSend picks notify vs listen.
	inline void GatherRuntimeTriggerGroups(bool bSend, FName MsgKey, TArray<GMP::FGMPRuntimeTriggerGroup>& Out)
	{
		Out.Reset();
		if (GEditor && GEditor->PlayWorld && GEngine)
		{
			for (const FWorldContext& WC : GEngine->GetWorldContexts())
			{
				if (WC.WorldType != EWorldType::PIE)
				{
					continue;
				}
				GMP::GetRuntimeTriggersGroupedBySig(MsgKey, bSend, WC.PIEInstance, Out, 9);
			}
		}
		else
		{
			GMP::ReadRuntimeTraceFromDisk(MsgKey, bSend, Out);
		}
	}

	// One call row: "obj @time" plus the sender's notify source as a clickable file:line link.
	inline void AddTriggerRow(TSharedRef<SVerticalBox> List, const GMP::FGMPRuntimeTriggerEntry& E)
	{
		FString FilePath;
		int32 Line = 0;
		const bool bJumpable = ParseLoc(E.Loc, FilePath, Line);
		const FString Head = FString::Printf(TEXT("%s  @%.2fs"), *E.ObjName, E.WorldTime);

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.9f, 0.4f)))
				.Text(FText::FromString(Head))
			];

		if (bJumpable)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6, 0, 0, 0))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ContentPadding(FMargin(0))
				.ToolTipText(FText::FromString(E.Loc))
				.OnClicked_Lambda([FilePath, Line] {
					FSlateApplication::Get().CloseToolTip();
					FSourceCodeNavigation::OpenSourceFile(FPaths::ConvertRelativePathToFull(FilePath), Line, 0);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f)))
					.Text(FText::FromString(FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(FilePath), Line)))
				]
			];
		}

		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))[Row];
	}

	// Notify nodes: current listeners of the tag.
	inline void BuildListenersPanel(TSharedRef<SVerticalBox> List, FName MsgKey)
	{
		auto* Hub = GMP::FMessageUtils::GetMessageHub();
		if (!Hub)
		{
			AddInfoRow(List, TEXT("(hub not initialised)"), FLinearColor(1, 1, 1, 0.35f), FMargin(0, 4));
			return;
		}
		TArray<FWeakObjectPtr> Listeners;
		const bool bHasMore = Hub->GetListeners(WorldFilterSigSource(), MsgKey, Listeners, 20);
		if (Listeners.Num() == 0)
		{
			const bool bInPIE = GEditor && GEditor->PlayWorld;
			AddInfoRow(List, bInPIE ? TEXT("(no listeners in PIE world)") : TEXT("(no active listeners)"), FLinearColor(1, 1, 1, 0.35f), FMargin(0, 4));
			return;
		}
		AddInfoRow(List, FString::Printf(TEXT("LISTENERS (%d)"), Listeners.Num()), FLinearColor(1, 1, 1, 0.4f), FMargin(0, 2));
		for (const FWeakObjectPtr& Weak : Listeners)
		{
			UObject* Obj = Weak.Get();
			const FString Name = Obj ? Obj->GetName() : TEXT("<stale>");
			AddListenerRow(List, Name, TWeakObjectPtr<UObject>(Obj), CanJumpTo(Obj));
		}
		if (bHasMore)
		{
			AddInfoRow(List, TEXT("..."), FLinearColor(1, 1, 1, 0.35f), FMargin(12, 1, 0, 1));
		}
	}

	// Listen nodes: recent trigger history grouped by sig source (listener host). Each row jumps to the sender's notify location.
	inline void BuildCallInfosPanel(TSharedRef<SVerticalBox> List, FName MsgKey)
	{
		TArray<GMP::FGMPRuntimeTriggerGroup> Groups;
		GatherRuntimeTriggerGroups(/*bSend*/ true, MsgKey, Groups);
		if (Groups.Num() == 0)
		{
			AddInfoRow(List, TEXT("(no triggers yet)"), FLinearColor(1, 1, 1, 0.35f), FMargin(0, 4));
			return;
		}
		AddInfoRow(List, TEXT("CALL HISTORY"), FLinearColor(1, 1, 1, 0.4f), FMargin(0, 2));
		for (const GMP::FGMPRuntimeTriggerGroup& Group : Groups)
		{
			// Sig group header (listener host, usually GameInstance/World).
			AddInfoRow(List, Group.SigName, FLinearColor(0.7f, 0.85f, 1.0f), FMargin(0, 3, 0, 1));
			for (int32 i = Group.Entries.Num() - 1; i >= 0; --i)
			{
				AddTriggerRow(List, Group.Entries[i]);
			}
		}
	}

	// Parses the "@%0.2fs" world time carried in a CallInfo string; -1 if none.
	inline double ParseCallInfoTime(const FString& Info)
	{
		int32 At = INDEX_NONE;
		if (!Info.FindLastChar(TEXT('@'), At))
		{
			return -1.0;
		}
		int32 SEnd = INDEX_NONE;
		FString Rest = Info.Mid(At + 1);
		if (Rest.FindChar(TEXT('s'), SEnd))
		{
			Rest = Rest.Left(SEnd);
		}
		return Rest.IsNumeric() || Rest.Contains(TEXT(".")) ? FCString::Atod(*Rest.TrimStartAndEnd()) : -1.0;
	}

	// Live badge stats: OutCount = cumulative trigger count (matches bubble Invoked, unbounded); OutLatestTime = newest trigger time from the runtime ring (0 if none). Both 0 outside PIE / no hub.
	inline void GetLatestActivity(int32 Role, FName MsgKey, int32& OutCount, double& OutLatestTime)
	{
		OutCount = 0;
		OutLatestTime = 0.0;
		auto* Hub = GMP::FMessageUtils::GetMessageHub();
		if (!Hub || Role == 0)
		{
			return;
		}
		OutCount = Hub->GetTotalInvokeCount(MsgKey);
		TArray<GMP::FGMPRuntimeTriggerGroup> Groups;
		GatherRuntimeTriggerGroups(/*bSend*/ true, MsgKey, Groups);
		for (const GMP::FGMPRuntimeTriggerGroup& Group : Groups)
		{
			for (const GMP::FGMPRuntimeTriggerEntry& E : Group.Entries)
			{
				if (E.WorldTime > OutLatestTime)
				{
					OutLatestTime = E.WorldTime;
				}
			}
		}
	}

	// Clears and (re)fills List with the runtime info panel: header + status + role-dispatched listeners/callinfos.
	inline void FillRuntimeInfoList(TSharedRef<SVerticalBox> List, int32 Role, const FMessageTag& InTag)
	{
		List->ClearChildren();

		List->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 2))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
			.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.7f)))
			.Text(FText::FromName(InTag.GetTagName()))
		];

		// Listening/Stopped status line (replaces what the old node bubble showed).
		if (Role != 0)
		{
			int32 Count = 0;
			double Latest = 0.0;
			GetLatestActivity(Role, InTag.GetTagName(), Count, Latest);
			const bool bActive = Count > 0;
			AddInfoRow(List, bActive ? TEXT("Listening") : TEXT("Stopped"), bActive ? FLinearColor(0.35f, 0.7f, 1.0f) : FLinearColor(1, 1, 1, 0.4f), FMargin(0, 0, 0, 2));
		}

		List->AddSlot().AutoHeight()[SNew(SSeparator).Thickness(1.0f)];

		if (Role == +1)
		{
			BuildListenersPanel(List, InTag.GetTagName());
		}
		else if (Role == -1)
		{
			BuildCallInfosPanel(List, InTag.GetTagName());
		}
		else
		{
			AddInfoRow(List, TEXT("(runtime info only for GMP nodes)"), FLinearColor(1, 1, 1, 0.35f), FMargin(0, 4));
		}
	}

	// Full interactive hover panel dispatched by node role.
	inline TSharedRef<SToolTip> MakeRuntimeInfoToolTip(int32 Role, const FMessageTag& InTag)
	{
		TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		FillRuntimeInfoList(List, Role, InTag);

		return SNew(SToolTip)
			.IsInteractive(true)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.TextMargin(FMargin(0.0f))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
				.Padding(FMargin(8, 6))
				[
					SNew(SBox)
					.MaxDesiredWidth(360.0f)
					[
						List
					]
				]
			];
	}
}

#endif // WITH_EDITOR
