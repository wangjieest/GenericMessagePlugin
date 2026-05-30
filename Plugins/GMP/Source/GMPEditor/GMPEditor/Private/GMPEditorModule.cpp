//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "AssetManagerEditorModule.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintCore.h"
#include "FindInBlueprintManager.h"
#include "FindInBlueprints.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_MessageBase.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Toolkits/ToolkitManager.h"
#include "UObject/SoftObjectPath.h"
#include "UnrealCompatibility.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SSearchBox.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if !UE_4_20_OR_LATER
#include "ReferenceViewer.h"
#endif

#if WITH_EDITOR
#include "../Private/SMessageTagGraphPin.h"
#include "GMPCore.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "KismetCompiler.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "EdGraph/EdGraphPin.h"
#include "GMP/GMPBPLib.h"
#include "GMP/GMPInlineHook.h"
#endif

#ifndef GS_PRIVATEACCESS_MEMBER
#define GS_PRIVATEACCESS_MEMBER(Class, Member, ...)                                      \
	namespace PrivateAccess                                                              \
	{                                                                                    \
	using Z_##MemberPtr##Class##Member##M##Type = __VA_ARGS__;                           \
	using Z_##MemberPtr##Class##Member = Z_##MemberPtr##Class##Member##M##Type Class::*; \
	template<Z_##MemberPtr##Class##Member MemPtr>                                        \
	struct Z_Get##Class##Member                                                          \
	{                                                                                    \
		friend Z_##MemberPtr##Class##Member Access##Class##Member()                      \
		{                                                                                \
			return MemPtr;                                                               \
		}                                                                                \
	};                                                                                   \
	Z_##MemberPtr##Class##Member Access##Class##Member();                                \
	template struct Z_Get##Class##Member<&Class::Member>;                                \
	auto& Member(const Class& obj)                                                       \
	{                                                                                    \
		return const_cast<Class&>(obj).*Access##Class##Member();                         \
	}                                                                                    \
	}
#endif

// --- GMPRefEvent hooks ---
namespace GMPRefEvent
{
	// Access protected member function pointer via template instantiation trick
	using FCreateStubMemberPtr = void(FKismetCompilerContext::*)(UK2Node_Event*, UObject*);
	template<FCreateStubMemberPtr Ptr>
	struct FStealCreateStub
	{
		friend FCreateStubMemberPtr GetCreateStubForEvent() { return Ptr; }
	};
	FCreateStubMemberPtr GetCreateStubForEvent();
	template struct FStealCreateStub<&FKismetCompilerContext::CreateFunctionStubForEvent>;

	// --- Hook 1: HasFunctionAnyOutputParameter ---
	static TFunction<bool(const UFunction*)> GOutputParamOverride;
	using FHasOutputParamFunc = bool(*)(const UFunction*);
	static FHasOutputParamFunc OriginalHasOutputParam = nullptr;

	bool Hook_HasFunctionAnyOutputParameter(const UFunction* InFunction)
	{
		if (GOutputParamOverride && GOutputParamOverride(InFunction))
			return false;
		return OriginalHasOutputParam(InFunction);
	}

	// --- Hook 2: CreateFunctionStubForEvent ---
	using FCreateStubFunc = void(FKismetCompilerContext::*)(UK2Node_Event*, UObject*);
	using FCreateStubFuncRaw = void(*)(FKismetCompilerContext*, UK2Node_Event*, UObject*);
	static FCreateStubFuncRaw OriginalCreateStub = nullptr;

	static bool IsGMPRefEvent(UK2Node_Event* Event)
	{
		if (!Event) return false;
		UFunction* Func = Event->FindEventSignatureFunction();
		return Func && Func->HasMetaData(TEXT("GMPRefEvent"));
	}

	void Hook_CreateFunctionStubForEvent(FKismetCompilerContext* This, UK2Node_Event* Event, UObject* Owner)
	{
		OriginalCreateStub(This, Event, Owner);

		if (!IsGMPRefEvent(Event))
			return;

		// Find the stub graph just created — it's the last one added to Blueprint->EventGraphs
		UBlueprint* BP = This->Blueprint;
		if (!BP || BP->EventGraphs.Num() == 0)
			return;

		UEdGraph* StubGraph = BP->EventGraphs.Last();

		// Find CallFunction(ExecuteUbergraph) node
		UK2Node_CallFunction* CallUbergraph = nullptr;
		for (UEdGraphNode* Node : StubGraph->Nodes)
		{
			auto* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (CallNode && CallNode->FunctionReference.GetMemberName().ToString().Contains(TEXT("ExecuteUbergraph")))
			{
				CallUbergraph = CallNode;
				break;
			}
		}
		if (!CallUbergraph)
			return;

		// Add CallFunction(WriteBackFromPersistentFrame) after ExecuteUbergraph
		UK2Node_CallFunction* WriteBackNode = This->SpawnIntermediateNode<UK2Node_CallFunction>(Event, StubGraph);
		WriteBackNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UGMPBPLib, WriteBackFromPersistentFrame),
			UGMPBPLib::StaticClass());
		WriteBackNode->AllocateDefaultPins();

		// Rewire: CallUbergraph.Then → WriteBackNode.Execute
		UEdGraphPin* UbergraphThen = CallUbergraph->GetThenPin();
		if (UbergraphThen)
		{
			UbergraphThen->BreakAllPinLinks();
			UEdGraphPin* WriteBackExec = WriteBackNode->GetExecPin();
			if (WriteBackExec)
			{
				UbergraphThen->MakeLinkTo(WriteBackExec);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("GMPRefEvent: injected WriteBack into stub for %s"), *Event->GetName());
	}

	// --- Hook 3: FindDebuggingData — show split-struct SubPin values in debugger ---
	using FFindDebuggingDataFunc = FKismetDebugUtilities::EWatchTextResult (*)(
		UBlueprint*, UObject*, const UEdGraphPin*, const FProperty*&, const void*&, const void*&, UObject*&, TArray<UObject*>&, bool*);
	static FFindDebuggingDataFunc OriginalFindDebuggingData = nullptr;

	// Access protected static FindDebuggingData address via template friend trick
	template<FFindDebuggingDataFunc Ptr>
	struct FStealFindDebuggingData
	{
		friend FFindDebuggingDataFunc GetFindDebuggingDataAddr() { return Ptr; }
	};
	FFindDebuggingDataFunc GetFindDebuggingDataAddr();
	template struct FStealFindDebuggingData<&FKismetDebugUtilities::FindDebuggingData>;

	// Derive struct member name from a split SubPin: PinName == "{ParentName}_{MemberName}"
	static FName GetSubPinMemberName(const UEdGraphPin* SubPin)
	{
		FString SubName = SubPin->PinName.ToString();
		FString ParentName = SubPin->ParentPin->PinName.ToString();
		if (SubName.RemoveFromStart(ParentName + TEXT("_")))
			return FName(*SubName);
		return SubPin->PinType.PinSubCategoryMemberReference.MemberName;
	}

	FKismetDebugUtilities::EWatchTextResult Hook_FindDebuggingData(
		UBlueprint* Blueprint, UObject* ActiveObject, const UEdGraphPin* WatchPin,
		const FProperty*& OutProperty, const void*& OutData, const void*& OutDelta,
		UObject*& OutParent, TArray<UObject*>& SeenObjects, bool* bOutIsDirectPtr)
	{
		// Only intercept split SubPins that the engine can't resolve on their own
		if (WatchPin && WatchPin->ParentPin != nullptr
			&& FKismetDebugUtilities::FindClassPropertyForPin(Blueprint, WatchPin) == nullptr)
		{
			// Build chain from this SubPin up to the top-level (non-split) parent
			const UEdGraphPin* TopParent = WatchPin;
			TArray<const UEdGraphPin*, TInlineAllocator<4>> SubChain;
			while (TopParent->ParentPin)
			{
				SubChain.Add(TopParent);
				TopParent = TopParent->ParentPin;
			}

			// Resolve the top-level struct property + data via the original function
			const FProperty* ParentProp = nullptr;
			const void* ParentData = nullptr;
			const void* ParentDelta = nullptr;
			UObject* ParentParent = nullptr;
			bool bParentDirect = false;
			auto ParentResult = OriginalFindDebuggingData(
				Blueprint, ActiveObject, TopParent,
				ParentProp, ParentData, ParentDelta, ParentParent, SeenObjects, &bParentDirect);

			if (ParentResult == FKismetDebugUtilities::EWatchTextResult::EWTR_Valid && ParentProp)
			{
				const void* CurData = bParentDirect ? ParentData : ParentProp->ContainerPtrToValuePtr<void>(ParentData);
				const FProperty* CurProp = ParentProp;

				// Walk from the parent down into the requested SubPin's member
				for (int32 i = SubChain.Num() - 1; i >= 0; --i)
				{
					const FStructProperty* StructProp = CastField<FStructProperty>(CurProp);
					if (!StructProp || !CurData)
						return OriginalFindDebuggingData(Blueprint, ActiveObject, WatchPin, OutProperty, OutData, OutDelta, OutParent, SeenObjects, bOutIsDirectPtr);

					FName MemberName = GetSubPinMemberName(SubChain[i]);
					FProperty* Member = StructProp->Struct->FindPropertyByName(MemberName);
					if (!Member)
						return OriginalFindDebuggingData(Blueprint, ActiveObject, WatchPin, OutProperty, OutData, OutDelta, OutParent, SeenObjects, bOutIsDirectPtr);

					CurData = Member->ContainerPtrToValuePtr<void>(CurData);
					CurProp = Member;
				}

				OutProperty = CurProp;
				OutData = CurData;
				OutDelta = CurData;
				OutParent = ParentParent;
				if (bOutIsDirectPtr)
					*bOutIsDirectPtr = true;  // CurData already points at the member value
				return FKismetDebugUtilities::EWatchTextResult::EWTR_Valid;
			}
		}

		return OriginalFindDebuggingData(Blueprint, ActiveObject, WatchPin, OutProperty, OutData, OutDelta, OutParent, SeenObjects, bOutIsDirectPtr);
	}
}

class FGMPEditorPlugin : public IModuleInterface
{
protected:
	// IModuleInterface implementation
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		// Install hook on HasFunctionAnyOutputParameter to allow ref-param events
		GMPRefEvent::OriginalHasOutputParam = GMPHook::InstallHook(
			&UEdGraphSchema_K2::HasFunctionAnyOutputParameter,
			&GMPRefEvent::Hook_HasFunctionAnyOutputParameter);

		if (GMPRefEvent::OriginalHasOutputParam)
		{
			GMPRefEvent::GOutputParamOverride = [](const UFunction* Func) -> bool {
				return Func && Func->HasMetaData(TEXT("GMPRefEvent"));
			};
			UE_LOG(LogTemp, Log, TEXT("GMPRefEvent: HasFunctionAnyOutputParameter hook installed"));
		}

		// Hook CreateFunctionStubForEvent to inject writeback bytecode
		{
			GMPRefEvent::FCreateStubFunc MemberFunc = GMPRefEvent::GetCreateStubForEvent();
			void* RawAddr;
			FMemory::Memcpy(&RawAddr, &MemberFunc, sizeof(void*));
			GMPRefEvent::OriginalCreateStub = reinterpret_cast<GMPRefEvent::FCreateStubFuncRaw>(
				GMPHook::Install(RawAddr, reinterpret_cast<void*>(&GMPRefEvent::Hook_CreateFunctionStubForEvent)));
			if (GMPRefEvent::OriginalCreateStub)
			{
				UE_LOG(LogTemp, Log, TEXT("GMPRefEvent: CreateFunctionStubForEvent hook installed"));
			}
		}

		// Hook FindDebuggingData to show split-struct SubPin values in debugger
		if (!IsRunningCommandlet())
		{
			GMPRefEvent::OriginalFindDebuggingData = GMPHook::InstallHook(
				GMPRefEvent::GetFindDebuggingDataAddr(),
				&GMPRefEvent::Hook_FindDebuggingData);
			if (GMPRefEvent::OriginalFindDebuggingData)
			{
				UE_LOG(LogTemp, Log, TEXT("GMPRefEvent: FindDebuggingData hook installed"));
			}
		}

		class FStringAsMessageTagPinFactory : public FGraphPanelPinFactory
		{
			static bool StringAsMessageTag(UEdGraphPin* InGraphPinObj)
			{
				bool bRet = false;
				do
				{
					if (!InGraphPinObj)
						break;

					UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(InGraphPinObj->GetOwningNode());
					if (!CallFuncNode)
						break;

					const UFunction* ThisFunction = CallFuncNode->GetTargetFunction();
					if (!ThisFunction || !ThisFunction->HasMetaData(TEXT("StringAsMessageTag")))
						break;

					TArray<FString> ParameterNames;
					ThisFunction->GetMetaData(TEXT("StringAsMessageTag")).ParseIntoArray(ParameterNames, TEXT(","), true);
					bRet = (ParameterNames.Contains(::ToString(InGraphPinObj->PinName)));
				} while (0);
				return bRet;
			}
			virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InPin) const override
			{
				if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String && StringAsMessageTag(InPin))
				{
					return SNew(SMessageTagGraphPin, InPin)
							.bRawName(true)
							.bRefresh(true);
				}
				return nullptr;
			}
		};
		FEdGraphUtilities::RegisterVisualPinFactory(MakeShareable(new FStringAsMessageTagPinFactory()));

#if 0
		UToolMenus* ToolMenus = UToolMenus::Get();
		//UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("AssetEditor.BlueprintEditor.ToolBar");
		UToolMenu* FoundMenu = ToolMenus->FindMenu("AssetEditorToolbar.CommonActions");
		if (FoundMenu)
		{
			FToolMenuSection& Section = FoundMenu->FindOrAddSection("CommonActions");
			//Section.AddEntry(FToolMenuEntry::InitToolBarButton(FGlobalEditorCommonCommands::Get().FindInContentBrowser, LOCTEXT("FindInContentBrowserButton", "Browse")));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				FAssetEditorCommands::Get().OpenReferenceViewer
			));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(FGlobalEditorCommonCommands::Get().FindInContentBrowser, LOCTEXT("FindInContentBrowserButton", "Browse")));
			//Section.AddSeparator(NAME_None);
		}
#endif

#endif
	}

	virtual void ShutdownModule() override
	{
		if (!UObjectInitialized())
		{
			return;
		}
#if WITH_EDITOR
		GMPHook::UninstallHook(&UEdGraphSchema_K2::HasFunctionAnyOutputParameter);
		if (GMPRefEvent::OriginalCreateStub)
		{
			void* RawAddr;
			auto MemberFunc = GMPRefEvent::GetCreateStubForEvent();
			FMemory::Memcpy(&RawAddr, &MemberFunc, sizeof(void*));
			GMPHook::Uninstall(RawAddr);
		}
		if (GMPRefEvent::OriginalFindDebuggingData)
		{
			GMPHook::UninstallHook(GMPRefEvent::GetFindDebuggingDataAddr());
		}
		GMPRefEvent::GOutputParamOverride = nullptr;
		GMPRefEvent::OriginalHasOutputParam = nullptr;
		GMPRefEvent::OriginalCreateStub = nullptr;
		GMPRefEvent::OriginalFindDebuggingData = nullptr;
#endif
	}
	// End of IModuleInterface implementation
private:
};

#ifdef MESSAGETAGSEDITOR_API
void EditorSearchNodeTitleInBlueprints(const FString& InStr, UBlueprint* Blueprint)
{
	extern MESSAGETAGSEDITOR_API void MesageTagsEditor_FindMessageInBlueprints(const FString& InStr, UBlueprint* Blueprint = nullptr);
	MesageTagsEditor_FindMessageInBlueprints(InStr, Blueprint);
}
#else
GS_PRIVATEACCESS_MEMBER(SFindInBlueprints, SearchTextField, TSharedPtr<SSearchBox>)
GS_PRIVATEACCESS_MEMBER(SFindInBlueprints, BlueprintEditorPtr, TWeakPtr<FBlueprintEditor>)
GS_PRIVATEACCESS_MEMBER(SSearchBox, OnTextChangedDelegate, FOnTextChanged)
GS_PRIVATEACCESS_MEMBER(SSearchBox, OnTextCommittedDelegate, FOnTextCommitted)
void EditorSearchNodeTitleInBlueprints(const FString& InStr, UBlueprint* Blueprint)
{
	if (!InStr.IsEmpty())
	{
		auto SearchKey = MessageKey;
		if (!SearchKey.StartsWith(TEXT("('")))
			SearchKey = FString::Printf(TEXT("('%s')"), *MessageKey);
		if (Blueprint)
		{
			TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(Blueprint);
			if (FoundAssetEditor.IsValid())
			{
				TSharedRef<IBlueprintEditor> BlueprintEditor = StaticCastSharedRef<IBlueprintEditor>(FoundAssetEditor.ToSharedRef());
				BlueprintEditor->FocusWindow();
				BlueprintEditor->SummonSearchUI(true, SearchKey, false);
				return;
			}
		}

		auto FindResultsToUse = FFindInBlueprintSearchManager::Get().GetGlobalFindResults();
		if (FindResultsToUse.IsValid())
		{
			auto BlueprintEditor = PrivateAccess::BlueprintEditorPtr(*FindResultsToUse.Get()).Pin();
			if (BlueprintEditor.IsValid())
			{
				BlueprintEditor->FocusWindow();
				BlueprintEditor->SummonSearchUI(true, SearchKey, false);
			}
			else
			{
				TSharedPtr<SSearchBox>& SearchField = PrivateAccess::SearchTextField(*FindResultsToUse.Get());
				auto Txt = FText::FromString(SearchKey);
				SearchField->SetText(Txt);
				SearchField->SetSearchText(Txt);
				PrivateAccess::OnTextChangedDelegate(*SearchField.Get()).ExecuteIfBound(Txt);
				PrivateAccess::OnTextCommittedDelegate(*SearchField.Get()).ExecuteIfBound(Txt, ETextCommit::OnEnter);
			}
			return;
		}
	}
}
#endif

void EditorSearchMessageReferences(const FMessageTag& MessageKey)
{
	if (!MessageKey.IsValid())
		return;

#if UE_4_24_OR_LATER
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), MessageKey.GetTagName());
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
#elif UE_4_23_OR_LATER
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), MessageKey.GetTagName());
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers);
	}
#elif UE_4_20_OR_LATER
	if (IAssetManagerEditorModule::IsAvailable())
	{
		IAssetManagerEditorModule& ManagerEditorModule = IAssetManagerEditorModule::Get();
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), MessageKey.GetTagName());
		ManagerEditorModule.OpenReferenceViewerUI(AssetIdentifiers);
	}
#else
	if (IReferenceViewerModule::IsAvailable())
	{
		IReferenceViewerModule& ReferenceViewerModule = IReferenceViewerModule::Get();
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), MessageKey.GetTagName());
		ReferenceViewerModule.InvokeReferenceViewerTab(AssetIdentifiers);
	}
#endif
}

IMPLEMENT_MODULE(FGMPEditorPlugin, GMPEditor)

#include "XConsoleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

auto GetLocalTimeZone()
{
	const FDateTime LocalNow = FDateTime::Now();
	const FDateTime UTCNow = FDateTime::UtcNow();
	const FTimespan Difference = LocalNow - UTCNow;
	int32 MinutesDifference = FMath::RoundToInt(Difference.GetTotalMinutes());
	MinutesDifference = MinutesDifference >= 0 ? ((MinutesDifference + 29) / 30) : ((MinutesDifference - 29) / 30);
	return FTimespan(MinutesDifference / 60, MinutesDifference / 60, 0);
}

bool CopyFileToTempLocation(FString& InOutFilename)
{
	FString InFilename = InOutFilename;
	FString OutBaseFilename = FPaths::GetBaseFilename(InFilename);
	const TCHAR* InvalidCharacters = INVALID_LONGPACKAGE_CHARACTERS;
	for (; *InvalidCharacters; ++InvalidCharacters)
	{
		const TCHAR InvalidStr[] = {*InvalidCharacters, '\0'};
		OutBaseFilename.ReplaceInline(InvalidStr, TEXT("_"));
	}
	FString OutFilename = FString::Printf(TEXT("%s%s%s"), *FPaths::DiffDir(), *OutBaseFilename, *FPaths::GetExtension(InFilename, true));
	if (IFileManager::Get().Copy(*OutFilename, *InFilename, true, true))
	{
		OutFilename = FString::Printf(TEXT("%s%s%s%s"), *FPaths::DiffDir(), *OutBaseFilename, *FGuid::NewGuid().ToString(EGuidFormats::Short), *FPaths::GetExtension(InFilename, true));
		if (IFileManager::Get().Copy(*OutFilename, *InFilename, true, true))
		{
			return false;
		}
	}
	InOutFilename = *OutFilename;
	return true;
}

void DiffTwoAssets(FString BaseFilePath, FString MineFilePath)
{
	// Ensure the path is absolute and the file exists
	if (!CopyFileToTempLocation(BaseFilePath) || !CopyFileToTempLocation(MineFilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("One or both of the provided .uasset files do not exist!"));
		return;
	}

	UObject* LeftAsset = LoadPackage(nullptr, *BaseFilePath, LOAD_ForDiff);
	UObject* RightAsset = LoadPackage(nullptr, *MineFilePath, LOAD_ForDiff);
	if (auto Pkg = Cast<UPackage>(LeftAsset))
		LeftAsset = Pkg->FindAssetInPackage();
	if (auto Pkg = Cast<UPackage>(RightAsset))
		RightAsset = Pkg->FindAssetInPackage();
	if (!LeftAsset || !RightAsset)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

	UClass* OldClass = LeftAsset->GetClass();
	UClass* NewClass = RightAsset->GetClass();
	// If same class and blueprint
	if (OldClass == NewClass && OldClass && OldClass->IsChildOf<UBlueprint>())
	{
		FRevisionInfo LeftRevision;
		FRevisionInfo RightRevision;
		AssetToolsModule.Get().DiffAssets(LeftAsset, RightAsset, LeftRevision, RightRevision);
	}
}

UObject* LoadAssetForDiffingNoCompile(const FString& DiskPath)
{
	FString LongName = DiskPath;
	bool bInnerPkg = FPackageName::TryConvertFilenameToLongPackageName(DiskPath, LongName);
	const uint32 Flags = LOAD_EditorOnly | LOAD_DisableCompileOnLoad | LOAD_NoVerify | LOAD_Quiet | (bInnerPkg ? LOAD_ForDiff : LOAD_None);
	UPackage* Pkg = LoadPackage(nullptr, *LongName, Flags);
	if (!Pkg)
		return nullptr;

	const FString AssetName = FPaths::GetBaseFilename(DiskPath);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *Pkg->GetName(), *AssetName);
	UObject* Obj = StaticLoadObject(UObject::StaticClass(), Pkg, *ObjectPath, nullptr, Flags);
	if (Obj)
	{
		Obj->SetFlags(RF_Transient | RF_Standalone);
	}
	return Obj;
}

FXConsoleCommandLambda XVar_SearchMessageReferences(TEXT("gmp.diffassets"), [](const FString& LeftFilename, const FString& RightFilename, UWorld* InWorld) {
	//
	DiffTwoAssets(LeftFilename, RightFilename);
});
