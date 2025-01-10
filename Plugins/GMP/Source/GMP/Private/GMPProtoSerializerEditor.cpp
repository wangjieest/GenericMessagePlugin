//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoSerializerEditor.h"

#include "Misc/ConfigCacheIni.h"

#if defined(GMP_WITH_UPB)
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "EdGraphSchema_K2.h"
#include "HAL/PlatformFile.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UnrealCompatibility.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#include <cmath>

#if UE_5_00_OR_LATER
#include "UObject/ArchiveCookContext.h"
#include "UObject/SavePackage.h"
#endif

// Must be last
#include "upb/port/def.inc"
namespace upb
{
namespace generator
{
	void FPreGenerator::PreAddProtoDesc(TArrayView<const uint8> Buf)
	{
		Descriptors.Add(Arena.AllocString(Buf));
	}

	void FPreGenerator::PreAddProtoDesc(upb_StringView Buf)
	{
		Descriptors.Add(Buf);
	}

	bool FPreGenerator::PreAddProto(upb_StringView Buf)
	{
		auto Proto = FDefPool::ParseProto(Buf, Arena);
		if (Proto && !ProtoNames.Contains(Proto))
		{
			ProtoDescs.Add(Proto, Buf);
			FNameType Name = StringView(FDefPool::GetProtoName(Proto));
			ProtoNames.Add(Proto, Name);
			ProtoMap.Add(Name, Proto);

			auto& Arr = ProtoDeps.FindOrAdd(Name);

			size_t ProtoSize = 0;
			auto DepPtr = FDefPool::GetProtoDepencies(Proto, &ProtoSize);
			for (auto i = 0; i < ProtoSize; ++i)
			{
				Arr.Add(StringView(DepPtr[i]));
			}
			return true;
		}

		return false;
	}

	void FPreGenerator::Reset()
	{
		Descriptors.Empty();
		ProtoNames.Empty();
		ProtoMap.Empty();
		ProtoDescs.Empty();
		ProtoDeps.Empty();
		Arena = FArena();
	}

	TArray<const FDefPool::FProtoDescType*> FPreGenerator::GenerateProtoList() const
	{
		for (auto& Elm : Descriptors)
		{
			const_cast<FPreGenerator*>(this)->PreAddProto(Elm);
		}

		TArray<FNameType> ResultNames;
		for (auto& Pair : ProtoDeps)
		{
			AddProtoImpl(ResultNames, Pair.Key);
		}
		TArray<const FDefPool::FProtoDescType*> Ret;
		for (auto& Name : ResultNames)
		{
			Ret.Add(ProtoMap.FindChecked(Name));
		}
		return Ret;
	}

	void FPreGenerator::AddProtoImpl(TArray<FNameType>& Results, const FNameType& ProtoName) const
	{
		if (Results.Contains(ProtoName))
		{
			return;
		}
		auto Names = ProtoDeps.FindChecked(ProtoName);
		for (auto i = 0; i < Names.Num(); ++i)
		{
			AddProtoImpl(Results, Names[i]);
		}
		Results.Add(ProtoName);
	}

	FPreGenerator& FPreGenerator::GetPreGenerator()
	{
		static FPreGenerator PreGenerator;
		return PreGenerator;
	}

	bool upbRegFileDescProtoImpl(const _upb_DefPool_Init* DefInit)
	{
		return FPreGenerator::GetPreGenerator().PreAddProto(DefInit->descriptor);
	}

	static TOptional<FString> ProtoDefaultPath;
	FString GatherRootDir(UWorld* InWorld)
	{
		FString OutFolderPath;
#if defined(SLATE_API) && defined(DESKTOPPLATFORM_API) && (defined(PROTOBUF_API) || defined(WITH_PROTOBUF))
		void* ParentWindowHandle = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

		if (!ProtoDefaultPath.IsSet())
		{
			FString ValueRead;
			GConfig->GetString(TEXT("GMP"), TEXT("LastProtoDir"), ValueRead, GEditorPerProjectIni);
			if (!ValueRead.IsEmpty())
			{
				ProtoDefaultPath = ValueRead;
			}
		}
		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("please choose proto's root directory"), ProtoDefaultPath.Get(FPaths::ProjectContentDir()), OutFolderPath))
		{
			ProtoDefaultPath = OutFolderPath;
			{
				GConfig->SetString(TEXT("GMP"), TEXT("LastProtoDir"), *OutFolderPath, GEditorPerProjectIni);
				GConfig->Flush(false, GEditorPerProjectIni);
			}
		}
#endif
		return OutFolderPath;
	}
}  // namespace generator
}  // namespace upb

#if defined(PROTOBUF_API) || defined(WITH_PROTOBUF)
#pragma warning(push)
#pragma warning(disable : 4800)
#pragma warning(disable : 4125)
#pragma warning(disable : 4647)
#pragma warning(disable : 4668)
#pragma warning(disable : 4582)
#pragma warning(disable : 4583)
#pragma warning(disable : 4946)
#pragma warning(disable : 4577)
#pragma warning(disable : 4996)
#ifndef __GLIBCXX__
#define __GLIBCXX__ 0
#endif
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.pb.h>
#pragma warning(pop)

namespace upb
{
namespace generator
{
	TArray<TArray<uint8>> GatherFileDescriptorProtosForDir(FString RootDir)
	{
		TArray<TArray<uint8>> ProtoDescriptors;
		TArray<FString> ProtoFiles;
		IPlatformFile::GetPlatformPhysical().FindFilesRecursively(ProtoFiles, *RootDir, TEXT(".proto"));
		if (ProtoFiles.Num() > 0)
		{
			using namespace google::protobuf;
			struct FErrorCollector final : public compiler::MultiFileErrorCollector
			{
			public:
				virtual void AddWarning(const std::string& filename, int line, int column, const std::string& message) {}
				virtual void AddError(const std::string& filename, int line, int column, const std::string& message) override
				{
					GMP_ERROR(TEXT("%s(%d:%d) : %s"), UTF8_TO_TCHAR(filename.c_str()), line, column, UTF8_TO_TCHAR(message.c_str()));
				}
			};
			FErrorCollector Error;
			compiler::DiskSourceTree SrcTree;
			SrcTree.MapPath("", TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(RootDir)));

			compiler::SourceTreeDescriptorDatabase Database(&SrcTree);
			Database.RecordErrorsTo(&Error);

			if (!RootDir.EndsWith(TEXT("/")))
				RootDir.AppendChar('/');
			for (auto& ProtoFile : ProtoFiles)
			{
				FPaths::MakePathRelativeTo(ProtoFile, *RootDir);
				FileDescriptorProto DescProto;
				if (!ensure(Database.FindFileByName(TCHAR_TO_UTF8(*ProtoFile), &DescProto)))
					continue;

				auto Size = DescProto.ByteSizeLong();
				if (!ensure(Size > 0))
					continue;

				TArray<uint8> ProtoDescriptor;
				ProtoDescriptor.AddUninitialized(Size);
				if (!ensure(DescProto.SerializeToArray((char*)ProtoDescriptor.GetData(), Size)))
					continue;
				ProtoDescriptors.Add(MoveTemp(ProtoDescriptor));
			}
		}
		return ProtoDescriptors;
	}
}  // namespace generator
}  // namespace upb
#else
namespace upb
{
namespace generator
{
	TArray<TArray<uint8>> GatherFileDescriptorProtosForDir(FString RootDir)
	{
		return {};
	}
}  // namespace generator
}  // namespace upb
#endif  // defined(PROTOBUF_API)

namespace GMP
{
namespace PB
{
	using namespace upb;
	void PreInitProtoList(TFunctionRef<void(const FDefPool::FProtoDescType*)> Func)
	{
		auto& PreGenerator = upb::generator::FPreGenerator::GetPreGenerator();
		auto ProtoList = PreGenerator.GenerateProtoList();
		for (auto&& Proto : ProtoList)
		{
			Func(Proto);
		}
	}

}  // namespace PB
}  // namespace GMP

#include "upb/port/undef.inc"
#endif  // WITH_EDITOR
#endif
