// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_NEWER_THAN(5, 0, 0)
#include "SMessageTagWidget.h"
#else
class SGameplayTagWidget;
#endif
class IDetailLayoutBuilder;

//////////////////////////////////////////////////////////////////////////
// FMessageTagsSettingsCustomization

class FMessageTagsSettingsCustomization : public IDetailCustomization
{
public:
	FMessageTagsSettingsCustomization();
	virtual ~FMessageTagsSettingsCustomization();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

#if !UE_VERSION_NEWER_THAN(5, 0, 0)
private:

	/** Callback for when a tag changes */
	void OnTagChanged();

	/** Module callback for when the tag tree changes */
	void OnTagTreeChanged();

	TSharedPtr<SMessageTagWidget> TagWidget;

	TSharedPtr<SMessageTagWidget> RestrictedTagWidget;
#endif
};
