// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserFrontEndFilterExtension.h"
#include "MessageTagSearchFilter.generated.h"

UCLASS()
class UMessageTagSearchFilter : public UContentBrowserFrontEndFilterExtension
{
public:
	GENERATED_BODY()

	// UContentBrowserFrontEndFilterExtension interface
	virtual void AddFrontEndFilterExtensions(TSharedPtr<class FFrontendFilterCategory> DefaultCategory, TArray< TSharedRef<class FFrontendFilter> >& InOutFilterList) const override;
	// End of UContentBrowserFrontEndFilterExtension interface
};
