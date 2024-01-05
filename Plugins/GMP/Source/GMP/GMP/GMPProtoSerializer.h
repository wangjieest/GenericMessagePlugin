//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPReflection.h"
#include "GMPSerializer.h"
#include "GMPUnion.h"
#include "Misc/FileHelper.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/UnrealTypeTraits.h"

namespace GMP
{
namespace PB
{
	GMP_API bool AddProto(const char* InBuf, uint32 InSize);
	GMP_API bool AddProtos(const char* InBuf, uint32 InSize);
	GMP_API void ClearProtos();

	namespace Serializer
	{
		GMP_API uint32 UStructToPBImpl(FArchive& Ar, UScriptStruct* Struct, const void* StructAddr);
		GMP_API uint32 UStructToPBImpl(TArray<uint8>& Out, UScriptStruct* Struct, const void* StructAddr);
	}  // namespace Serializer
	template<typename T>
	uint32 UStructToPB(T& Out, UScriptStruct* Struct, const uint8* ValueAddr)
	{
		return Serializer::UStructToPBImpl(Out, Struct, ValueAddr);
	}
	template<typename T, typename DataType>
	uint32 UStructToPB(T& Out, const DataType& Data)
	{
		return UStructToPB(Out, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
	}

	namespace Deserializer
	{
		GMP_API uint32 UStructFromPBImpl(FArchive& Ar, UScriptStruct* Struct, void* StructAddr);
		GMP_API uint32 UStructFromPBImpl(TArrayView<const uint8> In, UScriptStruct* Struct, void* StructAddr);
	}  // namespace Deserializer
	template<typename T>
	uint32 UStructFromPB(T&& In, UScriptStruct* Struct, uint8* OutStructAddr)
	{
		return Deserializer::UStructFromPBImpl(Forward<T>(In), Struct, OutStructAddr);
	}
	template<typename T, typename DataType>
	uint32 UStructFromPB(T&& In, DataType& OutData)
	{
		return UStructFromPB(Forward<T>(In), GMP::TypeTraits::StaticStruct<DataType>(), (uint8*)std::addressof(OutData));
	}
}  // namespace PB
}  // namespace GMP
