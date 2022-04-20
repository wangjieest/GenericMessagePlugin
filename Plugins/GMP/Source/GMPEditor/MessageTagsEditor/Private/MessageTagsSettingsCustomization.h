// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor/PropertyEditor/Public/IDetailCustomization.h"
#include "SMessageTagWidget.h"

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

private:

	/** Callback for when a tag changes */
	void OnTagChanged();

	/** Module callback for when the tag tree changes */
	void OnTagTreeChanged();

	TSharedPtr<SMessageTagWidget> TagWidget;

	TSharedPtr<SMessageTagWidget> RestrictedTagWidget;
};
