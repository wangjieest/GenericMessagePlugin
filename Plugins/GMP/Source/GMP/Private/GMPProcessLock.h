//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "HAL/PlatformFileManager.h"

class FGMPProcessLock
{
public:
	FGMPProcessLock() = default;
	~FGMPProcessLock();

	FGMPProcessLock(const FGMPProcessLock&) = delete;
	FGMPProcessLock& operator=(const FGMPProcessLock&) = delete;
	FGMPProcessLock(FGMPProcessLock&& Other) noexcept;
	FGMPProcessLock& operator=(FGMPProcessLock&& Other) noexcept;
	bool TryLock(const FString& InLockFilePath);
	void Unlock();
	bool IsLocked() const { return FileHandle != nullptr; }
	const FString& GetLockFilePath() const { return LockFilePath; }
	static bool ReadLockInfo(const FString& InLockFilePath, uint32& OutPID, FDateTime& OutTimestamp);
	static bool IsLockStale(const FString& InLockFilePath);
	static FString GetDefaultLockFilePath();

private:
	void WriteProcessInfo();
	uint32 LockPid = 0;
	IFileHandle* FileHandle = nullptr;
	FString LockFilePath;
};