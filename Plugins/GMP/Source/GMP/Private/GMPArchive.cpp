//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPArchive.h"

#include "CoreUObject.h"

#include "Engine/NetConnection.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/World.h"
#include "GMPBPLib.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ObjectKey.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace GMP
{
//////////////////////////////////////////////////////////////////////////
namespace SerializeFlag
{
	enum Type : uint8
	{
		None,
		Save,
		Load,
		Both,
	};
}

template<SerializeFlag::Type StaticFlag, typename T>
FORCEINLINE void NetSerializeObject(T* Ar, UObject*& Object, SerializeFlag::Type RuntimeFlag = StaticFlag)
{
#if 0  // GMP_DIRECT_SERIALIZE_OBJECT
	Ar->Serialize(&Object, sizeof(void*));
#else
	if ((StaticFlag & RuntimeFlag & SerializeFlag::Save) == SerializeFlag::Save)
	{
		FObjectKey ObjectKey(Object);
		Ar->Serialize(&ObjectKey, sizeof(ObjectKey));
	}
	else if ((StaticFlag & RuntimeFlag & SerializeFlag::Load) == SerializeFlag::Load)
	{
		FObjectKey ObjectKey;
		Ar->Serialize(&ObjectKey, sizeof(ObjectKey));
		Object = ObjectKey.ResolveObjectPtr();
	}
#endif
}

FArchive& FGMPMemoryArchive::operator<<(UObject*& Object)
{
	NetSerializeObject<SerializeFlag::Both>(this, Object, IsSaving() ? SerializeFlag::Save : SerializeFlag::Load);
	return *this;
}

FGMPMemoryReader::FGMPMemoryReader(uint8* InData, uint32 InSize)
{
	MemSizeData = InSize;
	MemPayloadData = InData;
#if UE_4_20_OR_LATER
	this->SetIsLoading(true);
#else
	ArIsLoading = true;
#endif
}

int64 FGMPMemoryReader::TotalSize()
{
	return MemSizeData;
}

void FGMPMemoryReader::Serialize(void* Data, int64 Num)
{
	if (Num > 0 && !ArIsError)
	{
		if (Offset + Num <= TotalSize())
		{
			FMemory::Memcpy(Data, MemPayloadData + Offset, Num);
			Offset += Num;
		}
		else
		{
			ArIsError = true;
		}
	}
}

FGMPMemoryWriter::FGMPMemoryWriter(uint8* InData, uint32* InSize)
{
	MemSizeData = InSize;
	MemPayloadData = InData;
#if UE_4_20_OR_LATER
	this->SetIsSaving(true);
#else
	ArIsSaving = true;
#endif
	*MemSizeData = 0;
}

int64 FGMPMemoryWriter::TotalSize()
{
	return *MemSizeData;
}

void FGMPMemoryWriter::Serialize(void* Data, int64 Num)
{
	if (Num > 0)
	{
		FMemory::Memcpy(MemPayloadData + Offset, Data, Num);

		Offset += Num;

		*MemSizeData = Offset;
	}
}

FGMPNetBitReader::FGMPNetBitReader(APlayerController* PC, uint8* Src, int64 CountBits)
	: FNetBitReader(UGMPBPLib::GetPackageMap(PC), Src, CountBits)
{
}

FGMPNetBitReader::FGMPNetBitReader(UPackageMap* InPackageMap, uint8* Src, int64 CountBits)
	: FNetBitReader(InPackageMap, Src, CountBits)
{
}

FArchive& FGMPNetBitReader::operator<<(UObject*& Object)
{
	if (PackageMap)
	{
		PackageMap->SerializeObject(*this, UObject::StaticClass(), Object);
	}
	else
	{
		NetSerializeObject<SerializeFlag::Load>(this, Object);
	}

	return *this;
}

FGMPNetBitWriter::FGMPNetBitWriter(APlayerController* PC, int64 InMaxBits)
	: FNetBitWriter(UGMPBPLib::GetPackageMap(PC), InMaxBits)
{
}

FGMPNetBitWriter::FGMPNetBitWriter(UPackageMap* InPackageMap, int64 InMaxBits)
	: FNetBitWriter(InPackageMap, InMaxBits)
{
}

FArchive& FGMPNetBitWriter::operator<<(UObject*& Object)
{
	if (PackageMap)
	{
		PackageMap->SerializeObject(*this, UObject::StaticClass(), Object);
	}
	else
	{
		NetSerializeObject<SerializeFlag::Save>(this, Object);
	}
	return *this;
}

extern void DestroyFunctionParameters(UFunction* Function, void* Ptr);

FGMPNetFrameReader::FGMPFrameBuilder::~FGMPFrameBuilder()
{
	if (bSucc)
		DestroyFunctionParameters(Func, Ptr);
}

FGMPNetFrameReader::FGMPFrameBuilder::FGMPFrameBuilder(FArchive& ArToLoad, UFunction* Function, void* FramePtr, UPackageMap* PackageMap)
	: Func(Function)
	, Ptr(FramePtr)
	, bSucc(UGMPBPLib::ArchiveToFrame(ArToLoad, Function, FramePtr, PackageMap))
{
}

FGMPNetFrameWriter::FGMPNetFrameWriter(UFunction* Function, const TArray<FGMPTypedAddr>& Params, UPackageMap* InPackageMap, int64 CountBits)
	: FGMPNetBitWriter(InPackageMap, CountBits)
	, bSucc(UGMPBPLib::MessageToArchive(*this, Function, Params, PackageMap))
{
}

FGMPNetFrameWriter::FGMPNetFrameWriter(UFunction* Function, const TArray<FGMPTypedAddr>& Params, APlayerController* PC, int64 CountBits)
	: FGMPNetBitWriter(PC, CountBits)
	, bSucc(UGMPBPLib::MessageToArchive(*this, Function, Params, PackageMap))
{
}
}  // namespace GMP
