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
}  // namespace Serializer
}  // namespace GMP
