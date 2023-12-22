//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "GMPValueOneOf.h"

#include "GMPJsonBPLib.h"
#include "GMPOneOfBPLib.h"

int32 FGMPValueOneOf::IterateKeyValueImpl(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPJsonUtils::IterateKeyValueImpl(*this, Idx, OutKey, OutValue);
	}
	else
	{
		return UGMPOneOfUtils::IterateKeyValueImpl(*this, Idx, OutKey, OutValue);
	}
	return 0;
}

bool FGMPValueOneOf::AsValueImpl(FProperty* Prop, void* Out, FName SubKey, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPJsonUtils::AsValueImpl(*this, Prop, Out, SubKey);
	}
	else
	{
		return UGMPOneOfUtils::AsValueImpl(*this, Prop, Out, SubKey);
	}
	return false;
}
