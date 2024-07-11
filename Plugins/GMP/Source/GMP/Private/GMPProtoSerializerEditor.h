//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPProtoSerializer.h"

#if WITH_EDITOR
#if defined(GMP_WITH_UPB) && GMP_WITH_UPB
#include "Editor.h"
#include "GMPEditorUtils.h"
#include "GMPProtoUtils.h"
#include "UnrealCompatibility.h"
#include "upb/libupb.h"

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

			void PreAddProtoDesc(upb_StringView Buf);
			void PreAddProtoDesc(TArrayView<const uint8> Buf);

			bool PreAddProto(upb_StringView Buf);

			void Reset();

			TArray<const FDefPool::FProtoDescType*> GenerateProtoList() const;
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
			void AddProtoImpl(TArray<FNameType>& Results, const FNameType& ProtoName) const;
			static FPreGenerator& GetPreGenerator();
		};

		template<typename DefPoolType>
		TArray<FFileDefPtr> FillDefPool(DefPoolType& Pool, TMap<const upb_FileDef*, upb_StringView>& OutMap)
		{
			return FPreGenerator::GetPreGenerator().FillDefPool(Pool, OutMap);
		}

		TArray<TArray<uint8>> GatherFileDescriptorProtosForDir(FString RootDir);
		FString GatherRootDir(UWorld* InWorld);
	}  // namespace generator
}  // namespace upb
#include "upb/port/undef.inc"

#endif  // GMP_WITH_UPB
#endif  // WITH_EDITOR
