// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "MessageTagContainer.h"

// Forward declarations
class UEdGraphPin;
class FString;

/** Utility functions for Message tag-related pins */
namespace UE::MessageTags::EditorUtilities
{
	/**
	 * Given a editor graph pin representing a tag or container, extract the appropriate filter
	 * string from any associated metadata
	 * 
	 * @param InTagPin	Pin to extract from
	 * 
	 * @return Filter string, if any, to apply from the tag pin based on metadata
	 */
	FString ExtractTagFilterStringFromGraphPin(UEdGraphPin* InTagPin);

	/**
	 * Exports a Message tag to text compatible with Import/Export text.
	 * @param Tag tag to export
	 * @return tag as text.
	 */
	FString MessageTagExportText(const FMessageTag Tag);

	/**
     * Tries to import Message tag from text.
	 * @param Text string to import from
	 * @param PortFlags EPropertyPortFlags controlling import behavior. 
	 * @return parsed Message tag, or None if import failed. 
     */
	FMessageTag MessageTagTryImportText(const FString& Text, const int32 PortFlags = 0);

	/**
	 * Exports a Message tag container to text compatible with Import/Export text.
	 * @param TagContainer tag container to export
	 * @return tag container as text.
	 */
	FString MessageTagContainerExportText(const FMessageTagContainer& TagContainer);

	/**
     * Tries to import Message tag container from text.
	 * @param Text string to import from
	 * @param PortFlags EPropertyPortFlags controlling import behavior.
	 * @return parsed Message tag container, or empty if import failed. 
     */
	FMessageTagContainer MessageTagContainerTryImportText(const FString& Text, int32 PortFlags = 0);

	/**
	 * Exports a Message tag query to text compatible with Import/Export text.
	 * @param TagQuery tag query to export
	 * @return tag query as text.
	 */
	FString MessageTagQueryExportText(const FMessageTagQuery& TagQuery);
	
	/**
	 * Tries to import Message tag query from text.
	 * @param Text string to import from
	 * @param PortFlags EPropertyPortFlags controlling import behavior.
	 * @return parsed Message tag query, or empty if import failed. 
	 */
	FMessageTagQuery MessageTagQueryTryImportText(const FString Text, int32 PortFlags = 0);

	/**
	 * Formats Message Tag Query description to multiple lines.
	 * @param Desc Description to format
	 * @return Description in multiline format.
	 */
	FString FormatMessageTagQueryDescriptionToLines(const FString& Desc);

} // UE::MessageTags::EditorUtilities
