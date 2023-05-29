//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPMeta.h"

#include "GMPStruct.h"
#include "GMPWorldLocals.h"
#include "Misc/ConfigCacheIni.h"

namespace FGMPMetaUtils
{
static UGMPMeta* GetGMPMeta(const UObject* InObj = nullptr)
{
#if WITH_EDITOR
	return GetMutableDefault<UGMPMeta>();
#else
	return GMP::WorldLocalObject<UGMPMeta>(InObj);
#endif
}

auto AccessGMPMeta(const UObject* InObj = nullptr)
{
	struct UGMPMetaFriend : public UGMPMeta
	{
		using UGMPMeta::GMPTypes;
		using UGMPMeta::MessageTagsList;
#if WITH_EDITORONLY_DATA
		using UGMPMeta::GMPMetaVersion;
		using UGMPMeta::GMPTagFileList;
#endif
	};
	return (UGMPMetaFriend*)(GetGMPMeta(InObj));
}

GMP_API void IncVersion()
{
	auto Meta = AccessGMPMeta();
	Meta->GMPTypes.Empty();
	Meta->MessageTagsList.Empty();
#if WITH_EDITORONLY_DATA
	Meta->GMPTagFileList.Empty();
	Meta->GMPMetaVersion++;
#endif
}

GMP_API void InsertMeta(const FName& Key, TArray<FName> ParamTypes, TArray<FName> ResTypes)
{
	auto Meta = AccessGMPMeta();

	{
		auto& Ref = Meta->GMPTypes.Add(Key);
		Ref.ParameterTypes = ParamTypes;
		Ref.ResponseTypes = ResTypes;
	}
	{
		auto& Ref = Meta->MessageTagsList.AddDefaulted_GetRef();
		Ref.Tag = Key;
		Ref.Parameters = MoveTemp(ParamTypes);
		Ref.ResponseTypes = MoveTemp(ResTypes);
	}
}

GMP_API void InsertMetaPath(TFunctionRef<void(TArray<FString>&)> FuncRef)
{
	auto Meta = AccessGMPMeta();
#if WITH_EDITORONLY_DATA
	FuncRef(Meta->GMPTagFileList);
#endif
}
GMP_API void SaveMetaPaths()
{
	auto Meta = AccessGMPMeta();
#if UE_4_23_OR_LATER
	Algo::Sort(Meta->MessageTagsList, [](auto& Lhs, auto& Rhs) { return Lhs.Tag.LexicalLess(Rhs.Tag); });
#else
	Algo::Sort(Meta->MessageTagsList, [](auto& Lhs, auto& Rhs) { return Lhs.Tag < Rhs.Tag; });
#endif
	Meta->SaveConfig(CPF_Config, *Meta->GetDefaultConfigFilename());
}

}  // namespace FGMPMetaUtils

const TArray<FName>* GMP::FMessageBody::GetMessageTypes(const UObject* InObj, const FMSGKEYFind& MsgKey)
{
	return MsgKey ? UGMPMeta::GetTagMeta(InObj, MsgKey) : nullptr;
}

UGMPMeta::UGMPMeta()
{
#if WITH_EDITORONLY_DATA
	if (IsRunningCommandlet())
	{
		CollectTags();
		FGMPMetaUtils::SaveMetaPaths();
	}
#endif
}

const TArray<FName>* UGMPMeta::GetTagMeta(const UObject* InObj, FName MsgTag)
{
	auto Find = FGMPMetaUtils::GetGMPMeta(InObj)->GMPTypes.Find(MsgTag);
	return Find ? &Find->ParameterTypes : nullptr;
}

const TArray<FName>* UGMPMeta::GetSvrMeta(const UObject* InObj, FName MsgTag)
{
	auto Find = FGMPMetaUtils::GetGMPMeta(InObj)->GMPTypes.Find(MsgTag);
	return (Find && Find->ResponseTypes.Num() > 0) ? &Find->ResponseTypes : nullptr;
}

void UGMPMeta::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (MessageTagsList.Num() == 0)
		{
			CollectTags();
		}

		//Read to GMPTypes
		for (auto& Dummy : MessageTagsList)
		{
			auto& Ref = GMPTypes.Add(Dummy.Tag);
			Ref.ParameterTypes = MoveTemp(Dummy.Parameters);
			Ref.ResponseTypes = MoveTemp(Dummy.ResponseTypes);
		}
	}
}

void UGMPMeta::CollectTags()
{
	const TCHAR* SectionName = TEXT("/Script/GMP.GMPMeta");
	FString ConfigIniPath = FPaths::SourceConfigDir().Append(TEXT("DefaultGMPMeta.ini"));
#if WITH_EDITORONLY_DATA
	UGMPMeta& Settings = *GetMutableDefault<UGMPMeta>();
#if UE_5_01_OR_LATER
	ConfigIniPath = FConfigCacheIni::NormalizeConfigIniPath(ConfigIniPath);
#endif
	if (GConfig->DoesSectionExist(SectionName, ConfigIniPath))
	{
		GConfig->GetArray(SectionName, TEXT("+GMPTagFileList"), Settings.GMPTagFileList, ConfigIniPath);
	}
	GMPTagFileList.AddUnique(TEXT("Config/NativeMessageTags.ini"));

	TArray<FString> Values;
	for (auto& Ini : GMPTagFileList)
	{
		TArray<FString> TmpArr;
		auto CfgPath = FPaths::Combine(FPaths::ProjectDir(), Ini);
		CfgPath = FPaths::ConvertRelativePathToFull(CfgPath);
		FConfigFile ConfigFile;
		ConfigFile.Read(CfgPath);

		ConfigFile.GetArray(TEXT("/Script/MessageTags.MessageTagsList"), TEXT("MessageTagList"), TmpArr);
		if (!TmpArr.Num())
			ConfigFile.GetArray(TEXT("/Script/MessageTags.MessageTagsSettings"), TEXT("+MessageTagList"), TmpArr);
		Values.Append(MoveTemp(TmpArr));
	}

	static auto ScriptStruct = FGMPTagMetaSrc::StaticStruct();
	MessageTagsList.Reset();
	for (auto& Cell : Values)
	{
		FGMPTagMetaSrc DummySrc;
		ScriptStruct->ImportText(*Cell, &DummySrc, nullptr, PPF_None, GLog, TEXT("FGMPTagMeta"));

		FGMPTagMetaBase Dummy(DummySrc);
		MessageTagsList.Add_GetRef(Dummy);
		auto& Ref = GMPTypes.Add(Dummy.Tag);
		Ref.ParameterTypes = MoveTemp(Dummy.Parameters);
		Ref.ResponseTypes = MoveTemp(Dummy.ResponseTypes);
	}
#else
	TArray<FString> Values;
	if (GConfig->DoesSectionExist(SectionName, ConfigIniPath))
	{
		GConfig->GetArray(SectionName, TEXT("+MessageTagList"), Values, ConfigIniPath);
	}
	static auto ScriptStruct = FGMPTagMetaBase::StaticStruct();
	for (auto& Cell : Values)
	{
		auto& Dummy = MessageTagsList.Add_GetRef();
		ScriptStruct->ImportText(*Cell, &Dummy, nullptr, PPF_None, GLog, TEXT("FGMPTagMeta"));
	}
#endif
}
#if WITH_EDITORONLY_DATA
FGMPTagMetaBase::FGMPTagMetaBase(FGMPTagMetaSrc& Src)
{
	Tag = Src.Tag;
	Parameters.Reserve(Src.Parameters.Num());
	ResponseTypes.Reserve(Src.ResponseTypes.Num());
	for (auto& P : Src.Parameters)
	{
		Parameters.Add(P.Type);
	}
	for (auto& P : Src.Parameters)
	{
		ResponseTypes.Add(P.Type);
	}
}
#endif

#if WITH_EDITOR && 0
#include "HAL/IConsoleManager.h"
namespace FGMPMetaUtils
{
FAutoConsoleCommandWithWorld XVar_CollectTagsTest(TEXT("gmp.CollectTagsTest"), TEXT(""), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld) {
													  UGMPMeta* Meta = GMP::WorldLocalObject<UGMPMeta>(InWorld);
													  Meta->CollectTags();
												  }));
}
#endif
