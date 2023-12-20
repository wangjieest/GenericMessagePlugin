//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPSerializer.h"

#include "UObject/NameTypes.h"

namespace GMP
{
namespace Serializer
{
	const FLazyName NAME_Text = TEXT("Text");
	const FLazyName NAME_DateTime = TEXT("DateTime");
	const FLazyName NAME_Guid = TEXT("Guid");
	const FLazyName NAME_Color = TEXT("Color");
	const FLazyName NAME_LinearColor = TEXT("LinearColor");
	const FLazyName NAME_MemResVersion = TEXT("MemResVersion");
	const TCHAR* Str_Ticks = TEXT("Ticks");
	const TCHAR* Str_Max = TEXT("max");
	const TCHAR* Str_Min = TEXT("min");
	const TCHAR* Str_FutureNow = TEXT("now");

	bool StripUserDefinedStructName(FStringView& InOutName)
	{
		FStringView NameView = InOutName;
		const int32 GuidStrLen = 32;
		const int32 MinimalPostfixlen = GuidStrLen + 3;
		if (NameView.Len() > MinimalPostfixlen)
		{
			NameView.LeftChopInline(GuidStrLen + 1);
			int32 FirstCharToRemove = -1;
			const bool bCharFound = NameView.FindLastChar(TCHAR('_'), FirstCharToRemove);
			if (bCharFound && (FirstCharToRemove > 0))
			{
				InOutName = FStringView(NameView.GetData(), FirstCharToRemove);
				return true;
			}
		}
		return false;
	}

	bool StripUserDefinedStructName(FString& InOutName)
	{
		FStringView NameView = InOutName;
		const int32 GuidStrLen = 32;
		const int32 MinimalPostfixlen = GuidStrLen + 3;
		if (NameView.Len() > MinimalPostfixlen)
		{
			NameView.LeftChopInline(GuidStrLen + 1);
			int32 FirstCharToRemove = -1;
			const bool bCharFound = NameView.FindLastChar(TCHAR('_'), FirstCharToRemove);
			if (bCharFound && (FirstCharToRemove > 0))
			{
				InOutName = FString(FirstCharToRemove, NameView.GetData());
				return true;
			}
		}
		return false;
	}

	FString AsFString(const ANSICHAR* Str, int64 Len)
	{
		FString Ret;
		auto Size = FUTF8ToTCHAR_Convert::ConvertedLength(Str, Len);
		Ret.GetCharArray().Reserve(Size + 1);
		Ret.GetCharArray().AddUninitialized(Size);
		Ret.GetCharArray().Add('\0');
		FUTF8ToTCHAR_Convert::Convert(&Ret[0], Size, Str, Len);
		return Ret;
	}

	FName GetAuthoredFNameForField(FName InName)
	{
		TStringBuilder<256> StrBuilder;
		InName.ToString(StrBuilder);
		FStringView NameView(StrBuilder.GetData(), StrBuilder.Len());
		if (!StripUserDefinedStructName(NameView))
			return InName;
		else
			return FName(NameView.Len(), NameView.GetData());
	}

}  // namespace Serializer
}  // namespace GMP
