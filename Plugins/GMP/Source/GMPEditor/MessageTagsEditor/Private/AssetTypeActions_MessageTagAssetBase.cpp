// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_MessageTagAssetBase.h"
#if UE_4_24_OR_LATER
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#else
#include "Framework/MultiBox/MultiBoxBuilder.h"
#endif
#include "UObject/UnrealType.h"
#include "Framework/Application/SlateApplication.h"

#include "MessageTagContainer.h"
#include "SMessageTagWidget.h"
#include "Interfaces/IMainFrameModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FAssetTypeActions_MessageTagAssetBase::FAssetTypeActions_MessageTagAssetBase(FName InTagPropertyName)
	: OwnedMessageTagPropertyName(InTagPropertyName)
{}

bool FAssetTypeActions_MessageTagAssetBase::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

#if UE_4_24_OR_LATER
void FAssetTypeActions_MessageTagAssetBase::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
#else
void FAssetTypeActions_MessageTagAssetBase::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
#endif
{
	TArray<UObject*> ContainerObjectOwners;
	TArray<FMessageTagContainer*> Containers;
	for (int32 ObjIdx = 0; ObjIdx < InObjects.Num(); ++ObjIdx)
	{
		UObject* CurObj = InObjects[ObjIdx];
		if (CurObj)
		{
			FStructProperty* StructProp = FindFProperty<FStructProperty>(CurObj->GetClass(), OwnedMessageTagPropertyName);
			if(StructProp != NULL)
			{
				ContainerObjectOwners.Add(CurObj);
				Containers.Add(StructProp->ContainerPtrToValuePtr<FMessageTagContainer>(CurObj));
			}
		}
	}

	ensure(Containers.Num() == ContainerObjectOwners.Num());
	if (Containers.Num() > 0 && (Containers.Num() == ContainerObjectOwners.Num()))
	{
#if UE_4_24_OR_LATER
		Section.AddMenuEntry(
			"MessageTags_Edit",
			LOCTEXT("MessageTags_Edit", "Edit Message Tags..."),
			LOCTEXT("MessageTags_EditToolTip", "Opens the Message Tag Editor."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FAssetTypeActions_MessageTagAssetBase::OpenMessageTagEditor, ContainerObjectOwners, Containers), FCanExecuteAction()));
#else
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MessageTags_Edit", "Edit Message Tags..."),
			LOCTEXT("MessageTags_EditToolTip", "Opens the Message Tag Editor."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FAssetTypeActions_MessageTagAssetBase::OpenMessageTagEditor, ContainerObjectOwners, Containers), FCanExecuteAction()));
#endif
	}
}

void FAssetTypeActions_MessageTagAssetBase::OpenMessageTagEditor(TArray<UObject*> Objects, TArray<FMessageTagContainer*> Containers)
{
	TArray<SMessageTagWidget::FEditableMessageTagContainerDatum> EditableContainers;
	for (int32 ObjIdx = 0; ObjIdx < Objects.Num() && ObjIdx < Containers.Num(); ++ObjIdx)
	{
		EditableContainers.Add(SMessageTagWidget::FEditableMessageTagContainerDatum(Objects[ObjIdx], Containers[ObjIdx]));
	}

	FText Title;
	FText AssetName;

	const int32 NumAssets = EditableContainers.Num();
	if (NumAssets > 1)
	{
		AssetName = FText::Format( LOCTEXT("AssetTypeActions_MessageTagAssetBaseMultipleAssets", "{0} Assets"), FText::AsNumber( NumAssets ) );
		Title = FText::Format( LOCTEXT("AssetTypeActions_MessageTagAssetBaseEditorTitle", "Tag Editor: Owned Message Tags: {0}"), AssetName );
	}
	else if (NumAssets > 0 && EditableContainers[0].TagContainerOwner.IsValid())
	{
		AssetName = FText::FromString( EditableContainers[0].TagContainerOwner->GetName() );
		Title = FText::Format( LOCTEXT("AssetTypeActions_MessageTagAssetBaseEditorTitle", "Tag Editor: Owned Message Tags: {0}"), AssetName );
	}

	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(600, 400))
		[
			SNew(SMessageTagWidget, EditableContainers)
		];

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}
}

uint32 FAssetTypeActions_MessageTagAssetBase::GetCategories()
{ 
	return EAssetTypeCategories::Misc; 
}

#undef LOCTEXT_NAMESPACE
