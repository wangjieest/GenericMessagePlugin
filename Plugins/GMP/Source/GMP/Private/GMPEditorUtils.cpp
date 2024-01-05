//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPEditorUtils.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "XConsoleManager.h"
#include "GMPMacros.h"

#define LOCTEXT_NAMESPACE "GMPProtoSerializer"

namespace GMP
{
namespace FEditorUtils
{
	bool DelayExecImpl(const UObject* InObj, FSimpleDelegate Delegate, float InDelay, bool bEnsureExec)
	{
		InDelay = FMath::Max(InDelay, 0.00001f);
		auto World = GEngine->GetWorldFromContextObject(InObj, InObj ? EGetWorldErrorMode::LogAndReturnNull : EGetWorldErrorMode::ReturnNull);
		if (bEnsureExec && (!World || !World->IsGameWorld()))
		{
			if (GEditor && GEditor->IsTimerManagerValid())
			{
				FTimerHandle TimerHandle;
				GEditor->GetTimerManager()->SetTimer(TimerHandle, MoveTemp(Delegate), InDelay, false);
			}
			else
			{
				auto Holder = World ? NewObject<UGMPPlaceHolder>(World) : NewObject<UGMPPlaceHolder>();
				Holder->SetFlags(RF_Standalone);
				if (World)
					World->PerModuleDataObjects.Add(Holder);
				else
					Holder->AddToRoot();

				GEditor->OnPostEditorTick().AddWeakLambda(Holder, [Holder, WeakWorld{MakeWeakObjectPtr(World)}, Delegate(MoveTemp(Delegate)), InDelay](float Delta) mutable {
					InDelay -= Delta;
					if (InDelay <= 0.f)
					{
						if (auto OwnerWorld = WeakWorld.Get())
							OwnerWorld->PerModuleDataObjects.Remove(Holder);
						Holder->RemoveFromRoot();

						Delegate.ExecuteIfBound();
						Holder->ClearFlags(RF_Standalone);
						Holder->MarkAsGarbage();
					}
				});
			}
			return true;
		}
		else
		{
			World = World ? World : GWorld;
			ensure(!bEnsureExec || World);
			if (World)
			{
				FTimerHandle TimerHandle;
				World->GetTimerManager().SetTimer(TimerHandle, MoveTemp(Delegate), InDelay, false);
				return true;
			}
		}
		return false;
	}

	bool IsInFilterList(const FStringView& PathId)
	{
		return PathId.StartsWith(TEXT("/Game/"));
	}

	void GetReferenceAssets(const UObject* InObj, const TArray<FString>& PathIdArray, TMap<FName, TArray<FName>>& OutRef, TMap<FName, TArray<FName>>& OutDep, bool bRecur)
	{
		TArray<FName> PkgNames;
		for (const FString& PathId : PathIdArray)
		{
			FName PkgName(*PathId);
			if (ensureWorldMsgf(InObj, PkgName.IsValid() && !PkgName.IsNone(), TEXT("Invalid Name : %s"), *PathId) && IsInFilterList(PathId))
				PkgNames.Emplace(PkgName);
		}
		if (!ensure(PkgNames.Num() > 0))
			return;

		IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
		UE::AssetRegistry::EDependencyCategory Categories = UE::AssetRegistry::EDependencyCategory::Package;
		UE::AssetRegistry::EDependencyQuery QueryFlag = UE::AssetRegistry::EDependencyQuery::Hard;
		TSet<FName> TestedPkgSet;
		// TestedPkgSet.Append(PkgNames);
		for (auto It = PkgNames.CreateIterator(); It; ++It)
		{
			auto AssetPackageName = *It;
			if (!ensureWorld(InObj, AssetPackageName.IsValid() && !AssetPackageName.IsNone()))
				continue;

			bool bExisted = false;
			TestedPkgSet.Add(AssetPackageName, &bExisted);
			if (bExisted)
				continue;

			TArray<FAssetDependency> LinksToAsset;
			AssetRegistry.GetReferencers(FAssetIdentifier(AssetPackageName), LinksToAsset, Categories, QueryFlag);
			for (auto& SubId : LinksToAsset)
			{
				FName SubPackageName = SubId.AssetId.PackageName;
				if (!ensureWorldMsgf(InObj, SubPackageName.IsValid() && !SubPackageName.IsNone(), TEXT("Invalid Name : %s"), *SubPackageName.ToString()))
					continue;

				OutRef.FindOrAdd(AssetPackageName).AddUnique(SubPackageName);
				OutDep.FindOrAdd(SubPackageName).AddUnique(AssetPackageName);

				FString SubPackageNameStr = SubPackageName.ToString();
				ensure(!(FPackageName::IsScriptPackage(SubPackageNameStr) && !SubId.AssetId.IsValue()));
				if (bRecur && IsInFilterList(SubPackageNameStr))
					PkgNames.AddUnique(SubPackageName);
			}
		}
	}

	void UnloadToBePlacedPackages(const UObject* InObj, TArray<FString> PathIdArray, TDelegate<void(bool, TArray<FString>)> OnResult)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TMap<FName, TArray<FName>> RefMap;
		TMap<FName, TArray<FName>> DepMap;
		GetReferenceAssets(InObj, PathIdArray, RefMap, DepMap, true);

		TSet<FName> InvalidNames;
		for (const auto& Pair : RefMap)
		{
			auto KeyStr = Pair.Key.ToString();
			if (ensure(IsInFilterList(KeyStr)))
			{
				for (auto& Item : Pair.Value)
				{
					auto ValueStr = Item.ToString();
					if (ensure(IsInFilterList(ValueStr)))
						PathIdArray.AddUnique(MoveTemp(ValueStr));
					else if (ensureWorldMsgf(InObj, ValueStr.StartsWith(TEXT("/")), TEXT("Invalid PathId : %s"), *ValueStr))
					{
						InvalidNames.Add(Item);
						UE_LOG(LogGMP, Error, TEXT("RefMap Error Invalid Value: %s [%s]"), *KeyStr, *ValueStr);
					}
				}
				PathIdArray.AddUnique(MoveTemp(KeyStr));
			}
			else if (ensureWorldMsgf(InObj, KeyStr.StartsWith(TEXT("/")), TEXT("Invalid PathId : %s"), *KeyStr))
			{
				InvalidNames.Add(Pair.Key);
				UE_LOG(LogGMP, Error, TEXT("RefMap Error Invalid Key: [%s]"), *KeyStr);
				break;
			}
		}

		if (InvalidNames.Num() > 0)
		{
			TStringBuilder<256> ErrMsg;
			ErrMsg.Append(TEXT("Dependency Invalid:\n"));
			ErrMsg.Appendf(TEXT("\t%s\n"), *FString::JoinBy(InvalidNames, TEXT("\n\t"), [](auto& Name) { return Name.ToString(); }));

			bool bDependencyError = false;

			for (auto& Pair : DepMap)
			{
				auto KeyStr = Pair.Key.ToString();
				if (IsInFilterList(KeyStr))
					continue;

				for (auto& Item : Pair.Value)
				{
					auto ValueStr = Item.ToString();
					if (!IsInFilterList(ValueStr))
						continue;

					if (!bDependencyError)
					{
						bDependencyError = true;
						ErrMsg.Append(TEXT("Dependency Error:\n"));
					}
					ErrMsg.Appendf(TEXT("\t[%s] should not depends on [%s]\n"), *KeyStr, *ValueStr);
				}
			}
			ensureWorldMsgf(InObj, false, TEXT("%s"), *ErrMsg);
			DelayExec(InObj, [OnResult, ErrTxt{FText::FromString(*ErrMsg)}, PathIdArray{MoveTemp(PathIdArray)}] {
				static auto Title = LOCTEXT("UnloadError", "unload failed");
				FMessageDialog::Open(EAppMsgType::Ok, ErrTxt, &Title);
				OnResult.ExecuteIfBound(false, PathIdArray);
			});
			return;
		}

		bool bUnloadSuc = true;
		TArray<UPackage*> ExistingPackageArray;
		ExistingPackageArray.Reserve(PathIdArray.Num());
		TArray<TWeakObjectPtr<UPackage>> WeakArr;
		for (const FString& PathId : PathIdArray)
		{
			UPackage* ExistingPackage = FindPackage(nullptr, *PathId);
			//ExistingPackage = ExistingPackage ? ExistingPackage : ::LoadPackage(nullptr, *PathId, LOAD_EditorOnly);
			if (ExistingPackage)
			{
				ExistingPackageArray.Emplace(ExistingPackage);
				WeakArr.Emplace(ExistingPackage);
				ExistingPackage->SetDirtyFlag(false);
				UE_LOG(LogGMP, Display, TEXT("detect package to be unload : %s"), *PathId);
			}
		}

		if (false)
		{
			TArray<UPackage*> SubPackageArray;
			for (int i = 0; i < ExistingPackageArray.Num(); ++i)
			{
				UPackage* Pkg = ExistingPackageArray[i];
				TArray<UClass*> SubClasses;
				auto BP = Cast<UBlueprint>(Pkg->FindAssetInPackage());
				if (BP)
				{
					GetDerivedClasses(BP->GeneratedClass, SubClasses, true);
					for (UClass* SubClass : SubClasses)
					{
						UPackage* SubPackage = SubClass->GetOutermost();
						if (SubPackage)
						{
							SubPackageArray.Emplace(SubPackage);
							FString SubClassString = SubClass->GetName();
							UE_LOG(LogGMP, Display, TEXT("SubClassString:%s"), *SubClassString);
							PathIdArray.AddUnique(SubPackage->GetPathName());
						}
					}
				}
			}
			for (UPackage* Pkg : SubPackageArray)
			{
				if (ensure(Pkg))
					ExistingPackageArray.AddUnique(Pkg);
			}
		}

		if (!ExistingPackageArray.Num())
		{
			OnResult.ExecuteIfBound(true, PathIdArray);
			return;
		}

		bool bOnlyCurMap = false;
		if (ExistingPackageArray.Num() == 1)
		{
			if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			{
				bOnlyCurMap = EditorWorld->GetOutermost() == ExistingPackageArray[0];
			}
		}

		static auto MakeObjectPurgeable = [](UObject* InObject) {
			if (InObject->IsRooted())
			{
				InObject->RemoveFromRoot();
			}
			InObject->ClearFlags(RF_Public | RF_Standalone);
		};

		static auto RemovePackage = [](UPackage* InPackage, bool bRename = false) {
			if (bRename)
				InPackage->Rename(nullptr, nullptr, REN_ForceNoResetLoaders | REN_SkipGeneratedClasses | REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
			else
				MakeObjectPurgeable(InPackage);
			ForEachObjectWithPackage(InPackage, [](UObject* InObject) {
				MakeObjectPurgeable(InObject);
				return true;
			});
		};

		static bool bCollectWeakPkgs = false;
		static FAutoConsoleVariableRef CVar_CollectWeakPkgs(TEXT("x.gmp.proto.CollectWeakPkgs"), bCollectWeakPkgs, TEXT(""));
		TArray<TWeakObjectPtr<UPackage>> WeakPkgs;
		if (bCollectWeakPkgs)
		{
			for (auto* Pkg : ExistingPackageArray)
			{
				WeakPkgs.AddUnique(Pkg);
			}
		}

		{
			FScopedSlowTask ScopedSlowTask(1.f, LOCTEXT("Unloading", "unloading assets..."));
			if (!ensure(UPackageTools::UnloadPackages(ExistingPackageArray) || bOnlyCurMap))
			{
				bUnloadSuc = false;
			}
			ExistingPackageArray.SetNum(0);
		}

		for (auto WeakPkg : WeakPkgs)
		{
			if (!WeakPkg.IsValid())
				continue;
			ExistingPackageArray.AddUnique(WeakPkg.Get());
		}

		static bool bRemoveAssetRegistry = false;
		static FAutoConsoleVariableRef CVar_RemoveAssetRegistry(TEXT("x.gmp.proto.bRemoveAssetRegistry"), bRemoveAssetRegistry, TEXT(""));
		if (bRemoveAssetRegistry)
		{
			for (auto Pkg : ExistingPackageArray)
			{
				UObject* Asset = Pkg->FindAssetInPackage();
				if (IsValid(Asset))
					FAssetRegistryModule::AssetDeleted(Asset);
				FAssetRegistryModule::PackageDeleted(Pkg);
			}
		}

		static bool bRenameUnloadPkg = true;
		static FAutoConsoleVariableRef CVar_RenameUnloadPkg(TEXT("x.gmp.proto.RenameUnloadPkg"), bRenameUnloadPkg, TEXT(""));
		if (bRenameUnloadPkg)
		{
			for (auto i = 0; i < ExistingPackageArray.Num(); ++i)
			{
				RemovePackage(ExistingPackageArray[i], true);

				if (WeakArr[i].IsValid())
				{
					UPackage* Pkg = ExistingPackageArray[i];
					RemovePackage(Pkg);
				}
			}
		}

		ExistingPackageArray.SetNum(0);
		GEngine->ForceGarbageCollection(true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		DelayExec(nullptr, [OnResult, bUnloadSuc, PathIdArray{MoveTemp(PathIdArray)}] {
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			OnResult.ExecuteIfBound(bUnloadSuc, PathIdArray);
		});
	}
}  // namespace FEditorUtils
}  // namespace GMP
#undef LOCTEXT_NAMESPACE
#endif
