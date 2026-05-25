//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProcessLock.h"
#include "GMP/GMPJsonSerializer.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GMPValueOneOf.h"

FGMPProcessLock::~FGMPProcessLock()
{
	Unlock();
}

FGMPProcessLock::FGMPProcessLock(FGMPProcessLock&& Other) noexcept
	: FileHandle(Other.FileHandle)
	, LockFilePath(MoveTemp(Other.LockFilePath))
{
	Other.FileHandle = nullptr;
}

FGMPProcessLock& FGMPProcessLock::operator=(FGMPProcessLock&& Other) noexcept
{
	if (this != &Other)
	{
		Unlock();
		FileHandle = Other.FileHandle;
		LockFilePath = MoveTemp(Other.LockFilePath);
		Other.FileHandle = nullptr;
	}
	return *this;
}

bool FGMPProcessLock::TryLock(const FString& InLockFilePath)
{
	if (FileHandle)
	{
		UE_LOG(LogGMP, Warning, TEXT("[InstanceLock] already locked: %s"), *LockFilePath);
		return true;
	}
	if (LockPid != 0)
	{
		FProcHandle Proc = FPlatformProcess::OpenProcess(LockPid);
		if (Proc.IsValid())
		{
			return false;
		}
	}

	LockFilePath = InLockFilePath;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(LockFilePath));

	if (PlatformFile.FileExists(*LockFilePath) && CanLock(LockFilePath))
	{
		UE_LOG(LogGMP, Log, TEXT("[InstanceLock] clean locks: %s"), *LockFilePath);
		PlatformFile.DeleteFile(*LockFilePath);
	}

	FileHandle = PlatformFile.OpenWrite(*LockFilePath, /*bAppend=*/false, /*bAllowRead=*/true);

	if (!FileHandle)
	{
		UE_LOG(LogGMP, Warning, TEXT("[InstanceLock] lock failed，another instance is running: %s"), *LockFilePath);
		LockFilePath.Empty();
		return false;
	}

	WriteProcessInfo();

	UE_LOG(LogGMP, Log, TEXT("[InstanceLock] lock successful PID=%u, File=%s"), FPlatformProcess::GetCurrentProcessId(), *LockFilePath);
	return true;
}

void FGMPProcessLock::Unlock()
{
	if (!FileHandle)
	{
		return;
	}

	UE_LOG(LogGMP, Log, TEXT("[InstanceLock] release lock: %s"), *LockFilePath);

	delete FileHandle;
	FileHandle = nullptr;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteFile(*LockFilePath);

	LockFilePath.Empty();
}

void FGMPProcessLock::WriteProcessInfo()
{
	if (!FileHandle)
	{
		return;
	}

	GMP::Json::FJsonObjBuilder JsonBuilder;
	JsonBuilder.AddKeyValue(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	JsonBuilder.AddKeyValue(TEXT("timestamp"), FDateTime::UtcNow());

	FileHandle->Seek(0);
	TArray<uint8> Arr = TArray<uint8>(JsonBuilder);
	FileHandle->Write((const uint8*)Arr.GetData(), Arr.Num());
	FileHandle->Flush();
}

bool FGMPProcessLock::ReadLockInfo(const FString& InLockFilePath, uint32& OutPID, FDateTime& OutTimestamp)
{
	FGMPValueOneOf ValueOneOf;
	if (!ValueOneOf.LoadFromFile(InLockFilePath))
	{
		return false;
	}

	ValueOneOf.AsValue(OutPID, TEXT("pid"));
	ValueOneOf.AsValue(OutTimestamp, TEXT("timestamp"));
	return true;
}

bool FGMPProcessLock::CanLock(const FString& InLockFilePath)
{
	FDateTime LockTime;
	uint32 LockPID = 0;
	if (!ReadLockInfo(InLockFilePath, LockPID, LockTime))
	{
		return true;
	}

	FProcHandle Proc = FPlatformProcess::OpenProcess(LockPID);
	if (!Proc.IsValid())
	{
		return true;
	}

	LockPid = LockPID;
	FPlatformProcess::CloseProc(Proc);
	return false;
}

FString FGMPProcessLock::GetDefaultLockFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("GMPLocks/Editor.lock");
}