// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsSettings.h"
#include "MessageTagsModule.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

UMessageTagsList::UMessageTagsList(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// No config filename, needs to be set at creation time
}

void UMessageTagsList::SortTags()
{
	MessageTagList.Sort();
}

URestrictedMessageTagsList::URestrictedMessageTagsList(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// No config filename, needs to be set at creation time
}

void URestrictedMessageTagsList::SortTags()
{
	RestrictedMessageTagList.Sort();
}

bool FRestrictedMessageCfg::operator==(const FRestrictedMessageCfg& Other) const
{
	if (RestrictedConfigName != Other.RestrictedConfigName)
	{
		return false;
	}

	if (Owners.Num() != Other.Owners.Num())
	{
		return false;
	}

	for (int32 Idx = 0; Idx < Owners.Num(); ++Idx)
	{
		if (Owners[Idx] != Other.Owners[Idx])
		{
			return false;
		}
	}

	return true;
}

bool FRestrictedMessageCfg::operator!=(const FRestrictedMessageCfg& Other) const
{
	return !(operator==(Other));
}

UMessageTagsSettings::UMessageTagsSettings(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	ConfigFileName = GetDefaultConfigFilename();
	ImportTagsFromConfig = true;
	WarnOnInvalidTags = true;
	ClearInvalidTags = false;
	FastReplication = false;
	AllowEditorTagUnloading = true;
	AllowGameTagUnloading = false;
	InvalidTagCharacters = ("\"',");
	NumBitsForContainerSize = 6;
	NetIndexFirstBitSegment = 16;
}

#if WITH_EDITOR
void UMessageTagsSettings::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (PropertyThatWillChange && PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(UMessageTagsSettings, RestrictedConfigFiles))
	{
		RestrictedConfigFilesTempCopy = RestrictedConfigFiles;
	}
}

void UMessageTagsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetName() == "RestrictedConfigName")
		{
			UMessageTagsManager& Manager = UMessageTagsManager::Get();
			for (FRestrictedMessageCfg& Info : RestrictedConfigFiles)
			{
				if (!Info.RestrictedConfigName.IsEmpty())
				{
					if (!Info.RestrictedConfigName.EndsWith(TEXT(".ini")))
					{
						Info.RestrictedConfigName.Append(TEXT(".ini"));
					}
					FMessageTagSource* Source = Manager.FindOrAddTagSource(*Info.RestrictedConfigName, EMessageTagSourceType::RestrictedTagList);
					if (!Source)
					{
						FNotificationInfo NotificationInfo(FText::Format(NSLOCTEXT("MessageTagsSettings", "UnableToAddRestrictedTagSource", "Unable to add restricted tag source {0}. It may already be in use."), FText::FromString(Info.RestrictedConfigName)));
						FSlateNotificationManager::Get().AddNotification(NotificationInfo);
						Info.RestrictedConfigName.Empty();
					}
				}
			}
		}

		// if we're adding a new restricted config file we will try to auto populate the owner
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMessageTagsSettings, RestrictedConfigFiles))
		{
			if (RestrictedConfigFilesTempCopy.Num() + 1 == RestrictedConfigFiles.Num())
			{
				int32 FoundIdx = RestrictedConfigFilesTempCopy.Num();
				for (int32 Idx = 0; Idx < RestrictedConfigFilesTempCopy.Num(); ++Idx)
				{
					if (RestrictedConfigFilesTempCopy[Idx] != RestrictedConfigFiles[Idx])
					{
						FoundIdx = Idx;
						break;
					}
				}

				ensure(FoundIdx < RestrictedConfigFiles.Num());
				RestrictedConfigFiles[FoundIdx].Owners.Add(FPlatformProcess::UserName());

			}
		}
		IMessageTagsModule::OnTagSettingsChanged.Broadcast();
	}
}
#endif

// ---------------------------------

UMessageTagsDeveloperSettings::UMessageTagsDeveloperSettings(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	
}

static const FName NAME_Advanced("Advanced");

FName UMessageTagsDeveloperSettings::GetCategoryName() const
{
	return NAME_Advanced;
}
