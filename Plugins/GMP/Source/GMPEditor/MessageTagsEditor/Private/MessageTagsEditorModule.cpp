// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsEditorModule.h"

#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Factories/Factory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GMPCore.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFile.h"
#if (ENGINE_MAJOR_VERSION >= 5)
#include "HAL/PlatformFileManager.h"
#else
#include "HAL/PlatformFilemanager.h"
#endif
#include "ISettingsEditorModule.h"
#include "ISettingsModule.h"
#include "ISourceControlModule.h"
#include "MessageTagContainer.h"
#include "MessageTagCustomization.h"
#include "MessageTagsGraphPanelPinFactory.h"
#include "MessageTagsManager.h"
#include "MessageTagsModule.h"
#include "MessageTagsSettings.h"
#include "MessageTagsSettingsCustomization.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PropertyEditorModule.h"
#include "SourceControlHelpers.h"
#include "Stats/StatsMisc.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Notifications/SNotificationList.h"
#if UE_5_01_OR_LATER
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#if !UE_4_20_OR_LATER
#include "ReferenceViewer.h"
#elif !UE_4_23_OR_LATER
#include "AssetManagerEditorModule.h"
#endif

#include "BlueprintEditor.h"
#include "FindInBlueprintManager.h"
#include "FindInBlueprints.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Input/SSearchBox.h"
#include "Algo/IndexOf.h"

#if UE_5_00_OR_LATER
#include "UObject/ObjectSaveContext.h"
#endif
#include "MessageTagStyle.h"
#include "SMessageTagPicker.h"

#include "SRenameMessageTagDialog.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "MessageTagEditor"

void MesageTagsEditor_SearchMessageReferences(const TArray<FAssetIdentifier>& AssetIdentifiers)
{
	if (AssetIdentifiers.Num() == 0)
		return;

#if UE_4_24_OR_LATER
	{
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
#elif UE_4_23_OR_LATER
	{
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers);
	}
#elif UE_4_20_OR_LATER
	if (IAssetManagerEditorModule::IsAvailable())
	{
		IAssetManagerEditorModule& ManagerEditorModule = IAssetManagerEditorModule::Get();
		ManagerEditorModule.OpenReferenceViewerUI(AssetIdentifiers);
	}
#else
	if (IReferenceViewerModule::IsAvailable())
	{
		IReferenceViewerModule& ReferenceViewerModule = IReferenceViewerModule::Get();
		ReferenceViewerModule.InvokeReferenceViewerTab(AssetIdentifiers);
	}
#endif
}
#ifndef GS_PRIVATEACCESS_MEMBER
#define GS_PRIVATEACCESS_MEMBER(Class, Member, ...)                                          \
	namespace PrivateAccess                                                                  \
	{                                                                                        \
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
GS_PRIVATEACCESS_MEMBER(SFindInBlueprints, SearchTextField, TSharedPtr<class SSearchBox>)
GS_PRIVATEACCESS_MEMBER(SFindInBlueprints, BlueprintEditorPtr, TWeakPtr<FBlueprintEditor>)
GS_PRIVATEACCESS_MEMBER(SSearchBox, OnTextChangedDelegate, FOnTextChanged)
GS_PRIVATEACCESS_MEMBER(SSearchBox, OnTextCommittedDelegate, FOnTextCommitted)

MESSAGETAGSEDITOR_API void MesageTagsEditor_FindMessageInBlueprints(const FString& MessageKey, class UBlueprint* Blueprint)
{
	if (!MessageKey.IsEmpty())
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
extern GMP_API const TCHAR* GMPGetNativeTagType();
class FMessageTagsEditorModule : public IMessageTagsEditorModule
{
public:
	void InitMessageTagBindding()
	{
#if WITH_EDITOR && GMP_WITH_DYNAMIC_CALL_CHECK
		if (!TrueOnFirstCall([]{}))
			return;
		UMessageTagsManager::OnOpenModifyMessageTagDialog().AddLambda([](TSharedPtr<FMessageTagNode> TagNode, FSimpleDelegate OnRenamed) {
			TSharedRef<SWindow> ModifyTagWindow = SNew(SWindow)
													.Title(LOCTEXT("ModifyTagWindowTitle", "Modify Message Tag"))
													.SizingRule(ESizingRule::Autosized)
													.SupportsMaximize(false)
													.SupportsMinimize(false);
			TSharedRef<SRenameMessageTagDialog> ModifyTagDialog = SNew(SRenameMessageTagDialog)
																	.MessageTagNode(TagNode)
																	.OnMessageTagRenamed_Lambda([OnRenamed](FString Old, FString New) { OnRenamed.ExecuteIfBound(); });
			ModifyTagWindow->SetContent(SNew(SBox)
										.MinDesiredWidth(320.0f)
										[
											ModifyTagDialog
										]);
			if (TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
			{
				FSlateApplication::Get().AddModalWindow(ModifyTagWindow, CurrentWindow);
			}
			else
			{
				FSlateApplication::Get().AddWindow(ModifyTagWindow, true);
			}
		});

		FGMPMessageHub::InitMessageTagBinding(FGMPMessageHub::FOnUpdateMessageTagDelegate::CreateLambda([this](const FString& MsgKey, const auto* Types, const auto* RspTypes, const TCHAR* TagType) {
			auto& Mgr = UMessageTagsManager::Get();
			auto MsgTag = FMessageTag::RequestMessageTag(*MsgKey, false);
			TArray<FName> OrignalParamNames;
			TArray<FName> OrignalResponseNames;
			TArray<FMessageParameter> ResponseTypes;
			TSharedPtr<FMessageTagNode> TagNode;

			const bool bIsNativeTag = FCString::Strcmp(TagType, GMPGetNativeTagType()) == 0;
			if (MsgTag.IsValid())
			{
				TagNode = Mgr.FindTagNode(MsgTag);
				bool bParameterMatch = true;
				if (Types)
				{
					static TSet<FName> NativeMarks;
					bool bMarked = false;
					NativeMarks.Emplace(MsgTag.GetTagName(), &bMarked);
					auto& TypesRef = *Types;
					if (TagNode.IsValid() && (!bMarked || TypesRef.Num() <= TagNode->Parameters.Num()))
					{
						TagNode->Parameters.SetNum(TypesRef.Num());
						for (int32 i = 0; i < TypesRef.Num(); ++i)
						{
							if (TagNode->Parameters[i].Type != TypesRef[i])
							{
								if (FGMPNameSuccession::IsDerivedFrom(TypesRef[i], TagNode->Parameters[i].Type))
									continue;

								bParameterMatch = false;
								break;
							}
						}
					}
				}

				bool bResponseMatch = true;
				if (RspTypes && RspTypes->Num() > 0)
				{
					static TSet<FName> NativeMarks;
					bool bMarked = false;
					NativeMarks.Emplace(MsgTag.GetTagName(), &bMarked);
					auto& RspTypesRef = *RspTypes;
					if (TagNode.IsValid() && (!bMarked || RspTypesRef.Num() <= TagNode->ResponseTypes.Num()))
					{
						TagNode->ResponseTypes.SetNum(RspTypesRef.Num());
						for (int32 i = 0; i < RspTypesRef.Num(); ++i)
						{
							if (TagNode->ResponseTypes[i].Type != RspTypesRef[i])
							{
								if (FGMPNameSuccession::IsDerivedFrom(RspTypesRef[i], TagNode->ResponseTypes[i].Type))
									continue;

								bResponseMatch = false;
								break;
							}
						}
					}
				}
				else if (TagNode.IsValid() && TagNode->ResponseTypes.Num() > 0)
				{
					ResponseTypes = TagNode->ResponseTypes;
				}

				if (bParameterMatch && bResponseMatch && !bIsNativeTag)
					return;

				if (TagNode.IsValid())
				{
					for (auto&& ExistInfo : TagNode->Parameters)
						OrignalParamNames.Add(ExistInfo.Name);

					for (auto&& ExistInfo : TagNode->ResponseTypes)
						OrignalResponseNames.Add(ExistInfo.Name);
				}
			}

			static FName ParamBase = TEXT("Param");
			TArray<FMessageParameter> Parameters;

			if (ensure(Types) && Types->Num() > 0)
			{
				Parameters.Reserve(Types->Num());
				{
					for (int32 i = 0; i < Types->Num(); ++i)
						Parameters.Add(FMessageParameter{OrignalParamNames.IsValidIndex(i) ? OrignalParamNames[i] : FName(ParamBase, i), (*Types)[i]});
				}
			}

			if (RspTypes && RspTypes->Num() > 0)
			{
				ResponseTypes.Reserve(RspTypes->Num());
				for (int32 i = 0; i < RspTypes->Num(); ++i)
				{
					// Param Param_1 Param_2 ...
					ResponseTypes.Add(FMessageParameter{OrignalResponseNames.IsValidIndex(i) ? OrignalResponseNames[i] : FName(ParamBase, i + 1), (*RspTypes)[i]});
				}
			}

			TGuardValue<bool> RunningGame(bIsRunningGame, true);
			if (bIsNativeTag)
			{
				AddNewMessageTagToINI(MsgKey, TEXT("CodeGen"), FMessageTagSource::GetNativeName(), true, false, Parameters, ResponseTypes);
			}
			else if (TagType && TagNode)
			{
				FName TagSource = TagNode->GetFirstSourceName();
				if (TagSource.IsNone())
				{
					TagSource = TagType;
				}
				AddNewMessageTagToINI(MsgKey, TEXT("CodeGen"), TagSource, false, false, Parameters, ResponseTypes);
			}
			Mgr.SyncToGMPMeta();
		}));
#endif
	}

	// IModuleInterface

	virtual void StartupModule() override
	{
		InitMessageTagBindding();
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMessageTagsEditorModule::OnPostEngineInit);
		FMessageTagStyle::Initialize();
	}

	void OnPostEngineInit()
	{
		// Register the details customizer
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.RegisterCustomPropertyTypeLayout("MessageTag", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMessageTagCustomizationPublic::MakeInstance));

			PropertyModule.RegisterCustomPropertyTypeLayout("MessageTagCreationWidgetHelper", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMessageTagCreationWidgetHelperDetails::MakeInstance));

			PropertyModule.RegisterCustomClassLayout(UMessageTagsSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FMessageTagsSettingsCustomization::MakeInstance));

			PropertyModule.NotifyCustomizationModuleChanged();
		}

		TSharedPtr<FMessageTagsGraphPanelPinFactory> MessageTagsGraphPanelPinFactory = MakeShareable(new FMessageTagsGraphPanelPinFactory());
		FEdGraphUtilities::RegisterVisualPinFactory(MessageTagsGraphPanelPinFactory);

		// 		TSharedPtr<FMessageTagsGraphPanelNodeFactory> MessageTagsGraphPanelNodeFactory = MakeShareable(new FMessageTagsGraphPanelNodeFactory());
		// 		FEdGraphUtilities::RegisterVisualNodeFactory(MessageTagsGraphPanelNodeFactory);

		// These objects are not UDeveloperSettings because we only want them to register if the editor plugin is enabled

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Project", "MessageTags", LOCTEXT("MessageTagSettingsName", "MessageTags"), LOCTEXT("MessageTagSettingsNameDesc", "MessageTag Settings"), GetMutableDefault<UMessageTagsSettings>());
		}

		MessageTagPackageName = FMessageTag::StaticStruct()->GetOutermost()->GetFName();
		MessageTagStructName = FMessageTag::StaticStruct()->GetFName();

		// Hook into notifications for object re-imports so that the message tag tree can be reconstructed if the table changes
		if (GIsEditor)
		{
			GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.AddRaw(this, &FMessageTagsEditorModule::OnObjectReimported);
			FEditorDelegates::OnEditAssetIdentifiers.AddRaw(this, &FMessageTagsEditorModule::OnEditMessageTag);
			IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &FMessageTagsEditorModule::OnEditorSettingsChanged);
#if UE_5_00_OR_LATER
			UPackage::PackageSavedWithContextEvent.AddRaw(this, &FMessageTagsEditorModule::OnPackageSaved);
#else
			UPackage::PackageSavedEvent.AddRaw(this, &FMessageTagsEditorModule::OnPackageSaved);
#endif
		}
		InitMessageTagBindding();
	}

	void OnObjectReimported(UFactory* ImportFactory, UObject* InObject)
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		// Re-construct the message tag tree if the base table is re-imported
		if (GIsEditor && !IsRunningCommandlet() && InObject && Manager.MessageTagTables.Contains(Cast<UDataTable>(InObject)))
		{
			Manager.EditorRefreshMessageTagTree();
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);

		// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
		// we call this function before unloading the module.

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Project", "MessageTags");
			SettingsModule->UnregisterSettings("Project", "Project", "MessageTags Developer");
		}

#if !UE_4_22_OR_LATER
		FEditorDelegates::OnAssetPostImport.RemoveAll(this);
#else
		if (GEditor)
		{
			GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.RemoveAll(this);
		}
		FEditorDelegates::OnEditAssetIdentifiers.RemoveAll(this);
#endif
		IMessageTagsModule::OnTagSettingsChanged.RemoveAll(this);
#if UE_5_00_OR_LATER
		UPackage::PreSavePackageWithContextEvent.RemoveAll(this);
#else
		UPackage::PackageSavedEvent.RemoveAll(this);
#endif

#if !UE_4_22_OR_LATER
		FAssetRegistryModule* AssetRegistryModule = FModuleManager::FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
		if (AssetRegistryModule)
		{
			AssetRegistryModule->Get().OnEditSearchableName(MessageTagPackageName, MessageTagStructName).Unbind();
		}
#endif
	}

	void OnEditorSettingsChanged()
	{
		// This is needed to make networking changes as well, so let's always refresh
		UMessageTagsManager::Get().EditorRefreshMessageTagTree();

		// Attempt to migrate the settings if needed
		MigrateSettings();
	}

	void WarnAboutRestart()
	{
		ISettingsEditorModule* SettingsEditorModule = FModuleManager::GetModulePtr<ISettingsEditorModule>("SettingsEditor");
		if (SettingsEditorModule)
		{
			SettingsEditorModule->OnApplicationRestartRequired();
		}
	}
#if UE_5_00_OR_LATER
	void OnPackageSaved(const FString& PackageFileName, UPackage* PackageObj, FObjectPostSaveContext Ctx)
#else
	void OnPackageSaved(const FString& PackageFileName, UObject* PackageObj)
#endif
	{
		if (GIsEditor && !IsRunningCommandlet())
		{
			UMessageTagsManager& Manager = UMessageTagsManager::Get();

			bool bRefreshMessageTagTree = false;

			TArray<UObject*> Objects;
			const bool bIncludeNestedObjects = false;
			GetObjectsWithOuter(PackageObj, Objects, bIncludeNestedObjects);
			for (UObject* Entry : Objects)
			{
				if (UDataTable* DataTable = Cast<UDataTable>(Entry))
				{
					if (Manager.MessageTagTables.Contains(DataTable))
					{
						bRefreshMessageTagTree = true;
						break;
					}
				}
			}

			// Re-construct the message tag tree if a data table is saved (presumably with modifications).
			if (bRefreshMessageTagTree)
			{
				Manager.EditorRefreshMessageTagTree();
			}
		}
	}

#if UE_4_22_OR_LATER
	void OnEditMessageTag(TArray<FAssetIdentifier> AssetIdentifierList)
	{
		// If any of these are message tags, open up tag viewer
		for (FAssetIdentifier Identifier : AssetIdentifierList)
		{
			if (Identifier.IsValue() && Identifier.PackageName == MessageTagPackageName && Identifier.ObjectName == MessageTagStructName)
			{
				if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
				{
					// TODO: Select tag maybe?
					SettingsModule->ShowViewer("Project", "Project", "MessageTags");
				}
				return;
			}
		}
	}
#else
	bool OnEditMessageTag(const FAssetIdentifier& AssetId)
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			// TODO: Select tag maybe?
			SettingsModule->ShowViewer("Project", "Project", "MessageTags");
		}

		return true;
	}
#endif

	void ShowNotification(const FText& TextToDisplay, float TimeToDisplay, bool bLogError = false, bool bOnlyLog = false)
	{
		if (bOnlyLog)
		{
			if (bLogError)
			{
				UE_LOG(LogMessageTags, Error, TEXT("%s"), *TextToDisplay.ToString())
			}
			else
			{
				UE_LOG(LogMessageTags, Display, TEXT("%s"), *TextToDisplay.ToString())
			}
		}
		else
		{
			FNotificationInfo Info(TextToDisplay);
			Info.ExpireDuration = TimeToDisplay;

			FSlateNotificationManager::Get().AddNotification(Info);

			// Also log if error
			if (bLogError)
			{
				UE_LOG(LogMessageTags, Error, TEXT("%s"), *TextToDisplay.ToString())
			}
		}
	}

	void MigrateSettings()
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		FString DefaultEnginePath = FString::Printf(TEXT("%sDefaultEngine.ini"), *FPaths::SourceConfigDir());

		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();

		// The refresh has already set the in memory version of this to be correct, just need to save it out now
#if UE_5_04_OR_LATER
		if (!GConfig->GetSection(TEXT("MessageTags"), false, DefaultEnginePath))
		{
			// Already migrated or no data
			return;
		}
#else
		if (!GConfig->GetSectionPrivate(TEXT("MessageTags"), false, true, DefaultEnginePath))
		{
			// Already migrated or no data
			return;
		}
#endif
		// Check out defaultengine
		MessageTagsUpdateSourceControl(DefaultEnginePath);

		// Delete message tags section entirely. This modifies the disk version
		GConfig->EmptySection(TEXT("MessageTags"), DefaultEnginePath);

#if UE_5_04_OR_LATER
		GConfig->RemoveKeyFromSection(TEXT("/Script/Engine.Engine"), TEXT("+MessageTagRedirects"), DefaultEnginePath);
#else
		if (FConfigSection* PackageRedirects = GConfig->GetSectionPrivate(TEXT("/Script/Engine.Engine"), false, false, DefaultEnginePath))
		{
			for (FConfigSection::TIterator It(*PackageRedirects); It; ++It)
			{
				if (It.Key() == TEXT("+MessageTagRedirects"))
				{
					It.RemoveCurrent();
				}
			}
		}
#endif
		// This will remove comments, etc. It is expected for someone to diff this before checking in to manually fix it
		GConfig->Flush(false, DefaultEnginePath);

		// Write out messagetags.ini
		MessageTagsUpdateSourceControl(Settings->GetDefaultConfigFilename());
#if UE_5_00_OR_LATER
		Settings->TryUpdateDefaultConfigFile();
#else
		Settings->UpdateDefaultConfigFile();
#endif

		GConfig->LoadFile(Settings->GetDefaultConfigFilename());

		// Write out all other tag lists
		TArray<const FMessageTagSource*> Sources;

		Manager.FindTagSourcesWithType(EMessageTagSourceType::TagList, Sources);
		Manager.FindTagSourcesWithType(EMessageTagSourceType::RestrictedTagList, Sources);

		for (const FMessageTagSource* Source : Sources)
		{
			UMessageTagsList* TagList = Source->SourceTagList;
			if (TagList)
			{
				MessageTagsUpdateSourceControl(TagList->ConfigFileName);
#if UE_5_00_OR_LATER
				TagList->TryUpdateDefaultConfigFile(TagList->ConfigFileName);
#else
				TagList->UpdateDefaultConfigFile(TagList->ConfigFileName);
#endif

				// Reload off disk
				GConfig->LoadFile(TagList->ConfigFileName);
				//FString DestFileName;
				//FConfigCacheIni::LoadGlobalIniFile(DestFileName, *FString::Printf(TEXT("Tags/%s"), *Source->SourceName.ToString()), nullptr, true);

				// Explicitly remove user tags section
				GConfig->EmptySection(TEXT("UserTags"), TagList->ConfigFileName);
			}
		}

		ShowNotification(LOCTEXT("MigrationText", "Migrated Tag Settings, check DefaultEngine.ini before checking in!"), 10.0f);
	}

	void MessageTagsUpdateSourceControl(const FString& RelativeConfigFilePath, bool bOnlyLog = false)
	{
		if (bIsRunningGame)
			return;
		TArray<FString> RelativeConfigFilePaths = {RelativeConfigFilePath};
		MessageTagsUpdateSourceControl(RelativeConfigFilePaths, bOnlyLog);
	}

	void MessageTagsUpdateSourceControl(const TArray<FString>& RelativeConfigFilePaths, bool bOnlyLog = false)
	{
		TArray<FString> ExistingConfigPaths;
		for (const FString& RelativeConfigFilePath : RelativeConfigFilePaths)
		{
			FString ConfigPath = FPaths::ConvertRelativePathToFull(RelativeConfigFilePath);

			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ConfigPath))
			{
				ExistingConfigPaths.Add(ConfigPath);
			}
		}

		if (ISourceControlModule::Get().IsEnabled())
		{
			FText ErrorMessage;

			if (ExistingConfigPaths.Num() == 1)
			{
				const FString& ConfigPath = ExistingConfigPaths[0];
				if (!SourceControlHelpers::CheckoutOrMarkForAdd(ConfigPath, FText::FromString(ConfigPath), NULL, ErrorMessage))
				{
					ShowNotification(ErrorMessage, 3.0f, false, bOnlyLog);
				}
			}
			else
			{
				SourceControlHelpers::CheckOutOrAddFiles(ExistingConfigPaths);
			}
		}
		else
		{
			for (const FString& ConfigPath : ExistingConfigPaths)
			{
				if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigPath, false))
				{
					ShowNotification(FText::Format(LOCTEXT("FailedToMakeWritable", "Could not make {0} writable."), FText::FromString(ConfigPath)), 3.0f, false, bOnlyLog);
				}
			}
		}
	}

	bool DeleteTagRedirector(const FName& TagToDelete, bool bOnlyLog = false, bool bRefresh = true, TMap<UObject*, FString>* OutObjectsToUpdateConfig = nullptr)
	{
		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		for (int32 i = 0; i < Settings->MessageTagRedirects.Num(); i++)
		{
			if (Settings->MessageTagRedirects[i].OldTagName == TagToDelete)
			{
				Settings->MessageTagRedirects.RemoveAt(i);

				if (bRefresh)
				{
					MessageTagsUpdateSourceControl(Settings->GetDefaultConfigFilename());
#if UE_5_00_OR_LATER
					Settings->TryUpdateDefaultConfigFile();
#else
					Settings->UpdateDefaultConfigFile();
#endif
					GConfig->LoadFile(Settings->GetDefaultConfigFilename());

					Manager.EditorRefreshMessageTagTree();
				}
				else
				{
					if (OutObjectsToUpdateConfig)
					{
						OutObjectsToUpdateConfig->Add(Settings, Settings->GetDefaultConfigFilename());
					}
				}

				ShowNotification(FText::Format(LOCTEXT("RemoveTagRedirect", "Deleted tag redirect {0}"), FText::FromName(TagToDelete)), 5.0f, false, bOnlyLog);

				WarnAboutRestart();

				if (bRefresh)
				{
					TSharedPtr<FMessageTagNode> FoundNode = Manager.FindTagNode(TagToDelete);

					ensureMsgf(!FoundNode.IsValid() || FoundNode->GetCompleteTagName() == TagToDelete, TEXT("Failed to delete redirector %s!"), *TagToDelete.ToString());
				}

				return true;
			}
		}

		return false;
	}

	virtual bool AddNewMessageTagToINI(const FString& NewTag,
									   const FString& Comment,
									   FName TagSourceName,
									   bool bIsRestrictedTag,
									   bool bAllowNonRestrictedChildren,
									   const TArray<FMessageParameter>& Parameters,
									   const TArray<FMessageParameter>& ResponseTypes) override
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		if (NewTag.IsEmpty())
		{
			return false;
		}

		if (Manager.ShouldImportTagsFromINI() == false)
		{
			return false;
		}

		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();
		UMessageTagsDeveloperSettings* DevSettings = GetMutableDefault<UMessageTagsDeveloperSettings>();

		FText ErrorText;
		FString FixedString;
		if (!Manager.IsValidMessageTagString(NewTag, &ErrorText, &FixedString) && !Comment.Equals("CodeGen"))
		{
			ShowNotification(FText::Format(LOCTEXT("AddTagFailure_BadString", "Failed to add message tag {0}: {1}, try {2} instead!"), FText::FromString(NewTag), ErrorText, FText::FromString(FixedString)), 10.0f, true);
			return false;
		}

		FName NewTagName = FName(*NewTag);

		// Delete existing redirector
		DeleteTagRedirector(NewTagName);

		// Already in the list as an explicit tag, ignore. Note we want to add if it is in implicit tag. (E.g, someone added A.B.C then someone tries to add A.B)
		if (Manager.IsDictionaryTag(NewTagName) && !Comment.Equals("CodeGen"))
		{
			ShowNotification(FText::Format(LOCTEXT("AddTagFailure_AlreadyExists", "Failed to add message tag {0}, already exists!"), FText::FromString(NewTag)), 10.0f, true);

			return false;
		}

		if (bIsRestrictedTag)
		{
			// restricted tags can't be children of non-restricted tags
			FString AncestorTag = NewTag;
			bool bWasSplit = NewTag.Split(TEXT("."), &AncestorTag, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			while (bWasSplit)
			{
				if (Manager.IsDictionaryTag(FName(*AncestorTag)))
				{
					FString TagComment;
					FName Source;
					bool bIsExplicit;
					bool bIsRestricted;
					bool bAllowsNonRestrictedChildren;

					Manager.GetTagEditorData(*AncestorTag, TagComment, Source, bIsExplicit, bIsRestricted, bAllowsNonRestrictedChildren);
					if (bIsRestricted)
					{
						break;
					}
					ShowNotification(FText::Format(LOCTEXT("AddRestrictedTagFailure", "Failed to add restricted message tag {0}, {1} is not a restricted tag"), FText::FromString(NewTag), FText::FromString(AncestorTag)), 10.0f, true);

					return false;
				}

				bWasSplit = AncestorTag.Split(TEXT("."), &AncestorTag, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			}
		}
		else
		{
			// non-restricted tags can only be children of restricted tags if the restricted tag allows it
			FString AncestorTag = NewTag;
			bool bWasSplit = NewTag.Split(TEXT("."), &AncestorTag, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			while (bWasSplit)
			{
				if (Manager.IsDictionaryTag(FName(*AncestorTag)))
				{
					FString TagComment;
					FName Source;
					bool bIsExplicit;
					bool bIsRestricted;
					bool bAllowsNonRestrictedChildren;

					Manager.GetTagEditorData(*AncestorTag, TagComment, Source, bIsExplicit, bIsRestricted, bAllowsNonRestrictedChildren);
					if (bIsRestricted)
					{
						if (bAllowsNonRestrictedChildren)
						{
							break;
						}

						ShowNotification(FText::Format(LOCTEXT("AddTagFailure_RestrictedTag", "Failed to add message tag {0}, {1} is a restricted tag and does not allow non-restricted children"),
													   FText::FromString(NewTag),
													   FText::FromString(AncestorTag)),
										 10.0f,
										 true);

						return false;
					}
				}

				bWasSplit = AncestorTag.Split(TEXT("."), &AncestorTag, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			}
		}

		if ((TagSourceName == NAME_None || TagSourceName == FMessageTagSource::GetDefaultName()) && DevSettings && !DevSettings->DeveloperConfigName.IsEmpty())
		{
			// Try to use developer config file
			TagSourceName = FName(*FString::Printf(TEXT("%s.ini"), *DevSettings->DeveloperConfigName));
		}

		if (TagSourceName == NAME_None)
		{
			// If not set yet, set to default
			TagSourceName = FMessageTagSource::GetDefaultName();
		}

		const FMessageTagSource* TagSource = Manager.FindTagSource(TagSourceName);

		if (!TagSource)
		{
			// Create a new one
			TagSource = Manager.FindOrAddTagSource(TagSourceName, EMessageTagSourceType::TagList);
		}

		bool bSuccess = false;
		if (TagSource)
		{
			UObject* TagListObj = nullptr;
			FString ConfigFileName;

			if (bIsRestrictedTag && TagSource->SourceRestrictedTagList)
			{
				URestrictedMessageTagsList* RestrictedTagList = TagSource->SourceRestrictedTagList;
				auto NewTable = FRestrictedMessageTagTableRow(FName(*NewTag), Comment, bAllowNonRestrictedChildren, Parameters, ResponseTypes);
				auto Idx = Algo::IndexOfByPredicate(RestrictedTagList->RestrictedMessageTagList, [&NewTable](const FMessageTagTableRow& Row) { return Row.Tag == NewTable.Tag; });
				if (Idx != INDEX_NONE)
				{
					if (RestrictedTagList->RestrictedMessageTagList[Idx].Parameters == Parameters && RestrictedTagList->RestrictedMessageTagList[Idx].ResponseTypes == ResponseTypes)
						return true;
				}

				TagListObj = RestrictedTagList;
				Idx = RestrictedTagList->RestrictedMessageTagList.AddUnique(NewTable);
				RestrictedTagList->RestrictedMessageTagList[Idx].Parameters = Parameters;
				RestrictedTagList->RestrictedMessageTagList[Idx].ResponseTypes = ResponseTypes;
				RestrictedTagList->SortTags();
				ConfigFileName = RestrictedTagList->ConfigFileName;
				bSuccess = true;
			}
			else if (TagSource->SourceTagList)
			{
				UMessageTagsList* TagList = TagSource->SourceTagList;
				auto NewTable = FMessageTagTableRow(FName(*NewTag), Comment, Parameters, ResponseTypes);
				auto Idx = Algo::IndexOfByPredicate(TagList->MessageTagList, [&NewTable](const FMessageTagTableRow& Row) { return Row.Tag == NewTable.Tag; });
				if (Idx != INDEX_NONE)
				{
					if (TagList->MessageTagList[Idx].Parameters == Parameters && TagList->MessageTagList[Idx].ResponseTypes == ResponseTypes)
						return true;
				}
				TagListObj = TagList;
				Idx = TagList->MessageTagList.AddUnique(NewTable);
				TagList->MessageTagList[Idx].Parameters = Parameters;
				TagList->MessageTagList[Idx].ResponseTypes = ResponseTypes;
				TagList->SortTags();
				ConfigFileName = TagList->ConfigFileName;
				bSuccess = true;
			}

			if (ensure(TagListObj))
			{
				// Check source control before and after writing, to make sure it gets created or checked out
				MessageTagsUpdateSourceControl(ConfigFileName);
#if UE_5_00_OR_LATER
				TagListObj->TryUpdateDefaultConfigFile(ConfigFileName);
#else
				TagListObj->UpdateDefaultConfigFile(ConfigFileName);
#endif
				MessageTagsUpdateSourceControl(ConfigFileName);
				GConfig->LoadFile(ConfigFileName);
				RemoveINIImpl(NewTagName);
			}
		}

		if (!bSuccess)
		{
			ShowNotification(FText::Format(LOCTEXT("AddTagFailure", "Failed to add message tag {0} to dictionary {1}!"), FText::FromString(NewTag), FText::FromName(TagSourceName)), 10.0f, true);

			return false;
		}

		{
			FString PerfMessage = FString::Printf(TEXT("ConstructMessageTagTree MessageTag tables after adding new tag"));
			CONDITIONAL_SCOPE_LOG_TIME_IN_SECONDS(false, *PerfMessage, nullptr)

			Manager.EditorRefreshMessageTagTree();
		}

		return true;
	}

	virtual bool DeleteTagFromINI(TSharedPtr<FMessageTagNode> TagNodeToDelete) override
	{
		if (!TagNodeToDelete.IsValid())
		{
			return false;
		}

		TMap<UObject*, FString> ObjectsToUpdateConfig;
		const bool bOnlyLog = false;
		bool bReturnValue = DeleteTagFromINIInternal(TagNodeToDelete, bOnlyLog, ObjectsToUpdateConfig);
		if (ObjectsToUpdateConfig.Num() > 0)
		{
			UpdateTagSourcesAfterDelete(bOnlyLog, ObjectsToUpdateConfig);

			// This invalidates all local variables, need to return right away
			UMessageTagsManager::Get().EditorRefreshMessageTagTree();
		}
		return bReturnValue;
	}

	virtual void DeleteTagsFromINI(const TArray<TSharedPtr<FMessageTagNode>>& TagNodesToDelete) override
	{
		TMap<UObject*, FString> ObjectsToUpdateConfig;
		const bool bOnlyLog = true;

		{
			FScopedSlowTask SlowTask(TagNodesToDelete.Num(), LOCTEXT("RemovingTags", "Removing Tags"));
			for (const TSharedPtr<FMessageTagNode>& TagNodeToDelete : TagNodesToDelete)
			{
				if (ensure(!TagNodeToDelete->GetCompleteTagName().IsNone()))
				{
					SlowTask.EnterProgressFrame();
					if (TagNodeToDelete.IsValid())
					{
						DeleteTagFromINIInternal(TagNodeToDelete, bOnlyLog, ObjectsToUpdateConfig);
					}
					ensureMsgf(!TagNodeToDelete->GetCompleteTagName().IsNone(),
							   TEXT("A 'None' tag here implies somone may have added a EditorRefreshMessageTagTree() call in DeleteTagFromINI. Do not do this, the refresh must happen after the bulk operation is done."));
				}
			}
		}

		if (ObjectsToUpdateConfig.Num() > 0)
		{
			UpdateTagSourcesAfterDelete(bOnlyLog, ObjectsToUpdateConfig);

			UMessageTagsManager& Manager = UMessageTagsManager::Get();
			Manager.EditorRefreshMessageTagTree();
		}
	}

	void RemoveINIImpl(FName InTagName, bool bIncludeRestricted = false)
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		auto TagNode = Manager.FindTagNode(InTagName);
		if (!TagNode.IsValid())
			return;
		for (auto& TagSourceName : TagNode->GetAllSourceNames())
		{
			if (TagSourceName == FMessageTagSource::GetNativeName())
				continue;

			auto TagSource = Manager.FindTagSource(TagSourceName);
			if (!TagSource)
				continue;
			if (bIncludeRestricted && TagSource->SourceRestrictedTagList)
			{
				const FString& ConfigFileName = TagSource->SourceRestrictedTagList->ConfigFileName;
				int32 TagListSize = TagSource->SourceRestrictedTagList->RestrictedMessageTagList.Num();

				for (int32 i = TagListSize - 1; i >= 0; i--)
				{
					if (TagSource->SourceRestrictedTagList->RestrictedMessageTagList[i].Tag == InTagName)
					{
						TagSource->SourceRestrictedTagList->RestrictedMessageTagList.RemoveAt(i);
						MessageTagsUpdateSourceControl(ConfigFileName);
#if UE_5_00_OR_LATER
						TagSource->SourceRestrictedTagList->TryUpdateDefaultConfigFile(ConfigFileName);
#else
						TagSource->SourceRestrictedTagList->UpdateDefaultConfigFile(ConfigFileName);
#endif
					}
				}
				GConfig->LoadFile(ConfigFileName);
			}
			else if (!bIncludeRestricted && TagSource->SourceTagList)
			{
				const FString& ConfigFileName = TagSource->SourceTagList->ConfigFileName;
				int32 TagListSize = TagSource->SourceTagList->MessageTagList.Num();

				for (int32 i = TagListSize - 1; i >= 0; i--)
				{
					if (TagSource->SourceTagList->MessageTagList[i].Tag == InTagName)
					{
						TagSource->SourceTagList->MessageTagList.RemoveAt(i);
						MessageTagsUpdateSourceControl(ConfigFileName);
#if UE_5_00_OR_LATER
						TagSource->SourceTagList->TryUpdateDefaultConfigFile(ConfigFileName);
#else
						TagSource->SourceTagList->UpdateDefaultConfigFile(ConfigFileName);
#endif
					}
				}
				GConfig->LoadFile(ConfigFileName);
			}
		}
	}

	bool DeleteTagFromINIInternal(const TSharedPtr<FMessageTagNode>& TagNodeToDelete, bool bOnlyLog, TMap<UObject*, FString>& OutObjectsToUpdateConfig)
	{
		FName TagName = TagNodeToDelete->GetCompleteTagName();

		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();

		FString Comment;
		TArray<FName> TagSourceNames;
		bool bTagIsExplicit;
		bool bTagIsRestricted;
		bool bTagAllowsNonRestrictedChildren;

		if (DeleteTagRedirector(TagName, bOnlyLog, false, &OutObjectsToUpdateConfig))
		{
			return true;
		}

		if (!Manager.GetTagEditorData(TagName, Comment, TagSourceNames, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren))
		{
			ShowNotification(FText::Format(LOCTEXT("RemoveTagFailureNoTag", "Cannot delete tag {0}, does not exist!"), FText::FromName(TagName)), 10.0f, true, bOnlyLog);
			return false;
		}

		ensure(bTagIsRestricted == TagNodeToDelete->IsRestrictedMessageTag());

		// Check if the tag is implicitly defined
		if (!bTagIsExplicit || TagSourceNames.Num() == 0)
		{
			ShowNotification(FText::Format(LOCTEXT("RemoveTagFailureNoSource", "Cannot delete tag {0} as it is implicit, remove children manually"), FText::FromName(TagName)), 10.0f, true, bOnlyLog);
			return false;
		}

		FMessageTag ActualTag = Manager.RequestMessageTag(TagName);
		FMessageTagContainer ChildTags = Manager.RequestMessageTagChildrenInDictionary(ActualTag);

		TArray<FName> TagsThatWillBeDeleted;

		TagsThatWillBeDeleted.Add(TagName);

		FMessageTag ParentTag = ActualTag.RequestDirectParent();
		while (ParentTag.IsValid() && !Manager.FindTagNode(ParentTag)->IsExplicitTag())
		{
			// See if there are more children than the one we are about to delete
			FMessageTagContainer ParentChildTags = Manager.RequestMessageTagChildrenInDictionary(ParentTag);

			ensure(ParentChildTags.HasTagExact(ActualTag));
			if (ParentChildTags.Num() == 1)
			{
				// This is the only tag, add to deleted list
				TagsThatWillBeDeleted.Add(ParentTag.GetTagName());
				ParentTag = ParentTag.RequestDirectParent();
			}
			else
			{
				break;
			}
		}

		for (FName TagNameToDelete : TagsThatWillBeDeleted)
		{
			// Verify references
			FAssetIdentifier TagId = FAssetIdentifier(FMessageTag::StaticStruct(), TagNameToDelete);
			TArray<FAssetIdentifier> Referencers;

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			AssetRegistryModule.Get().GetReferencers(TagId, Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);

			if (Referencers.Num() > 0)
			{
				ShowNotification(
					FText::Format(LOCTEXT("RemoveTagFailureBadSource_Referenced", "Cannot delete tag {0}, still referenced by {1} and possibly others"), FText::FromName(TagNameToDelete), FText::FromString(Referencers[0].ToString())),
					10.0f,
					true,
					bOnlyLog);

				return false;
			}
		}

		bool bRemovedAny = false;
		for (const FName& TagSourceName : TagSourceNames)
		{
			const FMessageTagSource* TagSource = Manager.FindTagSource(TagSourceName);

			if (bTagIsRestricted && !TagSource->SourceRestrictedTagList)
			{
				ShowNotification(FText::Format(LOCTEXT("RemoveTagFailureBadSource", "Cannot delete tag {0} from source {1}, remove manually"), FText::FromName(TagName), FText::FromName(TagSourceName)), 10.0f, true, bOnlyLog);
				continue;
			}

			if (!bTagIsRestricted && !TagSource->SourceTagList)
			{
				ShowNotification(FText::Format(LOCTEXT("RemoveTagFailureBadSource", "Cannot delete tag {0} from source {1}, remove manually"), FText::FromName(TagName), FText::FromName(TagSourceName)), 10.0f, true, bOnlyLog);
				continue;
			}

			// Passed, delete and save
			const FString& ConfigFileName = bTagIsRestricted ? TagSource->SourceRestrictedTagList->ConfigFileName : TagSource->SourceTagList->ConfigFileName;

			int32 NumRemoved = 0;
			if (bTagIsRestricted)
			{
				NumRemoved = TagSource->SourceRestrictedTagList->RestrictedMessageTagList.RemoveAll([&TagName](const FRestrictedMessageTagTableRow& Row) { return Row.Tag == TagName; });
				if (NumRemoved > 0)
				{
					OutObjectsToUpdateConfig.Add(TagSource->SourceRestrictedTagList, ConfigFileName);
				}
			}
			else
			{
				NumRemoved = TagSource->SourceTagList->MessageTagList.RemoveAll([&TagName](const FMessageTagTableRow& Row) { return Row.Tag == TagName; });
				if (NumRemoved > 0)
				{
					OutObjectsToUpdateConfig.Add(TagSource->SourceTagList, ConfigFileName);
				}
			}

			if (NumRemoved > 0)
			{
				// See if we still live due to child tags
				if (ChildTags.Num() > 0)
				{
					ShowNotification(FText::Format(LOCTEXT("RemoveTagChildrenExist", "Deleted explicit tag {0}, still exists implicitly due to children"), FText::FromName(TagName)), 5.0f, false, bOnlyLog);
				}
				else
				{
					ShowNotification(FText::Format(LOCTEXT("RemoveTag", "Deleted tag {0}"), FText::FromName(TagName)), 5.0f, false, bOnlyLog);
				}

				bRemovedAny = true;
			}
		}

		if (!bRemovedAny)
		{
			ShowNotification(FText::Format(LOCTEXT("RemoveTagFailureNoTag", "Cannot delete tag {0}, does not exist!"), FText::FromName(TagName)), 10.0f, true, bOnlyLog);
		}

		return bRemovedAny;
	}

	void UpdateTagSourcesAfterDelete(bool bOnlyLog, const TMap<UObject*, FString>& ObjectsToUpdateConfig)
	{
		TSet<FString> ConfigFileNames;
		for (auto ObjectToUpdateIt = ObjectsToUpdateConfig.CreateConstIterator(); ObjectToUpdateIt; ++ObjectToUpdateIt)
		{
			ConfigFileNames.Add(ObjectToUpdateIt.Value());
		}

		FScopedSlowTask SlowTask(ObjectsToUpdateConfig.Num() + ConfigFileNames.Num(), LOCTEXT("UpdateTagSourcesAfterDelete", "Updating Tag Sources"));

		MessageTagsUpdateSourceControl(ConfigFileNames.Array(), bOnlyLog);

		for (auto ObjectToUpdateIt = ObjectsToUpdateConfig.CreateConstIterator(); ObjectToUpdateIt; ++ObjectToUpdateIt)
		{
			SlowTask.EnterProgressFrame();
			check(ObjectToUpdateIt.Key());
			ObjectToUpdateIt.Key()->TryUpdateDefaultConfigFile(ObjectToUpdateIt.Value());
		}

		for (const FString& ConfigFileName : ConfigFileNames)
		{
			SlowTask.EnterProgressFrame();
			GConfig->LoadFile(ConfigFileName);
		}
	}

	virtual bool UpdateTagInINI(const FString& TagToUpdate, const FString& Comment, bool bIsRestrictedTag, bool bAllowNonRestrictedChildren) override
	{
		FName TagName = FName(*TagToUpdate);

		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();

		FString OldComment;
		FName TagSourceName;
		bool bTagIsExplicit;
		bool bTagWasRestricted;
		bool bTagDidAllowNonRestrictedChildren;

		bool bSuccess = false;

		if (Manager.GetTagEditorData(TagName, OldComment, TagSourceName, bTagIsExplicit, bTagWasRestricted, bTagDidAllowNonRestrictedChildren))
		{
			if (const FMessageTagSource* TagSource = Manager.FindTagSource(TagSourceName))
			{
				// if we're disallowing non-restricted children make sure we don't already have some
				if (bTagDidAllowNonRestrictedChildren && !bAllowNonRestrictedChildren)
				{
					FMessageTag ActualTag = Manager.RequestMessageTag(TagName);
					FMessageTagContainer ChildTags = Manager.RequestMessageTagDirectDescendantsInDictionary(ActualTag, EMessageTagSelectionType::NonRestrictedOnly);
					if (!ChildTags.IsEmpty())
					{
						ShowNotification(LOCTEXT("ToggleAllowNonRestrictedChildrenFailure", "Cannot prevent non-restricted children since some already exist! Delete them first."), 10.0f, true);
						return false;
					}
				}

				UObject* TagListObj = nullptr;
				FString ConfigFileName;

				if (bIsRestrictedTag && TagSource->SourceRestrictedTagList)
				{
					URestrictedMessageTagsList* RestrictedTagList = TagSource->SourceRestrictedTagList;
					TagListObj = RestrictedTagList;
					ConfigFileName = RestrictedTagList->ConfigFileName;

					for (int32 i = 0; i < RestrictedTagList->RestrictedMessageTagList.Num(); i++)
					{
						if (RestrictedTagList->RestrictedMessageTagList[i].Tag == TagName)
						{
							RestrictedTagList->RestrictedMessageTagList[i].bAllowNonRestrictedChildren = bAllowNonRestrictedChildren;
							bSuccess = true;
							break;
						}
					}
				}

				if (bSuccess)
				{
					// Check source control before and after writing, to make sure it gets created or checked out
					MessageTagsUpdateSourceControl(ConfigFileName);
#if UE_5_00_OR_LATER
					TagListObj->TryUpdateDefaultConfigFile(ConfigFileName);
#else
					TagListObj->UpdateDefaultConfigFile(ConfigFileName);
#endif
					// MessageTagsUpdateSourceControl(ConfigFileName);

					GConfig->LoadFile(ConfigFileName);
				}
			}
		}

		return bSuccess;
	}

	virtual bool RenameTagInINI(const FString& TagToRename, const FString& TagToRenameTo, const TArray<FMessageParameter>& Parameters, const TArray<FMessageParameter>& ResponseTypes) override
	{
		FName OldTagName = FName(*TagToRename);
		FName NewTagName = FName(*TagToRenameTo);

		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();

		FString OldComment, NewComment;
		FName OldTagSourceName, NewTagSourceName;
		bool bTagIsExplicit;
		bool bTagIsRestricted;
		bool bTagAllowsNonRestrictedChildren;

		// Delete existing redirector
		DeleteTagRedirector(NewTagName);
		DeleteTagRedirector(OldTagName);

		const FMessageTagSource* OldTagSource = nullptr;
		if (Manager.GetTagEditorData(OldTagName, OldComment, OldTagSourceName, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren))
		{
			// Add new tag if needed
			if (!Manager.GetTagEditorData(NewTagName, NewComment, NewTagSourceName, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren))
			{
				if (!AddNewMessageTagToINI(TagToRenameTo, OldComment, OldTagSourceName, bTagIsRestricted, bTagAllowsNonRestrictedChildren, Parameters, ResponseTypes))
				{
					// Failed to add new tag, so fail
					return false;
				}
			}

			// Delete old tag if possible, still make redirector if this fails
			OldTagSource = Manager.FindTagSource(OldTagSourceName);
			if (OldTagSource && OldTagSource->SourceTagList)
			{
				UMessageTagsList* TagList = OldTagSource->SourceTagList;

				for (int32 i = 0; i < TagList->MessageTagList.Num(); i++)
				{
					if (TagList->MessageTagList[i].Tag == OldTagName)
					{
						if (OldTagName != NewTagName)
						{
							TagList->MessageTagList.RemoveAt(i);
						}
						else
						{
							TagList->MessageTagList[i].Parameters = Parameters;
							TagList->MessageTagList[i].ResponseTypes = ResponseTypes;
						}

#if UE_5_00_OR_LATER
						TagList->TryUpdateDefaultConfigFile(TagList->ConfigFileName);
#else
						TagList->UpdateDefaultConfigFile(TagList->ConfigFileName);
#endif
						MessageTagsUpdateSourceControl(TagList->ConfigFileName);
						GConfig->LoadFile(TagList->ConfigFileName);

						break;
					}
				}
			}
			else
			{
				ShowNotification(FText::Format(LOCTEXT("RenameFailure", "Tag {0} redirector was created but original tag was not destroyed as it has children"), FText::FromString(TagToRename)), 10.0f, true);
			}
		}

		if (OldTagName != NewTagName)
		{
			// Add redirector no matter what
			FMessageTagRedirect Redirect;
			Redirect.OldTagName = OldTagName;
			Redirect.NewTagName = NewTagName;

			UMessageTagsList* ListToUpdate = (OldTagSource && OldTagSource->SourceTagList) ? OldTagSource->SourceTagList : GetMutableDefault<UMessageTagsSettings>();
			check(ListToUpdate);

			Settings->MessageTagRedirects.AddUnique(Redirect);

			MessageTagsUpdateSourceControl(Settings->GetDefaultConfigFilename());
#if UE_5_00_OR_LATER
			Settings->TryUpdateDefaultConfigFile();
#else
			Settings->UpdateDefaultConfigFile();
#endif
			GConfig->LoadFile(Settings->GetDefaultConfigFilename());

			ShowNotification(FText::Format(LOCTEXT("AddTagRedirect", "Renamed tag {0} to {1}"), FText::FromString(TagToRename), FText::FromString(TagToRenameTo)), 3.0f);
		}
		Manager.EditorRefreshMessageTagTree();

		UMessageTagsManager::OnMessageTagSignatureChanged().Broadcast(OldTagName, OldTagName != NewTagName);
		//	if (OldTagName != NewTagName)
		//		WarnAboutRestart();

		return true;
	}
	virtual bool MoveTagsBetweenINI(const TArray<FString>& TagsToMove, const FName& TargetTagSource, TArray<FString>& OutTagsMoved, TArray<FString>& OutFailedToMoveTags) override
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		// Find and check out the destination .ini file
		FMessageTagSource* NewTagSource = Manager.FindTagSource(TargetTagSource);
		if (NewTagSource == nullptr)
		{
			ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_UnknownTarget", "Failed to move tags as target {0} could not be found"), FText::FromName(TargetTagSource)), 10.0f, true);
			return false;
		}

		if (NewTagSource->SourceType != EMessageTagSourceType::DefaultTagList && NewTagSource->SourceType != EMessageTagSourceType::TagList)
		{
			ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_UnsupportedTarget", "Invalid target source `{0}`! Tags can only be moved to DefaultTagList and TagList target sources."), FText::FromName(TargetTagSource)), 10.0f, true);
			return false;
		}

		ensure(NewTagSource->SourceTagList);

		// Tracking which lists are modified for bulk operations (checkout, config file update/reload).
		TSet<UMessageTagsList*> ModifiedTagsList;

		// For each Message tag, remove it from the current MessageTagList and add it to the destination
		for (const FString& TagToMove : TagsToMove)
		{
			FString Comment;
			TArray<FName> TagSourceNames;
			bool bIsTagExplicit, bIsRestrictedTag, bAllowNonRestrictedChildren;
			Manager.GetTagEditorData(FName(TagToMove), Comment, TagSourceNames, bIsTagExplicit, bIsRestrictedTag, bAllowNonRestrictedChildren);

			if (bIsRestrictedTag)
			{
				ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_RestrictedTag", "Restriced Tag {0} cannot be moved"), FText::FromString(TagToMove)), 10.0f, true);
				OutFailedToMoveTags.Add(TagToMove);
				continue;
			}

			if (TagSourceNames.IsEmpty())
			{
				ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_UnknownSource", "Tag {0} sources could not be found"), FText::FromString(TagToMove)), 10.0f, true);
				OutFailedToMoveTags.Add(TagToMove);
				continue;
			}
			else if (TagSourceNames.Num() > 1)
			{
				UE_LOG(LogMessageTags, Display, TEXT("%d tag sources found for tag %s. Moving first found ini source only! (DefaultTagList or TagList)"), TagSourceNames.Num(), *TagToMove);
			}

			// Move the first found .ini tag list only
			FMessageTagSource* OldTagSource = nullptr;
			for (FName TagSourceName : TagSourceNames)
			{
				FMessageTagSource* TagSource = Manager.FindTagSource(TagSourceName);
				if (TagSource != nullptr && (TagSource->SourceType == EMessageTagSourceType::DefaultTagList || TagSource->SourceType == EMessageTagSourceType::TagList))
				{
					OldTagSource = TagSource;
					break;
				}
			}

			if (OldTagSource == nullptr)
			{
				ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_InvalidSource", "Invalid source for `{0}`! Tags can only be moved from DefaultTagList and TagList sources."), FText::FromString(TagToMove)), 10.0f, true);
				OutFailedToMoveTags.Add(TagToMove);
				continue;
			}

			ensure(OldTagSource->SourceTagList);

			bool bFound = false;
			FName TagToMoveName(TagToMove);

			// Remove from the old tag source
			TArray<FMessageTagTableRow>& MessageTagList = OldTagSource->SourceTagList->MessageTagList;
			for (int32 Index = 0; Index < MessageTagList.Num(); ++Index)
			{
				if (MessageTagList[Index].Tag == TagToMoveName)
				{
					MessageTagList.RemoveAt(Index);
					ModifiedTagsList.Add(OldTagSource->SourceTagList);
					bFound = true;
					break;
				}
			}

			if (bFound)
			{
				// Add to the new tag source
				NewTagSource->SourceTagList->MessageTagList.AddUnique(FMessageTagTableRow(FName(TagToMove), Comment));
				ModifiedTagsList.Add(NewTagSource->SourceTagList);

				OutTagsMoved.Add(TagToMove);
			}
			else
			{
				ShowNotification(FText::Format(LOCTEXT("MoveTagsFailure_TagNotFound", "Tag {0} could not be found in the source tag list {0}"), FText::FromString(TagToMove), FText::FromString(OldTagSource->SourceTagList->ConfigFileName)),
								 10.0f,
								 true);
				OutFailedToMoveTags.Add(TagToMove);
			}
		}

		// Update all modified tags list
		for (UMessageTagsList* TagsList : ModifiedTagsList)
		{
			MessageTagsUpdateSourceControl(TagsList->ConfigFileName);

			TagsList->SortTags();
			TagsList->TryUpdateDefaultConfigFile(TagsList->ConfigFileName);

			GConfig->LoadFile(TagsList->ConfigFileName);
		};

		// Refresh editor
		Manager.EditorRefreshMessageTagTree();

		return true;
	}

	virtual bool AddTransientEditorMessageTag(const FString& NewTransientTag) override
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		if (NewTransientTag.IsEmpty())
		{
			return false;
		}

		Manager.TransientEditorTags.Add(*NewTransientTag);

		{
			FString PerfMessage = FString::Printf(TEXT("ConstructMessageTagTree MessageTag tables after adding new transient tag"));
			SCOPE_LOG_TIME_IN_SECONDS(*PerfMessage, nullptr)

			Manager.EditorRefreshMessageTagTree();
		}

		return true;
	}

	virtual bool AddNewMessageTagSource(const FString& NewTagSource, const FString& RootDirToUse = FString()) override
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		if (NewTagSource.IsEmpty())
		{
			return false;
		}

		// Tag lists should always end with .ini
		FName TagSourceName;
		if (NewTagSource.EndsWith(TEXT(".ini")))
		{
			TagSourceName = FName(*NewTagSource);
		}
		else
		{
			TagSourceName = FName(*(NewTagSource + TEXT(".ini")));
		}

		Manager.FindOrAddTagSource(TagSourceName, EMessageTagSourceType::TagList, RootDirToUse);

		ShowNotification(FText::Format(LOCTEXT("AddTagSource", "Added {0} as a source for saving new tags"), FText::FromName(TagSourceName)), 3.0f);

		IMessageTagsModule::OnTagSettingsChanged.Broadcast();

		return true;
	}

	TSharedRef<SWidget> MakeMessageTagContainerWidget(FOnSetMessageTagContainer OnSetTag, TSharedPtr<FMessageTagContainer> MessageyTagContainer, const FString& FilterString) override
	{
		if (!MessageyTagContainer.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		TArray<FMessageTagContainer> EditableContainers;
		EditableContainers.Emplace(*MessageyTagContainer);

		SMessageTagPicker::FOnTagChanged OnChanged = SMessageTagPicker::FOnTagChanged::CreateLambda([OnSetTag, MessageyTagContainer](const TArray<FMessageTagContainer>& TagContainers) {
			if (TagContainers.Num() > 0)
			{
				*MessageyTagContainer.Get() = TagContainers[0];
				OnSetTag.Execute(*MessageyTagContainer.Get());
			}
		});

		return SNew(SMessageTagPicker)
				.TagContainers(EditableContainers)
				.Filter(FilterString)
				.ReadOnly(false)
				.MultiSelect(true)
				.OnTagChanged(OnChanged);
	}

	TSharedRef<SWidget> MakeMessageTagWidget(FOnSetMessageTag OnSetTag, TSharedPtr<FMessageTag> MessageTag, const FString& FilterString) override
	{
		if (!MessageTag.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		TArray<FMessageTagContainer> EditableContainers;
		EditableContainers.Emplace(*MessageTag);

		SMessageTagPicker::FOnTagChanged OnChanged = SMessageTagPicker::FOnTagChanged::CreateLambda([OnSetTag, MessageTag](const TArray<FMessageTagContainer>& TagContainers) {
			if (TagContainers.Num() > 0)
			{
				*MessageTag.Get() = TagContainers[0].First();
				OnSetTag.Execute(*MessageTag.Get());
			}
		});

		return SNew(SMessageTagPicker)
				.TagContainers(EditableContainers)
				.Filter(FilterString)
				.ReadOnly(false)
				.MultiSelect(false)
				.OnTagChanged(OnChanged);
	}

	void GetUnusedMessageTags(TArray<TSharedPtr<FMessageTagNode>>& OutUnusedTags) override
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		int32 NumUsedExplicitTags = 0;
		TSet<FString> AllConfigValues;
		TMultiMap<FName, FName> RerverseRedirectorMap;

		// Populate all config values from all config files so that we can check if any config contains a reference to a tag later
		{
			TArray<FString> AllConfigFilenames;
			GConfig->GetConfigFilenames(AllConfigFilenames);

			for (const FString& ConfigFilename : AllConfigFilenames)
			{
				if (FConfigFile* ConfigFile = GConfig->FindConfigFile(ConfigFilename))
				{
					for (const TPair<FString, FConfigSection>& FileIt : AsConst(*ConfigFile))
					{
						const FString& SectionName = FileIt.Key;

						// Do not include sections that define the tags, as we don't wan't their definition showing up as a reference
						if (SectionName != TEXT("/Script/MessageTags.MessageTagsSettings") && SectionName != TEXT("/Script/MessageTags.MessageTagsList"))
						{
							for (const TPair<FName, FConfigValue>& SectionIt : FileIt.Value)
							{
								const FString& ConfigValue = SectionIt.Value.GetValue();

								// Cut down on values by skipping pure numbers
								if (!ConfigValue.IsNumeric())
								{
									AllConfigValues.Add(ConfigValue);
								}
							}
						}
					}
				}
			}
		}

		// Build a reverse map of tag redirectors so we can find the old names of a given tag to see if any of them are still referenced
		UMessageTagsSettings* Settings = GetMutableDefault<UMessageTagsSettings>();
		for (const FMessageTagRedirect& Redirect : Settings->MessageTagRedirects)
		{
			RerverseRedirectorMap.Add(Redirect.NewTagName, Redirect.OldTagName);
		}

		FScopedSlowTask SlowTask((float)Manager.GetNumMessageTagNodes(), LOCTEXT("PopulatingUnusedTags", "Populating Unused Tags"));

		// Function to determine if a single node is referenced by content
		auto IsNodeUsed = [&AssetRegistry, &AllConfigValues, &RerverseRedirectorMap](const TSharedPtr<FMessageTagNode>& Node) -> bool {
			// Look for references to the input tag or any of its old names if there were redirectors
			FName InitialTagName = Node->GetCompleteTagName();
			TArray<FName> TagsToCheck = {InitialTagName};
			RerverseRedirectorMap.MultiFind(InitialTagName, TagsToCheck);

			for (const FName& TagName : TagsToCheck)
			{
				// Look for asset references
				FAssetIdentifier TagId = FAssetIdentifier(FMessageTag::StaticStruct(), TagName);
				TArray<FAssetIdentifier> Referencers;
				AssetRegistry.GetReferencers(TagId, Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
				if (Referencers.Num() != 0)
				{
					return true;
				}

				// Look for config references
				FString TagString = TagName.ToString();
				for (const FString& ConfigValue : AllConfigValues)
				{
					if (ConfigValue.Contains(TagString))
					{
						return true;
					}
				}
			}

			return false;
		};

		// Recursive function to traverse the Message Tag Node tree and find all unused tags
		TFunction<void(const TSharedPtr<FMessageTagNode>&)> RecursiveProcessTagNode;
		RecursiveProcessTagNode = [&RecursiveProcessTagNode, &NumUsedExplicitTags, &Manager, &SlowTask, &IsNodeUsed, &OutUnusedTags](const TSharedPtr<FMessageTagNode>& Node) {
			check(Node.IsValid());

			SlowTask.EnterProgressFrame();

			if (Node->IsExplicitTag())
			{
				bool bSourcesDetectable = false;
				for (const FName& SourceName : Node->GetAllSourceNames())
				{
					const FMessageTagSource* TagSource = Manager.FindTagSource(SourceName);
					if (TagSource && (TagSource->SourceType == EMessageTagSourceType::DefaultTagList || TagSource->SourceType == EMessageTagSourceType::TagList || TagSource->SourceType == EMessageTagSourceType::RestrictedTagList))
					{
						bSourcesDetectable = true;
					}
					else
					{
						bSourcesDetectable = false;
						break;
					}
				}

				if (bSourcesDetectable && !IsNodeUsed(Node))
				{
					OutUnusedTags.Add(Node);
				}
				else
				{
					NumUsedExplicitTags++;
				}
			}

			// Iterate children recursively
			const int32 NumUsedExplicitTagsBeforeChildren = NumUsedExplicitTags;
			const int32 NumUnusedExplicitTagsBeforeChildren = OutUnusedTags.Num();
			const TArray<TSharedPtr<FMessageTagNode>>& ChildNodes = Node->GetChildTagNodes();
			for (const TSharedPtr<FMessageTagNode>& ChildNode : ChildNodes)
			{
				RecursiveProcessTagNode(ChildNode);
			}

			// Implicit tags need at least one explicit child in order to exist.
			// If an implicit tag is referenced, treat the last explicit child as being referenced too
			const bool bAtLeastOneUsedExplicitChild = NumUsedExplicitTags > NumUsedExplicitTagsBeforeChildren;
			const bool bAtLeastOneUnusedExplicitChild = OutUnusedTags.Num() > NumUnusedExplicitTagsBeforeChildren;
			if (!Node->IsExplicitTag() && !bAtLeastOneUsedExplicitChild && bAtLeastOneUnusedExplicitChild)
			{
				// This is an implicit tag that has only unused explicit children. If this tag is referenced, then remove the last unused child from the list
				if (IsNodeUsed(Node))
				{
					// This implicit tag is referenced. Remove the last unused child as this tag is using that child in order to exist.
					OutUnusedTags.RemoveAt(OutUnusedTags.Num() - 1, 1, EAllowShrinking::No);
					NumUsedExplicitTags++;
				}
			}
		};

		// Go through all root nodes to process entire tree
		TArray<TSharedPtr<FMessageTagNode>> AllRoots;
		Manager.GetFilteredMessageRootTags(TEXT(""), AllRoots);
		for (const TSharedPtr<FMessageTagNode>& Root : AllRoots)
		{
			RecursiveProcessTagNode(Root);
		}
	}

	static bool WriteCustomReport(FString FileName, TArray<FString>& FileLines)
	{
		// Has a report been generated
		bool ReportGenerated = false;

		// Ensure we have a log to write
		if (FileLines.Num())
		{
			// Create the file name
			FString FileLocation = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() + TEXT("Reports/"));
			FString FullPath = FString::Printf(TEXT("%s%s"), *FileLocation, *FileName);

			// save file
			FArchive* LogFile = IFileManager::Get().CreateFileWriter(*FullPath);

			if (LogFile != NULL)
			{
				for (int32 Index = 0; Index < FileLines.Num(); ++Index)
				{
					FString LogEntry = FString::Printf(TEXT("%s"), *FileLines[Index]) + LINE_TERMINATOR;
					LogFile->Serialize(TCHAR_TO_ANSI(*LogEntry), LogEntry.Len());
				}

				LogFile->Close();
				delete LogFile;

				// A report has been generated
				ReportGenerated = true;
			}
		}

		return ReportGenerated;
	}

	static void DumpTagList()
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		TArray<FString> ReportLines;
		TArray<FString> ReportReferencers;
		TArray<FString> ReportSources;

		ReportLines.Add(TEXT("Tag,Explicit,HasNativeSource,HasConfigSource,Reference Count,Sources Count,Comment"));
		ReportReferencers.Add(TEXT("Asset,Tag"));
		ReportSources.Add(TEXT("Source,Tag"));

		FMessageTagContainer AllTags;
		Manager.RequestAllMessageTags(AllTags, true);

		TArray<FMessageTag> ExplicitList;
		AllTags.GetMessageTagArray(ExplicitList);

		ExplicitList.Sort();

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		for (const FMessageTag& Tag : ExplicitList)
		{
			TArray<FAssetIdentifier> Referencers;
			FAssetIdentifier TagId = FAssetIdentifier(FMessageTag::StaticStruct(), Tag.GetTagName());
#if UE_4_26_OR_LATER
			auto SearchableName = UE::AssetRegistry::EDependencyCategory::SearchableName;
#else
			auto SearchableName = EAssetRegistryDependencyType::SearchableName;
#endif
			AssetRegistryModule.Get().GetReferencers(TagId, Referencers, SearchableName);

			FString Comment;
			TArray<FName> TagSources;
			bool bExplicit, bRestricted, bAllowNonRestrictedChildren;

			Manager.GetTagEditorData(Tag.GetTagName(), Comment, TagSources, bExplicit, bRestricted, bAllowNonRestrictedChildren);

			bool bHasNative = TagSources.Contains(FMessageTagSource::GetNativeName());
			bool bHasConfigIni = TagSources.Contains(FMessageTagSource::GetDefaultName());

			FString TagName = Tag.ToString();

			ReportLines.Add(FString::Printf(TEXT("%s,%s,%s,%s,%d,%d,\"%s\""),
											*TagName,
											bExplicit ? TEXT("true") : TEXT("false"),
											bHasNative ? TEXT("true") : TEXT("false"),
											bHasConfigIni ? TEXT("true") : TEXT("false"),
											Referencers.Num(),
											TagSources.Num(),
											*Comment));

			for (const FAssetIdentifier& Referencer : Referencers)
			{
				ReportReferencers.Add(FString::Printf(TEXT("%s,%s"), *Referencer.ToString(), *TagName));
			}

			for (const FName& TagSource : TagSources)
			{
				ReportSources.Add(FString::Printf(TEXT("%s,%s"), *TagSource.ToString(), *TagName));
			}
		}

		WriteCustomReport(TEXT("TagList.csv"), ReportLines);
		WriteCustomReport(TEXT("TagReferencesList.csv"), ReportReferencers);
		WriteCustomReport(TEXT("TagSourcesList.csv"), ReportSources);
	}

	FDelegateHandle AssetImportHandle;
	FDelegateHandle SettingsChangedHandle;

	FName MessageTagPackageName;
	FName MessageTagStructName;
	bool bIsRunningGame = false;
};

static FAutoConsoleCommand CVarDumpTagList(TEXT("GMP.DumpTagList"),
										   TEXT("Writes out a csvs with all tags to Reports/TagList.csv, ") TEXT("Reports/TagReferencesList.csv and Reports/TagSourcesList.csv"),
										   FConsoleCommandDelegate::CreateStatic(FMessageTagsEditorModule::DumpTagList),
										   ECVF_Cheat);

IMPLEMENT_MODULE(FMessageTagsEditorModule, MessageTagsEditor)

#undef LOCTEXT_NAMESPACE
