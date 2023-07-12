// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsManager.h"

#include "Engine/Engine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "MessageTagsModule.h"
#include "MessageTagsSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "NativeMessageTags.h"
#include "Stats/StatsMisc.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectThreadContext.h"
#include "Widgets/Notifications/SNotificationList.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ISourceControlModule.h"
#include "PropertyHandle.h"
#include "SourceControlHelpers.h"
FSimpleMulticastDelegate UMessageTagsManager::OnEditorRefreshMessageTagTree;
#endif

const FName UMessageTagsManager::NAME_Categories("Categories");
const FName UMessageTagsManager::NAME_MessageTagFilter("MessageTagFilter");

#define LOCTEXT_NAMESPACE "MessageTagManager"

namespace MessageTagUtil
{
static void GetRestrictedConfigsFromIni(const FString& IniFilePath, TArray<FRestrictedMessageCfg>& OutRestrictedConfigs)
{
	FConfigFile ConfigFile;
	ConfigFile.Read(IniFilePath);

	TArray<FString> IniConfigStrings;
	if (ConfigFile.GetArray(TEXT("/Script/MessageTags.MessageTagsSettings"), TEXT("RestrictedConfigFiles"), IniConfigStrings))
	{
		for (const FString& ConfigString : IniConfigStrings)
		{
			FRestrictedMessageCfg Config;
			if (FRestrictedMessageCfg::StaticStruct()->ImportText(*ConfigString, &Config, nullptr, PPF_None, nullptr, FRestrictedMessageCfg::StaticStruct()->GetName()))
			{
				OutRestrictedConfigs.Add(Config);
			}
		}
	}
}
}  // namespace MessageTagUtil

//////////////////////////////////////////////////////////////////////
// FMessageTagSource

static const FName NAME_Native = FName(TEXT("Native"));
static const FName NAME_DefaultMessageTagsIni("DefaultMessageTags.ini");

FString FMessageTagSource::GetConfigFileName() const
{
	if (SourceTagList)
	{
		return SourceTagList->ConfigFileName;
	}
	if (SourceRestrictedTagList)
	{
		return SourceRestrictedTagList->ConfigFileName;
	}

	return FString();
}

FName FMessageTagSource::GetNativeName()
{
	return NAME_Native;
}

FName FMessageTagSource::GetDefaultName()
{
	return NAME_DefaultMessageTagsIni;
}

#if WITH_EDITOR
static const FName NAME_TransientEditor("TransientEditor");

FName FMessageTagSource::GetFavoriteName()
{
	return GetDefault<UMessageTagsDeveloperSettings>()->FavoriteTagSource;
}

void FMessageTagSource::SetFavoriteName(FName TagSourceToFavorite)
{
	UMessageTagsDeveloperSettings* MutableSettings = GetMutableDefault<UMessageTagsDeveloperSettings>();

	if (MutableSettings->FavoriteTagSource != TagSourceToFavorite)
	{
		MutableSettings->Modify();
		MutableSettings->FavoriteTagSource = TagSourceToFavorite;

		FPropertyChangedEvent ChangeEvent(MutableSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMessageTagsDeveloperSettings, FavoriteTagSource)), EPropertyChangeType::ValueSet);
		MutableSettings->PostEditChangeProperty(ChangeEvent);

		MutableSettings->SaveConfig();
	}
}

FName FMessageTagSource::GetTransientEditorName()
{
	return NAME_TransientEditor;
}
#endif

//////////////////////////////////////////////////////////////////////
// UMessageTagsManager

UMessageTagsManager* UMessageTagsManager::SingletonManager = nullptr;
#define MessageTagsFolder TEXT("MsgTags")
#define MessageTagsPathFmt TEXT("%s") MessageTagsFolder TEXT("/%s")

UMessageTagsManager::UMessageTagsManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseFastReplication = false;
	bShouldWarnOnInvalidTags = true;
	bDoneAddingNativeTags = false;
	NetIndexFirstBitSegment = 16;
	NetIndexTrueBitNum = 16;
	NumBitsForContainerSize = 6;
	NetworkMessageTagNodeIndexHash = 0;
}

// Enable to turn on detailed startup logging
#define MESSAGETAGS_VERBOSE 0

#if STATS && MESSAGETAGS_VERBOSE
#define SCOPE_LOG_MESSAGETAGS(Name) SCOPE_LOG_TIME_IN_SECONDS(Name, nullptr)
#else
#define SCOPE_LOG_MESSAGETAGS(Name)
#endif

void UMessageTagsManager::LoadMessageTagTables(bool bAllowAsyncLoad)
{
	UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();
	MessageTagTables.Empty();

#if !WITH_EDITOR
	// If we're a cooked build and in a safe spot, start an async load so we can pipeline it
	if (bAllowAsyncLoad && !IsLoading() && MutableDefault->MessageTagTableList.Num() > 0)
	{
		for (FSoftObjectPath DataTablePath : MutableDefault->MessageTagTableList)
		{
			LoadPackageAsync(DataTablePath.GetLongPackageName());
		}

		return;
	}
#endif  // !WITH_EDITOR

	SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::LoadMessageTagTables"));
	for (FSoftObjectPath DataTablePath : MutableDefault->MessageTagTableList)
	{
		UDataTable* TagTable = LoadObject<UDataTable>(nullptr, *DataTablePath.ToString(), nullptr, LOAD_None, nullptr);

		// Handle case where the module is dynamically-loaded within a LoadPackage stack, which would otherwise
		// result in the tag table not having its RowStruct serialized in time. Without the RowStruct, the tags manager
		// will not be initialized correctly.
		if (TagTable)
		{
			FLinkerLoad* TagLinker = TagTable->GetLinker();
			if (TagLinker)
			{
				TagTable->GetLinker()->Preload(TagTable);
			}
		}
		MessageTagTables.Add(TagTable);
	}
}

struct FCompareFMessageTagNodeByTag
{
	FORCEINLINE bool operator()(const TSharedPtr<FMessageTagNode>& A, const TSharedPtr<FMessageTagNode>& B) const
	{
		// Note: GetSimpleTagName() is not good enough here. The individual tag nodes are share frequently (E.g, Dog.Tail, Cat.Tail have sub nodes with the same simple tag name)
		// Compare with equal FNames will look at the backing number/indice to the FName. For FNames used elsewhere, like "A" for example, this can cause non determinism in platforms
		// (For example if static order initialization differs on two platforms, the "version" of the "A" FName that two places get could be different, causing this comparison to also be)
		return (A->GetCompleteTagName().Compare(B->GetCompleteTagName())) < 0;
	}
};

void UMessageTagsManager::AddTagIniSearchPath(const FString& RootDir)
{
	// SCOPE_SECONDS_ACCUMULATOR(STAT_MessageTags_AddTagIniSearchPath);

	FMessageTagSearchPathInfo* PathInfo = RegisteredSearchPaths.Find(RootDir);

	if (!PathInfo)
	{
		PathInfo = &RegisteredSearchPaths.FindOrAdd(RootDir);
	}

	if (!PathInfo->bWasSearched)
	{
		PathInfo->Reset();

		// Read all tags from the ini
		TArray<FString> FilesInDirectory;
		IFileManager::Get().FindFilesRecursive(FilesInDirectory, *RootDir, TEXT("*.ini"), true, false);

		if (FilesInDirectory.Num() > 0)
		{
			FilesInDirectory.Sort();

			for (const FString& IniFilePath : FilesInDirectory)
			{
				const FName TagSource = FName(*FPaths::GetCleanFilename(IniFilePath));
				PathInfo->SourcesInPath.Add(TagSource);
				PathInfo->TagIniList.Add(IniFilePath);
			}
		}

		PathInfo->bWasSearched = true;
	}

	if (!PathInfo->bWasAddedToTree)
	{
#if WITH_EDITOR && 0
		EditorRefreshMessageTagTree();
#else
		for (const FString& IniFilePath : PathInfo->TagIniList)
		{
			TArray<FRestrictedMessageCfg> IniRestrictedConfigs;
			MessageTagUtil::GetRestrictedConfigsFromIni(IniFilePath, IniRestrictedConfigs);
			const FString IniDirectory = FPaths::GetPath(IniFilePath);
			for (const FRestrictedMessageCfg& Config : IniRestrictedConfigs)
			{
				const FString RestrictedFileName = FString::Printf(TEXT("%s/%s"), *IniDirectory, *Config.RestrictedConfigName);
				AddRestrictedMessageTagSource(RestrictedFileName);
			}
		}

		AddTagsFromAdditionalLooseIniFiles(PathInfo->TagIniList);

		PathInfo->bWasAddedToTree = true;

		if (!bIsConstructingMessageTagTree)
		{
			InvalidateNetworkIndex();
			IMessageTagsModule::OnMessageTagTreeChanged.Broadcast();
			SyncToGMPMeta();
		}
#endif
	}
}

void UMessageTagsManager::GetTagSourceSearchPaths(TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	RegisteredSearchPaths.GenerateKeyArray(OutPaths);
}

void UMessageTagsManager::AddRestrictedMessageTagSource(const FString& FileName)
{
	FName TagSource = FName(*FPaths::GetCleanFilename(FileName));
	if (TagSource == NAME_None)
	{
		return;
	}

	if (RestrictedMessageTagSourceNames.Contains(TagSource))
	{
		// Was already added on this pass
		return;
	}

	RestrictedMessageTagSourceNames.Add(TagSource);
	FMessageTagSource* FoundSource = FindOrAddTagSource(TagSource, EMessageTagSourceType::RestrictedTagList);

	// Make sure we have regular tag sources to match the restricted tag sources but don't try to read any tags from them yet.
	FindOrAddTagSource(TagSource, EMessageTagSourceType::TagList);

	if (FoundSource && FoundSource->SourceRestrictedTagList)
	{
		FoundSource->SourceRestrictedTagList->LoadConfig(URestrictedMessageTagsList::StaticClass(), *FileName);

#if WITH_EDITOR
		if (GIsEditor || IsRunningCommandlet())  // Sort tags for UI Purposes but don't sort in -game scenario since this would break compat with noneditor cooked builds
		{
			FoundSource->SourceRestrictedTagList->SortTags();
		}
#endif
		for (const FRestrictedMessageTagTableRow& TableRow : FoundSource->SourceRestrictedTagList->RestrictedMessageTagList)
		{
			AddTagTableRow(TableRow, TagSource, true, TableRow.bAllowNonRestrictedChildren);
		}
	}
}

void UMessageTagsManager::AddTagsFromAdditionalLooseIniFiles(const TArray<FString>& IniFileList)
{
	// Read all tags from the ini
	for (const FString& IniFilePath : IniFileList)
	{
		const FName TagSource = FName(*FPaths::GetCleanFilename(IniFilePath));

		// skip the restricted tag files
		if (RestrictedMessageTagSourceNames.Contains(TagSource))
		{
			continue;
		}

		FMessageTagSource* FoundSource = FindOrAddTagSource(TagSource, EMessageTagSourceType::TagList);

		UE_CLOG(MESSAGETAGS_VERBOSE, LogMessageTags, Display, TEXT("Loading Tag File: %s"), *IniFilePath);

		if (FoundSource && FoundSource->SourceTagList)
		{
			FoundSource->SourceTagList->ConfigFileName = IniFilePath;

			// Check deprecated locations
			TArray<FString> Tags;
			if (GConfig->GetArray(TEXT("UserTags"), TEXT("MessageTags"), Tags, IniFilePath))
			{
				for (const FString& Tag : Tags)
				{
					FoundSource->SourceTagList->MessageTagList.AddUnique(FMessageTagTableRow(FName(*Tag)));
				}
			}
			else
			{
				// Load from new ini
				FoundSource->SourceTagList->LoadConfig(UMessageTagsList::StaticClass(), *IniFilePath);
			}

#if WITH_EDITOR
			if (GIsEditor || IsRunningCommandlet())  // Sort tags for UI Purposes but don't sort in -game scenario since this would break compat with noneditor cooked builds
			{
				FoundSource->SourceTagList->SortTags();
			}
#endif

			for (const FMessageTagTableRow& TableRow : FoundSource->SourceTagList->MessageTagList)
			{
				AddTagTableRow(TableRow, TagSource);
			}
		}
	}
}

void UMessageTagsManager::ConstructMessageTagTree()
{
	SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree"));
	TGuardValue<bool> GuardRebuilding(bIsConstructingMessageTagTree, true);
	if (!MessageRootTag.IsValid())
	{
		MessageRootTag = MakeShareable(new FMessageTagNode());

		UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();

		// Copy invalid characters, then add internal ones
		InvalidTagCharacters = MutableDefault->InvalidTagCharacters;
		InvalidTagCharacters.Append(TEXT("\r\n\t"));

		// Add prefixes first
		if (ShouldImportTagsFromINI())
		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: ImportINI prefixes"));

			TArray<FString> RestrictedMessageTagFiles;
			GetRestrictedTagConfigFiles(RestrictedMessageTagFiles);
			RestrictedMessageTagFiles.Sort();

			for (const FString& FileName : RestrictedMessageTagFiles)
			{
				AddRestrictedMessageTagSource(FileName);
			}
		}

		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: Add native tags"));
			// Add native tags before other tags
			for (FName TagToAdd : LegacyNativeTags)
			{
				AddTagTableRow(FMessageTagTableRow(TagToAdd), FMessageTagSource::GetNativeName());
			}
#if 0
			for (const class FNativeMessageTag* NativeTag : FNativeMessageTag::GetRegisteredNativeTags())
			{
				AddTagTableRow(NativeTag->GetMessageTagTableRow(), FMessageTagSource::GetNativeName());
			}
#endif
		}

		// If we didn't load any tables it might be async loading, so load again with a flush
		if (MessageTagTables.Num() == 0)
		{
			LoadMessageTagTables(false);
		}

		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: Construct from data asset"));
			for (UDataTable* DataTable : MessageTagTables)
			{
				if (DataTable)
				{
					PopulateTreeFromDataTable(DataTable);
				}
			}
		}

		// Create native source
		FName NativeTagSource = FMessageTagSource::GetNativeName();
		FMessageTagSource* NativeSource = FindOrAddTagSource(NativeTagSource, EMessageTagSourceType::Native);
		auto NativePath = (FPaths::ProjectConfigDir() / (NativeTagSource.ToString() + TEXT("MessageTags.ini")));
		{
			auto& List = NativeSource->SourceTagList;
			if (!List)
				List = NewObject<UMessageTagsList>(this, NativeTagSource, RF_Transient);

			List->ConfigFileName = NativePath;
			List->MessageTagList.Reset();
			if (FPaths::FileExists(*NativePath))
			{
				List->LoadConfig(UMessageTagsList::StaticClass(), *NativePath);
				for (const FMessageTagTableRow& TableRow : List->MessageTagList)
				{
					AddTagTableRow(TableRow, NativeTagSource, true, true);
				}
			}
		}

		if (ShouldImportTagsFromINI())
		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: ImportINI tags"));

			TArray<FString> MessageTags;
			#if 0
			// Copy from deprecated list in DefaultEngine.ini
			GConfig->GetArray(TEXT("/Script/MessageTags.MessageTagsSettings"), TEXT("+MessageTags"), MessageTags, GEngineIni);

			for (const FString& MessageTag : MessageTags)
			{
				MutableDefault->MessageTagList.AddUnique(FMessageTagTableRow(FName(*MessageTag)));
			}
			#endif

			// Copy from deprecated list in DefaultMessageTags.ini
			MessageTags.Empty();
			GConfig->GetArray(TEXT("/Script/MessageTags.MessageTagsSettings"), TEXT("+MessageTags"), MessageTags, MutableDefault->GetDefaultConfigFilename());

			for (const FString& MessageTag : MessageTags)
			{
				MutableDefault->MessageTagList.AddUnique(FMessageTagTableRow(FName(*MessageTag)));
			}

#if WITH_EDITOR
			MutableDefault->SortTags();
#endif

			FName TagSource = FMessageTagSource::GetDefaultName();
			FMessageTagSource* DefaultSource = FindOrAddTagSource(TagSource, EMessageTagSourceType::DefaultTagList);

			for (const FMessageTagTableRow& TableRow : MutableDefault->MessageTagList)
			{
				AddTagTableRow(TableRow, TagSource);
			}

			// Make sure default config list is added
			FString DefaultPath = FPaths::ProjectConfigDir() / MessageTagsFolder;
			AddTagIniSearchPath(DefaultPath);

			// Refresh any other search paths that need it
			for (TPair<FString, FMessageTagSearchPathInfo>& Pair : RegisteredSearchPaths)
			{
				if (!Pair.Value.IsValid())
				{
					AddTagIniSearchPath(Pair.Key);
				}
			}
		}

#if WITH_EDITOR
		// Add any transient editor-only tags
		for (FName TransientTag : TransientEditorTags)
		{
			AddTagTableRow(FMessageTagTableRow(TransientTag), FMessageTagSource::GetTransientEditorName());
		}
#endif
		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: Request common tags"));
			// Grab the commonly replicated tags
			CommonlyReplicatedTags.Empty();
			for (FName TagName : MutableDefault->CommonlyReplicatedTags)
			{
				CommonlyReplicatedTags.Add(FMessageTag(TagName));

				FMessageTag Tag = RequestMessageTag(TagName);
				if (Tag.IsValid())
				{
					CommonlyReplicatedTags.Add(Tag);
				}
				else
				{
					UE_LOG(LogMessageTags, Warning, TEXT("%s was found in the CommonlyReplicatedTags list but doesn't appear to be a valid tag!"), *TagName.ToString());
				}
			}

			bUseFastReplication = MutableDefault->FastReplication;
			bShouldWarnOnInvalidTags = MutableDefault->WarnOnInvalidTags;
			bShouldClearInvalidTags = MutableDefault->ClearInvalidTags;
			NumBitsForContainerSize = MutableDefault->NumBitsForContainerSize;
			NetIndexFirstBitSegment = MutableDefault->NetIndexFirstBitSegment;
		}

		if (ShouldUseFastReplication())
		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: Reconstruct NetIndex"));
			ConstructNetIndex();
		}

		{
			SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::ConstructMessageTagTree: MessageTagTreeChangedEvent.Broadcast"));
			IMessageTagsModule::OnMessageTagTreeChanged.Broadcast();
			SyncToGMPMeta();
		}
	}
}

int32 MessagePrintNetIndiceAssignment = 0;
static FAutoConsoleVariableRef CVarMessagePrintNetIndiceAssignment(TEXT("MessageTags.PrintNetIndiceAssignment"), MessagePrintNetIndiceAssignment, TEXT("Logs MessageTag NetIndice assignment"), ECVF_Default);
void UMessageTagsManager::ConstructNetIndex()
{
	bNetworkIndexInvalidated = false;

	NetworkMessageTagNodeIndex.Empty();

	MessageTagNodeMap.GenerateValueArray(NetworkMessageTagNodeIndex);

	NetworkMessageTagNodeIndex.Sort(FCompareFMessageTagNodeByTag());

	check(CommonlyReplicatedTags.Num() <= NetworkMessageTagNodeIndex.Num());

	// Put the common indices up front
	for (int32 CommonIdx = 0; CommonIdx < CommonlyReplicatedTags.Num(); ++CommonIdx)
	{
		int32 BaseIdx = 0;
		FMessageTag& Tag = CommonlyReplicatedTags[CommonIdx];

		bool Found = false;
		for (int32 findidx = 0; findidx < NetworkMessageTagNodeIndex.Num(); ++findidx)
		{
			if (NetworkMessageTagNodeIndex[findidx]->GetCompleteTag() == Tag)
			{
				NetworkMessageTagNodeIndex.Swap(findidx, CommonIdx);
				Found = true;
				break;
			}
		}

		// A non fatal error should have been thrown when parsing the CommonlyReplicatedTags list. If we make it here, something is seriously wrong.
		checkf(Found, TEXT("Tag %s not found in NetworkMessageTagNodeIndex"), *Tag.ToString());
	}

	InvalidTagNetIndex = NetworkMessageTagNodeIndex.Num() + 1;
	NetIndexTrueBitNum = FMath::CeilToInt(FMath::Log2((float)InvalidTagNetIndex));

	// This should never be smaller than NetIndexTrueBitNum
	NetIndexFirstBitSegment = FMath::Min<int64>(NetIndexFirstBitSegment, NetIndexTrueBitNum);

	// This is now sorted and it should be the same on both client and server
	if (NetworkMessageTagNodeIndex.Num() >= INVALID_TAGNETINDEX)
	{
		ensureMsgf(false, TEXT("Too many tags in dictionary for networking! Remove tags or increase tag net index size"));

		NetworkMessageTagNodeIndex.SetNum(INVALID_TAGNETINDEX - 1);
	}

	UE_CLOG(MessagePrintNetIndiceAssignment, LogMessageTags, Display, TEXT("Assigning NetIndices to %d tags."), NetworkMessageTagNodeIndex.Num());

	NetworkMessageTagNodeIndexHash = 0;

	for (FMessageTagNetIndex i = 0; i < NetworkMessageTagNodeIndex.Num(); i++)
	{
		if (NetworkMessageTagNodeIndex[i].IsValid())
		{
			NetworkMessageTagNodeIndex[i]->NetIndex = i;

			NetworkMessageTagNodeIndexHash = FCrc::StrCrc32(*NetworkMessageTagNodeIndex[i]->GetCompleteTagString().ToLower(), NetworkMessageTagNodeIndexHash);

			UE_CLOG(MessagePrintNetIndiceAssignment, LogMessageTags, Display, TEXT("Assigning NetIndex (%d) to Tag (%s)"), i, *NetworkMessageTagNodeIndex[i]->GetCompleteTag().ToString());
		}
		else
		{
			UE_LOG(LogMessageTags, Warning, TEXT("TagNode Indice %d is invalid!"), i);
		}
	}

	UE_LOG(LogMessageTags, Log, TEXT("NetworkMessageTagNodeIndexHash is %x"), NetworkMessageTagNodeIndexHash);
}

FName UMessageTagsManager::GetTagNameFromNetIndex(FMessageTagNetIndex Index) const
{
	VerifyNetworkIndex();

	if (Index >= NetworkMessageTagNodeIndex.Num())
	{
		// Ensure Index is the invalid index. If its higher than that, then something is wrong.
		ensureMsgf(Index == InvalidTagNetIndex, TEXT("Received invalid tag net index %d! Tag index is out of sync on client!"), Index);
		return NAME_None;
	}
	return NetworkMessageTagNodeIndex[Index]->GetCompleteTagName();
}

FMessageTagNetIndex UMessageTagsManager::GetNetIndexFromTag(const FMessageTag& InTag) const
{
	VerifyNetworkIndex();

	TSharedPtr<FMessageTagNode> MessageTagNode = FindTagNode(InTag);

	if (MessageTagNode.IsValid())
	{
		return MessageTagNode->GetNetIndex();
	}

	return InvalidTagNetIndex;
}

bool UMessageTagsManager::ShouldImportTagsFromINI() const
{
	UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();

	// Deprecated path
	bool ImportFromINI = false;
	if (GConfig->GetBool(TEXT("MessageTags"), TEXT("ImportTagsFromConfig"), ImportFromINI, GEngineIni))
	{
		if (ImportFromINI)
		{
			MutableDefault->ImportTagsFromConfig = ImportFromINI;
			UE_LOG(LogMessageTags, Log, TEXT("ImportTagsFromConfig is in a deprecated location, open and save MessageTag settings to fix"));
		}
		return ImportFromINI;
	}

	return MutableDefault->ImportTagsFromConfig;
}

void UMessageTagsManager::GetRestrictedTagConfigFiles(TArray<FString>& RestrictedConfigFiles) const
{
	UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();

	if (MutableDefault)
	{
		for (const FRestrictedMessageCfg& Config : MutableDefault->RestrictedConfigFiles)
		{
			RestrictedConfigFiles.Add(FString::Printf(MessageTagsPathFmt, *FPaths::SourceConfigDir(), *Config.RestrictedConfigName));
		}
	}

	for (const TPair<FString, FMessageTagSearchPathInfo>& Pair : RegisteredSearchPaths)
	{
		for (const FString& IniFilePath : Pair.Value.TagIniList)
		{
			TArray<FRestrictedMessageCfg> IniRestrictedConfigs;
			MessageTagUtil::GetRestrictedConfigsFromIni(IniFilePath, IniRestrictedConfigs);
			for (const FRestrictedMessageCfg& Config : IniRestrictedConfigs)
			{
				RestrictedConfigFiles.Add(FString::Printf(TEXT("%s/%s"), *FPaths::GetPath(IniFilePath), *Config.RestrictedConfigName));
			}
		}
	}
}

void UMessageTagsManager::GetRestrictedTagSources(TArray<const FMessageTagSource*>& Sources) const
{
	for (const TPair<FName, FMessageTagSource>& Pair : TagSources)
	{
		if (Pair.Value.SourceType == EMessageTagSourceType::RestrictedTagList)
		{
			Sources.Add(&Pair.Value);
		}
	}
}

void UMessageTagsManager::GetOwnersForTagSource(const FString& SourceName, TArray<FString>& OutOwners) const
{
	UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();

	if (MutableDefault)
	{
		for (const FRestrictedMessageCfg& Config : MutableDefault->RestrictedConfigFiles)
		{
			if (Config.RestrictedConfigName.Equals(SourceName))
			{
				OutOwners = Config.Owners;
				return;
			}
		}
	}
}

void UMessageTagsManager::MessageTagContainerLoaded(FMessageTagContainer& Container, FProperty* SerializingProperty) const
{
	RedirectTagsForContainer(Container, SerializingProperty);

	if (OnMessageTagLoadedDelegate.IsBound())
	{
		for (const FMessageTag& Tag : Container)
		{
			OnMessageTagLoadedDelegate.Broadcast(Tag);
		}
	}
}

void UMessageTagsManager::SingleMessageTagLoaded(FMessageTag& Tag, FProperty* SerializingProperty) const
{
	RedirectSingleMessageTag(Tag, SerializingProperty);

	OnMessageTagLoadedDelegate.Broadcast(Tag);
}

void UMessageTagsManager::RedirectTagsForContainer(FMessageTagContainer& Container, FProperty* SerializingProperty) const
{
	TSet<FName> NamesToRemove;
	TSet<const FMessageTag*> TagsToAdd;

	// First populate the NamesToRemove and TagsToAdd sets by finding tags in the container that have redirects
	for (auto TagIt = Container.CreateConstIterator(); TagIt; ++TagIt)
	{
		const FName TagName = TagIt->GetTagName();
		const FMessageTag* NewTag = FMessageTagRedirectors::Get().RedirectTag(TagName);
		if (NewTag)
		{
			NamesToRemove.Add(TagName);
			if (NewTag->IsValid())
			{
				TagsToAdd.Add(NewTag);
			}
		}
#if WITH_EDITOR
		else if (SerializingProperty)
		{
			// Warn about invalid tags at load time in editor builds, too late to fix it in cooked builds
			FMessageTag OldTag = RequestMessageTag(TagName, false);
			if (!OldTag.IsValid() && ShouldWarnOnInvalidTags())
			{
#if UE_4_23_OR_LATER
				FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
				UObject* LoadingObject = LoadContext ? LoadContext->SerializedObject : nullptr;
				UE_LOG(LogMessageTags, Warning, TEXT("Invalid MessageTag %s found while loading %s in property %s."), *TagName.ToString(), *GetPathNameSafe(LoadingObject), *GetPathNameSafe(SerializingProperty));
#else
				UE_LOG(LogMessageTags, Warning, TEXT("Invalid MessageTag %s found while loading property %s."), *TagName.ToString(), *GetPathNameSafe(SerializingProperty));
#endif
				if (ShouldClearInvalidTags())
				{
					NamesToRemove.Add(TagName);
				}
			}
		}
#endif
	}

	// Remove all tags from the NamesToRemove set
	for (FName RemoveName : NamesToRemove)
	{
		Container.RemoveTag(FMessageTag(RemoveName));
	}

	// Add all tags from the TagsToAdd set
	for (const FMessageTag* AddTag : TagsToAdd)
	{
		check(AddTag);
		Container.AddTag(*AddTag);
	}
}

void UMessageTagsManager::RedirectSingleMessageTag(FMessageTag& Tag, FProperty* SerializingProperty) const
{
	const FName TagName = Tag.GetTagName();
	const FMessageTag* NewTag = FMessageTagRedirectors::Get().RedirectTag(TagName);
	if (NewTag)
	{
		if (NewTag->IsValid())
		{
			Tag = *NewTag;
		}
	}
#if WITH_EDITOR
	else if (TagName != NAME_None && SerializingProperty)
	{
		// Warn about invalid tags at load time in editor builds, too late to fix it in cooked builds
		FMessageTag OldTag = RequestMessageTag(TagName, false);
		if (!OldTag.IsValid() && ShouldWarnOnInvalidTags())
		{
#if UE_4_23_OR_LATER
			FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
			UObject* LoadingObject = LoadContext ? LoadContext->SerializedObject : nullptr;
			UE_LOG(LogMessageTags, Warning, TEXT("Invalid MessageTag %s found while loading %s in property %s."), *TagName.ToString(), *GetPathNameSafe(LoadingObject), *GetPathNameSafe(SerializingProperty));
#else
			UE_LOG(LogMessageTags, Warning, TEXT("Invalid MessageTag %s found while loading property %s."), *TagName.ToString(), *GetPathNameSafe(SerializingProperty));
#endif
			if (ShouldClearInvalidTags())
			{
				Tag.TagName = NAME_None;
			}
		}
	}
#endif
}

bool UMessageTagsManager::ImportSingleMessageTag(FMessageTag& Tag, FName ImportedTagName, bool bImportFromSerialize) const
{
	// None is always valid, no need to do any real work.
	if (ImportedTagName == NAME_None)
	{
		return true;
	}

	bool bRetVal = false;
	if (const FMessageTag* RedirectedTag = FMessageTagRedirectors::Get().RedirectTag(ImportedTagName))
	{
		Tag = *RedirectedTag;
		bRetVal = true;
	}
	else if (ValidateTagCreation(ImportedTagName))
	{
		// The tag name is valid
		Tag.TagName = ImportedTagName;
		bRetVal = true;
	}

	if (!bRetVal && bImportFromSerialize && !ImportedTagName.IsNone())
	{
#if WITH_EDITOR
		if (ShouldWarnOnInvalidTags())
		{
			FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
			UObject* LoadingObject = LoadContext ? LoadContext->SerializedObject : nullptr;
			if (LoadingObject)
			{
				// If this is a serialize with a real object and it failed to find the tag, warn about it
				UE_ASSET_LOG(LogMessageTags, Warning, *GetPathNameSafe(LoadingObject), TEXT("Invalid MessageTag %s found in object %s."), *ImportedTagName.ToString(), *LoadingObject->GetName());
			}
		}

		// Always keep invalid tags in cooked game to be consistent with properties
		if (!ShouldClearInvalidTags())
#endif
		{
			// For imported tags that are part of a serialize, leave invalid ones the same way normal serialization does to avoid data loss
			Tag.TagName = ImportedTagName;
			bRetVal = true;
		}
	}

	if (bRetVal)
	{
		OnMessageTagLoadedDelegate.Broadcast(Tag);
	}
	else
	{
		// No valid tag established in this attempt
		Tag.TagName = NAME_None;
	}

	return bRetVal;
}

namespace FGMPMetaUtils
{
GMP_API void IncVersion();
GMP_API void InsertMeta(const FName&, TArray<FName>, TArray<FName>);
GMP_API void InsertMetaPath(TFunctionRef<void(TArray<FString>&)> FuncRef);
GMP_API void SaveMetaPaths();
};  // namespace FGMPMetaUtils

void UMessageTagsManager::SyncToGMPMeta()
{
	FGMPMetaUtils::IncVersion();
	for (auto& Pair : MessageTagNodeMap)
	{
		if (Pair.Value->Tag.IsNone())
			continue;

		TArray<FName> ParamTypes;
		for (auto& Cell : Pair.Value->Parameters)
		{
			ParamTypes.Add(Cell.Type);
		}

		TArray<FName> ResponseTypes;
		for (auto& Cell : Pair.Value->ResponseTypes)
		{
			ResponseTypes.Add(Cell.Type);
		}

		FGMPMetaUtils::InsertMeta(Pair.Value->GetCompleteTagName(), MoveTemp(ParamTypes), MoveTemp(ResponseTypes));
	}

	FGMPMetaUtils::InsertMetaPath([&](auto& Container) {
		TArray<const FMessageTagSource*> Sources;
		for (auto TagSourceIt = TagSources.CreateConstIterator(); TagSourceIt; ++TagSourceIt)
		{
			if (TagSourceIt.Value().SourceType == EMessageTagSourceType::DefaultTagList ||  //
				TagSourceIt.Value().SourceType == EMessageTagSourceType::TagList ||         //
				TagSourceIt.Value().SourceType == EMessageTagSourceType::RestrictedTagList)
			{
				auto CfgPath = FPaths::ConvertRelativePathToFull(TagSourceIt.Value().SourceTagList->ConfigFileName);
				FPaths::MakePathRelativeTo(CfgPath, *FPaths::ProjectDir());
				Container.AddUnique(MoveTemp(CfgPath));
			}
		}
	});
	FGMPMetaUtils::SaveMetaPaths();
}

void UMessageTagsManager::InitializeManager()
{
	check(!SingletonManager);
#if UE_4_23_OR_LATER
	SCOPED_BOOT_TIMING("UMessageTagsManager::InitializeManager");
	SCOPE_LOG_TIME_IN_SECONDS(TEXT("UMessageTagsManager::InitializeManager"), nullptr);
#endif
	SingletonManager = NewObject<UMessageTagsManager>(GetTransientPackage(), NAME_None);
	SingletonManager->AddToRoot();

	UMessageTagsSettings* MutableDefault = nullptr;
	{
		SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::InitializeManager: Load settings"));
		MutableDefault = GetMutableDefault<UMessageTagsSettings>();
	}

	{
		SCOPE_LOG_MESSAGETAGS(TEXT("UMessageTagsManager::InitializeManager: Load deprecated"));

		TArray<FString> MessageTagTablePaths;
		GConfig->GetArray(TEXT("MessageTags"), TEXT("+MessageTagTableList"), MessageTagTablePaths, GEngineIni);

		// Report deprecation
		if (MessageTagTablePaths.Num() > 0)
		{
			UE_LOG(LogMessageTags, Log, TEXT("MessageTagTableList is in a deprecated location, open and save MessageTag settings to fix"));
			for (const FString& DataTable : MessageTagTablePaths)
			{
				MutableDefault->MessageTagTableList.AddUnique(DataTable);
			}
		}
	}

	SingletonManager->LoadMessageTagTables(true);
	SingletonManager->ConstructMessageTagTree();

	// Bind to end of engine init to be done adding native tags
	FCoreDelegates::OnPostEngineInit.AddUObject(SingletonManager, &UMessageTagsManager::DoneAddingNativeTags);
}

void UMessageTagsManager::PopulateTreeFromDataTable(class UDataTable* InTable)
{
	checkf(MessageRootTag.IsValid(), TEXT("ConstructMessageTagTree() must be called before PopulateTreeFromDataTable()"));
	static const FString ContextString(TEXT("UMessageTagsManager::PopulateTreeFromDataTable"));

	TArray<FMessageTagTableRow*> TagTableRows;
	InTable->GetAllRows<FMessageTagTableRow>(ContextString, TagTableRows);

	FName SourceName = InTable->GetOutermost()->GetFName();

	FMessageTagSource* FoundSource = FindOrAddTagSource(SourceName, EMessageTagSourceType::DataTable);

	for (const FMessageTagTableRow* TagRow : TagTableRows)
	{
		if (TagRow)
		{
			AddTagTableRow(*TagRow, SourceName);
		}
	}
}

void UMessageTagsManager::AddTagTableRow(const FMessageTagTableRow& TagRow, FName SourceName, bool bIsRestrictedTag, bool bAllowNonRestrictedChildren)
{
	TSharedPtr<FMessageTagNode> CurNode = MessageRootTag;
	TArray<TSharedPtr<FMessageTagNode>> AncestorNodes;

	// Split the tag text on the "." delimiter to establish tag depth and then insert each tag into the message tag tree
	// We try to avoid as many FString->FName conversions as possible as they are slow
	FName OriginalTagName = TagRow.Tag;
	FString FullTagString = OriginalTagName.ToString();

#if WITH_EDITOR
	{
		// In editor builds, validate string
		// These must get fixed up cooking to work properly
		FText ErrorText;
		FString FixedString;

		if (!IsValidMessageTagString(FullTagString, &ErrorText, &FixedString))
		{
			if (FixedString.IsEmpty())
			{
				// No way to fix it
				UE_LOG(LogMessageTags, Error, TEXT("Invalid tag %s from source %s: %s!"), *FullTagString, *SourceName.ToString(), *ErrorText.ToString());
				return;
			}
			else
			{
				UE_LOG(LogMessageTags, Error, TEXT("Invalid tag %s from source %s: %s! Replacing with %s, you may need to modify InvalidTagCharacters"), *FullTagString, *SourceName.ToString(), *ErrorText.ToString(), *FixedString);
				FullTagString = FixedString;
				OriginalTagName = FName(*FixedString);
			}
		}
	}
#endif

	TArray<FString> SubTags;
	FullTagString.ParseIntoArray(SubTags, TEXT("."), true);

	// We will build this back up as we go
	FullTagString.Reset();

	int32 NumSubTags = SubTags.Num();
	bool bHasSeenConflict = false;

	for (int32 SubTagIdx = 0; SubTagIdx < NumSubTags; ++SubTagIdx)
	{
		bool bIsExplicitTag = (SubTagIdx == (NumSubTags - 1));
		FName ShortTagName = *SubTags[SubTagIdx];
		FName FullTagName;

		if (bIsExplicitTag)
		{
			// We already know the final name
			FullTagName = OriginalTagName;
		}
		else if (SubTagIdx == 0)
		{
			// Full tag is the same as short tag, and start building full tag string
			FullTagName = ShortTagName;
			FullTagString = SubTags[SubTagIdx];
		}
		else
		{
			// Add .Tag and use that as full tag
			FullTagString += TEXT(".");
			FullTagString += SubTags[SubTagIdx];

			FullTagName = FName(*FullTagString);
		}

		TArray<TSharedPtr<FMessageTagNode>>& ChildTags = CurNode.Get()->GetChildTagNodes();
		int32 InsertionIdx = InsertTagIntoNodeArray(ShortTagName, FullTagName, CurNode, ChildTags, SourceName, TagRow, bIsExplicitTag, bIsRestrictedTag, bAllowNonRestrictedChildren);

		CurNode = ChildTags[InsertionIdx];

		// Tag conflicts only affect the editor so we don't look for them in the game
#if WITH_EDITORONLY_DATA
		if (bIsRestrictedTag)
		{
			CurNode->bAncestorHasConflict = bHasSeenConflict;

			// If the sources don't match and the tag is explicit and we should've added the tag explicitly here, we have a conflict
			if (CurNode->SourceName != SourceName && (CurNode->bIsExplicitTag && bIsExplicitTag))
			{
				// mark all ancestors as having a bad descendant
				for (TSharedPtr<FMessageTagNode> CurAncestorNode : AncestorNodes)
				{
					CurAncestorNode->bDescendantHasConflict = true;
				}

				// mark the current tag as having a conflict
				CurNode->bNodeHasConflict = true;

				// append source names
				FString CombinedSources = CurNode->SourceName.ToString();
				CombinedSources.Append(TEXT(" and "));
				CombinedSources.Append(SourceName.ToString());
				CurNode->SourceName = FName(*CombinedSources);

				// mark all current descendants as having a bad ancestor
				MarkChildrenOfNodeConflict(CurNode);
			}

			// mark any children we add later in this function as having a bad ancestor
			if (CurNode->bNodeHasConflict)
			{
				bHasSeenConflict = true;
			}

			AncestorNodes.Add(CurNode);
		}
#endif
	}
}

void UMessageTagsManager::MarkChildrenOfNodeConflict(TSharedPtr<FMessageTagNode> CurNode)
{
#if WITH_EDITORONLY_DATA
	TArray<TSharedPtr<FMessageTagNode>>& ChildTags = CurNode.Get()->GetChildTagNodes();
	for (TSharedPtr<FMessageTagNode> ChildNode : ChildTags)
	{
		ChildNode->bAncestorHasConflict = true;
		MarkChildrenOfNodeConflict(ChildNode);
	}
#endif
}

UMessageTagsManager::~UMessageTagsManager()
{
	DestroyMessageTagTree();
	SingletonManager = nullptr;
}

void UMessageTagsManager::DestroyMessageTagTree()
{
	if (MessageRootTag.IsValid())
	{
		MessageRootTag->ResetNode();
		MessageRootTag.Reset();
		MessageTagNodeMap.Reset();
	}
	RestrictedMessageTagSourceNames.Reset();

	for (TPair<FString, FMessageTagSearchPathInfo>& Pair : RegisteredSearchPaths)
	{
		Pair.Value.bWasAddedToTree = false;
	}
}

int32 UMessageTagsManager::InsertTagIntoNodeArray(FName Tag,
												  FName FullTag,
												  TSharedPtr<FMessageTagNode> ParentNode,
												  TArray<TSharedPtr<FMessageTagNode>>& NodeArray,
												  FName SourceName,
												  const FMessageTagTableRow& TagRow,
												  bool bIsExplicitTag,
												  bool bIsRestrictedTag,
												  bool bAllowNonRestrictedChildren)
{
	int32 FoundNodeIdx = INDEX_NONE;
	int32 WhereToInsert = INDEX_NONE;

	// See if the tag is already in the array
	for (int32 CurIdx = 0; CurIdx < NodeArray.Num(); ++CurIdx)
	{
		FMessageTagNode* CurrNode = NodeArray[CurIdx].Get();
		if (CurrNode)
		{
			FName SimpleTagName = CurrNode->GetSimpleTagName();
			if (SimpleTagName == Tag)
			{
				FoundNodeIdx = CurIdx;
#if WITH_EDITORONLY_DATA
				// If we are explicitly adding this tag then overwrite the existing children restrictions with whatever is in the ini
				// If we restrict children in the input data, make sure we restrict them in the existing node. This applies to explicit and implicitly defined nodes
				if (bAllowNonRestrictedChildren == false || bIsExplicitTag)
				{
					// check if the tag is explicitly being created in more than one place.
					if (CurrNode->bIsExplicitTag && bIsExplicitTag)
					{
						// restricted tags always get added first
						//
						// There are two possibilities if we're adding a restricted tag.
						// If the existing tag is non-restricted the restricted tag should take precedence. This may invalidate some child tags of the existing tag.
						// If the existing tag is restricted we have a conflict. This is explicitly not allowed.
						if (bIsRestrictedTag)
						{
						}
					}
					CurrNode->bAllowNonRestrictedChildren = bAllowNonRestrictedChildren;
					CurrNode->bIsExplicitTag = CurrNode->bIsExplicitTag || bIsExplicitTag;
				}
#endif
				break;
			}
#if UE_4_23_OR_LATER
			else if (Tag.LexicalLess(SimpleTagName) && WhereToInsert == INDEX_NONE)
#else
			else if (Tag < SimpleTagName && WhereToInsert == INDEX_NONE)
#endif
			{
				// Insert new node before this
				WhereToInsert = CurIdx;
			}
		}
	}

	if (FoundNodeIdx == INDEX_NONE)
	{
		if (WhereToInsert == INDEX_NONE)
		{
			// Insert at end
			WhereToInsert = NodeArray.Num();
		}

		// Don't add the root node as parent
		TSharedPtr<FMessageTagNode> TagNode = MakeShareable(new FMessageTagNode(Tag, FullTag, ParentNode != MessageRootTag ? ParentNode : nullptr, bIsExplicitTag, bIsRestrictedTag, bAllowNonRestrictedChildren));

		TagNode->Parameters = TagRow.Parameters;
		TagNode->ResponseTypes = TagRow.ResponseTypes;

		// Add at the sorted location
		FoundNodeIdx = NodeArray.Insert(TagNode, WhereToInsert);

		FMessageTag MessageTag = TagNode->GetCompleteTag();

		// These should always match
		ensure(MessageTag.GetTagName() == FullTag);

		{
#if WITH_EDITOR
			// This critical section is to handle an editor-only issue where tag requests come from another thread when async loading from a background thread in FMessageTagContainer::Serialize.
			// This function is not generically threadsafe.
			FScopeLock Lock(&MessageTagMapCritical);
#endif
			MessageTagNodeMap.Add(MessageTag, TagNode);
		}
	}

#if WITH_EDITOR
	static FName NativeSourceName = FMessageTagSource::GetNativeName();

	// Set/update editor only data
	if (NodeArray[FoundNodeIdx]->SourceName.IsNone() && !SourceName.IsNone())
	{
		NodeArray[FoundNodeIdx]->SourceName = SourceName;
	}
	else if (SourceName == NativeSourceName)
	{
		// Native overrides other types
		NodeArray[FoundNodeIdx]->SourceName = SourceName;
	}

	if (NodeArray[FoundNodeIdx]->DevComment.IsEmpty() && !TagRow.DevComment.IsEmpty())
	{
		NodeArray[FoundNodeIdx]->DevComment = TagRow.DevComment;
	}
#endif

	return FoundNodeIdx;
}

void UMessageTagsManager::PrintReplicationIndices()
{
	VerifyNetworkIndex();

	UE_LOG(LogMessageTags, Display, TEXT("::PrintReplicationIndices (TOTAL %d"), MessageTagNodeMap.Num());

	for (auto It : MessageTagNodeMap)
	{
		FMessageTag Tag = It.Key;
		TSharedPtr<FMessageTagNode> Node = It.Value;

		UE_LOG(LogMessageTags, Display, TEXT("Tag %s NetIndex: %d"), *Tag.ToString(), Node->GetNetIndex());
	}
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UMessageTagsManager::PrintReplicationFrequencyReport()
{
	UE_LOG(LogMessageTags, Warning, TEXT("================================="));
	UE_LOG(LogMessageTags, Warning, TEXT("Message Tags Replication Report"));

	UE_LOG(LogMessageTags, Warning, TEXT("\nTags replicated solo:"));
	ReplicationCountMap_SingleTags.ValueSort(TGreater<int32>());
	for (auto& It : ReplicationCountMap_SingleTags)
	{
		UE_LOG(LogMessageTags, Warning, TEXT("%s - %d"), *It.Key.ToString(), It.Value);
	}

	// ---------------------------------------

	UE_LOG(LogMessageTags, Warning, TEXT("\nTags replicated in containers:"));
	ReplicationCountMap_Containers.ValueSort(TGreater<int32>());
	for (auto& It : ReplicationCountMap_Containers)
	{
		UE_LOG(LogMessageTags, Warning, TEXT("%s - %d"), *It.Key.ToString(), It.Value);
	}

	// ---------------------------------------

	UE_LOG(LogMessageTags, Warning, TEXT("\nAll Tags replicated:"));
	ReplicationCountMap.ValueSort(TGreater<int32>());
	for (auto& It : ReplicationCountMap)
	{
		UE_LOG(LogMessageTags, Warning, TEXT("%s - %d"), *It.Key.ToString(), It.Value);
	}

	TMap<int32, int32> SavingsMap;
	int32 BaselineCost = 0;
	for (int32 Bits = 1; Bits < NetIndexTrueBitNum; ++Bits)
	{
		int32 TotalSavings = 0;
		BaselineCost = 0;

		FMessageTagNetIndex ExpectedNetIndex = 0;
		for (auto& It : ReplicationCountMap)
		{
			int32 ExpectedCostBits = 0;
			bool FirstSeg = ExpectedNetIndex < FMath::Pow(2.f, Bits + 0.f);
			if (FirstSeg)
			{
				// This would fit in the first Bits segment
				ExpectedCostBits = Bits + 1;
			}
			else
			{
				// Would go in the second segment, so we pay the +1 cost
				ExpectedCostBits = NetIndexTrueBitNum + 1;
			}

			int32 Savings = (NetIndexTrueBitNum - ExpectedCostBits) * It.Value;
			BaselineCost += NetIndexTrueBitNum * It.Value;

			//UE_LOG(LogMessageTags, Warning, TEXT("[Bits: %d] Tag %s would save %d bits"), Bits, *It.Key.ToString(), Savings);
			ExpectedNetIndex++;
			TotalSavings += Savings;
		}

		SavingsMap.FindOrAdd(Bits) = TotalSavings;
	}

	SavingsMap.ValueSort(TGreater<int32>());
	int32 BestBits = 0;
	for (auto& It : SavingsMap)
	{
		if (BestBits == 0)
		{
			BestBits = It.Key;
		}

		UE_LOG(LogMessageTags, Warning, TEXT("%d bits would save %d (%.2f)"), It.Key, It.Value, (float)It.Value / (float)BaselineCost);
	}

	UE_LOG(LogMessageTags, Warning, TEXT("\nSuggested config:"));

	// Write out a nice copy pastable config
	int32 Count = 0;
	for (auto& It : ReplicationCountMap)
	{
		UE_LOG(LogMessageTags, Warning, TEXT("+CommonlyReplicatedTags=%s"), *It.Key.ToString());

		if (Count == FMath::Pow(2.f, BestBits + 0.f))
		{
			// Print a blank line out, indicating tags after this are not necessary but still may be useful if the user wants to manually edit the list.
			UE_LOG(LogMessageTags, Warning, TEXT(""));
		}

		if (Count++ >= FMath::Pow(2.f, BestBits + 1.f))
		{
			break;
		}
	}

	UE_LOG(LogMessageTags, Warning, TEXT("NetIndexFirstBitSegment=%d"), BestBits);

	UE_LOG(LogMessageTags, Warning, TEXT("================================="));
}

void UMessageTagsManager::NotifyTagReplicated(FMessageTag Tag, bool WasInContainer)
{
	ReplicationCountMap.FindOrAdd(Tag)++;

	if (WasInContainer)
	{
		ReplicationCountMap_Containers.FindOrAdd(Tag)++;
	}
	else
	{
		ReplicationCountMap_SingleTags.FindOrAdd(Tag)++;
	}
}
#endif

#if WITH_EDITOR

static void RecursiveRootTagSearch(const FString& InFilterString, const TArray<TSharedPtr<FMessageTagNode>>& MessageRootTags, TArray<TSharedPtr<FMessageTagNode>>& OutTagArray)
{
	FString CurrentFilter, RestOfFilter;
	if (!InFilterString.Split(TEXT("."), &CurrentFilter, &RestOfFilter))
	{
		CurrentFilter = InFilterString;
	}

	for (int32 iTag = 0; iTag < MessageRootTags.Num(); ++iTag)
	{
		FString RootTagName = MessageRootTags[iTag].Get()->GetSimpleTagName().ToString();

		if (RootTagName.Equals(CurrentFilter) == true)
		{
			if (RestOfFilter.IsEmpty())
			{
				// We've reached the end of the filter, add tags
				OutTagArray.Add(MessageRootTags[iTag]);
			}
			else
			{
				// Recurse into our children
				RecursiveRootTagSearch(RestOfFilter, MessageRootTags[iTag]->GetChildTagNodes(), OutTagArray);
			}
		}
	}
}

void UMessageTagsManager::GetFilteredMessageRootTags(const FString& InFilterString, TArray<TSharedPtr<FMessageTagNode>>& OutTagArray) const
{
	TArray<FString> PreRemappedFilters;
	TArray<FString> Filters;
	TArray<TSharedPtr<FMessageTagNode>>& MessageRootTags = MessageRootTag->GetChildTagNodes();

	OutTagArray.Empty();
	if (InFilterString.ParseIntoArray(PreRemappedFilters, TEXT(","), true) > 0)
	{
		const UMessageTagsSettings* CDO = GetDefault<UMessageTagsSettings>();
		for (FString& Str : PreRemappedFilters)
		{
			bool Remapped = false;
			for (const FMessageTagCategoryRemap& RemapInfo : CDO->CategoryRemapping)
			{
				if (RemapInfo.BaseCategory == Str)
				{
					Remapped = true;
					Filters.Append(RemapInfo.RemapCategories);
				}
			}
			if (Remapped == false)
			{
				Filters.Add(Str);
			}
		}

		// Check all filters in the list
		for (int32 iFilter = 0; iFilter < Filters.Num(); ++iFilter)
		{
			RecursiveRootTagSearch(Filters[iFilter], MessageRootTags, OutTagArray);
		}

		if (OutTagArray.Num() == 0)
		{
			// We had filters but nothing matched. Ignore the filters.
			// This makes sense to do with engine level filters that games can optionally specify/override.
			// We never want to impose tag structure on projects, but still give them the ability to do so for their project.
			OutTagArray = MessageRootTags;
		}
	}
	else
	{
		// No Filters just return them all
		OutTagArray = MessageRootTags;
	}
}

FString UMessageTagsManager::GetCategoriesMetaFromPropertyHandle(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	// Global delegate override. Useful for parent structs that want to override tag categories based on their data (e.g. not static property meta data)
	FString DelegateOverrideString;
	OnGetCategoriesMetaFromPropertyHandle.Broadcast(PropertyHandle, DelegateOverrideString);
	if (DelegateOverrideString.IsEmpty() == false)
	{
		return DelegateOverrideString;
	}

	return StaticGetCategoriesMetaFromPropertyHandle(PropertyHandle);
}

FString UMessageTagsManager::StaticGetCategoriesMetaFromPropertyHandle(TSharedPtr<class IPropertyHandle> PropertyHandle)
{
	FString Categories;

	auto GetFieldMetaData = ([&](auto* Field) {
		if (Field->HasMetaData(NAME_Categories))
		{
			Categories = Field->GetMetaData(NAME_Categories);
			return true;
		}

		return false;
	});

	auto GetMetaData = ([&](auto* Field) {
		if (Field->HasMetaData(NAME_Categories))
		{
			Categories = Field->GetMetaData(NAME_Categories);
			return true;
		}

		return false;
	});

	while (PropertyHandle.IsValid())
	{
		if (auto* Property = PropertyHandle->GetProperty())
		{
			/**
			*	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories="MessageCue"))
			*	FMessageTag MessageCueTag;
			*/
			if (GetFieldMetaData(Property))
			{
				break;
			}

			/**
			*	USTRUCT(meta=(Categories="EventKeyword"))
			*	struct FMessageEventKeywordTag : public FMessageTag
			*/
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (GetMetaData(StructProperty->Struct))
				{
					break;
				}
			}

			/**	TArray<FMessageEventKeywordTag> QualifierTagTestList; */
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				if (GetFieldMetaData(ArrayProperty->Inner))
				{
					break;
				}
			}

			/**	TMap<FGameplayTag, ValueType> GameplayTagMap; */
			if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				if (GetFieldMetaData(MapProperty->KeyProp))
				{
					break;
				}
			}
		}
		PropertyHandle = PropertyHandle->GetParentHandle();
	}

	return Categories;
}

FString UMessageTagsManager::GetCategoriesMetaFromFunction(const UFunction* ThisFunction, FName ParamName /** = NAME_None */) const
{
	FString FilterString;
	if (ThisFunction)
	{
		// If a param name was specified, check it first for UPARAM metadata
		if (!ParamName.IsNone())
		{
			FProperty* ParamProp = FindFProperty<FProperty>(ThisFunction, ParamName);
			if (ParamProp)
			{
				FilterString = UMessageTagsManager::Get().GetCategoriesMetaFromField(ParamProp);
			}
		}

		// No filter found so far, fall back to UFUNCTION-level
		if (FilterString.IsEmpty() && ThisFunction->HasMetaData(NAME_MessageTagFilter))
		{
			FilterString = ThisFunction->GetMetaData(NAME_MessageTagFilter);
		}
	}

	return FilterString;
}

void UMessageTagsManager::GetAllTagsFromSource(FName TagSource, TArray<TSharedPtr<FMessageTagNode>>& OutTagArray) const
{
	for (const TPair<FMessageTag, TSharedPtr<FMessageTagNode>>& NodePair : MessageTagNodeMap)
	{
		if (NodePair.Value->SourceName == TagSource)
		{
			OutTagArray.Add(NodePair.Value);
		}
	}
}

bool UMessageTagsManager::IsDictionaryTag(FName TagName) const
{
	TSharedPtr<FMessageTagNode> Node = FindTagNode(TagName);
	if (Node.IsValid() && Node->bIsExplicitTag)
	{
		return true;
	}

	return false;
}

bool UMessageTagsManager::GetTagEditorData(FName TagName, FString& OutComment, FName& OutTagSource, bool& bOutIsTagExplicit, bool& bOutIsRestrictedTag, bool& bOutAllowNonRestrictedChildren) const
{
	TSharedPtr<FMessageTagNode> Node = FindTagNode(TagName);
	if (Node.IsValid())
	{
		OutComment = Node->DevComment;
		OutTagSource = Node->SourceName;
		bOutIsTagExplicit = Node->bIsExplicitTag;
		bOutIsRestrictedTag = Node->bIsRestrictedTag;
		bOutAllowNonRestrictedChildren = Node->bAllowNonRestrictedChildren;
		return true;
	}
	return false;
}

void UMessageTagsManager::EditorRefreshMessageTagTree()
{
#if 0
	// Clear out source path info so it will reload off disk
	for (TPair<FString, FGameplayTagSearchPathInfo>& Pair : RegisteredSearchPaths)
	{
		Pair.Value.bWasSearched = false;
	}
#endif

	DestroyMessageTagTree();
	LoadMessageTagTables(false);
	ConstructMessageTagTree();
	OnEditorRefreshMessageTagTree.Broadcast();
}

FMessageTagContainer UMessageTagsManager::RequestMessageTagChildrenInDictionary(const FMessageTag& MessageTag) const
{
	// Note this purposefully does not include the passed in MessageTag in the container.
	FMessageTagContainer TagContainer;

	TSharedPtr<FMessageTagNode> MessageTagNode = FindTagNode(MessageTag);
	if (MessageTagNode.IsValid())
	{
		AddChildrenTags(TagContainer, MessageTagNode, true, true);
	}
	return TagContainer;
}

#if WITH_EDITORONLY_DATA
FMessageTagContainer UMessageTagsManager::RequestMessageTagDirectDescendantsInDictionary(const FMessageTag& MessageTag, EMessageTagSelectionType SelectionType) const
{
	bool bIncludeRestrictedTags = (SelectionType == EMessageTagSelectionType::RestrictedOnly || SelectionType == EMessageTagSelectionType::All);
	bool bIncludeNonRestrictedTags = (SelectionType == EMessageTagSelectionType::NonRestrictedOnly || SelectionType == EMessageTagSelectionType::All);

	// Note this purposefully does not include the passed in MessageTag in the container.
	FMessageTagContainer TagContainer;

	TSharedPtr<FMessageTagNode> MessageTagNode = FindTagNode(MessageTag);
	if (MessageTagNode.IsValid())
	{
		TArray<TSharedPtr<FMessageTagNode>>& ChildrenNodes = MessageTagNode->GetChildTagNodes();
		int32 CurrArraySize = ChildrenNodes.Num();
		for (int32 Idx = 0; Idx < CurrArraySize; ++Idx)
		{
			TSharedPtr<FMessageTagNode> ChildNode = ChildrenNodes[Idx];
			if (ChildNode.IsValid())
			{
				// if the tag isn't in the dictionary, add its children to the list
				if (ChildNode->SourceName == NAME_None)
				{
					TArray<TSharedPtr<FMessageTagNode>>& GrandChildrenNodes = ChildNode->GetChildTagNodes();
					ChildrenNodes.Append(GrandChildrenNodes);
					CurrArraySize = ChildrenNodes.Num();
				}
				else
				{
					// this tag is in the dictionary so add it to the list
					if ((ChildNode->bIsRestrictedTag && bIncludeRestrictedTags) || (!ChildNode->bIsRestrictedTag && bIncludeNonRestrictedTags))
					{
						TagContainer.AddTag(ChildNode->GetCompleteTag());
					}
				}
			}
		}
	}
	return TagContainer;
}
#endif  // WITH_EDITORONLY_DATA

void UMessageTagsManager::NotifyMessageTagDoubleClickedEditor(FString TagName)
{
	FMessageTag Tag = RequestMessageTag(FName(*TagName), false);
	if (Tag.IsValid())
	{
		FSimpleMulticastDelegate Delegate;
		OnGatherMessageTagDoubleClickedEditor.Broadcast(Tag, Delegate);
		Delegate.Broadcast();
	}
}

bool UMessageTagsManager::ShowMessageTagAsHyperLinkEditor(FString TagName)
{
	FMessageTag Tag = RequestMessageTag(FName(*TagName), false);
	if (Tag.IsValid())
	{
		FSimpleMulticastDelegate Delegate;
		OnGatherMessageTagDoubleClickedEditor.Broadcast(Tag, Delegate);
		return Delegate.IsBound();
	}
	return false;
}

#endif  // WITH_EDITOR

const FMessageTagSource* UMessageTagsManager::FindTagSource(FName TagSourceName) const
{
	return TagSources.Find(TagSourceName);
}

FMessageTagSource* UMessageTagsManager::FindTagSource(FName TagSourceName)
{
	return TagSources.Find(TagSourceName);
}

void UMessageTagsManager::FindTagSourcesWithType(EMessageTagSourceType TagSourceType, TArray<const FMessageTagSource*>& OutArray) const
{
	for (auto TagSourceIt = TagSources.CreateConstIterator(); TagSourceIt; ++TagSourceIt)
	{
		if (TagSourceIt.Value().SourceType == TagSourceType)
		{
			OutArray.Add(&TagSourceIt.Value());
		}
	}
}

FMessageTagSource* UMessageTagsManager::FindOrAddTagSource(FName TagSourceName, EMessageTagSourceType SourceType, const FString& RootDirToUse)
{
	FMessageTagSource* FoundSource = FindTagSource(TagSourceName);
	if (FoundSource)
	{
		if (SourceType == FoundSource->SourceType)
		{
			return FoundSource;
		}

		return nullptr;
	}

	// Need to make a new one
	FMessageTagSource* NewSource = &TagSources.Add(TagSourceName, FMessageTagSource(TagSourceName, SourceType));

	if (SourceType == EMessageTagSourceType::DefaultTagList)
	{
		NewSource->SourceTagList = GetMutableDefault<UMessageTagsSettings>();
	}
	else if (SourceType == EMessageTagSourceType::TagList)
	{
		NewSource->SourceTagList = NewObject<UMessageTagsList>(this, TagSourceName, RF_Transient);
		if (RootDirToUse.IsEmpty())
		{
			NewSource->SourceTagList->ConfigFileName = FString::Printf(MessageTagsPathFmt, *FPaths::SourceConfigDir(), *TagSourceName.ToString());
		}
		else
		{
			// Use custom root and make sure it gets added to the ini list for later refresh
			NewSource->SourceTagList->ConfigFileName = RootDirToUse / *TagSourceName.ToString();
			RegisteredSearchPaths.FindOrAdd(NewSource->SourceTagList->ConfigFileName);
		}
		if (GUObjectArray.IsDisregardForGC(this))
		{
			NewSource->SourceTagList->AddToRoot();
		}
	}
	else if (SourceType == EMessageTagSourceType::RestrictedTagList)
	{
		NewSource->SourceRestrictedTagList = NewObject<URestrictedMessageTagsList>(this, TagSourceName, RF_Transient);
		if (RootDirToUse.IsEmpty())
		{
			NewSource->SourceRestrictedTagList->ConfigFileName = FString::Printf(MessageTagsPathFmt, *FPaths::SourceConfigDir(), *TagSourceName.ToString());
		}
		else
		{
			// Use custom root and make sure it gets added to the ini list for later refresh
			NewSource->SourceRestrictedTagList->ConfigFileName = RootDirToUse / *TagSourceName.ToString();
			RegisteredSearchPaths.FindOrAdd(NewSource->SourceTagList->ConfigFileName);
		}
		if (GUObjectArray.IsDisregardForGC(this))
		{
			NewSource->SourceRestrictedTagList->AddToRoot();
		}
	}

	return NewSource;
}

DECLARE_CYCLE_STAT(TEXT("UMessageTagsManager::RequestMessageTag"), STAT_UMessageTagsManager_RequestMessageTag, STATGROUP_MessageTags);

void UMessageTagsManager::RequestMessageTagContainer(const TArray<FString>& TagStrings, FMessageTagContainer& OutTagsContainer, bool bErrorIfNotFound /*=true*/) const
{
	for (const FString& CurrentTagString : TagStrings)
	{
		FMessageTag RequestedTag = RequestMessageTag(FName(*(CurrentTagString.TrimStartAndEnd())), bErrorIfNotFound);
		if (RequestedTag.IsValid())
		{
			OutTagsContainer.AddTag(RequestedTag);
		}
	}
}

FMessageTag UMessageTagsManager::RequestMessageTag(FName TagName, bool ErrorIfNotFound) const
{
	SCOPE_CYCLE_COUNTER(STAT_UMessageTagsManager_RequestMessageTag);

#if WITH_EDITOR
	// This critical section is to handle and editor-only issue where tag requests come from another thread when async loading from a background thread in FMessageTagContainer::Serialize.
	// This function is not generically threadsafe.
	FScopeLock Lock(&MessageTagMapCritical);
#endif

	FMessageTag PossibleTag(TagName);

	if (MessageTagNodeMap.Contains(PossibleTag))
	{
		return PossibleTag;
	}
	else if (ErrorIfNotFound)
	{
		static TSet<FName> MissingTagName;
		if (!MissingTagName.Contains(TagName))
		{
			ensureAlwaysMsgf(false, TEXT("Requested Tag %s was not found. Check tag data table."), *TagName.ToString());
			MissingTagName.Add(TagName);
		}
	}

	return FMessageTag();
}

bool UMessageTagsManager::IsValidMessageTagString(const FString& TagString, FText* OutError, FString* OutFixedString)
{
	bool bIsValid = true;
	FString FixedString = TagString;
	FText ErrorText;

	if (FixedString.IsEmpty())
	{
		ErrorText = LOCTEXT("EmptyStringError", "Tag is empty");
		bIsValid = false;
	}

	while (FixedString.StartsWith(TEXT("."), ESearchCase::CaseSensitive))
	{
		ErrorText = LOCTEXT("StartWithPeriod", "Tag starts with .");
		FixedString.RemoveAt(0);
		bIsValid = false;
	}

	while (FixedString.EndsWith(TEXT("."), ESearchCase::CaseSensitive))
	{
		ErrorText = LOCTEXT("EndWithPeriod", "Tag ends with .");
		FixedString.RemoveAt(FixedString.Len() - 1);
		bIsValid = false;
	}

	while (FixedString.StartsWith(TEXT(" "), ESearchCase::CaseSensitive))
	{
		ErrorText = LOCTEXT("StartWithSpace", "Tag starts with space");
		FixedString.RemoveAt(0);
		bIsValid = false;
	}

	while (FixedString.EndsWith(TEXT(" "), ESearchCase::CaseSensitive))
	{
		ErrorText = LOCTEXT("EndWithSpace", "Tag ends with space");
		FixedString.RemoveAt(FixedString.Len() - 1);
		bIsValid = false;
	}

	FText TagContext = LOCTEXT("MessageTagContext", "Tag");
	if (!FName::IsValidXName(TagString, InvalidTagCharacters, &ErrorText, &TagContext))
	{
		for (TCHAR& TestChar : FixedString)
		{
			for (TCHAR BadChar : InvalidTagCharacters)
			{
				if (TestChar == BadChar)
				{
					TestChar = TEXT('_');
				}
			}
		}

		bIsValid = false;
	}

	if (OutError)
	{
		*OutError = ErrorText;
	}
	if (OutFixedString)
	{
		*OutFixedString = FixedString;
	}

	return bIsValid;
}

FMessageTag UMessageTagsManager::FindMessageTagFromPartialString_Slow(FString PartialString) const
{
#if WITH_EDITOR
	// This critical section is to handle and editor-only issue where tag requests come from another thread when async loading from a background thread in FMessageTagContainer::Serialize.
	// This function is not generically threadsafe.
	FScopeLock Lock(&MessageTagMapCritical);
#endif

	// Exact match first
	FMessageTag PossibleTag(*PartialString);
	if (MessageTagNodeMap.Contains(PossibleTag))
	{
		return PossibleTag;
	}

	// Find shortest tag name that contains the match string
	FMessageTag FoundTag;
	FMessageTagContainer AllTags;
	RequestAllMessageTags(AllTags, false);

	int32 BestMatchLength = MAX_int32;
	for (FMessageTag MatchTag : AllTags)
	{
		FString Str = MatchTag.ToString();
		if (Str.Contains(PartialString))
		{
			if (Str.Len() < BestMatchLength)
			{
				FoundTag = MatchTag;
				BestMatchLength = Str.Len();
			}
		}
	}

	return FoundTag;
}

FMessageTag UMessageTagsManager::AddNativeMessageTag(FName TagName, const FString& TagDevComment)
{
	if (TagName.IsNone())
	{
		return FMessageTag();
	}

	// Unsafe to call after done adding
	if (ensure(!bDoneAddingNativeTags))
	{
		FMessageTag NewTag = FMessageTag(TagName);

		if (!LegacyNativeTags.Contains(TagName))
		{
			LegacyNativeTags.Add(TagName);
		}

		AddTagTableRow(FMessageTagTableRow(TagName, TagDevComment), FMessageTagSource::GetNativeName());

		return NewTag;
	}

	return FMessageTag();
}

void UMessageTagsManager::AddNativeMessageTag(FNativeMessageTag* TagSource)
{
#if 0
	AddTagTableRow(TagSource->GetMessageTagTableRow(), FMessageTagSource::GetNativeName());
	InvalidateNetworkIndex();
	IMessageTagsModule::OnMessageTagTreeChanged.Broadcast();
	SyncToGMPMeta();
#endif
}

void UMessageTagsManager::RemoveNativeMessageTag(const FNativeMessageTag* TagSource)
{
#if 0
	// TODO This is awful, need to invalidate the tag tree, not rebuild it.
	{
#if WITH_EDITOR
		EditorRefreshMessageTagTree();
#else
	//TODO - Removing tags isn't really a thing right now, but setting this up for the future.
	//DestroyMessageTagTree();
	//ConstructMessageTagTree();
	//OnEditorRefreshMessageTagTree.Broadcast();
#endif
	}
#endif
}

void UMessageTagsManager::CallOrRegister_OnDoneAddingNativeTagsDelegate(FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (bDoneAddingNativeTags)
	{
		Delegate.Execute();
	}
	else
	{
		bool bAlreadyBound = Delegate.GetUObject() != nullptr ? OnDoneAddingNativeTagsDelegate().IsBoundToObject(Delegate.GetUObject()) : false;
		if (!bAlreadyBound)
		{
			OnDoneAddingNativeTagsDelegate().Add(Delegate);
		}
	}
}

FSimpleMulticastDelegate& UMessageTagsManager::OnDoneAddingNativeTagsDelegate()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

FSimpleMulticastDelegate& UMessageTagsManager::OnLastChanceToAddNativeTags()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

UMessageTagsManager::FOnMessageTagSignatureChanged& UMessageTagsManager::OnMessageTagSignatureChanged()
{
	static FOnMessageTagSignatureChanged Delegate;
	return Delegate;
}
void UMessageTagsManager::DoneAddingNativeTags()
{
	// Safe to call multiple times, only works the first time, must be called after the engine
	// is initialized (DoneAddingNativeTags is bound to PostEngineInit to cover anything that's skipped).
	if (GEngine && !bDoneAddingNativeTags)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UMessageTagsManager::DoneAddingNativeTags);

		UE_CLOG(MESSAGETAGS_VERBOSE, LogMessageTags, Display, TEXT("UMessageTagsManager::DoneAddingNativeTags. DelegateIsBound: %d"), (int32)OnLastChanceToAddNativeTags().IsBound());
		OnLastChanceToAddNativeTags().Broadcast();
		bDoneAddingNativeTags = true;

		// We may add native tags that are needed for redirectors, so reconstruct the MessageTag tree
		DestroyMessageTagTree();
		ConstructMessageTagTree();

		OnDoneAddingNativeTagsDelegate().Broadcast();
	}
}

FMessageTagContainer UMessageTagsManager::RequestMessageTagParents(const FMessageTag& MessageTag) const
{
	const FMessageTagContainer* ParentTags = GetSingleTagContainer(MessageTag);

	if (ParentTags)
	{
		return ParentTags->GetMessageTagParents();
	}
	return FMessageTagContainer();
}

void UMessageTagsManager::RequestAllMessageTags(FMessageTagContainer& TagContainer, bool OnlyIncludeDictionaryTags) const
{
	TArray<TSharedPtr<FMessageTagNode>> ValueArray;
	MessageTagNodeMap.GenerateValueArray(ValueArray);
	for (const TSharedPtr<FMessageTagNode>& TagNode : ValueArray)
	{
#if WITH_EDITOR
		bool DictTag = IsDictionaryTag(TagNode->GetCompleteTagName());
#else
		bool DictTag = false;
#endif
		if (!OnlyIncludeDictionaryTags || DictTag)
		{
			const FMessageTag* Tag = MessageTagNodeMap.FindKey(TagNode);
			check(Tag);
			TagContainer.AddTagFast(*Tag);
		}
	}
}

FMessageTagContainer UMessageTagsManager::RequestMessageTagChildren(const FMessageTag& MessageTag) const
{
	FMessageTagContainer TagContainer;
	// Note this purposefully does not include the passed in MessageTag in the container.
	TSharedPtr<FMessageTagNode> MessageTagNode = FindTagNode(MessageTag);
	if (MessageTagNode.IsValid())
	{
		AddChildrenTags(TagContainer, MessageTagNode, true, false);
	}
	return TagContainer;
}

FMessageTag UMessageTagsManager::RequestMessageTagDirectParent(const FMessageTag& MessageTag) const
{
	TSharedPtr<FMessageTagNode> MessageTagNode = FindTagNode(MessageTag);
	if (MessageTagNode.IsValid())
	{
		TSharedPtr<FMessageTagNode> Parent = MessageTagNode->GetParentTagNode();
		if (Parent.IsValid())
		{
			return Parent->GetCompleteTag();
		}
	}
	return FMessageTag();
}

void UMessageTagsManager::AddChildrenTags(FMessageTagContainer& TagContainer, TSharedPtr<FMessageTagNode> MessageTagNode, bool RecurseAll, bool OnlyIncludeDictionaryTags) const
{
	if (MessageTagNode.IsValid())
	{
		TArray<TSharedPtr<FMessageTagNode>>& ChildrenNodes = MessageTagNode->GetChildTagNodes();
		for (TSharedPtr<FMessageTagNode> ChildNode : ChildrenNodes)
		{
			if (ChildNode.IsValid())
			{
				bool bShouldInclude = true;

#if WITH_EDITORONLY_DATA
				if (OnlyIncludeDictionaryTags && ChildNode->SourceName == NAME_None)
				{
					// Only have info to do this in editor builds
					bShouldInclude = false;
				}
#endif
				if (bShouldInclude)
				{
					TagContainer.AddTag(ChildNode->GetCompleteTag());
				}

				if (RecurseAll)
				{
					AddChildrenTags(TagContainer, ChildNode, true, OnlyIncludeDictionaryTags);
				}
			}
		}
	}
}

void UMessageTagsManager::SplitMessageTagFName(const FMessageTag& Tag, TArray<FName>& OutNames) const
{
	TSharedPtr<FMessageTagNode> CurNode = FindTagNode(Tag);
	while (CurNode.IsValid())
	{
		OutNames.Insert(CurNode->GetSimpleTagName(), 0);
		CurNode = CurNode->GetParentTagNode();
	}
}

int32 UMessageTagsManager::MessageTagsMatchDepth(const FMessageTag& MessageTagOne, const FMessageTag& MessageTagTwo) const
{
	TSet<FName> Tags1;
	TSet<FName> Tags2;

	TSharedPtr<FMessageTagNode> TagNode = FindTagNode(MessageTagOne);
	if (TagNode.IsValid())
	{
		GetAllParentNodeNames(Tags1, TagNode);
	}

	TagNode = FindTagNode(MessageTagTwo);
	if (TagNode.IsValid())
	{
		GetAllParentNodeNames(Tags2, TagNode);
	}

	return Tags1.Intersect(Tags2).Num();
}

int32 UMessageTagsManager::GetNumberOfTagNodes(const FMessageTag& MessageTag) const
{
	int32 Count = 0;

	TSharedPtr<FMessageTagNode> TagNode = FindTagNode(MessageTag);
	while (TagNode.IsValid())
	{
		++Count;                                // Increment the count of valid tag nodes.
		TagNode = TagNode->GetParentTagNode();  // Continue up the chain of parents.
	}

	return Count;
}

DECLARE_CYCLE_STAT(TEXT("UMessageTagsManager::GetAllParentNodeNames"), STAT_UMessageTagsManager_GetAllParentNodeNames, STATGROUP_MessageTags);

void UMessageTagsManager::GetAllParentNodeNames(TSet<FName>& NamesList, TSharedPtr<FMessageTagNode> MessageTag) const
{
	SCOPE_CYCLE_COUNTER(STAT_UMessageTagsManager_GetAllParentNodeNames);

	NamesList.Add(MessageTag->GetCompleteTagName());
	TSharedPtr<FMessageTagNode> Parent = MessageTag->GetParentTagNode();
	if (Parent.IsValid())
	{
		GetAllParentNodeNames(NamesList, Parent);
	}
}

DECLARE_CYCLE_STAT(TEXT("UMessageTagsManager::ValidateTagCreation"), STAT_UMessageTagsManager_ValidateTagCreation, STATGROUP_MessageTags);

bool UMessageTagsManager::ValidateTagCreation(FName TagName) const
{
	SCOPE_CYCLE_COUNTER(STAT_UMessageTagsManager_ValidateTagCreation);

	return FindTagNode(TagName).IsValid();
}

FMessageTagTableRow::FMessageTagTableRow(FMessageTagTableRow const& Other)
{
	*this = Other;
}

FMessageTagTableRow& FMessageTagTableRow::operator=(FMessageTagTableRow const& Other)
{
	// Guard against self-assignment
	if (this == &Other)
	{
		return *this;
	}

	Tag = Other.Tag;
	DevComment = Other.DevComment;
	Parameters = Other.Parameters;
	ResponseTypes = Other.ResponseTypes;
	return *this;
}

bool FMessageTagTableRow::operator==(FMessageTagTableRow const& Other) const
{
	return (Tag == Other.Tag);
}

bool FMessageTagTableRow::operator!=(FMessageTagTableRow const& Other) const
{
	return (Tag != Other.Tag);
}

bool FMessageTagTableRow::operator<(FMessageTagTableRow const& Other) const
{
#if UE_4_23_OR_LATER
	return Tag.LexicalLess(Other.Tag);
#else
	return Tag < Other.Tag;
#endif
}

FRestrictedMessageTagTableRow::FRestrictedMessageTagTableRow(FRestrictedMessageTagTableRow const& Other)
{
	*this = Other;
}

FRestrictedMessageTagTableRow& FRestrictedMessageTagTableRow::operator=(FRestrictedMessageTagTableRow const& Other)
{
	// Guard against self-assignment
	if (this == &Other)
	{
		return *this;
	}

	Super::operator=(Other);
	bAllowNonRestrictedChildren = Other.bAllowNonRestrictedChildren;

	return *this;
}

bool FRestrictedMessageTagTableRow::operator==(FRestrictedMessageTagTableRow const& Other) const
{
	if (bAllowNonRestrictedChildren != Other.bAllowNonRestrictedChildren)
	{
		return false;
	}

	if (Tag != Other.Tag)
	{
		return false;
	}

	return true;
}

bool FRestrictedMessageTagTableRow::operator!=(FRestrictedMessageTagTableRow const& Other) const
{
	if (bAllowNonRestrictedChildren == Other.bAllowNonRestrictedChildren)
	{
		return false;
	}

	if (Tag == Other.Tag)
	{
		return false;
	}

	return true;
}

FMessageTagNode::FMessageTagNode(FName InTag, FName InFullTag, TSharedPtr<FMessageTagNode> InParentNode, bool InIsExplicitTag, bool InIsRestrictedTag, bool InAllowNonRestrictedChildren)
	: Tag(InTag)
	, ParentNode(InParentNode)
	, NetIndex(INVALID_TAGNETINDEX)
{
	// Manually construct the tag container as we want to bypass the safety checks
	CompleteTagWithParents.MessageTags.Add(FMessageTag(InFullTag));

	FMessageTagNode* RawParentNode = ParentNode.Get();
	if (RawParentNode && RawParentNode->GetSimpleTagName() != NAME_None)
	{
		// Our parent nodes are already constructed, and must have it's tag in MessageTags[0]
		const FMessageTagContainer ParentContainer = RawParentNode->GetSingleTagContainer();

		CompleteTagWithParents.ParentTags.Add(ParentContainer.MessageTags[0]);
		CompleteTagWithParents.ParentTags.Append(ParentContainer.ParentTags);
	}

#if WITH_EDITORONLY_DATA
	bIsExplicitTag = InIsExplicitTag;
	bIsRestrictedTag = InIsRestrictedTag;
	bAllowNonRestrictedChildren = InAllowNonRestrictedChildren;

	bDescendantHasConflict = false;
	bNodeHasConflict = false;
	bAncestorHasConflict = false;
#endif
}

void FMessageTagNode::ResetNode()
{
	Tag = NAME_None;
	CompleteTagWithParents.Reset();
	NetIndex = INVALID_TAGNETINDEX;

	for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
	{
		ChildTags[ChildIdx]->ResetNode();
	}

	ChildTags.Empty();
	ParentNode.Reset();

#if WITH_EDITORONLY_DATA
	SourceName = NAME_None;
	DevComment = "";
	bIsExplicitTag = false;
	bIsRestrictedTag = false;
	bAllowNonRestrictedChildren = false;
	bDescendantHasConflict = false;
	bNodeHasConflict = false;
	bAncestorHasConflict = false;
#endif
}

#undef LOCTEXT_NAMESPACE
