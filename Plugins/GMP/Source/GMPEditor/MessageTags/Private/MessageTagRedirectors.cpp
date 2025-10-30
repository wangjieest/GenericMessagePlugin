// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagRedirectors.h"
#include "MessageTagsSettings.h"
#include "Misc/ConfigCacheIni.h"

FMessageTagRedirectors& FMessageTagRedirectors::Get()
{
	static FMessageTagRedirectors Singleton;
	return Singleton;
}

FMessageTagRedirectors::FMessageTagRedirectors()
{
	UMessageTagsSettings* MutableDefault = GetMutableDefault<UMessageTagsSettings>();

	// Check the deprecated location
	bool bFoundDeprecated = false;

#if UE_5_04_OR_LATER
	const FConfigSection* PackageRedirects = GConfig->GetSection(TEXT("/Script/Engine.Engine"), false, GEngineIni);
#else
	const FConfigSection* PackageRedirects = GConfig->GetSectionPrivate(TEXT("/Script/Engine.Engine"), false, true, GEngineIni);
#endif
	if (PackageRedirects)
	{
		for (FConfigSection::TConstIterator It(*PackageRedirects); It; ++It)
		{
			if (It.Key() == TEXT("+MessageTagRedirects"))
			{
				FName OldTagName = NAME_None;
				FName NewTagName;

				if (FParse::Value(*It.Value().GetValue(), TEXT("OldTagName="), OldTagName))
				{
					if (FParse::Value(*It.Value().GetValue(), TEXT("NewTagName="), NewTagName))
					{
						FMessageTagRedirect Redirect;
						Redirect.OldTagName = OldTagName;
						Redirect.NewTagName = NewTagName;

						MutableDefault->MessageTagRedirects.AddUnique(Redirect);

						bFoundDeprecated = true;
					}
				}
			}
		}
	}

	if (bFoundDeprecated)
	{
		UE_LOG(LogMessageTags, Error, TEXT("MessageTagRedirects is in a deprecated location, after editing MessageTags developer settings you must remove these manually"));
	}

#if WITH_EDITOR
	// Only doing the deprecated parse once at startup is fine, but we need to update from the settings object after in-editor config updates
	// This is a singleton that is never destroyed, so just bind raw once
	UMessageTagsManager::OnEditorRefreshMessageTagTree.AddRaw(this, &FMessageTagRedirectors::RefreshTagRedirects);
#endif

	RefreshTagRedirects();
}

void FMessageTagRedirectors::AddRedirectsFromSource(const FMessageTagSource* Source)
{
	if (Source && Source->SourceTagList)
	{
		AddRedirects(Source->SourceTagList->MessageTagRedirects);
	}
}

void FMessageTagRedirectors::RefreshTagRedirects()
{
	TagRedirects.Empty();

	// Check settings object
	const UMessageTagsSettings* Default = GetDefault<UMessageTagsSettings>();
	AddRedirects(Default->MessageTagRedirects);

	// Add redirects from all TagList sources
	UMessageTagsManager& MessageTagsManager = UMessageTagsManager::Get();
	TArray<FMessageTagRedirect> RedirectsFromTagSources;
	TArray<const FMessageTagSource*> TagListSources;
	MessageTagsManager.FindTagSourcesWithType(EMessageTagSourceType::TagList, TagListSources);
	for (const FMessageTagSource* Source : TagListSources)
	{
		AddRedirectsFromSource(Source);
	}

}

const FMessageTag* FMessageTagRedirectors::RedirectTag(const FName& InTagName) const
{
	if (const FMessageTag* NewTag = TagRedirects.Find(InTagName))
	{
		return NewTag;
	}

	return nullptr;
}

void FMessageTagRedirectors::AddRedirects(const TArray<FMessageTagRedirect>& Redirects)
{
	for (const FMessageTagRedirect& Redirect : Redirects)
	{
		FName OldTagName = Redirect.OldTagName;
		FName NewTagName = Redirect.NewTagName;

		if (OldTagName == NewTagName)
			continue;

		const FMessageTag* ExistingRedirect = TagRedirects.Find(OldTagName);
		if (!ExistingRedirect)
		{
			// Attempt to find multiple redirect hops and flatten the redirection so we only need to redirect once
			// to resolve the update.  Includes a basic infinite recursion guard, in case the redirects loop.
			int32 IterationsLeft = 10;
			while (NewTagName != NAME_None)
			{
				bool bFoundRedirect = false;

				// See if it got redirected again
				for (const FMessageTagRedirect& SecondRedirect : Redirects)
				{
					if (SecondRedirect.OldTagName == NewTagName)
					{
						NewTagName = SecondRedirect.NewTagName;
						bFoundRedirect = true;
						break;
					}
				}
				IterationsLeft--;

				if (!bFoundRedirect)
				{
					break;
				}

				if (IterationsLeft <= 0)
				{
					UE_LOG(LogMessageTags, Warning, TEXT("Invalid new tag %s!  Cannot replace old tag %s."), *Redirect.NewTagName.ToString(), *Redirect.OldTagName.ToString());
					break;
				}
			}

			// Populate the map
			TagRedirects.Add(OldTagName, FMessageTag(NewTagName));
		}
		else
		{
			ensureMsgf(ExistingRedirect->GetTagName() == NewTagName, TEXT("Old tag %s is being redirected to more than one tag. Please remove all the redirections except for one. NewTagName:%s ExistingRedirect:%s"), *OldTagName.ToString(), *NewTagName.ToString(), *ExistingRedirect->GetTagName().ToString());
		}
	}
}

#if WITH_EDITOR && UE_5_06_OR_LATER
void FMessageTagRedirectors::Hash(FBlake3& Hasher)
{
	TArray<FName> SortedKeys;
	TagRedirects.GenerateKeyArray(SortedKeys);
	Algo::Sort(SortedKeys, FNameLexicalLess());

	FNameBuilder Builder;
	for (FName Key : SortedKeys)
	{
		FMessageTag* Value = TagRedirects.Find(Key);
		check(Value);
		Builder.Reset();
		Builder << Key;
		for (TCHAR& C : Builder)
		{
			C = FChar::ToLower(C);
		}
		Hasher.Update(*Builder, Builder.Len() * sizeof(**Builder));
		Builder.Reset();
		Builder << Value->GetTagName();
		for (TCHAR& C : Builder)
		{
			C = FChar::ToLower(C);
		}
		Hasher.Update(*Builder, Builder.Len() * sizeof(**Builder));
	}
}
#endif
