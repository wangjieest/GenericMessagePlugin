// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/CoreMiscDefines.h"

struct UE_DEPRECATED(5.3, "Options are not used anymore to customize the property customization behavior.") MESSAGETAGSEDITOR_API FMessageTagContainerCustomizationOptions
{
	// If true, any Message Tag Widget created should not offer an 'Add Tag' option 
	bool bForceHideAddTag = false;

	// If true, any created Message Tag Widget created should not offer an 'Add Tag Source' option 
	bool bForceHideAddTagSource = false;
};
