//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoUtils.h"

void UProtoDescriptor::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Append(Deps);
}

void UProtoDescriptor::RegisterProto()
{
	if (bRegistered)
		return;
	bRegistered = true;

	for (auto Dep : Deps)
	{
		if (Dep)
		{
			Dep->RegisterProto();
		}
	}

	//
	extern void RegisterProtoDesc(const char*, size_t);
	RegisterProtoDesc(reinterpret_cast<const char*>(Desc.GetData()), Desc.Num());
}

void UProtoDefinedStruct::PostLoad()
{
	Super::PostLoad();
	if (auto Desc = ProtoDesc.LoadSynchronous())
	{
		Desc->RegisterProto();
	}
}

void UProtoDefinedStruct::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	if (auto Desc = ProtoDesc.LoadSynchronous())
	{
		OutDeps.Add(Desc);
	}
}

void UProtoDefinedEnum::PostLoad()
{
	Super::PostLoad();
	if (auto Desc = ProtoDesc.LoadSynchronous())
	{
		Desc->RegisterProto();
	}
}

void UProtoDefinedEnum::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	if (auto Desc = ProtoDesc.LoadSynchronous())
	{
		OutDeps.Add(Desc);
	}
}
