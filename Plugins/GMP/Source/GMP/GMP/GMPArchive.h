//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Serialization/MemoryArchive.h"
#include "UObject/CoreNet.h"

struct FGMPTypedAddr;
class APlayerController;
class UPackageMap;
namespace GMP
{
class FGMPMemoryArchive : public FMemoryArchive
{
protected:
	virtual FString GetArchiveName() const { return TEXT("FGMPMemoryArchive"); }
	virtual FArchive& operator<<(UObject*& Object) override;
};

class GMP_API FGMPMemoryWriter : public FGMPMemoryArchive
{
public:
	FGMPMemoryWriter(uint8* InData, uint32* InSize);

	virtual int64 TotalSize() override;
	virtual void Serialize(void* Data, int64 Num) override;

private:
	uint32* MemSizeData;
	uint8* MemPayloadData;
};

class GMP_API FGMPMemoryReader : public FGMPMemoryArchive
{
public:
	FGMPMemoryReader(uint8* InData, uint32 InSize);

	virtual int64 TotalSize() override;
	virtual void Serialize(void* Data, int64 Num) override;

private:
	uint32 MemSizeData;
	uint8* MemPayloadData;
};

class GMP_API FGMPNetBitWriter : public FNetBitWriter
{
public:
	FGMPNetBitWriter(UPackageMap* InPackageMap, int64 InMaxBits = 0);
	FGMPNetBitWriter(APlayerController* PC, int64 InMaxBits = 0);

	virtual FArchive& operator<<(UObject*& Object) override;
};

class GMP_API FGMPNetBitReader : public FNetBitReader
{
public:
	FGMPNetBitReader(UPackageMap* InPackageMap = nullptr, uint8* Src = nullptr, int64 CountBits = 0);
	FGMPNetBitReader(APlayerController* PC, uint8* Src = nullptr, int64 CountBits = 0);

	virtual FArchive& operator<<(UObject*& Object) override;
};

class GMP_API FGMPNetFrameWriter final : public FGMPNetBitWriter
{
public:
	FGMPNetFrameWriter(UFunction* Function, const TArray<FGMPTypedAddr>& Params, UPackageMap* InPackageMap = nullptr, int64 CountBits = 0);
	FGMPNetFrameWriter(UFunction* Function, const TArray<FGMPTypedAddr>& Params, APlayerController* PC, int64 CountBits = 0);
	explicit operator bool() const { return bSucc; }
	FORCEINLINE const TArray<uint8>* GetBuffer() const { return FGMPNetBitWriter::GetBuffer(); }
	FORCEINLINE TArray<uint8>* GetBuffer() { return const_cast<TArray<uint8>*>(FGMPNetBitWriter::GetBuffer()); }

protected:
	bool bSucc;
};

class GMP_API FGMPNetFrameReader final : public FGMPNetBitReader
{
public:
	FGMPNetFrameReader(UFunction* Function, void* FramePtr, UPackageMap* InPackageMap = nullptr, uint8* Src = nullptr, int64 CountBits = 0)
		: FGMPNetBitReader(InPackageMap, Src, CountBits)
		, FrameBuilder(*this, Function, FramePtr, PackageMap)
	{
	}
	FGMPNetFrameReader(UFunction* Function, void* FramePtr, APlayerController* PC, uint8* Src = nullptr, int64 CountBits = 0)
		: FGMPNetBitReader(PC, Src, CountBits)
		, FrameBuilder(*this, Function, FramePtr, PackageMap)
	{
	}
	explicit operator bool() const { return FrameBuilder.bSucc; }

protected:
	struct FGMPFrameBuilder
	{
		FGMPFrameBuilder(FArchive& ArToLoad, UFunction* Function, void* FramePtr, UPackageMap* PackageMap);
		~FGMPFrameBuilder();
		explicit operator bool() const { return bSucc; }

		FGMPFrameBuilder(const FGMPFrameBuilder&) = delete;
		FGMPFrameBuilder& operator=(const FGMPFrameBuilder&) = delete;

		UFunction* Func;
		void* Ptr;
		bool bSucc;
	};

	FGMPFrameBuilder FrameBuilder;
};

// class FGMPArchive : public FArchive
// {
// protected:
// 	virtual FString GetArchiveName() const { return TEXT("FGMPSteamArchive"); }
// 	virtual FArchive& operator<<(UObject*& Object) override;
//
// protected:
// 	IFileHandle* FileHandle;
// };
//
// class GMP_API FGMPSteamWriter : public FGMPArchive
// {
// public:
// 	FGMPSteamWriter(IFileHandle* InFileHandle);
//
// 	virtual int64 TotalSize() override;
// 	virtual void Serialize(void* Data, int64 Num) override;
// };
//
// class GMP_API FGMPStreamReader : public FGMPArchive
// {
// public:
// 	FGMPStreamReader(IFileHandle* InFileHandle);
//
// 	virtual int64 TotalSize() override;
// 	virtual void Serialize(void* Data, int64 Num) override;
// };

}  // namespace GMP
