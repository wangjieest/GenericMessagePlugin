//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPReflection.h"
#include "GMPSerializer.h"
#include "GMPUnion.h"
#include "Misc/FileHelper.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/UnrealTypeTraits.h"

#if defined(GMP_WITH_UPB)
namespace GMP
{
namespace PB
{
	GMP_API bool AddProto(const char* InBuf, uint32 InSize);
	GMP_API bool AddProtos(const char* InBuf, uint32 InSize);
	GMP_API void ClearProtos();

	namespace Serializer
	{
		GMP_API bool UStructToProtoImpl(FArchive& Ar, const UScriptStruct* Struct, const void* StructAddr);
		GMP_API bool UStructToProtoImpl(TArray<uint8>& Out, const UScriptStruct* Struct, const void* StructAddr);
	}  // namespace Serializer
	template<typename T>
	bool UStructToProto(T& Out, const UScriptStruct* Struct, const uint8* ValueAddr)
	{
		return Serializer::UStructToProtoImpl(Out, Struct, ValueAddr);
	}
	template<typename T, typename DataType>
	bool UStructToProto(T& Out, const DataType& Data)
	{
		return UStructToProto(Out, GMP::TypeTraits::StaticStruct<DataType>(), (const uint8*)std::addressof(Data));
	}

	namespace Deserializer
	{
		GMP_API bool UStructFromProtoImpl(FArchive& Ar, const UScriptStruct* Struct, void* StructAddr);
		GMP_API bool UStructFromProtoImpl(TConstArrayView<uint8> In, const UScriptStruct* Struct, void* StructAddr);
	}  // namespace Deserializer
	template<typename T>
	bool UStructFromProto(T&& In, const UScriptStruct* Struct, uint8* OutStructAddr)
	{
		return Deserializer::UStructFromProtoImpl(Forward<T>(In), Struct, OutStructAddr);
	}
	template<typename T, typename DataType>
	bool UStructFromProto(T&& In, DataType& OutData)
	{
		return UStructFromProto(Forward<T>(In), GMP::TypeTraits::StaticStruct<DataType>(), (uint8*)std::addressof(OutData));
	}
}  // namespace PB
}  // namespace GMP
#endif