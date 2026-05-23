//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "XConsoleCommandMeta.h"
#include "XConsoleManager.h"

static TMap<FString, FXConsoleObjectMeta>& GetConsoleMetaMap()
{
	static TMap<FString, FXConsoleObjectMeta> Map;
	return Map;
}

void IXConsoleManager::SetXConsoleMeta(const TCHAR* Name, FXConsoleObjectMeta&& Meta)
{
	GetConsoleMetaMap().Add(Name, MoveTemp(Meta));
}

const FXConsoleObjectMeta* IXConsoleManager::GetXConsoleMeta(const TCHAR* Name)
{
	return GetConsoleMetaMap().Find(Name);
}

FXConsoleMeta::~FXConsoleMeta()
{
	if (CmdName && *CmdName)
	{
		IXConsoleManager::SetXConsoleMeta(CmdName, MoveTemp(Meta));
	}
}

FXConsoleMeta::FParamBuilder& FXConsoleMeta::Param(int32 Index, const TCHAR* Name)
{
	while (Meta.Params.Num() <= Index)
		Meta.Params.AddDefaulted();

	if (Name)
		Meta.Params[Index].Name = Name;

	// Return builder from heap-stable storage
	static thread_local FParamBuilder* Builder = nullptr;
	delete Builder;
	Builder = new FParamBuilder(*this, Index);
	return *Builder;
}

FXConsoleMeta::FParamBuilder& FXConsoleMeta::FParamBuilder::Param(int32 Index, const TCHAR* Name)
{
	return Owner.Param(Index, Name);
}
