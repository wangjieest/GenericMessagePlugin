//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoSerializer.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "GMPEditorUtils.h"
#include "GMPProtoUtils.h"
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
#include "XConsoleManager.h"
#include "upb/libupb.h"

#include <variant>

#if UE_VERSION_NEWER_THAN(5, 0, 0)
#include "UObject/ArchiveCookContext.h"
#include "UObject/SavePackage.h"
#endif

namespace upb
{
namespace generator
{
	struct FPreGenerator
	{
		FDynamicArena Arena;
		TArray<upb_StringView> Descriptors;

		using FNameType = FString;

		TMap<const FDefPool::FProtoDescType*, FNameType> ProtoNames;
		TMap<FNameType, const FDefPool::FProtoDescType*> ProtoMap;
		TMap<const FDefPool::FProtoDescType*, upb_StringView> ProtoDescs;

		TMap<FNameType, TArray<FNameType>> ProtoDeps;

		void PreAddProtoDesc(upb_StringView Buf) { Descriptors.Add(Buf); }
		void PreAddProtoDesc(TArrayView<uint8> Buf) { Descriptors.Add(Arena.AllocString(Buf)); }

		bool PreAddProto(upb_StringView Buf)
		{
			auto Proto = FDefPool::ParseProto(Buf, Arena);
			if (Proto && !ProtoNames.Contains(Proto))
			{
				ProtoDescs.Add(Proto, Buf);
				FNameType Name = StringView(FDefPool::GetProtoName(Proto));
				ProtoNames.Add(Proto, Name);
				ProtoMap.Add(Name, Proto);

				auto& Arr = ProtoDeps.FindOrAdd(Name);
				for (auto DepName : FDefPool::GetProtoDepencies(Proto))
					Arr.Add(StringView(DepName));
				return true;
			}

			return false;
		}

		void Reset()
		{
			Descriptors.Empty();
			ProtoNames.Empty();
			ProtoMap.Empty();
			ProtoDescs.Empty();
			ProtoDeps.Empty();
			Arena = FArena();
		}

		TArray<const FDefPool::FProtoDescType*> GenerateProtoList() const
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

		void AddProtoImpl(TArray<FNameType>& Results, const FNameType& ProtoName) const
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

		TArray<FFileDefPtr> FillDefPool(FDefPool& Pool, TMap<const upb_FileDef*, upb_StringView>& OutMap)
		{
			auto ProtoList = GenerateProtoList();
			TArray<FFileDefPtr> FileDefs;
			for (auto Proto : ProtoList)
			{
				if (auto FileDef = Pool.AddProto(Proto))
				{
					FileDefs.Add(FileDef);
					OutMap.Emplace(*FileDef, ProtoDescs.FindChecked(Proto));
				}
			}
			return FileDefs;
		}
	};
	static FPreGenerator& GetPreGenerator()
	{
		static FPreGenerator PreGenerator;
		return PreGenerator;
	}
	static TArray<FFileDefPtr> FillDefPool(FDefPool& Pool, TMap<const upb_FileDef*, upb_StringView>& OutMap)
	{
		return GetPreGenerator().FillDefPool(Pool, OutMap);
	}
	bool upbRegFileDescProtoImpl(const _upb_DefPool_Init* DefInit)
	{
		return GetPreGenerator().PreAddProto(DefInit->descriptor);
	}
}  // namespace generator
}  // namespace upb

#if GMP_EXTEND_CONSOLE
namespace GMP
{
namespace PB
{
	using namespace upb;
	extern FDefPool& ResetEditorPoolPtr();
	void PreInitProtoList(TFunctionRef<void(const FDefPool::FProtoDescType*)> Func)
	{
		auto& PreGenerator = upb::generator::GetPreGenerator();
		auto ProtoList = PreGenerator.GenerateProtoList();
		for (auto Proto : ProtoList)
		{
			Func(Proto);
		}
	}

	static FString ProtoRootDir = TEXT("/Game/ProtoStructs");
	FAutoConsoleVariableRef CVar_ProtoRootDir(TEXT("x.gmp.proto.rootdir"), ProtoRootDir, TEXT(""));

	class FProtoTraveler
	{
	protected:
		FString GetPackagePath(const FString& Sub) const
		{
			auto RetPath = FString::Printf(TEXT("%s/%s"), *ProtoRootDir, *Sub);
			FString FolderPath;
			FPackageName::TryConvertGameRelativePackagePathToLocalPath(RetPath, FolderPath);
			IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*FPaths::GetPath(FolderPath));
			return RetPath;
		}

		FString GetProtoMessagePkgStr(FMessageDefPtr MsgDef) const
		{
			FString MsgFullNameStr = MsgDef.FullName();
			MsgFullNameStr.ReplaceCharInline(TEXT('.'), TEXT('/'));
			return GetPackagePath(MsgFullNameStr);
		}
		FString GetProtoEnumPkgStr(FEnumDefPtr EnumDef) const
		{
			FString EnumFullNameStr = EnumDef.FullName();
			TArray<FString> EnumNameList;
			EnumFullNameStr.ParseIntoArray(EnumNameList, TEXT("."));
			return GetPackagePath(FString::Join(EnumNameList, TEXT("/")));
		}
		FString GetProtoDescPkgStr(FFileDefPtr FileDef, FString* OutName = nullptr) const
		{
			FString FileDirStr = FileDef.Package();
			TArray<FString> FileDirList;
			FileDirStr.ParseIntoArray(FileDirList, TEXT("."));
			FString FileNameStr = FileDef.Name();
			FileNameStr.ReplaceCharInline(TEXT('.'), TEXT('_'));
			int32 Idx = INDEX_NONE;
			if (FileNameStr.FindLastChar(TEXT('/'), Idx))
			{
				FileNameStr.MidInline(Idx + 1);
			}
			FileNameStr = FString::Printf(TEXT("PB_%s"), *FileNameStr);
			if (OutName)
				*OutName = FileNameStr;
			FString DescAssetPath = GetPackagePath(FString::Join(FileDirList, TEXT("/")) / FileNameStr);
			return DescAssetPath;
		}

		void TravelProtoEnum(TSet<FString>& Sets, FEnumDefPtr EnumDef) const
		{
			auto Str = GetProtoEnumPkgStr(EnumDef);
			if (Sets.Contains(Str))
				return;
			Sets.Add(Str);
		}
		void TravelProtoMessage(TSet<FString>& Sets, FMessageDefPtr MsgDef) const
		{
			auto Str = GetProtoMessagePkgStr(MsgDef);
			if (Sets.Contains(Str))
				return;
			Sets.Add(Str);

			for (auto FieldIndex = 0; FieldIndex < MsgDef.FieldCount(); ++FieldIndex)
			{
				auto FieldDef = MsgDef.FindFieldByNumber(FieldIndex);
				if (!FieldDef)
					continue;
				auto CType = FieldDef.GetCType();
				if (CType == kUpb_CType_Enum)
				{
					auto SubEnumDef = FieldDef.EnumSubdef();
					TravelProtoEnum(Sets, SubEnumDef);
				}
				else if (CType == kUpb_CType_Message)
				{
					auto SubMsgDef = FieldDef.MessageSubdef();
					TravelProtoMessage(Sets, SubMsgDef);
				}
			}  // for
		}

		void TravelProtoFile(TSet<FString>& Sets, FFileDefPtr FileDef) const
		{
			auto Str = GetProtoDescPkgStr(FileDef);
			if (Sets.Contains(Str))
				return;
			Sets.Add(Str);

			for (auto i = 0; i < FileDef.DependencyCount(); ++i)
			{
				auto DepFileDef = FileDef.Dependency(i);
				if (!DepFileDef)
					continue;
				TravelProtoFile(Sets, DepFileDef);
			}

			for (auto i = 0; i < FileDef.ToplevelEnumCount(); ++i)
			{
				auto EnumDef = FileDef.ToplevelEnum(i);
				if (!EnumDef)
					continue;
				TravelProtoEnum(Sets, EnumDef);
			}

			for (auto i = 0; i < FileDef.ToplevelMessageCount(); ++i)
			{
				auto MsgDef = FileDef.ToplevelMessage(i);
				if (!MsgDef)
					continue;
				TravelProtoMessage(Sets, MsgDef);
			}
		}

		TMap<const upb_FileDef*, upb_StringView> DescMap;

	public:
		bool DeleteAssetFile(const FString& PkgPath)
		{
			FString FilePath;
			if (!FPackageName::DoesPackageExist(PkgPath, &FilePath))
				return false;
			return IPlatformFile::GetPlatformPhysical().DeleteFile(*FilePath);
		}

		TArray<FString> GatherAssets(TArray<FFileDefPtr> FileDefs) const
		{
			TSet<FString> Sets;
			for (auto FileDef : FileDefs)
				TravelProtoFile(Sets, FileDef);
			return Sets.Array();
		}
		FProtoTraveler() {}
		FProtoTraveler(const TMap<const upb_FileDef*, upb_StringView>& InDescMap)
			: DescMap(InDescMap)
		{
		}
	};

	class FProtoGenerator : public FProtoTraveler
	{
		TMap<const upb_FileDef*, UProtoDescrotor*> FileDefMap;
		TMap<const upb_MessageDef*, UUserDefinedStruct*> MsgDefs;
		TMap<const upb_EnumDef*, UUserDefinedEnum*> EnumDefs;
		TSet<UUserDefinedStruct*> UserStructs;
		FEdGraphPinType FillBasicInfo(FFieldDefPtr FieldDef, UProtoDescrotor* Desc, FString& DefaultVal, bool bRefresh)
		{
			FEdGraphPinType PinType;
			PinType.ContainerType = FieldDef.IsArray() ? EPinContainerType::Array : EPinContainerType::None;
			auto CType = FieldDef.GetCType();
			switch (CType)
			{
				case kUpb_CType_Bool:
					PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
					DefaultVal = LexToString(FieldDef.DefaultValue().bool_val);
					break;
				case kUpb_CType_Float:
#if UE_VERSION_NEWER_THAN(5, 0, 0)
					PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
					PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
#else
					PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
#endif
					DefaultVal = LexToString(FieldDef.DefaultValue().float_val);
					break;
				case kUpb_CType_Double:
#if UE_VERSION_NEWER_THAN(5, 0, 0)
					PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
					PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
#else
					PinType.PinCategory = UEdGraphSchema_K2::PC_Double;
#endif
					DefaultVal = LexToString(FieldDef.DefaultValue().double_val);
					break;
				case kUpb_CType_Int32:
				case kUpb_CType_UInt32:
					PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
					DefaultVal = LexToString(FieldDef.DefaultValue().int32_val);
					break;
				case kUpb_CType_Int64:
				case kUpb_CType_UInt64:
					PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
					DefaultVal = LexToString(FieldDef.DefaultValue().int64_val);
					break;
				case kUpb_CType_String:
					PinType.PinCategory = UEdGraphSchema_K2::PC_String;
					DefaultVal = StringView(FieldDef.DefaultValue().str_val);
					break;
				case kUpb_CType_Bytes:
				{
					ensure(!FieldDef.IsRepeated());
					PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
					PinType.ContainerType = EPinContainerType::Array;
				}
				break;
				case kUpb_CType_Enum:
				{
					PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
					auto SubEnumDef = FieldDef.EnumSubdef();
					auto ProtoDesc = AddProtoFileImpl(SubEnumDef.FileDef(), bRefresh);
					PinType.PinSubCategoryObject = AddProtoEnum(SubEnumDef, ProtoDesc, bRefresh);
					PinType.PinSubCategory = PinType.PinSubCategoryObject->GetFName();

					DefaultVal = SubEnumDef.Value(FieldDef.DefaultValue().int32_val).Name();
				}
				break;
				case kUpb_CType_Message:
				{
					if (FieldDef.IsMap())
					{
						ensureAlways(!FieldDef.IsArray());

						auto MapEntryDef = FieldDef.MapEntrySubdef();
						FFieldDefPtr KeyDef = MapEntryDef.MapKeyDef();
						FFieldDefPtr ValueDef = MapEntryDef.MapValueDef();

						ensureAlways(!KeyDef.IsSubMessage() && !ValueDef.IsRepeated() && !ValueDef.IsMap());

						FString IgnoreDfault;
						PinType = FillBasicInfo(KeyDef, Desc, IgnoreDfault, bRefresh);
						auto ValueType = FillBasicInfo(ValueDef, Desc, IgnoreDfault, bRefresh);
						PinType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
						PinType.ContainerType = EPinContainerType::Map;
					}
					else
					{
						PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
						auto SubMsgDef = FieldDef.MessageSubdef();
						auto ProtoDesc = AddProtoFileImpl(SubMsgDef.FileDef(), bRefresh);
						PinType.PinSubCategoryObject = AddProtoMessage(SubMsgDef, ProtoDesc, bRefresh);
						PinType.PinSubCategory = PinType.PinSubCategoryObject->GetFName();
					}
				}
				break;
			}

			return PinType;
		}

		UUserDefinedStruct* AddProtoMessage(FMessageDefPtr MsgDef, UProtoDescrotor* Desc, bool bRefresh)
		{
			check(MsgDef);

			for (auto i = 0; i < MsgDef.NestedEnumCount(); ++i)
			{
				auto NestedEnumDef = MsgDef.NestedEnum(i);
				if (!ensureAlways(NestedEnumDef))
					continue;
				AddProtoEnum(NestedEnumDef, Desc, bRefresh);
			}
			for (auto i = 0; i < MsgDef.NestedMessageCount(); ++i)
			{
				auto NestedMessageDef = MsgDef.NestedMessage(i);
				if (!ensureAlways(NestedMessageDef))
					continue;
				AddProtoMessage(NestedMessageDef, Desc, bRefresh);
			}

			static bool bRenameLater = true;
			static auto CreateProtoDefinedStruct = [](UPackage* InParent, FMessageDefPtr InMsgDef, UProtoDescrotor* InDesc, EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional) {
				UProtoDefinedStruct* Struct = nullptr;
				if (ensure(FStructureEditorUtils::UserDefinedStructEnabled()))
				{
					Struct = NewObject<UProtoDefinedStruct>(InParent, InMsgDef.Name(), Flags);
					check(Struct);
					Struct->FullName = InMsgDef.FullName();
					Struct->ProtoDesc = InDesc;

					Struct->EditorData = NewObject<UUserDefinedStructEditorData>(Struct, NAME_None, RF_Transactional);
					check(Struct->EditorData);

					Struct->Guid = FGuid::NewGuid();
					Struct->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
					Struct->Bind();
					Struct->StaticLink(true);
					Struct->Status = UDSS_Error;

					if (bRenameLater)
						FStructureEditorUtils::AddVariable(Struct, FEdGraphPinType(UEdGraphSchema_K2::PC_Boolean, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
				}
				return Struct;
			};
			static auto GenerateNameVariable = [](UUserDefinedStruct* Struct, const FString& NameBase, const FGuid Guid) -> FName {
				FString Result;
				if (!NameBase.IsEmpty())
				{
					if (!FName::IsValidXName(NameBase, INVALID_OBJECTNAME_CHARACTERS))
					{
						Result = MakeObjectNameFromDisplayLabel(NameBase, NAME_None).GetPlainNameString();
					}
					else
					{
						Result = NameBase;
					}
				}

				if (Result.IsEmpty())
				{
					Result = TEXT("MemberVar");
				}

				const uint32 UniqueNameId = CastChecked<UUserDefinedStructEditorData>(Struct->EditorData)->GenerateUniqueNameIdForMemberVariable();
				const FString FriendlyName = FString::Printf(TEXT("%s_%u"), *Result, UniqueNameId);
				const FName NameResult = *FString::Printf(TEXT("%s_%s"), *FriendlyName, *Guid.ToString(EGuidFormats::Digits));
				check(NameResult.IsValidXName(INVALID_OBJECTNAME_CHARACTERS));
				return NameResult;
			};

			FString MsgAssetPath = GetProtoMessagePkgStr(MsgDef);
			if (MsgDefs.Contains(*MsgDef))
			{
				return MsgDefs.FindChecked(*MsgDef);
			}

			FScopeMark ScopeMark(ScopeStack, MsgDef.FullName().ToFString());

			bool bPackageCreated = FPackageName::DoesPackageExist(MsgAssetPath);
			UPackage* StructPkg = bPackageCreated ? LoadPackage(nullptr, *MsgAssetPath, LOAD_NoWarn) : CreatePackage(*MsgAssetPath);
			ensure(StructPkg);
			UObject* OldStruct = StructPkg->FindAssetInPackage();
			UUserDefinedStruct* MsgStruct = CreateProtoDefinedStruct(StructPkg, MsgDef, Desc);
			MsgDefs.Add({*MsgDef, MsgStruct});
			UserStructs.Add(MsgStruct);

			auto OldDescs = FStructureEditorUtils::GetVarDesc(MsgStruct);

			if (OldDescs.Num() > 0 && MsgStruct->GetStructureSize() > 0)
				FStructureEditorUtils::CompileStructure(MsgStruct);

			TMap<FString, FGuid> NameList;
			for (auto FieldIndex = 0; FieldIndex < MsgDef.FieldCount(); ++FieldIndex)
			{
				auto FieldDef = MsgDef.Field(FieldIndex);
				if (!ensureAlways(FieldDef))
					continue;

				FString DefaultVal;
				auto PinType = FillBasicInfo(FieldDef, Desc, DefaultVal, bRefresh);
				FGuid VarGuid;
				auto FieldName = FieldDef.Name().ToFString();
				Algo::FindByPredicate(OldDescs, [&](const FStructVariableDescription& Desc) {
					FString MemberName = Desc.VarName.ToString();
					GMP::Serializer::StripUserDefinedStructName(MemberName);
					if (MemberName == FieldName)
					{
						VarGuid = Desc.VarGuid;
						return true;
					}
					return false;
				});

				if (!VarGuid.IsValid())
				{
					if (bRenameLater)
					{
						ensureAlways(FStructureEditorUtils::AddVariable(MsgStruct, PinType));
						VarGuid = FStructureEditorUtils::GetVarDesc(MsgStruct).Last().VarGuid;
					}
					else
					{
						FStructureEditorUtils::ModifyStructData(MsgStruct);
						FStructVariableDescription NewVar;
						VarGuid = FGuid::NewGuid();
						NewVar.VarName = GenerateNameVariable(MsgStruct, FieldDef.Name(), VarGuid);
						NewVar.FriendlyName = FieldDef.Name();
						NewVar.SetPinType(PinType);
						NewVar.VarGuid = VarGuid;
						FStructureEditorUtils::GetVarDesc(MsgStruct).Add(NewVar);
					}
					if (!DefaultVal.IsEmpty())
						FStructureEditorUtils::ChangeVariableDefaultValue(MsgStruct, VarGuid, DefaultVal);
					NameList.Add(FieldDef.Name(), VarGuid);
				}
				else
				{
					FStructureEditorUtils::ChangeVariableType(MsgStruct, VarGuid, PinType);
					FStructureEditorUtils::ChangeVariableDefaultValue(MsgStruct, VarGuid, DefaultVal);
					NameList.Add(FieldDef.Name(), FGuid());
				}
			}  // for

			if (OldStruct && OldStruct != MsgStruct)
			{
				TArray<UObject*> Olds{OldStruct};
				ObjectTools::ConsolidateObjects(MsgStruct, Olds, false);
				OldStruct->ClearFlags(RF_Standalone);
				OldStruct->RemoveFromRoot();
				OldStruct->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
				MsgStruct->Rename(MsgDef.Name().ToFStringData(), StructPkg);
			}
			else if (!OldStruct)
			{
				if (bRenameLater)
				{
					for (auto& Pair : NameList)
					{
						if (Pair.Value.IsValid())
							FStructureEditorUtils::RenameVariable(MsgStruct, Pair.Value, Pair.Key);
					}
				}
				else
				{
					FStructureEditorUtils::OnStructureChanged(MsgStruct, FStructureEditorUtils::EStructureEditorChangeInfo::AddedVariable);
				}

				auto& Descs = FStructureEditorUtils::GetVarDesc(MsgStruct);
				auto RemovedCnt = Descs.RemoveAll([&](auto& Desc) {
					FString MemberName = Desc.VarName.ToString();
					GMP::Serializer::StripUserDefinedStructName(MemberName);
					return !NameList.Contains(MemberName);
				});
				if (RemovedCnt > 0)
				{
					FStructureEditorUtils::OnStructureChanged(MsgStruct, FStructureEditorUtils::EStructureEditorChangeInfo::RemovedVariable);
				}
			}

			if (MsgStruct->Status != UDSS_UpToDate)
			{
				UE_LOG(LogGMP, Error, TEXT("%s"), *MsgStruct->ErrorMessage);
			}

			FString Filename;
			if (ensureAlways(FPackageName::TryConvertLongPackageNameToFilename(MsgAssetPath, Filename, FPackageName::GetAssetPackageExtension())))
			{
				StructPkg->Modify();
#if UE_VERSION_NEWER_THAN(5, 0, 0)
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
				SaveArgs.Error = GError;
				UPackage::SavePackage(StructPkg, MsgStruct, *Filename, SaveArgs);
#else
				UPackage::SavePackage(StructPkg, MsgStruct, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *Filename, GError, nullptr, true, true, SAVE_NoError);
#endif
				IAssetRegistry::Get()->AssetCreated(MsgStruct);
			}
			return MsgStruct;
		}

		UUserDefinedEnum* AddProtoEnum(FEnumDefPtr EnumDef, UProtoDescrotor* Desc, bool bRefresh)
		{
			check(EnumDef);

			FString EnumAssetPath = GetProtoEnumPkgStr(EnumDef);
			if (EnumDefs.Contains(*EnumDef))
			{
				return EnumDefs.FindChecked(*EnumDef);
			}

			FScopeMark ScopeMark(ScopeStack, EnumDef.FullName().ToFString());

			//RemoveOldAsset(*MsgAssetPath);
			bool bPackageCreated = FPackageName::DoesPackageExist(EnumAssetPath);
			UPackage* EnumPkg = bPackageCreated ? LoadPackage(nullptr, *EnumAssetPath, LOAD_NoWarn) : CreatePackage(*EnumAssetPath);
			ensure(EnumPkg);
			UUserDefinedEnum* EnumObj = FindObject<UUserDefinedEnum>(EnumPkg, *EnumAssetPath);
			if (!EnumObj)
			{
				static auto CreateUserDefinedEnum = [](UObject* InParent, FEnumDefPtr InEnumDef, UProtoDescrotor* InDesc, EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional) {
					// Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(InParent, InEnumDef.Name(), Flags));
					UProtoDefinedEnum* Enum = NewObject<UProtoDefinedEnum>(InParent, InEnumDef.Name(), Flags);
					Enum->FullName = InEnumDef.FullName();
					Enum->ProtoDesc = InDesc;
					TArray<TPair<FName, int64>> EmptyNames;
					Enum->SetEnums(EmptyNames, UEnum::ECppForm::Namespaced);
					Enum->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
					return Enum;
				};

				EnumObj = CreateUserDefinedEnum(EnumPkg, EnumDef, Desc);
			}
			else
			{
				//Clear
			}
			EnumDefs.Add({*EnumDef, EnumObj});

			TArray<TPair<FName, int64>> Names;
			for (int32 i = 0; i < EnumDef.ValueCount(); ++i)
			{
				FEnumValDefPtr EnumValDef = EnumDef.Value(i);
				if (!ensureAlways(EnumValDef))
					continue;
				const FString FullNameStr = EnumObj->GenerateFullEnumName(EnumValDef.Name().ToFStringData());
				Names.Add({*FullNameStr, EnumValDef.Number()});
			}
			EnumObj->SetEnums(Names, UEnum::ECppForm::Namespaced, EEnumFlags::Flags, true);

			for (int32 i = 0; i < EnumDef.ValueCount(); ++i)
			{
				FEnumValDefPtr EnumValDef = EnumDef.Value(i);
				if (!ensureAlways(EnumValDef))
					continue;
				FEnumEditorUtils::SetEnumeratorDisplayName(EnumObj, EnumValDef.Number(), FText::FromString(EnumValDef.Name()));
			}

			FString Filename;
			if (ensureAlways(FPackageName::TryConvertLongPackageNameToFilename(EnumAssetPath, Filename, FPackageName::GetAssetPackageExtension())))
			{
				EnumPkg->Modify();
#if UE_VERSION_NEWER_THAN(5, 0, 0)
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
				SaveArgs.Error = GError;
				UPackage::SavePackage(EnumPkg, EnumObj, *Filename, SaveArgs);
#else
				UPackage::SavePackage(EnumPkg, EnumObj, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *Filename, GError, nullptr, true, true, SAVE_NoError);
#endif
				IAssetRegistry::Get()->AssetCreated(EnumObj);
			}
			return EnumObj;
		}

		TPair<UProtoDescrotor*, bool> AddProtoDesc(FFileDefPtr FileDef, bool bFroce)
		{
			FString FileNameStr;
			FString DescAssetPath = GetProtoDescPkgStr(FileDef, &FileNameStr);

			//RemoveOldAsset(*MsgAssetPath);
			bool bPackageCreated = FPackageName::DoesPackageExist(DescAssetPath);
			UPackage* DescPkg = bPackageCreated ? LoadPackage(nullptr, *DescAssetPath, LOAD_NoWarn) : CreatePackage(*DescAssetPath);
			ensure(DescPkg);
			UProtoDescrotor* OldDescObj = FindObject<UProtoDescrotor>(DescPkg, *FileNameStr);
			if (OldDescObj && !bFroce && StringView(DescMap.FindChecked(*FileDef)) == OldDescObj->Desc)
			{
				return {OldDescObj, true};
			}

			return {NewObject<UProtoDescrotor>(DescPkg, *FileNameStr, RF_Public | RF_Standalone), false};
		}

		void SaveProtoDesc(UProtoDescrotor* DescObj, FFileDefPtr FileDef, TArray<UProtoDescrotor*> Deps)
		{
			DescObj->Deps = Deps;

			upb_StringView DescView = DescMap.FindChecked(*FileDef);
			DescObj->Desc.AddUninitialized(DescView.size);
			FMemory::Memcpy(DescObj->Desc.GetData(), DescView.data, DescObj->Desc.Num());

			FString FileNameStr;
			FString DescAssetPath = GetProtoDescPkgStr(FileDef, &FileNameStr);
			auto Pkg = FindPackage(nullptr, *DescAssetPath);
			UProtoDescrotor* OldDescObj = FindObject<UProtoDescrotor>(Pkg, *FileNameStr);
			if (OldDescObj && OldDescObj != DescObj)
			{
				TArray<UObject*> Olds{OldDescObj};
				ObjectTools::ConsolidateObjects(DescObj, Olds, false);
				OldDescObj->ClearFlags(RF_Standalone);
				OldDescObj->RemoveFromRoot();
				OldDescObj->Rename(nullptr, nullptr, REN_DontCreateRedirectors);
				DescObj->Rename(*FileNameStr, Pkg);
			}

			FString Filename;
			if (ensureAlways(FPackageName::TryConvertLongPackageNameToFilename(Pkg->GetPathName(), Filename, FPackageName::GetAssetPackageExtension())))
			{
				Pkg->Modify();
#if UE_VERSION_NEWER_THAN(5, 0, 0)
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
				SaveArgs.Error = GError;
				UPackage::SavePackage(Pkg, DescObj, *Filename, SaveArgs);
#else
				UPackage::SavePackage(Pkg, DescObj, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *Filename, GError, nullptr, true, true, SAVE_NoError);
#endif
				IAssetRegistry::Get()->AssetCreated(DescObj);
			}
		}

		UProtoDescrotor* AddProtoFileImpl(FFileDefPtr FileDef, bool bRefresh)
		{
			check(FileDef);
			if (FileDefMap.Contains(*FileDef))
				return FileDefMap.FindChecked(*FileDef);

			FScopeMark ScopeMark(ScopeStack, FileDef.Package().ToFString() / FileDef.Name().ToFString());

			auto DescPair = AddProtoDesc(FileDef, bRefresh);
			UProtoDescrotor* ProtoDesc = DescPair.Key;
			FileDefMap.Add({*FileDef, ProtoDesc});

			if (DescPair.Value)
			{
				TMap<FName, TArray<FName>> RefMap;
				TMap<FName, TArray<FName>> DepMap;
				auto PkgName = FName(*ProtoDesc->GetPackage()->GetPathName());
				FEditorUtils::GetReferenceAssets(nullptr, {PkgName.ToString()}, RefMap, DepMap, false);
				if (auto Find = RefMap.Find(PkgName))
				{
					for (auto& AssetName : *Find)
					{
						auto Struct = LoadObject<UProtoDefinedStruct>(nullptr, *AssetName.ToString());
						if (!Struct)
							continue;
						UserStructs.Add(Struct);
					}
				}
				return ProtoDesc;
			}

			TArray<UProtoDescrotor*> Deps;
			for (auto i = 0; i < FileDef.DependencyCount(); ++i)
			{
				auto DepFileDef = FileDef.Dependency(i);
				if (!ensureAlways(DepFileDef))
					continue;
				auto Desc = AddProtoFileImpl(DepFileDef, bRefresh);
				if (!ensureAlways(Desc))
					continue;
				Deps.Add(Desc);
			}
			SaveProtoDesc(ProtoDesc, FileDef, Deps);

			for (auto i = 0; i < FileDef.ToplevelEnumCount(); ++i)
			{
				auto EnumDef = FileDef.ToplevelEnum(i);
				if (!ensureAlways(EnumDef))
					continue;
				AddProtoEnum(EnumDef, ProtoDesc, bRefresh);
			}

			for (auto i = 0; i < FileDef.ToplevelMessageCount(); ++i)
			{
				auto MsgDef = FileDef.ToplevelMessage(i);
				if (!ensureAlways(MsgDef))
					continue;
				AddProtoMessage(MsgDef, ProtoDesc, bRefresh);
			}
			return ProtoDesc;
		}

	public:
		FProtoGenerator(const TMap<const upb_FileDef*, upb_StringView>& In)
			: FProtoTraveler(In)
		{
		}

		void AddProtoFile(FFileDefPtr FileDef, bool bRefresh = false) { AddProtoFileImpl(FileDef, bRefresh); }
		TSet<UUserDefinedStruct*> GetUserDefinedStructs() const { return UserStructs; }

		TArray<FString> ScopeStack;
		struct FScopeMark
		{
			TArray<FString>& StackRef;
			FString Str;
			int32 Lv;
			FScopeMark(TArray<FString>& Stack, FString InStr)
				: StackRef(Stack)
				, Str(MoveTemp(InStr))
			{
				StackRef.Add(Str);
				Lv = StackRef.Num();
				UE_LOG(LogGMP, Display, TEXT("ScopeMark : %s"), *Str);
			}
			~FScopeMark()
			{
				if (ensureAlways(Lv == StackRef.Num() && StackRef.Last() == Str))
					StackRef.Pop();
			}
		};
	};

	static void RandomizeProperties(FProperty* Prop, void* Addr)
	{
		if (Prop->IsA<FBoolProperty>())
		{
			auto BoolProp = CastFieldChecked<FBoolProperty>(Prop);
			BoolProp->SetPropertyValue(Addr, FMath::RandBool());
		}
		else if (Prop->IsA<FEnumProperty>())
		{
			auto EnumProp = CastFieldChecked<FEnumProperty>(Prop);
			auto NumericProp = EnumProp->GetUnderlyingProperty();
			if (ensure(NumericProp))
			{
				RandomizeProperties(NumericProp, Addr);
			}
		}
		else if (Prop->IsA<FByteProperty>())
		{
			auto ByteProp = CastFieldChecked<FByteProperty>(Prop);
			ByteProp->SetPropertyValue(Addr, (uint8)FMath::RandRange(0, 255));
		}
		else if (Prop->IsA<FInt8Property>())
		{
			auto Int8Prop = CastFieldChecked<FInt8Property>(Prop);
			Int8Prop->SetPropertyValue(Addr, (int8)FMath::RandRange(-127, 128));
		}
		else if (Prop->IsA<FIntProperty>())
		{
			auto IntProp = CastFieldChecked<FIntProperty>(Prop);
			IntProp->SetPropertyValue(Addr, FMath::RandRange(INT_MIN, INT_MAX));
		}
		else if (Prop->IsA<FUInt32Property>())
		{
			auto UIntProp = CastFieldChecked<FUInt32Property>(Prop);
			UIntProp->SetPropertyValue(Addr, (uint32)FMath::RandRange(INT_MIN, INT_MAX));
		}
		else if (Prop->IsA<FInt64Property>())
		{
			auto Int64Prop = CastFieldChecked<FInt64Property>(Prop);
			Int64Prop->SetPropertyValue(Addr, FMath::RandRange(INT64_MIN, INT64_MAX));
		}
		else if (Prop->IsA<FUInt64Property>())
		{
			auto UInt64Prop = CastFieldChecked<FUInt64Property>(Prop);
			UInt64Prop->SetPropertyValue(Addr, (uint64)FMath::RandRange(INT64_MIN, INT64_MAX));
		}
		else if (Prop->IsA<FFloatProperty>())
		{
			auto FloatProp = CastFieldChecked<FFloatProperty>(Prop);
			FloatProp->SetPropertyValue(Addr, FMath::RandRange(-FLT_MAX, FLT_MAX));
		}
		else if (Prop->IsA<FDoubleProperty>())
		{
			auto DoubleProp = CastFieldChecked<FDoubleProperty>(Prop);
			DoubleProp->SetPropertyValue(Addr, FMath::RandRange(-DBL_MAX, DBL_MAX));
		}
		else if (Prop->IsA<FStrProperty>())
		{
			auto StrProp = CastFieldChecked<FStrProperty>(Prop);
			StrProp->SetPropertyValue(Addr, FString::Printf(TEXT("%d"), FMath::RandRange(INT_MIN, INT_MAX)));
		}
		else if (Prop->IsA<FStructProperty>())
		{
			auto StructProp = CastFieldChecked<FStructProperty>(Prop);
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				RandomizeProperties(*It, It->ContainerPtrToValuePtr<void>(Addr));
			}
		}
		else if (Prop->IsA<FArrayProperty>())
		{
			auto ArrProp = CastFieldChecked<FArrayProperty>(Prop);
			FScriptArrayHelper Helper(ArrProp, Addr);
			Helper.Resize(FMath::RandRange(1, 16));
			for (auto i = 0; i < Helper.Num(); ++i)
			{
				RandomizeProperties(ArrProp->Inner, Helper.GetRawPtr(i));
			}
		}
	}

	static void VerifyProtoStruct(const UScriptStruct* UserStruct)
	{
		FStructOnScope StructOnScopeFrom;
		StructOnScopeFrom.Initialize(UserStruct);
		RandomizeProperties(GMP::Class2Prop::TTraitsStructBase::GetProperty(UserStruct), StructOnScopeFrom.GetStructMemory());

		TArray<uint8> Buffer;
		ensureAlways(Serializer::UStructToProtoImpl(Buffer, UserStruct, StructOnScopeFrom.GetStructMemory()));

		FStructOnScope StructOnScopeTo;
		StructOnScopeTo.Initialize(UserStruct);
		ensureAlways(Deserializer::UStructFromProtoImpl(Buffer, UserStruct, StructOnScopeTo.GetStructMemory()));

		ensureAlways(UserStruct->CompareScriptStruct(StructOnScopeFrom.GetStructMemory(), StructOnScopeTo.GetStructMemory(), CPF_None));
	}

	static void VerifyProtoStruct(const TArray<FString>& Args, UWorld* World)
	{
		for (auto& Arg : Args)
		{
			auto RetPath = FString::Printf(TEXT("%s/%s"), *GMP::PB::ProtoRootDir, *Arg);
			if (auto ProtoStruct = LoadObject<UProtoDefinedStruct>(nullptr, *RetPath))
			{
				VerifyProtoStruct(ProtoStruct);
			}
		}
	}
	FAutoConsoleCommandWithWorldAndArgs XVar_VerifyProtoStruct(TEXT("x.gmp.proto.testProto"),
															   TEXT("x.gmp.proto.testProto [PathsRelativeToProtoDirRoot]..."),  //
															   FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(VerifyProtoStruct));

	static void VerifyProtoStructs()
	{
		FString RootDir;
		FPackageName::TryConvertLongPackageNameToFilename(GMP::PB::ProtoRootDir, RootDir);
		TArray<FString> AssetFiles;
		IPlatformFile::GetPlatformPhysical().FindFilesRecursively(AssetFiles, *RootDir, *FPackageName::GetAssetPackageExtension());

		for (auto& AssetFile : AssetFiles)
		{
			auto PkgPath = FPackageName::FilenameToLongPackageName(AssetFile);
			if (auto ProtoStruct = LoadObject<UProtoDefinedStruct>(nullptr, *PkgPath))
			{
				VerifyProtoStruct(ProtoStruct);
			}
		}
	}
	FAutoConsoleCommand XVar_VerifyProtoStructs(TEXT("x.gmp.proto.testProtos"), TEXT(""), FConsoleCommandDelegate::CreateStatic(VerifyProtoStructs));

	static void GeneratePBStruct(UWorld* InWorld)
	{
		auto& DefPool = ResetEditorPoolPtr();

		TMap<const upb_FileDef*, upb_StringView> Storages;
		TArray<FFileDefPtr> FileDefs = upb::generator::FillDefPool(DefPool, Storages);

		auto AssetToUnload = FProtoTraveler(Storages).GatherAssets(FileDefs);
		if (!ensure(AssetToUnload.Num()))
			return;

		FEditorUtils::DeletePackages(InWorld, AssetToUnload, CreateWeakLambda(InWorld, [Storages, FileDefs](bool bSucc, TArray<FString> AllUnloadedList) {
										 if (!bSucc)
											 return;
										 TArray<FString> FilePaths;
										 for (auto& ResId : AllUnloadedList)
										 {
											 FString FilePath;
											 if (FPackageName::DoesPackageExist(ResId, &FilePath))
											 {
												 FilePaths.Add(MoveTemp(FilePath));
											 }
										 }
										 if (FilePaths.Num())
											 IAssetRegistry::Get()->ScanFilesSynchronous(FilePaths, true);

										 FScopedTransaction ScopedTransaction(NSLOCTEXT("GMPProto", "GeneratePBStruct", "GeneratePBStruct"));
										 bool bRefresh = false;
										 FProtoGenerator ProtoGenerator(Storages);
										 for (auto FileDef : FileDefs)
											 ProtoGenerator.AddProtoFile(FileDef, bRefresh);

										 IAssetRegistry::Get()->ScanFilesSynchronous(FilePaths, true);
										 for (auto UserStruct : ProtoGenerator.GetUserDefinedStructs())
										 {
											 VerifyProtoStruct(UserStruct);
										 }
									 }));
	}

	FAutoConsoleCommandWithWorld XVar_GeneratePBStruct(TEXT("x.gmp.proto.genNative"), TEXT(""), FConsoleCommandWithWorldDelegate::CreateStatic(GeneratePBStruct));
}  // namespace PB
}  // namespace GMP
#endif  // GMP_EXTEND_CONSOLE

#if defined(PROTOBUF_API)
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

namespace GMP
{
namespace PB
{
	static TArray<TArray<uint8>> GatherFileDescriptorProtosForDir(FString RootDir)
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
				virtual void AddWarning(const std::string& filename, int line, int column, const std::string& message) override {}
				virtual void AddError(const std::string& filename, int line, int column, const std::string& message) override
				{
					UE_LOG(LogGMP, Error, TEXT("%s(%d:%d) : %s"), UTF8_TO_TCHAR(filename.c_str()), line, column, UTF8_TO_TCHAR(message.c_str()));
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
	FAutoConsoleCommandWithWorld XVar_GatherProtos(TEXT("x.gmp.proto.genProtoRootDir"),
												   TEXT(""),  //
												   FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld) {
													   void* ParentWindowHandle = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
													   IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

													   FString OutFolderPath;
													   static TOptional<FString> DefaultPath;
													   if (!DefaultPath.IsSet())
													   {
														   FString ValueRead;
														   GConfig->GetString(TEXT("GMP"), TEXT("LastProtoDir"), ValueRead, GEditorPerProjectIni);
														   if (!ValueRead.IsEmpty())
														   {
															   DefaultPath = ValueRead;
														   }
													   }
													   if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("please choose proto's root directory"), DefaultPath.Get(FPaths::ProjectContentDir()), OutFolderPath))
														   return;

													   DefaultPath = OutFolderPath;
													   {
														   GConfig->SetString(TEXT("GMP"), TEXT("LastProtoDir"), *OutFolderPath, GEditorPerProjectIni);
														   GConfig->Flush(false, GEditorPerProjectIni);
													   }

													   auto Descs = GatherFileDescriptorProtosForDir(OutFolderPath);
													   if (!Descs.Num())
														   return;

													   auto& PreGenerator = upb::generator::GetPreGenerator();
													   PreGenerator.Reset();
													   for (auto& Desc : Descs)
													   {
														   PreGenerator.PreAddProtoDesc(Desc);
													   }
													   GeneratePBStruct(InWorld);
												   }));
}  // namespace PB
}  // namespace GMP
#endif  // defined(PROTOBUF_API)
#endif  // WITH_EDITOR
