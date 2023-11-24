// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FMessageTagStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static ISlateStyle& Get() { return *StyleSet.Get(); }
	static FName GetStyleSetName();

	static const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = nullptr)
	{
		return StyleSet->GetBrush(PropertyName, Specifier);
	}

	static const FLinearColor& GetColor(FName PropertyName, const ANSICHAR* Specifier = nullptr)
	{
		return StyleSet->GetColor(PropertyName, Specifier);
	}

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
