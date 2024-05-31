//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoSerializer.h"

#if defined(GMP_WITH_UPB)
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
#include "upb/libupb.h"
#include "upb/util/def_to_proto.h"
#include "upb/wire/types.h"

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
		void PreAddProtoDesc(TArrayView<const uint8> Buf) { Descriptors.Add(Arena.AllocString(Buf)); }

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

		template<typename DefPoolType>
		TArray<FFileDefPtr> FillDefPool(DefPoolType& Pool, TMap<const upb_FileDef*, upb_StringView>& OutMap)
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
	template<typename DefPoolType>
	static TArray<FFileDefPtr> FillDefPool(DefPoolType& Pool, TMap<const upb_FileDef*, upb_StringView>& OutMap)
	{
		return GetPreGenerator().FillDefPool(Pool, OutMap);
	}
	// TODO Generate upb sources
	static bool GenerateUPBSrcs(const upb_FileDef* FileDef)
	{
		return false;
	}

	bool upbRegFileDescProtoImpl(const _upb_DefPool_Init* DefInit)
	{
		return GetPreGenerator().PreAddProto(DefInit->descriptor);
	}
}  // namespace generator
}  // namespace upb

namespace GMP
{
namespace PB
{
	static const FString kClearMethodPrefix = "clear_";
	static const FString kSetMethodPrefix = "set_";
	static const FString kHasMethodPrefix = "has_";
	static const FString kDeleteMethodPrefix = "delete_";
	static const FString kAddToRepeatedMethodPrefix = "add_";
	static const FString kResizeArrayMethodPrefix = "resize_";
	static const FString kRepeatedFieldArrayGetterPostfix = "upb_array";
	static const FString kRepeatedFieldMutableArrayGetterPostfix = "mutable_upb_array";
	static const FString kAccessorPrefixes[] = {kClearMethodPrefix, kDeleteMethodPrefix, kAddToRepeatedMethodPrefix, kResizeArrayMethodPrefix, kSetMethodPrefix, kHasMethodPrefix};
	static const TCHAR* kEnumsInit = TEXT("enums_layout");
	static const TCHAR* kExtensionsInit = TEXT("extensions_layout");
	static const TCHAR* kMessagesInit = TEXT("messages_layout");

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
	FAutoConsoleVariableRef CVar_ProtoRootDir(TEXT("x.gmp.proto.RootDir"), ProtoRootDir, TEXT(""));
	static const TCHAR* PB_Delims[] = {TEXT("."), TEXT("/"), TEXT("-")};

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
#if UE_5_00_OR_LATER
			if (!FPackageName::DoesPackageExist(PkgPath, &FilePath))
				return false;
#else
			if (!FPackageName::DoesPackageExist(PkgPath, nullptr, &FilePath))
				return false;
#endif
			return IPlatformFile::GetPlatformPhysical().DeleteFile(*FilePath);
		}

		TArray<FString> GenerateAssets(TArray<FFileDefPtr> FileDefs) const
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

	class FProtoSrcTraveler : public FProtoTraveler
	{
	protected:
		class FDefPoolPair
		{
		public:
			FDefPoolPair()
			{
				pool32_.SetPlatform(kUpb_MiniTablePlatform_32Bit);
				pool64_.SetPlatform(kUpb_MiniTablePlatform_64Bit);
			}

			FFileDefPtr AddProto(const FDefPool::FProtoDescType* file_proto)
			{
				FFileDefPtr file32 = pool32_.AddProto(file_proto);
				FFileDefPtr file64 = pool64_.AddProto(file_proto);
				if (!file32)
					return file32;
				return file64;
			}

			const upb_MiniTable* GetMiniTable32(FMessageDefPtr m) const { return pool32_.FindMessageByName(m.FullName()).MiniTable(); }
			const upb_MiniTable* GetMiniTable64(FMessageDefPtr m) const { return pool64_.FindMessageByName(m.FullName()).MiniTable(); }
			const upb_MiniTableField* GetField32(FFieldDefPtr f) const { return GetFieldFromPool(&pool32_, f); }
			const upb_MiniTableField* GetField64(FFieldDefPtr f) const { return GetFieldFromPool(&pool64_, f); }

		private:
			static const upb_MiniTableField* GetFieldFromPool(const FDefPool* pool, FFieldDefPtr f)
			{
				if (f.IsExtension())
				{
					return pool->FindExtensionByName(f.FullName()).MiniTable();
				}
				else
				{
					return pool->FindMessageByName(f.ContainingType().FullName()).FindFieldByNumber(f.Number()).MiniTable();
				}
			}

			FDefPool pool32_;
			FDefPool pool64_;
		};

		static void AddMessages(FMessageDefPtr message, TArray<FMessageDefPtr>* messages)
		{
			messages->Add(message);
			for (int32_t i = 0; i < message.NestedMessageCount(); i++)
			{
				AddMessages(message.NestedMessage(i), messages);
			}
		}

		static void AddEnums(FMessageDefPtr message, TArray<FEnumDefPtr>* enums)
		{
			enums->Reserve(enums->Num() + message.NestedEnumCount());
			for (int i = 0; i < message.NestedEnumCount(); i++)
			{
				enums->Add(message.NestedEnum(i));
			}
			for (int i = 0; i < message.NestedMessageCount(); i++)
			{
				AddEnums(message.NestedMessage(i), enums);
			}
		}

		static TArray<FEnumDefPtr> SortedEnums(FFileDefPtr file)
		{
			TArray<FEnumDefPtr> enums;
			enums.Reserve(file.ToplevelEnumCount());
			for (int i = 0; i < file.ToplevelEnumCount(); i++)
			{
				enums.Add(file.ToplevelEnum(i));
			}
			for (int i = 0; i < file.ToplevelMessageCount(); i++)
			{
				AddEnums(file.ToplevelMessage(i), &enums);
			}
			Algo::Sort(enums, [](FEnumDefPtr a, FEnumDefPtr b) { return strcmp(a.FullName(), b.FullName()) < 0; });
			return enums;
		}

		// Ordering must match upb/def.c!
		//
		// The ordering is significant because each upb_MessageDef* will point at the
		// corresponding upb_MiniTable and we just iterate through the list without
		// any search or lookup.
		static TArray<FMessageDefPtr> SortedMessages(FFileDefPtr file)
		{
			TArray<FMessageDefPtr> messages;
			for (int i = 0; i < file.ToplevelMessageCount(); i++)
			{
				AddMessages(file.ToplevelMessage(i), &messages);
			}
			return messages;
		}

		static void AddExtensionsFromMessage(FMessageDefPtr message, TArray<FFieldDefPtr>* exts)
		{
			for (int i = 0; i < message.NestedExtensionCount(); i++)
			{
				exts->Add(message.NestedExtension(i));
			}
			for (int i = 0; i < message.NestedMessageCount(); i++)
			{
				AddExtensionsFromMessage(message.NestedMessage(i), exts);
			}
		}

		// Ordering must match upb/def.c!
		//
		// The ordering is significant because each upb_FieldDef* will point at the
		// corresponding upb_MiniTableExtension and we just iterate through the list
		// without any search or lookup.
		static TArray<FFieldDefPtr> SortedExtensions(FFileDefPtr file)
		{
			TArray<FFieldDefPtr> ret;
			ret.Reserve(file.ToplevelExtensionCount());
			for (int i = 0; i < file.ToplevelExtensionCount(); i++)
			{
				ret.Add(file.ToplevelExtension(i));
			}
			for (int i = 0; i < file.ToplevelMessageCount(); i++)
			{
				AddExtensionsFromMessage(file.ToplevelMessage(i), &ret);
			}
			return ret;
		}
		static TArray<FFieldDefPtr> FieldNumberOrder(FMessageDefPtr message)
		{
			TArray<FFieldDefPtr> fields;
			fields.Reserve(message.FieldCount());
			for (int i = 0; i < message.FieldCount(); i++)
			{
				fields.Add(message.Field(i));
			}
			Algo::Sort(fields, [](FFieldDefPtr a, FFieldDefPtr b) { return a.Number() < b.Number(); });
			return fields;
		}
		static TArray<FMessageDefPtr> SortedForwardMessages(const TArray<FMessageDefPtr>& this_file_messages, const TArray<FFieldDefPtr>& this_file_exts)
		{
			TMap<FString, FMessageDefPtr> forward_messages;
			for (auto message : this_file_messages)
			{
				for (int i = 0; i < message.FieldCount(); i++)
				{
					FFieldDefPtr Field = message.Field(i);
					if (Field.GetCType() == kUpb_CType_Message && Field.FileDef() != Field.MessageSubdef().FileDef())
					{
						forward_messages.FindOrAdd(Field.MessageSubdef().FullName()) = Field.MessageSubdef();
					}
				}
			}
			for (auto ext : this_file_exts)
			{
				if (ext.FileDef() != ext.ContainingType().FileDef())
				{
					forward_messages.FindOrAdd(ext.ContainingType().FullName()) = ext.ContainingType();
				}
			}
			TArray<FMessageDefPtr> ret;
			ret.Reserve(forward_messages.Num());
			for (const auto& pair : forward_messages)
			{
				ret.Add(pair.Value);
			}
			return ret;
		}
		static FString CTypeInternal(FFieldDefPtr field, bool is_const, bool purec = true)
		{
			FString maybe_const = is_const ? "const " : "";
			switch (field.GetCType())
			{
				case kUpb_CType_Message:
				{
					FString maybe_struct = (field.FileDef() != field.MessageSubdef().FileDef() && purec) ? TEXT("struct ") : TEXT("");
					return maybe_const + maybe_struct + MessageName(field.MessageSubdef()) + TEXT("*");
				}
				case kUpb_CType_Bool:
					return "bool";
				case kUpb_CType_Float:
					return "float";
				case kUpb_CType_Int32:
				case kUpb_CType_Enum:
					return "int32_t";
				case kUpb_CType_UInt32:
					return "uint32_t";
				case kUpb_CType_Double:
					return "double";
				case kUpb_CType_Int64:
					return "int64_t";
				case kUpb_CType_UInt64:
					return "uint64_t";
				case kUpb_CType_String:
				case kUpb_CType_Bytes:
					return "UPB_STRINGVIEW";
				default:
					abort();
			}
		}

		static FString FloatToCLiteral(float value)
		{
			if (value == std::numeric_limits<float>::infinity())
			{
				return "kUpb_FltInfinity";
			}
			else if (value == -std::numeric_limits<float>::infinity())
			{
				return "-kUpb_FltInfinity";
			}
			else if (std::isnan(value))
			{
				return "kUpb_NaN";
			}
			else if (value == 0.f)
			{
				return "0.f";
			}
			else
			{
				return LexToString(value);
			}
		}

		static FString DoubleToCLiteral(double value)
		{
			if (value == std::numeric_limits<double>::infinity())
			{
				return "kUpb_Infinity";
			}
			else if (value == -std::numeric_limits<double>::infinity())
			{
				return "-kUpb_Infinity";
			}
			else if (std::isnan(value))
			{
				return "kUpb_NaN";
			}
			else if (value == 0.0)
			{
				return "0.0";
			}
			else
			{
				return LexToString(value);
			}
		}

		// Generate a mangled C name for a proto object.
		static FString MangleName(FString str) { return str.Replace(TEXT("_"), TEXT("_0")).Replace(TEXT("."), TEXT("__")); }
		static FString ToCIdent(FString str) { return str.Replace(TEXT("."), TEXT("_")).Replace(TEXT("/"), TEXT("_")).Replace(TEXT("-"), TEXT("_")); }
		static FString ToPreproc(FString str) { return ToCIdent(str).ToUpper(); }
		static FString DefInitSymbol(FFileDefPtr file) { return ToCIdent(file.Name()) + TEXT("_upbdefinit"); }
		static FString StripExtension(FString fname) { return FPaths::GetBaseFilename(fname, false); }
		static FString DefHeaderFilename(FFileDefPtr file) { return StripExtension(file.Name()) + ".upb.hpp"; }
		static FString DefSourceFilename(FFileDefPtr file) { return StripExtension(file.Name()) + ".upb.cpp"; }

		static FString MessageInit(FString full_name) { return MangleName(full_name) + "_msg_init"; }
		static FString MessageInitName(FMessageDefPtr descriptor) { return MessageInit(descriptor.FullName()); }
		static FString MessageName(FMessageDefPtr descriptor) { return ToCIdent(descriptor.FullName()); }
		static FString FileLayoutName(FFileDefPtr file) { return ToCIdent(file.Name()) + "_upb_file_layout"; }
		static FString CApiHeaderFilename(FFileDefPtr file) { return StripExtension(file.Name()) + ".upb.h"; }
		static FString MiniTableHeaderFilename(FFileDefPtr file) { return StripExtension(file.Name()) + TEXT(".upb_minitable.h"); }
		static FString MiniTableSourceFilename(FFileDefPtr file) { return StripExtension(file.Name()) + TEXT(".upb_minitable.c"); }
		static FString EnumInit(FEnumDefPtr descriptor) { return ToCIdent(descriptor.FullName()) + "_enum_init"; }
		FString FieldInitializerRaw(FFieldDefPtr field, const upb_MiniTableField* field64, const upb_MiniTableField* field32)
		{
			return FString::Format(TEXT("{ {0}, {1}, {2}, {3}, {4}, {5} }"),
								   {field64->number,
									ArchDependentSize(field32->offset, field64->offset),
									ArchDependentSize(field32->presence, field64->presence),
#ifdef UPB_PRIVATE
									field64->UPB_PRIVATE(submsg_index) == kUpb_NoSub ? TEXT("kUpb_NoSub") : LexToString(field64->UPB_PRIVATE(submsg_index)),
									field64->UPB_PRIVATE(descriptortype),
#else
									field64->submsg_index_dont_copy_me__upb_internal_use_only == kUpb_NoSub ? TEXT("kUpb_NoSub") : LexToString(field64->submsg_index_dont_copy_me__upb_internal_use_only),
									field64->descriptortype_dont_copy_me__upb_internal_use_only,

#endif
									GetModeInit(field32, field64)});
		}
		FString FieldInitializerRaw(FFieldDefPtr field) { return FieldInitializerRaw(field, PoolPair.GetField64(field), PoolPair.GetField32(field)); }

		FString FieldInitializer(FFieldDefPtr field, const upb_MiniTableField* field64, const upb_MiniTableField* field32)
		{
			if (bBootstrap)
				return FormatOrdered(TEXT("*upb_MiniTable_FindFieldByNumber({0}, {1})"), MessageMiniTableRef(field.ContainingType()), field.Number());
			else
				return FieldInitializerRaw(field, field64, field32);
		}

		FString FieldInitializer(FFieldDefPtr field) { return FieldInitializer(field, PoolPair.GetField64(field), PoolPair.GetField32(field)); }

		static FString ArchDependentSize(int64_t size32, int64_t size64)
		{
			if (size32 == size64)
				return LexToString(size32);
			return FString::Format(TEXT("UPB_SIZE({0}, {1})"), {size32, size64});
		}

		static FString CEscape(FAnsiStringView src)
		{
			/* clang-format off */
			static unsigned char c_escaped_len[256] = {
				4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 2, 4, 4,  // \t, \n, \r
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1,  // ", '
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // '0'..'9'
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 'A'..'O'
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,  // 'P'..'Z', '\'
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 'a'..'o'
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4,  // 'p'..'z', DEL
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
			};
			/* clang-format on */
			// Calculates the length of the C-style escaped version of 'src'.
			// Assumes that non-printable characters are escaped using octal sequences, and
			// that UTF-8 bytes are not handled specially.
			static auto CEscapedLength = [](FAnsiStringView src) {
				size_t escaped_len = 0;
				for (char c : src)
					escaped_len += c_escaped_len[static_cast<unsigned char>(c)];
				return escaped_len;
			};

			TStringBuilderWithBuffer<ANSICHAR, 256> dest;
			size_t escaped_len = CEscapedLength(src);
			if (escaped_len == src.Len())
			{
				dest.Append(src.GetData(), src.Len());
			}
			else
			{
				size_t cur_dest_len = dest.Len();
				dest.AddUninitialized(escaped_len);
				char* append_ptr = dest.GetData() + cur_dest_len;

				for (char c : src)
				{
					size_t char_len = c_escaped_len[static_cast<unsigned char>(c)];
					if (char_len == 1)
					{
						*append_ptr++ = c;
					}
					else if (char_len == 2)
					{
						switch (c)
						{
							case '\n':
								*append_ptr++ = '\\';
								*append_ptr++ = 'n';
								break;
							case '\r':
								*append_ptr++ = '\\';
								*append_ptr++ = 'r';
								break;
							case '\t':
								*append_ptr++ = '\\';
								*append_ptr++ = 't';
								break;
							case '\"':
								*append_ptr++ = '\\';
								*append_ptr++ = '\"';
								break;
							case '\'':
								*append_ptr++ = '\\';
								*append_ptr++ = '\'';
								break;
							case '\\':
								*append_ptr++ = '\\';
								*append_ptr++ = '\\';
								break;
						}
					}
					else
					{
						*append_ptr++ = '\\';
						*append_ptr++ = '0' + static_cast<unsigned char>(c) / 64;
						*append_ptr++ = '0' + (static_cast<unsigned char>(c) % 64) / 8;
						*append_ptr++ = '0' + static_cast<unsigned char>(c) % 8;
					}
				}
			}
			return ANSI_TO_TCHAR(*dest);
		}

		static FString EscapeTrigraphs(FString to_escape) { return to_escape.Replace(TEXT("?"), TEXT("\\?")); }
		static FString FieldDefault(FFieldDefPtr field)
		{
			switch (field.GetCType())
			{
				case kUpb_CType_Message:
					return "nullptr";
				case kUpb_CType_Bytes:
				case kUpb_CType_String:
				{
					upb_StringView str = field.DefaultValue().str_val;
					return FormatOrdered(TEXT("upb_StringView_FromString(\"{0}\")"), EscapeTrigraphs(CEscape(FAnsiStringView(str.data, str.size))));
				}
				case kUpb_CType_Int32:
					return FormatOrdered(TEXT("(int32_t){0}"), field.DefaultValue().int32_val);
				case kUpb_CType_Int64:
					if (field.DefaultValue().int64_val == INT64_MIN)
					{
						// Special-case to avoid:
						//   integer literal is too large to be represented in a signed integer
						//   type, interpreting as unsigned
						//   [-Werror,-Wimplicitly-unsigned-literal]
						//   int64_t default_val = (int64_t)-9223372036854775808ll;
						//
						// More info here: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52661
						return "INT64_MIN";
					}
					else
					{
						return FormatOrdered(TEXT("(int64_t){0}ll"), field.DefaultValue().int64_val);
					}
				case kUpb_CType_UInt32:
					return FormatOrdered(TEXT("(uint32_t){0}u"), field.DefaultValue().uint32_val);
				case kUpb_CType_UInt64:
					return FormatOrdered(TEXT("(uint64_t){0}ull"), field.DefaultValue().uint64_val);
				case kUpb_CType_Float:
					return FloatToCLiteral(field.DefaultValue().float_val);
				case kUpb_CType_Double:
					return DoubleToCLiteral(field.DefaultValue().double_val);
				case kUpb_CType_Bool:
					return field.DefaultValue().bool_val ? "true" : "false";
				case kUpb_CType_Enum:
					// Use a number instead of a symbolic name so that we don't require
					// this enum's header to be included.
					return LexToString(field.DefaultValue().int32_val);
			}
			checkNoEntry();
			return "XXX";
		}

		static FString CPPType(FFieldDefPtr field) { return CTypeInternal(field, false, false); }
		static FString CPPTypeConst(FFieldDefPtr field) { return CTypeInternal(field, true, false); }
		static FString CType(FFieldDefPtr field) { return CTypeInternal(field, false); }
		static FString CTypeConst(FFieldDefPtr field) { return CTypeInternal(field, true); }
		static FString MapKeyCType(FFieldDefPtr map_field) { return CType(map_field.MessageSubdef().MapKeyDef()); }
		static FString MapValueCType(FFieldDefPtr map_field) { return CType(map_field.MessageSubdef().MapValueDef()); }
		static FString MessageCPPName(FMessageDefPtr descriptor) { return ToCPPIdent(descriptor.FullName()); }
		static FString CTypePPInternal(FFieldDefPtr field, bool is_const)
		{
			FString maybe_const = is_const ? "const " : "";
			switch (field.GetCType())
			{
				case kUpb_CType_Message:
				{
					//return maybe_const + CType(field);
					return MessageCPPName(field.MessageSubdef());
				}
				case kUpb_CType_Bool:
					return "bool";
				case kUpb_CType_Float:
					return "float";
				case kUpb_CType_Int32:
				case kUpb_CType_Enum:
					return "int32_t";
				case kUpb_CType_UInt32:
					return "uint32_t";
				case kUpb_CType_Double:
					return "double";
				case kUpb_CType_Int64:
					return "int64_t";
				case kUpb_CType_UInt64:
					return "uint64_t";
				case kUpb_CType_String:
				case kUpb_CType_Bytes:
					return "UPB_STRINGVIEW";
				default:
					checkNoEntry();
			}
			return "UPB_STRINGVIEW";
		}
		static FString CTypePP(FFieldDefPtr field) { return CTypePPInternal(field, false); }
		static FString CTypePPConst(FFieldDefPtr field) { return CTypePPInternal(field, true); }
		static FString MapKeySize(FFieldDefPtr map_field, FString expr) { return map_field.MessageSubdef().MapKeyDef().GetCType() == kUpb_CType_String ? TEXT("0") : FormatOrdered(TEXT("sizeof({0})"), expr); }
		static FString MapValueSize(FFieldDefPtr map_field, FString expr) { return map_field.MessageSubdef().MapValueDef().GetCType() == kUpb_CType_String ? TEXT("0") : FormatOrdered(TEXT("sizeof({0})"), expr); }

		static FString GetFieldRep(const upb_MiniTableField* field32, const upb_MiniTableField* field64)
		{
			switch (_upb_MiniTableField_GetRep(field32))
			{
				case kUpb_FieldRep_1Byte:
					return "kUpb_FieldRep_1Byte";
					break;
				case kUpb_FieldRep_4Byte:
				{
					if (_upb_MiniTableField_GetRep(field64) == kUpb_FieldRep_4Byte)
					{
						return "kUpb_FieldRep_4Byte";
					}
					else
					{
						assert(_upb_MiniTableField_GetRep(field64) == kUpb_FieldRep_8Byte);
						return "UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte)";
					}
					break;
				}
				case kUpb_FieldRep_StringView:
					return "kUpb_FieldRep_StringView";
					break;
				case kUpb_FieldRep_8Byte:
					return "kUpb_FieldRep_8Byte";
					break;
			}
			checkNoEntry();
			return "XXX";
		}
		static FString GetFieldRep(const FDefPoolPair& pools, FFieldDefPtr field) { return GetFieldRep(pools.GetField32(field), pools.GetField64(field)); }

		// Returns the field mode as a string initializer.
		//
		// We could just emit this as a number (and we may yet go in that direction) but
		// for now emitting symbolic constants gives this better readability and debuggability.
		static FString GetModeInit(const upb_MiniTableField* field32, const upb_MiniTableField* field64)
		{
			FString ret;
			uint8_t mode32 = field32->mode;
			switch (mode32 & kUpb_FieldMode_Mask)
			{
				case kUpb_FieldMode_Map:
					ret = "(int)kUpb_FieldMode_Map";
					break;
				case kUpb_FieldMode_Array:
					ret = "(int)kUpb_FieldMode_Array";
					break;
				case kUpb_FieldMode_Scalar:
					ret = "(int)kUpb_FieldMode_Scalar";
					break;
				default:
					break;
			}

			if (mode32 & kUpb_LabelFlags_IsPacked)
			{
				ret.Append(" | (int)kUpb_LabelFlags_IsPacked");
			}

			if (mode32 & kUpb_LabelFlags_IsExtension)
			{
				ret.Append(" | (int)kUpb_LabelFlags_IsExtension");
			}

			if (mode32 & kUpb_LabelFlags_IsAlternate)
			{
				ret.Append(" | (int)kUpb_LabelFlags_IsAlternate");
			}

			ret.Appendf(TEXT(" | ((int)%s << kUpb_FieldRep_Shift)"), *GetFieldRep(field32, field64));
			return ret;
		}
		static void __FormatOrderedImpl(FStringFormatOrderedArguments& Result, const UPB_STRINGVIEW& Value) { Result.Emplace(Value.ToFString()); }
		template<typename TValue>
		static std::enable_if_t<!std::is_same<UPB_STRINGVIEW, std::decay_t<TValue>>::value> __FormatOrderedImpl(FStringFormatOrderedArguments& Result, TValue&& Value)
		{
			Result.Emplace(Forward<TValue>(Value));
		}

		template<typename TValue, typename... TArguments>
		static void _FormatOrderedImpl(FStringFormatOrderedArguments& Result, TValue&& Value, TArguments&&... Args)
		{
			__FormatOrderedImpl(Result, Forward<TValue>(Value));
			_FormatOrderedImpl(Result, Forward<TArguments>(Args)...);
		}
		static void _FormatOrderedImpl(FStringFormatOrderedArguments& Result) {}

		template<typename... TArguments>
		static FString FormatOrdered(const TCHAR* Fmt, TArguments&&... Args)
		{
			FStringFormatOrderedArguments FormatArguments;
			_FormatOrderedImpl(FormatArguments, Forward<TArguments>(Args)...);
			return FString::Format(Fmt, MoveTemp(FormatArguments));
		}

		template<typename TBuilder, typename... TArguments>
		static void AppendOrdered(TBuilder& Builder, const TCHAR* Fmt, TArguments&&... Args)
		{
			Builder.Append(*FormatOrdered(Fmt, Forward<TArguments>(Args)...));
		}

		static FString ToCPPIdent(FString str, FString delim = ".")
		{
			int32 Idx;
			auto pos = str.FindLastCharByPredicate([&](auto Elm) { return delim.FindChar(Elm, Idx); });
			if (pos == INDEX_NONE)
			{
				return str;
			}
			return str.Mid(pos + delim.Len());
		}

		static FString TypeDeclareImpl(FString fullname, FString Type)
		{
			TArray<FString> Splits;
			fullname.ParseIntoArray(Splits, PB_Delims, 3);
			if (Splits.Num() > 1)
			{
				fullname = FString::Join(Splits, TEXT("::"));
				fullname += "{ ";
				fullname += Type;
				fullname += *Splits.Last();
				fullname += "; }\n";
			}
			return fullname;
		}

		static FString ExtensionIdentBase(FFieldDefPtr ext)
		{
			assert(ext.is_extension());
			if (ext.ExtensionScope())
			{
				return MessageName(ext.ExtensionScope());
			}
			else
			{
				return ToCIdent(ext.FileDef().Package());
			}
		}

		static FString ExtensionLayout(FFieldDefPtr ext) { return ExtensionIdentBase(ext) + "_" + ext.Name() + "_ext"; }
		using NameToFieldDefMap = TMap<FString, FFieldDefPtr>;

		static FString ResolveFieldName(FFieldDefPtr field, const NameToFieldDefMap& field_names)
		{
			FString field_name = field.Name();
			for (auto& prefix : kAccessorPrefixes)
			{
				// If field name starts with a prefix such as clear_ and the proto
				// contains a field name with trailing end, depending on type of field
				// (repeated, map, message) we have a conflict to resolve.
				if (field_name.StartsWith(prefix))
				{
					auto Find = field_names.Find(field_name.Mid(prefix.Len()));
					if (Find)
					{
						const auto candidate = *Find;
						if (candidate.IsRepeated() || candidate.IsMap() || (candidate.GetCType() == kUpb_CType_String && prefix == kClearMethodPrefix) || prefix == kSetMethodPrefix || prefix == kHasMethodPrefix)
						{
							return field_name + TEXT("_");
						}
					}
				}
			}
			return field_name;
		}

		template<typename TBuilder>
		static auto ScopeNamespace(TBuilder& output, FString fullname)
		{
			struct NamespaceGuard
			{
				TBuilder& output;
				FString fullname;
				NamespaceGuard(TBuilder& output, FString InFullname)
					: output(output)
					, fullname(InFullname)
				{
					BeginNamespace(output, fullname);
				}
				~NamespaceGuard() { EndNamespace(output, fullname); }
				static void BeginNamespace(TBuilder& output, FString fullname)
				{
					TArray<FString> Splits;
					fullname.ParseIntoArray(Splits, PB_Delims, 3);
					AppendOrdered(output, TEXT("\n/* {0} */\n"), fullname);
					if (Splits.Num() > 1)
					{
						for (auto Idx = 0; Idx <= Splits.Num() - 2; ++Idx)
						{
							AppendOrdered(output, TEXT("namespace {0} {\n"), *Splits[Idx]);
						}
					}
				}
				static void EndNamespace(TBuilder& output, FString fullname)
				{
					TArray<FString> Splits;
					fullname.ParseIntoArray(Splits, PB_Delims, 3);
					if (Splits.Num() > 1)
					{
						for (auto Idx = Splits.Num() - 2; Idx >= 0; --Idx)
						{
							AppendOrdered(output, TEXT("}  // namespace {0}\n"), *Splits[Idx]);
						}
					}
				}
			};
			return NamespaceGuard(output, fullname);
		}

		static FString CPPTypeFull(FFieldDefPtr field, bool is_const = false)
		{
			if (field.GetCType() == kUpb_CType_Message)
			{
				FString fullname = field.MessageSubdef().FullName();
				TArray<FString> Splits;
				fullname.ParseIntoArray(Splits, PB_Delims, 3);
				if (Splits.Num() > 1)
				{
					fullname = is_const ? "const " : "";
					fullname += FString::Join(Splits, TEXT("::"));
				}
				return fullname;
			}
			return CTypePPInternal(field, is_const);
		}

		template<typename Output>
		static void DumpEnumValues(FEnumDefPtr desc, Output& output)
		{
			TArray<FEnumValDefPtr> values;
			values.Reserve(desc.ValueCount());
			for (int i = 0; i < desc.ValueCount(); i++)
			{
				values.Add(desc.Value(i));
			}
			Algo::Sort(values, [](FEnumValDefPtr a, FEnumValDefPtr b) { return a.Number() < b.Number(); });

			for (size_t i = 0; i < values.Num(); i++)
			{
				auto value = values[i];
				AppendOrdered(output, TEXT("  {0} = {1},\n"), ToCPPIdent(value.FullName()), value.Number());
			}
		}

		template<typename Output>
		void GenerateExtensionInHeader(FFieldDefPtr ext, Output& output, Output& s_output)
		{
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE bool {0}_has_{1}() const { return _upb_Message_HasExtensionField((const upb_Message*)msgval_, &{3}); }
)cc"
							  // clang-format on
							  ),
						  ExtensionIdentBase(ext),
						  ext.Name(),
						  MessageName(ext.ContainingType()),
						  ExtensionLayout(ext));

			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE void {0}_clear_{1}() { _upb_Message_ClearExtensionField((upb_Message*)msgval_, &{3}); }
)cc"
							  // clang-format on
							  ),
						  ExtensionIdentBase(ext),
						  ext.Name(),
						  MessageName(ext.ContainingType()),
						  ExtensionLayout(ext));

			if (ext.IsRepeated())
			{
				// TODO: We need generated accessors for repeated extensions.
			}
			else
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE {0} {1}_{2}() const {
		const upb_MiniTableExtension* ext = &{4};
		UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
		UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == {5});
		{0} default_val = {6};
		{0} ret = {6};
		_upb_Message_GetExtensionField((const upb_Message*)msgval_, ext, &default_val, &ret);
		return ret;
	}
)cc"
								  // clang-format on
								  ),
							  CTypeConst(ext),
							  ExtensionIdentBase(ext),
							  ext.Name(),
							  MessageName(ext.ContainingType()),
							  ExtensionLayout(ext),
							  GetFieldRep(PoolPair, ext),
							  FieldDefault(ext));
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE void {1}_set_{2}({0} val, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		const upb_MiniTableExtension* ext = &{4};
		UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
		UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == {5});
		bool ok = _upb_Message_SetExtensionField((upb_Message*)msgval_, ext, &val, arena);
		UPB_ASSERT(ok);
	}
)cc"
								  // clang-format on
								  ),
							  CTypeConst(ext),
							  ExtensionIdentBase(ext),
							  ext.Name(),
							  MessageName(ext.ContainingType()),
							  ExtensionLayout(ext),
							  GetFieldRep(PoolPair, ext));
			}
		}

		static FString MessageMiniTableRef(FMessageDefPtr descriptor) { return TEXT("&") + MessageInitName(descriptor); }

		template<typename Output>
		static void GenerateMessageFunctionsInHeader(FMessageDefPtr message, Output& output, Output& s_output)
		{
			// TODO: The generated code here does not check the return values
			// from upb_Encode(). How can we even fix this without breaking other things?
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	public:
		static {0}* _Make(upb_Arena* arena DEFAULT_ARENA_PARAMETER) { return ({0}*)_upb_Message_New({1}, arena); }
		static {2} Make(upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
			UPB_VALID_ARENA(arena);
			return _Make(arena);
		}
		static {0}* parse_ex(const void* buf, size_t size, const upb_ExtensionRegistry* extreg, int options, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
			UPB_VALID_ARENA(arena);
			{0}* ret = _Make(arena);
			if (!ret) return nullptr;
			if (upb_Decode((const char*)buf, size, ret, {1}, extreg, options, arena) != kUpb_DecodeStatus_Ok) {
				return nullptr;
			}	
			return ret;
		}
		static {0}* parse(const void* buf, size_t size, upb_Arena* arena DEFAULT_ARENA_PARAMETER) { return parse_ex(buf, size, nullptr, 0, arena); }
		UPB_INLINE char* serialize_ex(int options, size_t* size, upb_Arena* arena DEFAULT_ARENA_PARAMETER) const {
			char* ptr;
			UPB_VALID_ARENA(arena);
			(void)upb_Encode((const upb_Message*)msgval_, {1}, options, arena, &ptr, size);
			return ptr;
		}
		UPB_INLINE char* serialize(size_t* size, upb_Arena* arena DEFAULT_ARENA_PARAMETER) const { return serialize_ex(0, size, arena); }

		static {0}* parse(UPB_STRINGVIEW data, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
			upb_StringView view = data;
			return parse_ex(view.data, view.size, nullptr, 0, arena);
		}
		UPB_INLINE UPB_STRINGVIEW serialize(upb_Arena* arena DEFAULT_ARENA_PARAMETER) const {
			size_t size = 0;
			char* ptr = serialize(&size, arena);
			return upb_StringView_FromDataAndSize(ptr, size);
		}
)cc"
							  // clang-format on
							  ),
						  ToCIdent(message.FullName()),
						  MessageMiniTableRef(message),
						  ToCPPIdent(message.FullName()));
		}

		template<typename Output>
		void GenerateOneofInHeader(FOneofDefPtr oneof, FString msg_name, Output& output, Output& s_output)
		{
			FString fullname = ToCIdent(oneof.FullName());
			AppendOrdered(output, TEXT("enum oneofcases {\n"));
			for (int j = 0; j < oneof.FieldCount(); j++)
			{
				FFieldDefPtr field = oneof.Field(j);
				AppendOrdered(output, TEXT("  {0} = {1},\n"), field.Name(), field.Number());
			}
			AppendOrdered(output, TEXT("  NOT_SET = 0\n} ;\n"));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	public:
		UPB_INLINE oneofcases {1}_case() const {
			const upb_MiniTableField field = {2};
			return (oneofcases)upb_Message_WhichOneofFieldNumber((const upb_Message*)msgval_, &field);
		}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  oneof.Name(),
						  FieldInitializer(oneof.Field(0)));
		}

		template<typename Output>
		void GenerateHazzer(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			FString resolved_name = ResolveFieldName(field, field_names);
			if (field.HasPresence())
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE bool has_{1}() const {
		const upb_MiniTableField field = {2};
		return _upb_Message_HasNonExtensionField((const upb_Message*)msgval_, &field);
	}
)cc"
								  // clang-format on
								  ),
							  msg_name,
							  resolved_name,
							  FieldInitializer(field));
			}
			else if (field.IsMap())
			{
				// Do nothing.
			}
			else if (field.IsRepeated())
			{
				// TODO: remove.
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE bool has_{1}() const {
		size_t size;
		{1}(&size);
		return size != 0;
	}
)cc"
								  // clang-format on
								  ),
							  msg_name,
							  resolved_name);
			}
		}

		template<typename Output>
		void GenerateClear(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			if (field == field.ContainingType().MapKeyDef() || field == field.ContainingType().MapValueDef())
			{
				// Cannot be cleared.
				return;
			}
			FString resolved_name = ResolveFieldName(field, field_names);
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE void clear_{1}() {
		const upb_MiniTableField field = {2};
		_upb_Message_ClearNonExtensionField((upb_Message*)msgval_, &field);
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
		}

		template<typename Output>
		void GenerateMapGetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output)
		{
			FString resolved_name = ResolveFieldName(field, field_names);
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE size_t {1}_size() const {
		const upb_MiniTableField field = {2};
		const upb_Map* map = upb_Message_GetMap((const upb_Message*)msgval_, &field);
		return map ? _upb_Map_Size(map) : 0;
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE bool {1}_get({2} key, {3}* val) const {
		const upb_MiniTableField field = {4};
		const upb_Map* map = upb_Message_GetMap((const upb_Message*)msgval_, &field);
		if (!map) return false;
		return _upb_Map_Get(map, &key, {5}, val, {6});
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  MapKeyCType(field),
						  MapValueCType(field),
						  FieldInitializer(field),
						  MapKeySize(field, "key"),
						  MapValueSize(field, "*val"));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE {0} {2}_next(size_t* iter) const {
		const upb_MiniTableField field = {3};
		const upb_Map* map = upb_Message_GetMap((const upb_Message*)msgval_, &field);
		if (!map) return nullptr;
		return ({0})_upb_map_next(map, iter);
	}
)cc"
							  // clang-format on
							  ),
						  CTypeConst(field),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
		}

		template<typename Output>
		static void GenerateMapEntryGetters(FFieldDefPtr field, FString msg_name, Output& output, Output& s_output)
		{
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	{4} get_{2}();
	{4} get_{2}() const;
	{4} {2}() const;
)cc"
							  // clang-format on
							  ),
						  CType(field),                                                             // {0}
						  msg_name,                                                                 // {1}
						  field.Name(),                                                             // {2}
						  field.GetCType() == kUpb_CType_String ? TEXT("0") : TEXT("sizeof(ret)"),  // {3}
						  CPPTypeFull(field, field.GetCType() == kUpb_CType_Message)                // {4}
			);
			AppendOrdered(s_output,
						  TEXT(
							  // clang-format off
R"cc(
{4} {5}::get_{2}() {
	{3} ret;
	_upb_msg_map_{2}((upb_Message*)msgval_, &ret, {3});
	return ret;
}
{4} {5}::get_{2}() const {
	{3} ret;
	_upb_msg_map_{2}((const upb_Message*)msgval_, &ret, {3});
	return ret;
}
{4} {5}::{2}() const { return get_{2}(); }
)cc"
							  // clang-format on
							  ),
						  CType(field),                                                             // {0}
						  msg_name,                                                                 // {1}
						  field.Name(),                                                             // {2}
						  field.GetCType() == kUpb_CType_String ? TEXT("0") : TEXT("sizeof(ret)"),  // {3}
						  CPPTypeFull(field, field.GetCType() == kUpb_CType_Message),               // {4}
						  ToCPPIdent(field.ContainingType().FullName())                             // {5}
			);
		}

		template<typename Output>
		void GenerateRepeatedGetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			// Generate getter returning first item and size.
			//
			// Example:
			//   UPB_INLINE const struct Bar* const* name(const Foo* msg, size_t* size)
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	{0} const* {2}(size_t* size) const;
	{0} const* {2}_ptr(size_t* size = nullptr) const { return {2}(size); }
	size_t {2}_size() const;
	{4} {2}(size_t index) const;
)cc"
							  // clang-format on
							  ),
						  CTypeConst(field),                     // {0}
						  msg_name,                              // {1}
						  ResolveFieldName(field, field_names),  // {2}
						  FieldInitializer(field),               // #3
						  CPPTypeFull(field, true)               // {4}
			);
			AppendOrdered(s_output,
						  TEXT(
							  // clang-format off
R"cc(
{0} const* {5}::{2}(size_t* size) const {
	const upb_MiniTableField field = {3};
	const upb_Array* arr = upb_Message_GetArray((const upb_Message*)msgval_, &field);
	if (arr) {
		if (size) *size = arr->size;
		return ({ 0 } const*)_upb_array_constptr(arr);
	} else {
		if (size) *size = 0;
		return nullptr;
	}
}
size_t {5}::{2}_size() const {
	const upb_MiniTableField field = {3};
	const upb_Array* arr = upb_Message_GetArray((const upb_Message*)msgval_, &field);
	return arr ? arr->size : 0;
}
{4} {5}::{2}(size_t index) const {
	const upb_MiniTableField field = {3};
	const upb_Array* arr = upb_Message_GetArray((const upb_Message*)msgval_, &field);
	if (arr && index < arr->size) {
		return (({0} const*)_upb_array_constptr(arr))[index];
	} else {
		return {6};
	}
}
)cc"
							  // clang-format on
							  ),
						  CTypeConst(field),                                         // {0}
						  msg_name,                                                  // {1}
						  ResolveFieldName(field, field_names),                      // {2}
						  FieldInitializer(field),                                   // {3}
						  CPPTypeFull(field, true),                                  // {4}
						  ToCPPIdent(field.ContainingType().FullName()),             // {5}
						  field.GetCType() == kUpb_CType_Message ? "nullptr" : "{}"  // {6}
			);
			// Generate private getter returning array or nullptr for immutable and upb_Array
			// for mutable.
			//
			// Example:
			//   UPB_INLINE const upb_Array* _name_upbarray(size_t* size)
			//   UPB_INLINE upb_Array* _name_mutable_upbarray(size_t* size)
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE const upb_Array* _{2}_{4}(size_t* size) const {
		const upb_MiniTableField field = {3};
		const upb_Array* arr = upb_Message_GetArray((const upb_Message*)msgval_, &field);
		if (size) {
			*size = arr ? arr->size : 0;
		}
		return arr;
	}
	UPB_ITERATOR_SUPPORT({2}, {6})
	UPB_INLINE upb_Array* _{2}_{5}(size_t* size, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		const upb_MiniTableField field = {3};
		upb_Array* arr = upb_Message_GetOrCreateMutableArray((upb_Message*)msgval_, &field, arena);
		if (size) {
			*size = arr ? arr->size : 0;
		}
		return arr;
	}
)cc"
							  // clang-format on
							  ),
						  CTypeConst(field),                        // {0}
						  msg_name,                                 // {1}
						  ResolveFieldName(field, field_names),     // {2}
						  FieldInitializer(field),                  // {3}
						  kRepeatedFieldArrayGetterPostfix,         // {4}
						  kRepeatedFieldMutableArrayGetterPostfix,  // {5}
						  CPPTypeFull(field, true)                  // {6}
			);
		}

		template<typename Output>
		void GenerateScalarGetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			FString field_name = ResolveFieldName(field, field_names);
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	{5} get_{2}();
	{5} get_{2}() const;
	{5} {2}() const;
)cc"
							  // clang-format on
							  ),
						  CType(field),                                               // {0}
						  msg_name,                                                   // {1}
						  field_name,                                                 // {2}
						  FieldDefault(field),                                        // {3}
						  FieldInitializer(field),                                    // {4}
						  CPPTypeFull(field, field.GetCType() == kUpb_CType_Message), // {5}
						  ToCPPIdent(field.ContainingType().FullName())               // {6}
			);
			AppendOrdered(s_output,
						  TEXT(
							  // clang-format off
R"cc(
{5} {6}::get_{2}() {
	{0} default_val = {3};
	{0} ret = {3};
	const upb_MiniTableField field = {4};
	_upb_Message_GetNonExtensionField((upb_Message*)msgval_, &field, &default_val, &ret);
	return ret;
}
{5} {6}::get_{2}() const {
	{0} default_val = {3};
	{0} ret = {3};
	const upb_MiniTableField field = {4};
	_upb_Message_GetNonExtensionField((const upb_Message*)msgval_, &field, &default_val, &ret);
	return ret;
}
{5} {6}::{2}() const { return get_{2}(); }
)cc"
							  // clang-format on
							  ),
						  CType(field),                                               // {0}
						  msg_name,                                                   // {1}
						  field_name,                                                 // {2}
						  FieldDefault(field),                                        // {3}
						  FieldInitializer(field),                                    // {4}
						  CPPTypeFull(field, field.GetCType() == kUpb_CType_Message), // {5}
						  ToCPPIdent(field.ContainingType().FullName())               // {6}
			);
		}

		template<typename Output>
		void GenerateGetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			if (field.IsMap())
			{
				GenerateMapGetters(field, msg_name, field_names, output);
			}
#ifdef UPB_DESC
			else if (UPB_DESC(MessageOptions_map_entry)(field.ContainingType().Options()))
#else
			else if (google_protobuf_MessageOptions_map_entry(field.ContainingType().Options()))
#endif
			{
				GenerateMapEntryGetters(field, msg_name, output, s_output);
			}
			else if (field.IsRepeated())
			{
				GenerateRepeatedGetters(field, msg_name, field_names, output, s_output);
			}
			else
			{
				GenerateScalarGetters(field, msg_name, field_names, output, s_output);
			}
		}

		template<typename Output>
		void GenerateMapSetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			FString resolved_name = ResolveFieldName(field, field_names);
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE void {1}_clear() {
		const upb_MiniTableField field = {2};
		upb_Map* map = (upb_Map*)upb_Message_GetMap((upb_Message*)msgval_, &field);
		if (!map) return;
		_upb_Map_Clear(map);
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE bool {1}_set({2} key, {3} val, upb_Arena* a) {
		const upb_MiniTableField field = {4};
		upb_Map* map = _upb_Message_GetOrCreateMutableMap((upb_Message*)msgval_, &field, {5}, {6}, a);
		return _upb_Map_Insert(map, &key, {5}, &val, {6}, a) != kUpb_MapInsertStatus_OutOfMemory;
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  MapKeyCType(field),
						  MapValueCType(field),
						  FieldInitializer(field),
						  MapKeySize(field, "key"),
						  MapValueSize(field, "val"));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE bool {1}_delete({2} key) {
		const upb_MiniTableField field = {3};
		upb_Map* map = (upb_Map*)upb_Message_GetMap((upb_Message*)msgval_, &field);
		if (!map) return false;
		return _upb_Map_Delete(map, &key, {4}, nullptr);
	}
)cc"
							  // clang-format on
							  ),
						  msg_name,
						  resolved_name,
						  MapKeyCType(field),
						  FieldInitializer(field),
						  MapKeySize(field, "key"));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE {0} {2}_nextmutable(size_t* iter) {
		const upb_MiniTableField field = {3};
		upb_Map* map = (upb_Map*)upb_Message_GetMap((upb_Message*)msgval_, &field);
		if (!map) return nullptr;
		return ({0})_upb_map_next(map, iter);
	}
)cc"
							  // clang-format on
							  ),
						  CType(field),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE upb_Map* {2}_clone_from(const upb_Map* map, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		const upb_MiniTableField field = {3};
		upb_Map* cloned_map = upb_Map_DeepClone(arr, upb_MiniTableField_CType(&field), {4}, {5}, &{6}, arena);
		_upb_Message_SetField((upb_Message*)msgval_, &field, &cloned_map, arena);
		return cloned_map;
	}
)cc"
							  // clang-format on
							  ),
						  CType(field),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field),
						  MapKeyCType(field),
						  MapValueCType(field),
						  MessageInitName(field.ContainingType()));
		}

		template<typename Output>
		void GenerateRepeatedSetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			FString resolved_name = ResolveFieldName(field, field_names);
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE {0}* mutable_{2}(size_t* size = nullptr) {
		upb_MiniTableField field = {3};
		upb_Array* arr = upb_Message_GetMutableArray((upb_Message*)msgval_, &field);
		if (arr) {
			if (size) *size = arr->size;
			return ({0}*)_upb_array_ptr(arr);
		} else {
			if (size) *size = 0;
			return nullptr;
		}
	}
	{0}* resize_{2}(size_t size, upb_Arena* arena DEFAULT_ARENA_PARAMETER);
)cc"
							  // clang-format on
							  ),
						  CType(field),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field));
			AppendOrdered(s_output,
						  TEXT(
							  // clang-format off
R"cc(
{0}* {4}::resize_{2}(size_t size, upb_Arena* arena) {
	UPB_VALID_ARENA(arena);
	upb_MiniTableField field = {3};
	return ({0}*)upb_Message_ResizeArrayUninitialized((upb_Message*)msgval_, &field, size, arena);
}
)cc"
							  // clang-format on
							  ),
						  CType(field),
						  msg_name,
						  resolved_name,
						  FieldInitializer(field),
						  ToCPPIdent(field.ContainingType().FullName()));
			if (field.GetCType() == kUpb_CType_Message)
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	{5} add_{2}(upb_Arena* arena DEFAULT_ARENA_PARAMETER);)cc"
								  // clang-format on
								  ),
							  MessageName(field.MessageSubdef()),
							  ToCPPIdent(field.MessageSubdef().FullName()),
							  resolved_name,
							  MessageMiniTableRef(field.MessageSubdef()),
							  FieldInitializer(field),
							  CPPTypeFull(field));
				AppendOrdered(s_output,
							  TEXT(
								  // clang-format off
R"cc(
{5} {6}::add_{2}(upb_Arena* arena) {
	UPB_VALID_ARENA(arena);
	upb_MiniTableField field = {4};
	upb_Array* arr = upb_Message_GetOrCreateMutableArray((upb_Message*)msgval_, &field, arena);
	if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
		return nullptr;
	}
	{0}* sub = ({0}*)_upb_Message_New({3}, arena);
	if (!arr || !sub) return nullptr;
	_upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
	return sub;
}
)cc"
								  // clang-format on
								  ),
							  MessageName(field.MessageSubdef()),
							  ToCPPIdent(field.MessageSubdef().FullName()),
							  resolved_name,
							  MessageMiniTableRef(field.MessageSubdef()),
							  FieldInitializer(field),
							  CPPTypeFull(field),
							  ToCPPIdent(field.ContainingType().FullName()));
			}
			else
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE bool add_{2}({0} val, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		upb_MiniTableField field = {3};
		upb_Array* arr = upb_Message_GetOrCreateMutableArray((upb_Message*)msgval_, &field, arena);
		if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
			return false;
		}
		_upb_Array_Set(arr, arr->size - 1, &val, sizeof(val));
		return true;
	}
)cc"
								  // clang-format on
								  ),
							  CType(field),
							  msg_name,
							  resolved_name,
							  FieldInitializer(field));
				if (field.GetCType() == kUpb_CType_String || field.GetCType() == kUpb_CType_Bytes)
				{
					AppendOrdered(output,
								  TEXT(
									  // clang-format off
R"cc(
	UPB_INLINE bool add_{2}(const char* val, size_t size{4}, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		size = size != -1 ? size : strlen(val);
		char* buf = (char*)upb_Arena_Malloc(arena, size + 1);
		buf[size] = 0;
		memcpy(buf, val, size);
		upb_StringView strval = upb_StringView_FromDataAndSize(buf, size);
		return add_{2}(strval, arena);
	}
)cc"
									  // clang-format on
									  ),
								  CType(field),
								  msg_name,
								  resolved_name,
								  FieldInitializer(field),
								  field.GetCType() == kUpb_CType_Bytes ? "" : " = -1");
				}
			}
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	UPB_INLINE upb_Array* {2}_clone_from(const upb_Array* arr, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		const upb_MiniTableField field = {3};
		upb_Array* cloned_arr = upb_Array_DeepClone(arr, upb_MiniTableField_CType(&field), {6}, arena);
		_upb_Message_SetField((upb_Message*)msgval_, &field, &cloned_arr, arena);
		return cloned_arr;
	}
)cc"
							  // clang-format on
							  ),
						  CTypeConst(field),                        // {0}
						  msg_name,                                 // {1}
						  ResolveFieldName(field, field_names),     // {2}
						  FieldInitializer(field),                  // {3}
						  kRepeatedFieldArrayGetterPostfix,         // {4}
						  kRepeatedFieldMutableArrayGetterPostfix,  // {5}
#ifdef UPB_PRIVATE
						  (upb_MiniTableField_CType(field.MiniTable()) == kUpb_CType_Message && field.MiniTable()->UPB_PRIVATE(submsg_index) != kUpb_NoSub)
#else
						  (upb_MiniTableField_CType(field.MiniTable()) == kUpb_CType_Message && field.MiniTable()->submsg_index_dont_copy_me__upb_internal_use_only != kUpb_NoSub)
#endif
							  ? FormatOrdered(TEXT("upb_MiniTable_GetSubMessageTable(&{0}, &field)"), MessageInitName(field.ContainingType()))
							  : "nullptr"  // {6}
			);
		}

		template<typename Output>
		void GenerateNonRepeatedSetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			if (field == field.ContainingType().MapKeyDef())
			{
				// Key cannot be mutated.
				return;
			}

			FString field_name = ResolveFieldName(field, field_names);

			if (field == field.ContainingType().MapValueDef())
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE void set_{1}({2} value) { _upb_msg_map_set_value((upb_Message*)msgval_, &value, {3}); })cc"
								  // clang-format on
								  ),
							  msg_name,
							  field_name,
							  CType(field),
							  field.GetCType() == kUpb_CType_String ? "0" : "sizeof(" + CType(field) + ")");
				if (field.GetCType() == kUpb_CType_String || field.GetCType() == kUpb_CType_Bytes)
				{
					AppendOrdered(output,
								  TEXT(
									  // clang-format off
R"cc(
	UPB_INLINE void set_{1}(const char* val, size_t size{4}, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		size = size != -1 ? size : strlen(val);
		char* buf = (char*)upb_Arena_Malloc(arena, size + 1);
		buf[size] = 0;
		memcpy(buf, val, size);
		upb_StringView strval = upb_StringView_FromDataAndSize(buf, size);
		set_{1}(strval);
	}
)cc"
									  // clang-format on
									  ),
								  msg_name,
								  field_name,
								  CType(field),
								  field.GetCType() == kUpb_CType_String ? "0" : "sizeof(" + CType(field) + ")",
								  field.GetCType() == kUpb_CType_Bytes ? "" : " = -1");
				}
			}
			else
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	UPB_INLINE void set_{1}({2} value) {
		const upb_MiniTableField field = {3};
		_upb_Message_SetNonExtensionField((upb_Message*)msgval_, &field, &value);
	}
)cc"
								  // clang-format on
								  ),
							  msg_name,
							  field_name,
							  CType(field),
							  FieldInitializer(field));
				if (field.GetCType() == kUpb_CType_String || field.GetCType() == kUpb_CType_Bytes)
				{
					AppendOrdered(output,
								  TEXT(
									  // clang-format off
R"cc(
	UPB_INLINE void set_{1}(const char* val, size_t size{4}, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		size = size != -1 ? size : strlen(val);
		char* buf = (char*)upb_Arena_Malloc(arena, size + 1);
		buf[size] = 0;
		memcpy(buf, val, size);
		upb_StringView strval = upb_StringView_FromDataAndSize(buf, size);
		set_{1}(strval);
	}
)cc"
									  // clang-format on
									  ),
								  msg_name,
								  field_name,
								  CType(field),
								  FieldInitializer(field),
								  field.GetCType() == kUpb_CType_Bytes ? "" : " = -1");
				}
			}

			// Message fields also have a Msg_mutable_foo() accessor that will create
			// the sub-message if it doesn't already exist.
#ifdef UPB_DESC
			if (field.GetCType() == kUpb_CType_Message && !UPB_DESC(MessageOptions_map_entry)(field.ContainingType().Options()))
#else
			if (field.GetCType() == kUpb_CType_Message && !google_protobuf_MessageOptions_map_entry(field.ContainingType().Options()))
#endif
			{
				AppendOrdered(output,
							  TEXT(
								  // clang-format off
R"cc(
	{1} mutable_{2}(upb_Arena* arena DEFAULT_ARENA_PARAMETER);)cc"
								  // clang-format on
								  ),
							  MessageName(field.MessageSubdef()),            // {0}
							  CPPTypeFull(field),                            // {1}
							  field_name,                                    // {2}
							  MessageMiniTableRef(field.MessageSubdef()),    // {3}
							  ToCPPIdent(field.ContainingType().FullName())  // {4}
				);
				AppendOrdered(s_output,
							  TEXT(
								  // clang-format off
R"cc(
{1} {4}::mutable_{2}(upb_Arena* arena) {
	UPB_VALID_ARENA(arena);
	{0}* sub = ({0}*)get_{2}();
	if (sub == nullptr) {
		sub = ({0}*)_upb_Message_New({3}, arena);
		if (sub) set_{2}(sub);
	}
	return sub;
}
)cc"
								  // clang-format on
								  ),
							  MessageName(field.MessageSubdef()),            // {0}
							  CPPTypeFull(field),                            // {1}
							  field_name,                                    // {2}
							  MessageMiniTableRef(field.MessageSubdef()),    // {3}
							  ToCPPIdent(field.ContainingType().FullName())  // {4}
				);
			}
		}

		template<typename Output>
		void GenerateSetters(FFieldDefPtr field, FString msg_name, const NameToFieldDefMap& field_names, Output& output, Output& s_output)
		{
			if (field.IsMap())
			{
				GenerateMapSetters(field, msg_name, field_names, output, s_output);
			}
			else if (field.IsRepeated())
			{
				GenerateRepeatedSetters(field, msg_name, field_names, output, s_output);
			}
			else
			{
				GenerateNonRepeatedSetters(field, msg_name, field_names, output, s_output);
			}
		}

		static NameToFieldDefMap CreateFieldNameMap(FMessageDefPtr message)
		{
			NameToFieldDefMap field_names;
			field_names.Reserve(message.FieldCount());
			for (const auto& field : message.Fields())
			{
				field_names.Add(field.Name(), field);
			}
			return field_names;
		}

		template<typename Output>
		void GenerateMessageInHeader(FMessageDefPtr message, Output& output, Output& s_output)
		{
			FString msg_name = ToCIdent(message.FullName());
			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
struct {0} {
private:
	{1}* msgval_;
public:
	{0}(const {1}* msg) : msgval_(({1}*)msg){}
	{0}(std::nullptr_t) : msgval_(({1}*)nullptr){}
	{0}(const {0} & other) : {0}(other.msgval_){}
	{0}(upb_Arena* arena DEFAULT_ARENA_PARAMETER) : {0}(Make(arena)) {}

	operator bool() const { return !!msgval_; }
	operator {1}*() { return msgval_; }
	operator const {1}*() const { return msgval_; }

	bool clone_form(const {0} & other, upb_Arena* arena DEFAULT_ARENA_PARAMETER) {
		UPB_VALID_ARENA(arena);
		return msgval_ && other.msgval_ 
			&& upb_Message_DeepCopy((upb_Message*)msgval_, (upb_Message*)other.msgval_, &{2}, arena);
	}
	{0}* operator->() { return this; }
	const {0}* operator->() const { return this; }
)cc"
							  // clang-format on
							  ),
						  ToCPPIdent(message.FullName()),
						  msg_name,
						  MessageInitName(message));
#ifdef UPB_DESC
			if (!UPB_DESC(MessageOptions_map_entry)(message.Options()))
#else
			if (!google_protobuf_MessageOptions_map_entry(message.Options()))
#endif
			{
				GenerateMessageFunctionsInHeader(message, output, s_output);
			}

			for (int i = 0; i < message.RealOneofCount(); i++)
			{
				GenerateOneofInHeader(message.Oneof(i), msg_name, output, s_output);
			}

			auto field_names = CreateFieldNameMap(message);
			for (auto field : FieldNumberOrder(message))
			{
				GenerateClear(field, msg_name, field_names, output, s_output);
				GenerateGetters(field, msg_name, field_names, output, s_output);
				GenerateHazzer(field, msg_name, field_names, output, s_output);
			}

			AppendOrdered(output, TEXT("\n"));

			for (auto field : FieldNumberOrder(message))
			{
				GenerateSetters(field, msg_name, field_names, output, s_output);
			}

			AppendOrdered(output, TEXT("\n};// struct {0}\n"), ToCPPIdent(message.FullName()));
		}

		template<typename Output>
		void GenerateFileRaw(FFileDefPtr file, Output& output, Output& s_output)
		{
			const TArray<FMessageDefPtr> this_file_messages = SortedMessages(file);
			const TArray<FFieldDefPtr> this_file_exts = SortedExtensions(file);
			TArray<FEnumDefPtr> this_file_enums = SortedEnums(file);
			TArray<FMessageDefPtr> forward_messages = SortedForwardMessages(this_file_messages, this_file_exts);

			AppendOrdered(output, TEXT("#ifndef {0}_UPB_PP_H_\n") TEXT("#define {0}_UPB_PP_H_\n\n") TEXT("#include \"upb/generated_code_support.h\"\n\n"), ToPreproc(file.Name()));
			AppendOrdered(output,
						  TEXT("#ifndef UPB_ITERATOR_SUPPORT\n") TEXT("#define UPB_ITERATOR_SUPPORT(...) \n") TEXT("#endif\n") TEXT("#ifndef UPB_STRINGVIEW\n") TEXT("#define UPB_STRINGVIEW upb_StringView\n") TEXT("#endif\n")
							  TEXT("#ifndef DEFAULT_ARENA_PARAMETER\n") TEXT("#define DEFAULT_ARENA_PARAMETER\n") TEXT("#endif\n") TEXT("#ifndef UPB_VALID_ARENA\n") TEXT("#define UPB_VALID_ARENA(x) UPB_ASSERT(x)\n") TEXT("#endif\n"));

			for (int i = 0; i < file.PublicDependencyCount(); i++)
			{
				if (i == 0)
				{
					AppendOrdered(output, TEXT("/* Public Imports. */\n"));
				}
				AppendOrdered(output, TEXT("#include \"{0}\"\n"), *CApiHeaderFilename(file.PublicDependency(i)));
			}
			if (file.PublicDependencyCount() > 0)
			{
				AppendOrdered(output, TEXT("\n"));
			}
			if (bBootstrap)
			{
				AppendOrdered(output, TEXT("#include \"{0}\"\n\n"), *MiniTableHeaderFilename(file));
				for (int i = 0; i < file.DependencyCount(); i++)
				{
					AppendOrdered(output, TEXT("#include \"{0}\"\n"), *MiniTableHeaderFilename(file.Dependency(i)));
				}
			}
			if (file.DependencyCount() > 0)
			{
				AppendOrdered(output, TEXT("\n"));
			}
			TSet<FString> forwardfies;
			for (auto message : forward_messages)
			{
				if (forwardfies.Contains(DefHeaderFilename(message.FileDef())))
				{
					continue;
				}
				forwardfies.Add(DefHeaderFilename(message.FileDef()));
				AppendOrdered(s_output, TEXT("#include \"{0}\"\n"), DefHeaderFilename(message.FileDef()));
			}

			AppendOrdered(s_output, TEXT("// Must be last.\n") TEXT("#include \"upb/port/def.inc\"\n\n") TEXT("namespace upb {\n"));
			AppendOrdered(output, TEXT("// Must be last.\n") TEXT("#include \"upb/port/def.inc\"\n") TEXT("\n"));

			// Forward-declare c types defined in this file.
			if (this_file_messages.Num())
			{
				AppendOrdered(output, TEXT("// Forward-declare c types defined in this file.\n"));
				// Order by full name for consistent ordering.
				for (auto message : this_file_messages)
				{
					AppendOrdered(output, TEXT("typedef struct {0} {0};\n"), ToCIdent(message.FullName()));
				}
			}

			// Forward-declare c types not in this file, but used as submessages.
			if (forward_messages.Num())
			{
				AppendOrdered(output, TEXT("// Forward-declare c types not in this file.\n"));
				// Order by full name for consistent ordering.
				for (auto message : forward_messages)
				{
					AppendOrdered(output, TEXT("struct {0};\n"), MessageName(message));
				}

				AppendOrdered(output, TEXT("\n// Forward-declare cpp types not in this file."));
				AppendOrdered(output, TEXT("\nnamespace upb {\n"));
				for (auto message : forward_messages)
				{
					auto Guard = ScopeNamespace(output, message.FullName());
					AppendOrdered(output, TEXT("struct {0};\n"), ToCPPIdent(message.FullName()));
				}
				AppendOrdered(output, TEXT("} // namespace upb\n\n"));
			}

			FString package = file.Package();
			AppendOrdered(output, TEXT("\n// package : {0}") TEXT("\nnamespace upb {") TEXT("\n"), package);

			// Forward-declare types defined in this file.
			if (this_file_messages.Num())
			{
				AppendOrdered(output, TEXT("// Forward-declare cpp types defined in this file.\n"));
				for (auto message : this_file_messages)
				{
					auto Guard = ScopeNamespace(output, message.FullName());
					AppendOrdered(output, TEXT("struct {0};\n"), ToCPPIdent(message.FullName()));
				}
				AppendOrdered(output, TEXT("\n"));
			}

			for (auto& enumdesc : this_file_enums)
			{
				auto Guard = ScopeNamespace(output, enumdesc.FullName());
				AppendOrdered(output, TEXT("enum {0} {\n"), ToCPPIdent(enumdesc.FullName()));
				DumpEnumValues(enumdesc, output);
				AppendOrdered(output, TEXT("};\n"));
			}

			AppendOrdered(output, TEXT("\n"));
			for (auto message : this_file_messages)
			{
				auto Guard = ScopeNamespace(output, message.FullName());
				auto Guard1 = ScopeNamespace(s_output, message.FullName());
				GenerateMessageInHeader(message, output, s_output);
			}

			for (auto ext : this_file_exts)
			{
				GenerateExtensionInHeader(ext, output, s_output);
			}

			for (auto message : this_file_messages)
			{
				//AppendOrdered(output, TEXT("UPB_INLINE {0}::{1} ToCPPType(const {2}* val) { return ({2}*)val; }\n"), ToCPPNamespace(message.FullName()), ToCPPIdent(message.FullName()), MessageName(message));
			}

			if (file.Name() == "google/protobuf/descriptor.proto" || file.Name() == "net/proto2/proto/descriptor.proto")
			{
				// This is gratuitously inefficient with how many times it rebuilds
				// MessageLayout objects for the same message. But we only do this for one
				// proto (descriptor.proto) so we don't worry about it.
				FMessageDefPtr max32_message;
				FMessageDefPtr max64_message;
				size_t max32 = 0;
				size_t max64 = 0;

				for (const auto message : this_file_messages)
				{
					if (message.Name().EndsWith("Options"))
					{
#if 0
						size_t size32 = PoolPair.GetMiniTable32(message)->size;
						size_t size64 = PoolPair.GetMiniTable64(message)->size;
#else
#if PLATFORM_64BITS
						size_t size64 = message.MiniTable()->size;
						size_t size32 = size64;
#else
						size_t size32 = message.MiniTable()->size;
						size_t size64 = size32;
#endif
#endif
						if (size32 > max32)
						{
							max32 = size32;
							max32_message = message;
						}
						if (size64 > max64)
						{
							max64 = size64;
							max64_message = message;
						}
					}
				}

				AppendOrdered(output, TEXT("/* Max size 32 is {0} */\n"), max32_message.FullName());
				AppendOrdered(output, TEXT("/* Max size 64 is {0} */\n"), max64_message.FullName());
				AppendOrdered(output, TEXT("#define _UPB_MAXOPT_SIZE UPB_SIZE({0}, {1})\n\n"), (uint64)max32, (uint64)max64);
			}

			AppendOrdered(s_output, TEXT("}  // namespace upb\n#include \"upb/port/undef.inc\"\n"));
			AppendOrdered(output, TEXT("}  // namespace upb\n") TEXT("\n") TEXT("#include \"upb/port/undef.inc\"\n") TEXT("\n") TEXT("#endif  /* {0}_UPB_PP_H_ */\n"), ToPreproc(file.Name()));
		}

		//////////////////////////////////////////////////////////////////////////
		template<typename Output>
		void WriteHeader(FFileDefPtr file, Output& output)
		{
			AppendOrdered(output,
						  TEXT("#ifndef {0}_UPB_MINITABLE_H_\n"
							   "#define {0}_UPB_MINITABLE_H_\n\n"
							   "#include \"upb/generated_code_support.h\"\n"),
						  ToPreproc(file.Name()));

			for (int i = 0; i < file.PublicDependencyCount(); i++)
			{
				if (i == 0)
				{
					AppendOrdered(output, TEXT("/* Public Imports. */\n"));
				}
				if (bBootstrap)
					AppendOrdered(output, TEXT("#include \"{0}\"\n"), MiniTableHeaderFilename(file.PublicDependency(i)));

				if (i == file.PublicDependencyCount() - 1)
				{
					AppendOrdered(output, TEXT("\n"));
				}
			}

			AppendOrdered(output,
						  TEXT("\n"
							   "// Must be last.\n"
							   "#include \"upb/port/def.inc\"\n"
							   "\n"
							   "#ifdef __cplusplus\n"
							   "extern \"C\" {\n"
							   "#endif\n"
							   "\n"));

			const TArray<FMessageDefPtr> this_file_messages = SortedMessages(file);
			const TArray<FFieldDefPtr> this_file_exts = SortedExtensions(file);

			for (auto message : this_file_messages)
			{
				AppendOrdered(output, TEXT("extern const upb_MiniTable {0};\n"), MessageInitName(message));
			}
			for (auto ext : this_file_exts)
			{
				AppendOrdered(output, TEXT("extern const upb_MiniTableExtension {0};\n"), ExtensionLayout(ext));
			}

			AppendOrdered(output, TEXT("\n"));

			TArray<FEnumDefPtr> this_file_enums = SortedEnums(file);

			if (!file.Syntax3())
			{
				for (const auto enumdesc : this_file_enums)
				{
					AppendOrdered(output, TEXT("extern const upb_MiniTableEnum {0};\n"), EnumInit(enumdesc));
				}
			}

			AppendOrdered(output, TEXT("extern const upb_MiniTableFile {0};\n\n"), FileLayoutName(file));

			AppendOrdered(output,
						  TEXT("#ifdef __cplusplus\n"
							   "}  /* extern \"C\" */\n"
							   "#endif\n"
							   "\n"
							   "#include \"upb/port/undef.inc\"\n"
							   "\n"
							   "#endif  /* {0}_UPB_MINITABLE_H_ */\n"),
						  ToPreproc(file.Name()));
		}

		// Returns fields in order of "hotness", eg. how frequently they appear in
		// serialized payloads. Ideally this will use a profile. When we don't have
		// that, we assume that fields with smaller numbers are used more frequently.
		TArray<FFieldDefPtr> FieldHotnessOrder(FMessageDefPtr message)
		{
			TArray<FFieldDefPtr> fields;
			size_t field_count = message.FieldCount();
			fields.Reserve(field_count);
			for (size_t i = 0; i < field_count; i++)
			{
				fields.Add(message.Field(i));
			}
			Algo::Sort(fields, [](FFieldDefPtr a, FFieldDefPtr b) {
				if (!b.IsRequired() && a.IsRequired())
					return false;
				if (!a.IsRequired() && b.IsRequired())
					return true;
				return a.Number() < b.Number();
			});
			return fields;
		}

		uint32_t GetWireTypeForField(FFieldDefPtr field)
		{
			if (field.IsPacked())
				return kUpb_WireType_Delimited;
			switch (field.GetType())
			{
				case kUpb_FieldType_Double:
				case kUpb_FieldType_Fixed64:
				case kUpb_FieldType_SFixed64:
					return kUpb_WireType_64Bit;
				case kUpb_FieldType_Float:
				case kUpb_FieldType_Fixed32:
				case kUpb_FieldType_SFixed32:
					return kUpb_WireType_32Bit;
				case kUpb_FieldType_Int64:
				case kUpb_FieldType_UInt64:
				case kUpb_FieldType_Int32:
				case kUpb_FieldType_Bool:
				case kUpb_FieldType_UInt32:
				case kUpb_FieldType_Enum:
				case kUpb_FieldType_SInt32:
				case kUpb_FieldType_SInt64:
					return kUpb_WireType_Varint;
				case kUpb_FieldType_Group:
					return kUpb_WireType_StartGroup;
				case kUpb_FieldType_Message:
				case kUpb_FieldType_String:
				case kUpb_FieldType_Bytes:
					return kUpb_WireType_Delimited;
			}
			checkNoEntry();
			return kUpb_WireType_Varint;
		}

		uint32_t MakeTag(uint32_t field_number, uint32_t wire_type) { return field_number << 3 | wire_type; }

		size_t WriteVarint32ToArray(uint64_t val, char* buf)
		{
			size_t i = 0;
			do
			{
				uint8_t byte = val & 0x7fU;
				val >>= 7;
				if (val)
					byte |= 0x80U;
				buf[i++] = byte;
			} while (val);
			return i;
		}

		uint64_t GetEncodedTag(FFieldDefPtr field)
		{
			uint32_t wire_type = GetWireTypeForField(field);
			uint32_t unencoded_tag = MakeTag(field.Number(), wire_type);
			char tag_bytes[10] = {0};
			WriteVarint32ToArray(unencoded_tag, tag_bytes);
			uint64_t encoded_tag = 0;
			memcpy(&encoded_tag, tag_bytes, sizeof(encoded_tag));
			// TODO: byte-swap for big endian.
			return encoded_tag;
		}

		int GetTableSlot(FFieldDefPtr field)
		{
			uint64_t tag = GetEncodedTag(field);
			if (tag > 0x7fff)
			{
				// Tag must fit within a two-byte varint.
				return -1;
			}
			return (tag & 0xf8) >> 3;
		}

		TArray<TPair<FString, uint64_t>> FastDecodeTable(FMessageDefPtr message)
		{
			TArray<TPair<FString, uint64_t>> table;
			for (const auto field : FieldHotnessOrder(message))
			{
				TPair<FString, uint64_t> ent;
				int slot = GetTableSlot(field);
				if (slot < 0)
				{
					// Tag can't fit in the table.
					continue;
				}
				if (!TryFillTableEntry(field, ent))
				{
					// Unsupported field type or offset, hasbit index, etc. doesn't fit.
					continue;
				}
				while ((size_t)slot >= table.Num())
				{
					size_t size = std::max(1, table.Num() * 2);
					auto CurNum = table.Num();
					table.Reserve(size);
					while (CurNum < size)
					{
						table.Add(TPair<FString, uint64_t>{"_upb_FastDecoder_DecodeGeneric", 0});
						CurNum++;
					}
				}
				if (table[slot].Key != "_upb_FastDecoder_DecodeGeneric")
				{
					// A hotter field already filled this slot.
					continue;
				}
				table[slot] = ent;
			}
			return table;
		}

		bool TryFillTableEntry(FFieldDefPtr field, TPair<FString, uint64_t>& ent)
		{
			const upb_MiniTable* mt = PoolPair.GetMiniTable64(field.ContainingType());
			const upb_MiniTableField* mt_f = upb_MiniTable_FindFieldByNumber(mt, field.Number());
			FString type = "";
			FString cardinality = "";
			switch (upb_MiniTableField_Type(mt_f))
			{
				case kUpb_FieldType_Bool:
					type = "b1";
					break;
				case kUpb_FieldType_Enum:
					if (upb_MiniTableField_IsClosedEnum(mt_f))
					{
						// We don't have the means to test proto2 enum fields for valid values.
						return false;
					}
					// [[fallthrough]];
				case kUpb_FieldType_Int32:
				case kUpb_FieldType_UInt32:
					type = "v4";
					break;
				case kUpb_FieldType_Int64:
				case kUpb_FieldType_UInt64:
					type = "v8";
					break;
				case kUpb_FieldType_Fixed32:
				case kUpb_FieldType_SFixed32:
				case kUpb_FieldType_Float:
					type = "f4";
					break;
				case kUpb_FieldType_Fixed64:
				case kUpb_FieldType_SFixed64:
				case kUpb_FieldType_Double:
					type = "f8";
					break;
				case kUpb_FieldType_SInt32:
					type = "z4";
					break;
				case kUpb_FieldType_SInt64:
					type = "z8";
					break;
				case kUpb_FieldType_String:
					type = "s";
					break;
				case kUpb_FieldType_Bytes:
					type = "b";
					break;
				case kUpb_FieldType_Message:
					type = "m";
					break;
				default:
					return false;  // Not supported yet.
			}

			switch (upb_FieldMode_Get(mt_f))
			{
				case kUpb_FieldMode_Map:
					return false;  // Not supported yet (ever?).
				case kUpb_FieldMode_Array:
					if (mt_f->mode & kUpb_LabelFlags_IsPacked)
					{
						cardinality = "p";
					}
					else
					{
						cardinality = "r";
					}
					break;
				case kUpb_FieldMode_Scalar:
					if (mt_f->presence < 0)
					{
						cardinality = "o";
					}
					else
					{
						cardinality = "s";
					}
					break;
			}

			uint64_t expected_tag = GetEncodedTag(field);

			// Data is:
			//
			//                  48                32                16                 0
			// |--------|--------|--------|--------|--------|--------|--------|--------|
			// |   offset (16)   |case offset (16) |presence| submsg |  exp. tag (16)  |
			// |--------|--------|--------|--------|--------|--------|--------|--------|
			//
			// - |presence| is either hasbit index or field number for oneofs.

			uint64_t data = static_cast<uint64_t>(mt_f->offset) << 48 | expected_tag;

			if (field.IsRepeated())
			{
				// No hasbit/oneof-related fields.
			}
			if (field.RealContainingOneof())
			{
				uint64_t case_offset = ~mt_f->presence;
				if (case_offset > 0xffff || field.Number() > 0xff)
					return false;
				data |= field.Number() << 24;
				data |= case_offset << 32;
			}
			else
			{
				uint64_t hasbit_index = 63;  // No hasbit (set a high, unused bit).
				if (mt_f->presence)
				{
					hasbit_index = mt_f->presence;
					if (hasbit_index > 31)
						return false;
				}
				data |= hasbit_index << 24;
			}

			if (field.GetCType() == kUpb_CType_Message)
			{
#ifdef UPB_PRIVATE
				uint64_t idx = mt_f->UPB_PRIVATE(submsg_index);
#else
				uint64_t idx = mt_f->submsg_index_dont_copy_me__upb_internal_use_only;
#endif
				if (idx > 255)
					return false;
				data |= idx << 16;

				FString size_ceil = "max";
				size_t size = SIZE_MAX;
				if (field.MessageSubdef().FileDef() == field.FileDef())
				{
					// We can only be guaranteed the size of the sub-message if it is in the
					// same file as us.  We could relax this to increase the speed of
					// cross-file sub-message parsing if we are comfortable requiring that
					// users compile all messages at the same time.
					const upb_MiniTable* sub_mt = PoolPair.GetMiniTable64(field.MessageSubdef());
					size = sub_mt->size + 8;
				}
				TArray<size_t> breaks = {64, 128, 192, 256};
				for (auto brk : breaks)
				{
					if (size <= brk)
					{
						size_ceil = LexToString(brk);
						break;
					}
				}
				ent.Key = FormatOrdered(TEXT("upb_p{0}{1}_{2}bt_max{3}b"), cardinality, type, expected_tag > 0xff ? "2" : "1", size_ceil);
			}
			else
			{
				ent.Key = FormatOrdered(TEXT("upb_p{0}{1}_{2}bt"), cardinality, type, expected_tag > 0xff ? "2" : "1");
			}
			ent.Value = data;
			return true;
		}

		// Writes a single field into a .upb.c source file.
		template<typename Output>
		void WriteMessageField(FFieldDefPtr field, const upb_MiniTableField* field64, const upb_MiniTableField* field32, Output& output)
		{
			AppendOrdered(output, TEXT("  {0},\n"), FieldInitializerRaw(field, field64, field32));
		}

		FString GetSub(FFieldDefPtr field)
		{
			if (auto message_def = field.MessageSubdef())
			{
				return FormatOrdered(TEXT("{.submsg = &{0}}"), MessageInitName(message_def));
			}

			if (auto enum_def = field.EnumSubdef())
			{
				if (enum_def.IsClosed())
				{
					return FormatOrdered(TEXT("{.subenum = &{0}}"), EnumInit(enum_def));
				}
			}

			return FString("{.submsg = NULL}");
		}

		// Writes a single message into a .upb.c source file.
		template<typename Output>
		void WriteMessage(FMessageDefPtr message, Output& output)
		{
			FString msg_name = ToCIdent(message.FullName());
			FString fields_array_ref = "NULL";
			FString submsgs_array_ref = "NULL";
			FString subenums_array_ref = "NULL";
			const upb_MiniTable* mt_32 = PoolPair.GetMiniTable32(message);
			const upb_MiniTable* mt_64 = PoolPair.GetMiniTable64(message);
			TMap<int32, FString> subs;

			for (int32 i = 0; i < mt_64->field_count; i++)
			{
				const upb_MiniTableField* f = &mt_64->fields[i];
#ifdef UPB_PRIVATE
				uint32_t index = f->UPB_PRIVATE(submsg_index);
#else
				uint32_t index = f->submsg_index_dont_copy_me__upb_internal_use_only;
#endif

				if (index != kUpb_NoSub)
				{
					auto& Value = subs.Emplace((int32)index, GetSub(message.FindFieldByNumber(f->number)));
					// check(Value);
				}
			}

			if (subs.Num())
			{
				FString submsgs_array_name = msg_name + "_submsgs";
				submsgs_array_ref = "&" + submsgs_array_name + "[0]";
				AppendOrdered(output, TEXT("static const upb_MiniTableSub {0}[{1}] = {\n"), submsgs_array_name, subs.Num());

				int i = 0;
				for (const auto& pair : subs)
				{
					check(pair.Key == i++);
					AppendOrdered(output, TEXT("  {0},\n"), pair.Value);
				}

				AppendOrdered(output, TEXT("};\n\n"));
			}

			if (mt_64->field_count > 0)
			{
				FString fields_array_name = msg_name + TEXT("__fields");
				fields_array_ref = TEXT("&") + fields_array_name + TEXT("[0]");
				AppendOrdered(output, TEXT("static const upb_MiniTableField {0}[{1}] = {\n"), fields_array_name, mt_64->field_count);
				for (int i = 0; i < mt_64->field_count; i++)
				{
					WriteMessageField(message.FindFieldByNumber(mt_64->fields[i].number), &mt_64->fields[i], &mt_32->fields[i], output);
				}
				AppendOrdered(output, TEXT("};\n\n"));
			}

			TArray<TPair<FString, uint64_t>> table;
			uint8_t table_mask = -1;

			table = FastDecodeTable(message);

			if (table.Num() > 1)
			{
				check((table.Num() & (table.Num() - 1)) == 0);
				table_mask = (table.Num() - 1) << 3;
			}

			FString msgext = "kUpb_ExtMode_NonExtendable";

			if (message.ExtensionRangeCount())
			{
#ifdef UPB_DESC
				if (UPB_DESC(MessageOptions_message_set_wire_format)(message.Options()))
#else
				if (google_protobuf_MessageOptions_message_set_wire_format(message.Options()))
#endif
				{
					msgext = "kUpb_ExtMode_IsMessageSet";
				}
				else
				{
					msgext = "kUpb_ExtMode_Extendable";
				}
			}

			AppendOrdered(output, TEXT("const upb_MiniTable {0} = {\n"), MessageInitName(message));
			AppendOrdered(output, TEXT("  {0},\n"), submsgs_array_ref);
			AppendOrdered(output, TEXT("  {0},\n"), fields_array_ref);
			AppendOrdered(output, TEXT("  {0}, {1}, {2}, {3}, UPB_FASTTABLE_MASK({4}), {5},\n"), ArchDependentSize(mt_32->size, mt_64->size), mt_64->field_count, msgext, mt_64->dense_below, table_mask, mt_64->required_count);
			if (table.Num())
			{
				AppendOrdered(output, TEXT("  UPB_FASTTABLE_INIT({\n"));
				for (const auto& ent : table)
				{
					AppendOrdered(output, TEXT("    {0x{1}, &{0}},\n"), ent.Key, FString::Printf(TEXT("%016llx"), ent.Value));
				}
				AppendOrdered(output, TEXT("  })\n"));
			}
			AppendOrdered(output, TEXT("};\n\n"));
		}

		template<typename Output>
		void WriteEnum(FEnumDefPtr e, Output& output)
		{
			FString values_init = "{\n";
			const upb_MiniTableEnum* mt = e.MiniTable();
			uint32_t value_count = (mt->mask_limit / 32) + mt->value_count;
			for (uint32_t i = 0; i < value_count; i++)
			{
				values_init.Appendf(TEXT("                0x%llx,\n"), mt->data[i]);
			}
			values_init += "    }";

			AppendOrdered(output,
						  TEXT(
							  // clang-format off
R"cc(
	const upb_MiniTableEnum {0} = {
		{1},
		{2},
		{3},
	};
)cc"
							  // clang-format on
							  ),
						  EnumInit(e),
						  mt->mask_limit,
						  mt->value_count,
						  values_init);
			AppendOrdered(output, TEXT("\n"));
		}

		template<typename Output>
		int WriteEnums(FFileDefPtr file, Output& output)
		{
			if (file.Syntax3())
				return 0;

			TArray<FEnumDefPtr> this_file_enums = SortedEnums(file);

			for (const auto e : this_file_enums)
			{
				WriteEnum(e, output);
			}

			if (this_file_enums.Num())
			{
				AppendOrdered(output, TEXT("static const upb_MiniTableEnum* {0}[{1}] = {\n"), kEnumsInit, this_file_enums.Num());
				for (const auto e : this_file_enums)
				{
					AppendOrdered(output, TEXT("  &{0},\n"), EnumInit(e));
				}
				AppendOrdered(output, TEXT("};\n"));
				AppendOrdered(output, TEXT("\n"));
			}

			return this_file_enums.Num();
		}

		template<typename Output>
		int WriteMessages(FFileDefPtr file, Output& output)
		{
			TArray<FMessageDefPtr> file_messages = SortedMessages(file);

			if (!file_messages.Num())
				return 0;

			for (auto message : file_messages)
			{
				WriteMessage(message, output);
			}

			AppendOrdered(output, TEXT("static const upb_MiniTable* {0}[{1}] = {\n"), kMessagesInit, file_messages.Num());
			for (auto message : file_messages)
			{
				AppendOrdered(output, TEXT("  &{0},\n"), MessageInitName(message));
			}
			AppendOrdered(output, TEXT("};\n"));
			AppendOrdered(output, TEXT("\n"));
			return file_messages.Num();
		}

		template<typename Output>
		void WriteExtension(FFieldDefPtr ext, Output& output)
		{
			AppendOrdered(output, TEXT("{0},\n"), FieldInitializerRaw(ext));
			AppendOrdered(output, TEXT("  &{0},\n"), MessageInitName(ext.ContainingType()));
			AppendOrdered(output, TEXT("  {0},\n"), GetSub(ext));
		}

		template<typename Output>
		int WriteExtensions(FFileDefPtr file, Output& output)
		{
			auto exts = SortedExtensions(file);
			if (!exts.Num())
				return 0;

			// Order by full name for consistent ordering.
			TMap<FString, FMessageDefPtr> forward_messages;

			for (auto ext : exts)
			{
				forward_messages.FindOrAdd(ext.ContainingType().FullName()) = ext.ContainingType();
				if (ext.MessageSubdef())
				{
					forward_messages.FindOrAdd(ext.MessageSubdef().FullName()) = ext.MessageSubdef();
				}
			}

			for (auto ext : exts)
			{
				AppendOrdered(output, TEXT("const upb_MiniTableExtension {0} = {\n  "), ExtensionLayout(ext));
				WriteExtension(ext, output);
				AppendOrdered(output, TEXT("\n};\n"));
			}

			AppendOrdered(output,
						  TEXT("\n"
							   "static const upb_MiniTableExtension* {0}[{1}] = {\n"),
						  kExtensionsInit,
						  exts.Num());

			for (auto ext : exts)
			{
				AppendOrdered(output, TEXT("  &{0},\n"), ExtensionLayout(ext));
			}

			AppendOrdered(output,
						  TEXT("};\n"
							   "\n"));
			return exts.Num();
		}

		template<typename Output>
		void WriteMiniTableSource(FFileDefPtr file, Output& output)
		{
			AppendOrdered(output,
						  TEXT("#include <stddef.h>\n"
							   "#include \"upb/generated_code_support.h\"\n"
							   "#include \"{0}\"\n"),
						  MiniTableHeaderFilename(file));

			for (int i = 0; i < file.DependencyCount(); i++)
			{
				AppendOrdered(output, TEXT("#include \"{0}\"\n"), MiniTableHeaderFilename(file.Dependency(i)));
			}

			AppendOrdered(output,
						  TEXT("\n"
							   "// Must be last.\n"
							   "#include \"upb/port/def.inc\"\n"
							   "\n"));

			int msg_count = WriteMessages(file, output);
			int ext_count = WriteExtensions(file, output);
			int enum_count = WriteEnums(file, output);

			AppendOrdered(output, TEXT("const upb_MiniTableFile {0} = {\n"), FileLayoutName(file));
			AppendOrdered(output, TEXT("  {0},\n"), msg_count ? kMessagesInit : TEXT("NULL"));
			AppendOrdered(output, TEXT("  {0},\n"), enum_count ? kEnumsInit : TEXT("NULL"));
			AppendOrdered(output, TEXT("  {0},\n"), ext_count ? kExtensionsInit : TEXT("NULL"));
			AppendOrdered(output, TEXT("  {0},\n"), msg_count);
			AppendOrdered(output, TEXT("  {0},\n"), enum_count);
			AppendOrdered(output, TEXT("  {0},\n"), ext_count);
			AppendOrdered(output, TEXT("};\n\n"));

			AppendOrdered(output, TEXT("#include \"upb/port/undef.inc\"\n"));
			AppendOrdered(output, TEXT("\n"));
		}

		template<typename Output>
		void GenerateMiniTableRaw(FFileDefPtr file, Output& output, Output& s_output)
		{
			WriteHeader(file, output);
			WriteMiniTableSource(file, s_output);
		}

	public:
		FDefPoolPair PoolPair;
		bool bBootstrap = true;
		void GenerateSources(TMap<const upb_FileDef*, upb_StringView> InDescMap, TArray<FFileDefPtr> FileDefs, FString Dir, bool bInbootstrap = true)
		{
			TGuardValue<decltype(InDescMap)> DescGurad(DescMap, InDescMap);
			TGuardValue<decltype(bInbootstrap)> BootGurad(bBootstrap, bInbootstrap);

			for (auto file : FileDefs)
			{
				auto Notice = FormatOrdered(TEXT("// This file was generated by gmp_proto_generator from proto\n// source file : {0}\n"), file.Name());
				if (bBootstrap)
				{
					TStringBuilder<1024> output;
					TStringBuilder<1024> s_output;
					AppendOrdered(output, TEXT("{0}"), Notice);
					AppendOrdered(s_output, TEXT("{0}"), Notice);

					GenerateMiniTableRaw(file, output, s_output);

					FString HeaderPath = FPaths::Combine(Dir, MiniTableHeaderFilename(file));
					FString SourcePath = FPaths::Combine(Dir, MiniTableSourceFilename(file));
					FFileHelper::SaveStringToFile(output.ToString(), *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8);
					FFileHelper::SaveStringToFile(s_output.ToString(), *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8);
				}
				{
					TStringBuilder<1024> output;
					TStringBuilder<1024> s_output;
					AppendOrdered(output, TEXT("{0}"), Notice);
					AppendOrdered(s_output, TEXT("{0}"), Notice);

					AppendOrdered(s_output, TEXT("#include \"{0}\"\n"), DefHeaderFilename(file));

					GenerateFileRaw(file, output, s_output);

					FString HeaderPath = FPaths::Combine(Dir, DefHeaderFilename(file));
					FString SourcePath = FPaths::Combine(Dir, DefSourceFilename(file));
					FFileHelper::SaveStringToFile(output.ToString(), *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8);
					FFileHelper::SaveStringToFile(s_output.ToString(), *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8);
				}

				if (false)
				{
					FArena arena;
					size_t serialized_size;
					auto file_proto = upb_FileDef_ToProto(*file, arena);
#ifdef UPB_DESC
					const char* serialized = UPB_DESC(FileDescriptorProto_serialize)(file_proto, arena, &serialized_size);
#else
					const char* serialized = google_protobuf_FileDescriptorProto_serialize(file_proto, arena, &serialized_size);
#endif

					FAnsiStringView file_data(serialized, serialized_size);

					TStringBuilder<1024> reg_output;
					AppendOrdered(reg_output, TEXT("{0}"), Notice);
					AppendOrdered(reg_output, TEXT("#include \"upb/generated_code_support.h\"\n\n"));
					AppendOrdered(reg_output, TEXT("#ifdef UPB_REG_FILE_DESCRIPTOR_PROTO\n"));

					for (int i = 0; i < file.DependencyCount(); i++)
					{
						FFileDefPtr depfile = file.Dependency(i);
						AppendOrdered(reg_output, TEXT("namespace {0} {\n"), ToCIdent(depfile.Name()));
						AppendOrdered(reg_output, TEXT("extern _upb_DefPool_Init {0};\n"), DefInitSymbol(depfile));
						AppendOrdered(reg_output, TEXT("} // {0} \n"), ToCIdent(depfile.Name()));
					}

					AppendOrdered(reg_output, TEXT("namespace {0} {\n"), ToCIdent(file.Name()));
					AppendOrdered(reg_output, TEXT("static const char proto_desc[{0}] = {"), (uint64)serialized_size);
					for (size_t i = 0; i < serialized_size;)
					{
						for (size_t j = 0; j < 25 && i < serialized_size; ++i, ++j)
						{
							AppendOrdered(reg_output, TEXT("'{0}', "), CEscape(file_data.Mid(i, 1)));
						}
						AppendOrdered(reg_output, TEXT("\n"));
					}
					AppendOrdered(reg_output, TEXT("};\n\n"));

					AppendOrdered(reg_output, TEXT("static _upb_DefPool_Init* proto_deps[{0}] = {\n"), file.DependencyCount() + 1);
					for (int i = 0; i < file.DependencyCount(); i++)
					{
						FFileDefPtr depfile = file.Dependency(i);
						AppendOrdered(reg_output, TEXT("  &{1}::{0},\n"), DefInitSymbol(depfile), ToCIdent(depfile.Name()));
					}
					AppendOrdered(reg_output, TEXT("  NULL\n"));
					AppendOrdered(reg_output, TEXT("};\n"));
					AppendOrdered(reg_output, TEXT("\n"));

					AppendOrdered(reg_output, TEXT("_upb_DefPool_Init {0} = {\n"), DefInitSymbol(file));
					AppendOrdered(reg_output, TEXT("  proto_deps,\n"));
					//AppendOrdered(reg_output, TEXT("  &{0},\n"), FileLayoutName(file));
					AppendOrdered(reg_output, TEXT("  NULL,\n"), FileLayoutName(file));
					AppendOrdered(reg_output, TEXT("  \"{0}\",\n"), file.Name());
					AppendOrdered(reg_output, TEXT("  UPB_STRINGVIEW_INIT(proto_desc, {0})\n"), file_data.Len());
					AppendOrdered(reg_output, TEXT("};\n"));

					AppendOrdered(reg_output, TEXT("UPB_REG_FILE_DESCRIPTOR_PROTO({0})\n\n"), DefInitSymbol(file));
					AppendOrdered(reg_output, TEXT("}  // {0}\n"), ToCIdent(file.Name()));

					AppendOrdered(reg_output, TEXT("#endif\n\n"));

					FString DefPath = FPaths::Combine(Dir, DefSourceFilename(file), TEXT(".upbdesc.cpp"));
					FFileHelper::SaveStringToFile(reg_output.ToString(), *DefPath, FFileHelper::EEncodingOptions::ForceUTF8);
				}
			}
		}
	};

	class FProtoBPGenerator : public FProtoTraveler
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
#if UE_5_00_OR_LATER
					PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
					PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
#else
					PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
#endif
					DefaultVal = LexToString(FieldDef.DefaultValue().float_val);
					break;
				case kUpb_CType_Double:
#if UE_5_00_OR_LATER
					PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
					PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
#else
					PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
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

			FScopeMark ScopeMark(ScopeStack, MsgDef.FullName());

			bool bPackageCreated = FPackageName::DoesPackageExist(MsgAssetPath);
			UPackage* StructPkg = bPackageCreated ? LoadPackage(nullptr, *MsgAssetPath, LOAD_NoWarn) : CreatePackage(*MsgAssetPath);
			ensure(StructPkg);
			UObject* OldStruct = StructPkg->FindAssetInPackage();
			UUserDefinedStruct* MsgStruct = CreateProtoDefinedStruct(StructPkg, MsgDef, Desc);
			MsgDefs.Emplace(*MsgDef, MsgStruct);
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
				FString FieldName = FieldDef.Name();
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
#if UE_5_00_OR_LATER
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

			FScopeMark ScopeMark(ScopeStack, EnumDef.FullName());

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
			EnumDefs.Emplace(*EnumDef, EnumObj);

			TArray<TPair<FName, int64>> Names;
			for (int32 i = 0; i < EnumDef.ValueCount(); ++i)
			{
				FEnumValDefPtr EnumValDef = EnumDef.Value(i);
				if (!ensureAlways(EnumValDef))
					continue;
				const FString FullNameStr = EnumObj->GenerateFullEnumName(EnumValDef.Name().ToFStringData());
				Names.Emplace(*FullNameStr, EnumValDef.Number());
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
#if UE_5_00_OR_LATER
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
				return TPair<UProtoDescrotor*, bool>{OldDescObj, true};
			}

			return TPair<UProtoDescrotor*, bool>{NewObject<UProtoDescrotor>(DescPkg, *FileNameStr, RF_Public | RF_Standalone), false};
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
#if UE_5_00_OR_LATER
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

			FScopeMark ScopeMark(ScopeStack, FileDef.Package(), FileDef.Name());

			auto DescPair = AddProtoDesc(FileDef, bRefresh);
			UProtoDescrotor* ProtoDesc = DescPair.Key;
			FileDefMap.Emplace(*FileDef, ProtoDesc);

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
		FProtoBPGenerator(const TMap<const upb_FileDef*, upb_StringView>& In)
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
			FScopeMark(TArray<FString>& Stack, UPB_STRINGVIEW InStr)
				: FScopeMark(Stack, InStr.ToFString())
			{
			}
			FScopeMark(TArray<FString>& Stack, UPB_STRINGVIEW InStr1, UPB_STRINGVIEW InStr2)
				: FScopeMark(Stack, InStr1.ToFString() / InStr2.ToFString())
			{
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
			DoubleProp->SetPropertyValue(Addr, FMath::RandRange(-FLT_MAX, FLT_MAX));
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

	static TOptional<FString> ProtoDefaultPath;
	static FString GatherRootDir(UWorld* InWorld)
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

		auto AssetToUnload = FProtoTraveler(Storages).GenerateAssets(FileDefs);
		if (!ensure(AssetToUnload.Num()))
			return;

		FEditorUtils::DeletePackages(InWorld, AssetToUnload, CreateWeakLambda(InWorld, [Storages, FileDefs](bool bSucc, TArray<FString> AllUnloadedList) {
										 if (!bSucc)
											 return;
										 TArray<FString> FilePaths;
										 for (auto& ResId : AllUnloadedList)
										 {
											 FString FilePath;
#if UE_5_00_OR_LATER
											 if (FPackageName::DoesPackageExist(ResId, &FilePath))
#else
											 if (FPackageName::DoesPackageExist(ResId, nullptr, &FilePath))
#endif
											 {
												 FilePaths.Add(MoveTemp(FilePath));
											 }
										 }
										 if (FilePaths.Num())
											 IAssetRegistry::Get()->ScanFilesSynchronous(FilePaths, true);

										 FScopedTransaction ScopedTransaction(NSLOCTEXT("GMPProto", "GeneratePBStruct", "GeneratePBStruct"));
										 bool bRefresh = false;
										 FProtoBPGenerator ProtoGenerator(Storages);
										 for (auto FileDef : FileDefs)
											 ProtoGenerator.AddProtoFile(FileDef, bRefresh);

										 IAssetRegistry::Get()->ScanFilesSynchronous(FilePaths, true);
										 for (auto UserStruct : ProtoGenerator.GetUserDefinedStructs())
										 {
											 VerifyProtoStruct(UserStruct);
										 }
									 }));
	}
	FAutoConsoleCommandWithWorld XVar_GeneratePBStruct(TEXT("x.gmp.proto.genBP"), TEXT(""), FConsoleCommandWithWorldDelegate::CreateStatic(GeneratePBStruct));

	static void GenerateCppCode(UWorld* InWorld, FString RootDir)
	{
		RootDir = RootDir.IsEmpty() ? GatherRootDir(InWorld) : RootDir;
		if (!IPlatformFile::GetPlatformPhysical().DirectoryExists(*RootDir))
			return;

		FProtoSrcTraveler ProtoSrcTraveler;
		TMap<const upb_FileDef*, upb_StringView> Storages;
		TArray<FFileDefPtr> FileDefs = upb::generator::FillDefPool(ProtoSrcTraveler.PoolPair, Storages);

		ProtoSrcTraveler.GenerateSources(Storages, FileDefs, RootDir);
	}
	FAutoConsoleCommandWithWorldAndArgs XVar_GenerateCpp(TEXT("x.gmp.proto.genCpp"),
														 TEXT("x.gmp.proto.genCpp [SrcRootDir]"),
														 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Strs, UWorld* InWorld) {
															 FString SrcRootDir = Strs.Num() > 0 ? Strs[0] : TEXT("");
															 GenerateCppCode(InWorld, SrcRootDir);
														 }));
}  // namespace PB
}  // namespace GMP

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
				virtual void AddWarning(const std::string& filename, int line, int column, const std::string& message) {}
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

				auto Size = DescProto.ByteSize();
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
													   FString OutFolderPath = GatherRootDir(InWorld);
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

	FAutoConsoleCommandWithWorld XVar_GatherCpp(TEXT("x.gmp.proto.genCppRootDir"),
												TEXT(""),  //
												FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld) {
													FString OutFolderPath = GatherRootDir(InWorld);
													auto Descs = GatherFileDescriptorProtosForDir(OutFolderPath);
													if (!Descs.Num())
														return;

													auto& PreGenerator = upb::generator::GetPreGenerator();
													PreGenerator.Reset();
													for (auto& Desc : Descs)
													{
														PreGenerator.PreAddProtoDesc(Desc);
													}
													GenerateCppCode(InWorld, OutFolderPath);
												}));
}  // namespace PB
}  // namespace GMP
#endif  // defined(PROTOBUF_API)
#include "upb/port/undef.inc"
#endif  // WITH_EDITOR
#endif
