//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "GMPValueOneOf.h"

#include "GMPJsonValue.h"

int32 FGMPValueOneOf::IterateKeyValueImpl(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPValueOneOfJsonHelper::IterateKeyValueImpl(*this, Idx, OutKey, OutValue);
	}
	return 0;
}

bool FGMPValueOneOf::AsValueImpl(const FProperty* Prop, void* Out, FName SubKey, bool bBinary) const
{
	if (!bBinary)
	{
		return UGMPValueOneOfJsonHelper::AsValueImpl(*this, Prop, Out, SubKey);
	}
	return false;
}
