//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPValueOneOf.h"

#include "GMPJsonUtils.h"
#include "GMPProtoUtils.h"
#include "GMPJsonSerializer.h"
#include "GMPProtoSerializer.h"

int32 FGMPValueOneOf::IterateKeyValueImpl(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPJsonUtils::IterateKeyValueImpl(*this, Idx, OutKey, OutValue);
	}
	else
	{
		return UGMPProtoUtils::IterateKeyValueImpl(*this, Idx, OutKey, OutValue);
	}
}

bool FGMPValueOneOf::LoadFromFile(const FString& FilePath, bool bBinary /*=false*/)
{
	if (!bBinary)
	{
		return GMP::Json::UStructFromJsonFile(*FilePath, *this);
	}
	else
	{
		return GMP::Proto::UStructFromProtoFile(*FilePath, *this);
	}
}

bool FGMPValueOneOf::AsValueImpl(FProperty* Prop, void* Out, FName SubKey, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPJsonUtils::AsValueImpl(*this, Prop, Out, SubKey);
	}
	else
	{
		return UGMPProtoUtils::AsValueImpl(*this, Prop, Out, SubKey);
	}
}
