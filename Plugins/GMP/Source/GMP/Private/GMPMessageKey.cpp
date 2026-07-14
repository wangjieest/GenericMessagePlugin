//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#include "GMPMessageKey.h"
#include "GMPSignalsImpl.h"
#include "GMPStruct.h"
#include "XConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"

namespace GMP
{
static bool GGMPTraceScriptSource = true;
static FXConsoleVariableRef CVarGMPTraceScriptSource(
	TEXT("gmp.TraceScriptSource"),
	GGMPTraceScriptSource,
	TEXT("Collect Blueprint/Lua source locations for GMP message tags (editor only)."),
	ECVF_Default);

bool IsScriptSourceTraceEnabled()
{
	return GGMPTraceScriptSource;
}

#if GMP_TRACE_MSG_STACK
	static TMap<FMSGKEY, TSet<FString>> MsgkeyLocations;
	static TMap<FMSGKEY, TArray<FString>> MsgkeyListenLocations;
	static TMap<FMSGKEY, TArray<FString>> MsgkeyNotifyLocations;
	static TArray<TPair<const void*, FString>> MsgKeyStack;
#if GMP_TRACE_BP_STACK
	static TArray<FString> BPMsgKeyStack;
#endif

	const TCHAR* DebugNativeMsgFileLine(FName Key)
	{
		if (auto Find = MsgkeyLocations.Find(Key))
		{
			struct FTmpMsgLocation : public TThreadSingleton<FTmpMsgLocation>
			{
				TStringBuilder<2048> TmpMsgLocation;
			};
			auto& Ref = FTmpMsgLocation::Get().TmpMsgLocation;
			Ref.Reset();
			Ref.Append(FString::Join(*Find, TEXT(" | ")));
			return *Ref;
		}
		return TEXT("Unkown");
	}

	void GetMessageTagSourceLocations(FName MsgKey, TArray<FString>& OutLocations)
	{
		if (auto Find = MsgkeyLocations.Find(MsgKey))
		{
			OutLocations = Find->Array();
		}
	}

	// Records the current top-of-stack MSGKEY location into the listen/notify table; called from the hub where direction is known.
	void TraceMessageKeyDirection(FName MsgKey, bool bSend)
	{
		if (const auto Find = MsgkeyLocations.Find(MsgKey))
		{
			TArray<FString>& Dst = (bSend ? MsgkeyNotifyLocations : MsgkeyListenLocations).FindOrAdd(MsgKey);
			for (const FString& Loc : *Find)
			{
				Dst.Remove(Loc);
				Dst.Add(Loc);
			}
		}
	}

	void GetMessageTagSourceLocationsTyped(FName MsgKey, TArray<FString>& OutListen, TArray<FString>& OutNotify)
	{
		if (const auto Find = MsgkeyListenLocations.Find(MsgKey))
		{
			OutListen = *Find;
		}
		if (const auto Find = MsgkeyNotifyLocations.Find(MsgKey))
		{
			OutNotify = *Find;
		}
	}

	void RegisterScriptHistoryFlush();

	void TraceScriptMessageSource(FName MsgKey, const FString& Loc, bool bIsListen)
	{
		if (!IsScriptSourceTraceEnabled())
		{
			return;
		}
		RegisterScriptHistoryFlush();
		auto& Dst = bIsListen ? MsgkeyListenLocations : MsgkeyNotifyLocations;
		TArray<FString>& Arr = Dst.FindOrAdd(MsgKey);
		Arr.Remove(Loc);
		Arr.Add(Loc);
	}

#else
	void GetMessageTagSourceLocations(FName MsgKey, TArray<FString>& OutLocations) {}
	void TraceMessageKeyDirection(FName MsgKey, bool bSend) {}
	void GetMessageTagSourceLocationsTyped(FName MsgKey, TArray<FString>& OutListen, TArray<FString>& OutNotify) {}
	void TraceScriptMessageSource(FName MsgKey, const FString& Loc, bool bIsListen) {}
	void FlushScriptTraceHistory() {}
	void ClearScriptTrace() {}
	void TraceRuntimeTriggerFromSigSource(FName MsgKey, bool bSend, FSigSource InSigSrc) {}
	void GetRecentRuntimeTriggers(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerEntry>& OutEntries, int32 MaxN) {}
	void GetRuntimeTriggersGroupedBySig(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerGroup>& OutGroups, int32 MaxPerSig) {}
	void ClearRuntimeTriggersForSig(FSigSource InSigSrc) {}
	void FlushRuntimeTraceHistory() {}
	void ClearRuntimeTrace() {}
	void ReadRuntimeTraceFromDisk(FName MsgKey, bool bSend, TArray<FGMPRuntimeTriggerGroup>& OutGroups) {}
#endif
	const TCHAR* DebugCurrentMsgFileLine()
	{
#if GMP_TRACE_MSG_STACK
		if (MsgKeyStack.Num() > 0)
		{
			if (auto MsgKey = GMP::FMSGKEYFind(*MsgKeyStack.Last().Value))
				return DebugNativeMsgFileLine(*MsgKeyStack.Last().Value);
		}
#endif
		return TEXT("Unkown");
	}
	
#if GMP_TRACE_MSG_STACK
#if GMP_TRACE_BP_STACK
	void GMPTraceEnterBP(const FString& MsgStr, FString&& Loc)
	{
		if (auto MsgKey = FMSGKEYFind(MsgStr))
		{
			MsgkeyLocations.FindOrAdd(MsgKey).Emplace(MoveTemp(Loc));
		}
		BPMsgKeyStack.Emplace(MsgStr);
	}

	void GMPTraceLeaveBP(const FString& MsgStr)
	{
		ensureAlways(MsgStr == BPMsgKeyStack.Pop(EAllowShrinking::No));
	}
#endif

	void MSGKEY_TYPE::GMPTraceEnter(const ANSICHAR* File, int32 Line)
	{
		if (FMSGKEYFind(*this))
		{
			MsgkeyLocations.FindOrAdd(*this).Emplace(FString::Printf(TEXT("%s:%d"), ANSI_TO_TCHAR(File), Line));
		}
		MsgKeyStack.Emplace(this->Ptr(), this->Ptr());
	}

	void MSGKEY_TYPE::GMPTraceLeave()
	{
		ensureAlways(this->Ptr() == MsgKeyStack.Pop(EAllowShrinking::No).Key);
	}

	static FString GetScriptHistoryPath() { return FPaths::ProjectSavedDir() / TEXT("GMP") / TEXT("ScriptHistory.ini"); }
	static FString GetScriptMergedHistoryPath() { return FPaths::ProjectSavedDir() / TEXT("GMP") / TEXT("ScriptMergedHistory.ini"); }

	// Listen keeps only the latest location (last element); Notify keeps the whole current batch.
	static TArray<FString> ListenForOutput(const TArray<FString>& In) { return In.Num() > 0 ? TArray<FString>{ In.Last() } : TArray<FString>{}; }

	// Section is the message key; Listen / Notify arrays hold the collected script source locations.
	static void FillConfigFromMemory(FConfigFile& Out)
	{
		for (const auto& Pair : MsgkeyListenLocations)
		{
			if (Pair.Value.Num() > 0)
			{
				Out.SetArray(*Pair.Key.ToString(), TEXT("Listen"), ListenForOutput(Pair.Value));
			}
		}
		for (const auto& Pair : MsgkeyNotifyLocations)
		{
			if (Pair.Value.Num() > 0)
			{
				Out.SetArray(*Pair.Key.ToString(), TEXT("Notify"), Pair.Value);
			}
		}
	}

	// Upsert: overwrite each key/direction traced this run with this run's value; entries not seen this run keep their previous on-disk value (already loaded via Combine).
	static void MergeConfigWithMemory(FConfigFile& InOut)
	{
		for (const auto& Pair : MsgkeyListenLocations)
		{
			if (Pair.Value.Num() > 0)
			{
				InOut.SetArray(*Pair.Key.ToString(), TEXT("Listen"), ListenForOutput(Pair.Value));
			}
		}
		for (const auto& Pair : MsgkeyNotifyLocations)
		{
			if (Pair.Value.Num() > 0)
			{
				InOut.SetArray(*Pair.Key.ToString(), TEXT("Notify"), Pair.Value);
			}
		}
	}

	static void FlushScriptHistory()
	{
		if (MsgkeyListenLocations.Num() == 0 && MsgkeyNotifyLocations.Num() == 0)
		{
			return;
		}

		IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("GMP")), /*Tree*/ true);

		// History: this run only, overwrite.
		{
			FConfigFile History;
			FillConfigFromMemory(History);
			History.Write(GetScriptHistoryPath());
		}

		// MergedHistory: load previous, upsert this run's entries over it, write back.
		{
			const FString MergedPath = GetScriptMergedHistoryPath();
			FConfigFile Merged;
			if (FPaths::FileExists(MergedPath))
			{
				Merged.Combine(MergedPath);
			}
			MergeConfigWithMemory(Merged);
			Merged.Write(MergedPath);
		}
	}

	void RegisterScriptHistoryFlush()
	{
		static bool bRegistered = false;
		if (!bRegistered)
		{
			bRegistered = true;
			FCoreDelegates::OnPreExit.AddStatic(&FlushScriptHistory);
		}
	}

	void FlushScriptTraceHistory()
	{
		FlushScriptHistory();
	}

	void ClearScriptTrace()
	{
		MsgkeyListenLocations.Reset();
		MsgkeyNotifyLocations.Reset();
	}

#if WITH_EDITOR
	// Runtime trigger trace: per PIE instance -> per sig address (efficient 2nd-level filter) -> per msgkey -> ordered ring (last = newest).
	static const int32 MaxRuntimeTriggerEntries = 9;
	using FSigAddr = FSigSource::AddrType;
	using FRuntimeTriggerMap = TMap<int32, TMap<FSigAddr, TMap<FMSGKEY, TArray<FGMPRuntimeTriggerEntry>>>>;
	static FRuntimeTriggerMap RuntimeListenTriggers;
	static FRuntimeTriggerMap RuntimeNotifyTriggers;

	static bool GGMPTraceHistoryFlushLive = false;
	static FXConsoleVariableRef CVarGMPTraceHistoryFlushLive(
		TEXT("gmp.TraceHistoryFlushLive"),
		GGMPTraceHistoryFlushLive,
		TEXT("Flush GMPTraceHistory.ini on every runtime trigger (default off; high disk IO, debugging only)."),
		ECVF_Default);

	static FString GetRuntimeTraceHistoryPath() { return FPaths::ProjectSavedDir() / TEXT("GMP") / TEXT("GMPTraceHistory.ini"); }

	static void FillRuntimeConfig(FConfigFile& Out)
	{
		auto WriteDir = [&Out](const FRuntimeTriggerMap& Src, const TCHAR* DirKey)
		{
			for (const auto& InstancePair : Src)
			{
				// Merge all sigs of a key into one section; sig grouping on read is driven by the per-line SigName.
				TMap<FMSGKEY, TArray<FString>> KeyLines;
				for (const auto& SigPair : InstancePair.Value)
				{
					for (const auto& KeyPair : SigPair.Value)
					{
						TArray<FString>& Lines = KeyLines.FindOrAdd(KeyPair.Key);
						for (const auto& Entry : KeyPair.Value)
						{
							Lines.Add(FString::Printf(TEXT("%s|%s|%s|%f"), *Entry.SigName, *Entry.ObjName, *Entry.Loc, Entry.WorldTime));
						}
					}
				}
				for (const auto& KL : KeyLines)
				{
					if (KL.Value.Num() == 0)
					{
						continue;
					}
					const FString Section = FString::Printf(TEXT("PIE%d.%s"), InstancePair.Key, *KL.Key.ToString());
					Out.SetArray(*Section, DirKey, KL.Value);
				}
			}
		};
		WriteDir(RuntimeListenTriggers, TEXT("Listen"));
		WriteDir(RuntimeNotifyTriggers, TEXT("Notify"));
	}

	void FlushRuntimeTraceHistory()
	{
		if (RuntimeListenTriggers.Num() == 0 && RuntimeNotifyTriggers.Num() == 0)
		{
			return;
		}
		IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("GMP")), /*Tree*/ true);
		FConfigFile Cfg;
		FillRuntimeConfig(Cfg);
		Cfg.Write(GetRuntimeTraceHistoryPath());
	}

	void ClearRuntimeTrace()
	{
		RuntimeListenTriggers.Reset();
		RuntimeNotifyTriggers.Reset();
	}

	void TraceRuntimeTriggerFromSigSource(FName MsgKey, bool bSend, FSigSource InSigSrc)
	{
		if (!IsScriptSourceTraceEnabled() || !GEngine)
		{
			return;
		}
		// Only UObject sigs carry a world; the sig object (listener host, usually GameInstance/World) also names the group.
		UObject* SigObj = InSigSrc.TryGetUObject();
		if (!SigObj)
		{
			return;
		}
		UWorld* World = SigObj->GetWorld();
		if (!World || World->WorldType != EWorldType::PIE)
		{
			return;
		}
		const FWorldContext* WC = GEngine->GetWorldContextFromWorld(World);
		if (!WC)
		{
			return;
		}
		// Single-instance PIE keeps PIEInstance = -1 (INDEX_NONE); treat that as a valid bucket rather than skipping it.
		const int32 PIEInstance = WC->PIEInstance;
		const FSigAddr SigAddr = InSigSrc.GetAddrValue();

		FString Loc;
		if (const auto Find = MsgkeyListenLocations.Find(MsgKey); Find && !bSend && Find->Num() > 0)
		{
			Loc = Find->Last();
		}
		else if (const auto FindN = MsgkeyNotifyLocations.Find(MsgKey); FindN && bSend && FindN->Num() > 0)
		{
			Loc = FindN->Last();
		}

		FGMPRuntimeTriggerEntry Entry{GetNameSafe(SigObj), GetNameSafe(SigObj), Loc, World->GetTimeSeconds()};
		auto& Dir = bSend ? RuntimeNotifyTriggers : RuntimeListenTriggers;
		TArray<FGMPRuntimeTriggerEntry>& Ring = Dir.FindOrAdd(PIEInstance).FindOrAdd(SigAddr).FindOrAdd(MsgKey);
		// One entry per trigger (no dedup) so history reflects every fire; the ring caps at MaxRuntimeTriggerEntries.
		Ring.Add(Entry);
		if (Ring.Num() > MaxRuntimeTriggerEntries)
		{
			Ring.RemoveAt(0);
		}

		if (GGMPTraceHistoryFlushLive)
		{
			FlushRuntimeTraceHistory();
		}
	}

	void GetRecentRuntimeTriggers(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerEntry>& OutEntries, int32 MaxN)
	{
		const auto& Dir = bSend ? RuntimeNotifyTriggers : RuntimeListenTriggers;
		const auto InstanceMap = Dir.Find(PIEInstance);
		if (!InstanceMap)
		{
			return;
		}
		for (const auto& SigPair : *InstanceMap)
		{
			if (const auto Ring = SigPair.Value.Find(MsgKey))
			{
				const int32 Start = FMath::Max(0, Ring->Num() - MaxN);
				for (int32 i = Start; i < Ring->Num(); ++i)
				{
					OutEntries.Add((*Ring)[i]);
				}
			}
		}
	}

	void GetRuntimeTriggersGroupedBySig(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerGroup>& OutGroups, int32 MaxPerSig)
	{
		const auto& Dir = bSend ? RuntimeNotifyTriggers : RuntimeListenTriggers;
		const auto InstanceMap = Dir.Find(PIEInstance);
		if (!InstanceMap)
		{
			return;
		}
		for (const auto& SigPair : *InstanceMap)
		{
			const auto Ring = SigPair.Value.Find(MsgKey);
			if (!Ring || Ring->Num() == 0)
			{
				continue;
			}
			FGMPRuntimeTriggerGroup Group;
			Group.SigName = (*Ring)[0].SigName;
			const int32 Start = FMath::Max(0, Ring->Num() - MaxPerSig);
			for (int32 i = Start; i < Ring->Num(); ++i)
			{
				Group.Entries.Add((*Ring)[i]);
			}
			OutGroups.Add(MoveTemp(Group));
		}
	}

	void ClearRuntimeTriggersForSig(FSigSource InSigSrc)
	{
		const FSigAddr SigAddr = InSigSrc.GetAddrValue();
		for (auto& InstancePair : RuntimeListenTriggers)
		{
			InstancePair.Value.Remove(SigAddr);
		}
		for (auto& InstancePair : RuntimeNotifyTriggers)
		{
			InstancePair.Value.Remove(SigAddr);
		}
	}

	void ReadRuntimeTraceFromDisk(FName MsgKey, bool bSend, TArray<FGMPRuntimeTriggerGroup>& OutGroups)
	{
		const FString Path = GetRuntimeTraceHistoryPath();
		if (!FPaths::FileExists(Path))
		{
			return;
		}
		FConfigFile Cfg;
		Cfg.Read(Path);
		const FString KeyStr = MsgKey.ToString();
		const TCHAR* DirKey = bSend ? TEXT("Notify") : TEXT("Listen");
		TArray<FString> SectionNames;
		Cfg.GetKeys(SectionNames);
		TMap<FString, int32> SigToGroup;
		for (const FString& SectionName : SectionNames)
		{
			// Section format: PIE{n}.{msgkey}; match any instance for this key.
			int32 DotIdx = INDEX_NONE;
			if (!SectionName.FindChar(TEXT('.'), DotIdx) || SectionName.Mid(DotIdx + 1) != KeyStr)
			{
				continue;
			}
			TArray<FString> Lines;
			Cfg.GetArray(*SectionName, DirKey, Lines);
			for (const FString& Line : Lines)
			{
				// Line format: SigName|ObjName|Loc|WorldTime.
				TArray<FString> Parts;
				Line.ParseIntoArray(Parts, TEXT("|"), false);
				FGMPRuntimeTriggerEntry Entry;
				const FString SigName = Parts.IsValidIndex(0) ? Parts[0] : FString();
				Entry.SigName = SigName;
				Entry.ObjName = Parts.IsValidIndex(1) ? Parts[1] : Line;
				Entry.Loc = Parts.IsValidIndex(2) ? Parts[2] : FString();
				Entry.WorldTime = Parts.IsValidIndex(3) ? FCString::Atod(*Parts[3]) : 0.0;

				int32* GroupIdx = SigToGroup.Find(SigName);
				if (!GroupIdx)
				{
					FGMPRuntimeTriggerGroup Group;
					Group.SigName = SigName;
					GroupIdx = &SigToGroup.Add(SigName, OutGroups.Add(MoveTemp(Group)));
				}
				OutGroups[*GroupIdx].Entries.Add(MoveTemp(Entry));
			}
		}
	}
#else
	void TraceRuntimeTriggerFromSigSource(FName MsgKey, bool bSend, FSigSource InSigSrc) {}
	void GetRecentRuntimeTriggers(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerEntry>& OutEntries, int32 MaxN) {}
	void GetRuntimeTriggersGroupedBySig(FName MsgKey, bool bSend, int32 PIEInstance, TArray<FGMPRuntimeTriggerGroup>& OutGroups, int32 MaxPerSig) {}
	void ClearRuntimeTriggersForSig(FSigSource InSigSrc) {}
	void FlushRuntimeTraceHistory() {}
	void ClearRuntimeTrace() {}
	void ReadRuntimeTraceFromDisk(FName MsgKey, bool bSend, TArray<FGMPRuntimeTriggerGroup>& OutGroups) {}
#endif

#endif

}
