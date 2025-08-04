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

bool FGMPValueOneOf::FromJsonStr(const FStringView& Content)
{
	return GMP::Json::UStructFromJson(Content, *this);
}
bool FGMPValueOneOf::ToJsonStr(FString& Out) const
{
	return GMP::Json::UStructToJson(Out, *this);
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

bool FGMPValueOneOf::AsValueImpl(FProperty* ResultProp, void* Out, TConstArrayView<FName> SubKeys, bool bBinary) const
{
	check(SubKeys.Num());
	FGMPValueOneOf Val = *this;
	static auto OneOfProp = GMP::TClass2Prop<FGMPValueOneOf>::GetProperty();
	if (!bBinary)
	{
		for (auto i = 0; i < SubKeys.Num() - 1; ++i)
		{
			if (UGMPJsonUtils::AsValueImpl(Val, OneOfProp, &Val, SubKeys[i]))
			{
				return false;
			}
		}
		return UGMPJsonUtils::AsValueImpl(Val, ResultProp, Out, SubKeys.Last());
	}
	else
	{
		for (auto i = 0; i < SubKeys.Num() - 1; ++i)
		{
			if (UGMPProtoUtils::AsValueImpl(Val, OneOfProp, &Val, SubKeys[i]))
			{
				return false;
			}
		}
		return UGMPProtoUtils::AsValueImpl(Val, ResultProp, Out, SubKeys.Last());
	}
}
