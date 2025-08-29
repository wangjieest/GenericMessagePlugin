//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPBPLib.h"

#include "CoreUObject.h"

#include "Engine/NetConnection.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/World.h"
#include "GMPArchive.h"
#include "GMPReflection.h"
#include "GMPSerializer.h"
#include "GameFramework/PlayerController.h"
#include "Templates/TypeHash.h"
#include "UObject/ObjectKey.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"
#include "Misc/ExpressionParser.h"

#if WITH_EDITOR
#include "UnrealEd.h"
#endif
#include "HAL/IConsoleManager.h"

//////////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY(LogGMP);
namespace GMP
{
#if GMP_TRACE_MSG_STACK
void GMPTraceEnterBP(const FString& MsgStr, FString&& Loc);
void GMPTraceLeaveBP(const FString& MsgStr);
struct FGMPTraceBPGuard
{
	FGMPTraceBPGuard(const FString& MsgStr)
		: KeyRef(MsgStr)
	{
		TStringBuilder<1024> Loc;
#if DO_BLUEPRINT_GUARD
		{
			FFrame::GetScriptCallstack(Loc, true, true);
		}
#else
		if (const FFrame* CurrentFrame = FFrame::GetThreadLocalTopStackFrame())
		{
			CurrentFrame->Object->GetClass()->GetFullName(Loc);
		}
#endif
		GMPTraceEnterBP(KeyRef, Loc.ToString());
	}
	~FGMPTraceBPGuard() { GMPTraceLeaveBP(KeyRef); }
	const FString& KeyRef;
};
#else
struct FGMPTraceBPGuard
{
	FGMPTraceBPGuard(const FString& MsgStr) {}
	~FGMPTraceBPGuard() {}
};
#endif
FLatentActionKeeper::FLatentActionKeeper(const FLatentActionInfo& LatentInfo)
	: ExecutionFunction(LatentInfo.ExecutionFunction)
	, LinkID(LatentInfo.Linkage)
	, CallbackTarget(LatentInfo.CallbackTarget)
{
}

void FLatentActionKeeper::SetLatentInfo(const struct FLatentActionInfo& LatentInfo)
{
	ExecutionFunction = LatentInfo.ExecutionFunction;
	LinkID = LatentInfo.Linkage;
	CallbackTarget = (const UObject*)LatentInfo.CallbackTarget;
}

bool FLatentActionKeeper::ExecuteAction(bool bClear) const
{
	if (LinkID != INDEX_NONE)
	{
		if (UObject* Target = CallbackTarget.Get())
		{
			if (UFunction* Function = Target->FindFunction(ExecutionFunction))
			{
				Target->ProcessEvent(Function, &LinkID);
				if (bClear)
					LinkID = INDEX_NONE;
				return true;
			}
		}
	}
	GMP_WARNING(TEXT("FExecutionInfo::DoCallback Failed."));
	return false;
}

int32 GetPropertyCustomIndex(FProperty* Property)
{
	EClassCastFlags Flag = GetPropertyCastFlags(Property);

#define ELSE_FLAG_CHECK(Mask)                                     \
	else if (Flag & CASTCLASS_F##Mask##Property)                  \
	{                                                             \
		return TypeTraits::ToUnderlying(EGMPPropertyClass::Mask); \
	}

	if (Flag & CASTCLASS_FEnumProperty)
	{
		return GetPropertyCustomIndex(CastField<FEnumProperty>(Property)->GetUnderlyingProperty());
	}
	ELSE_FLAG_CHECK(Bool)
	ELSE_FLAG_CHECK(Byte)
	ELSE_FLAG_CHECK(Int16)
	ELSE_FLAG_CHECK(UInt16)
	ELSE_FLAG_CHECK(Int)
	ELSE_FLAG_CHECK(UInt32)
	ELSE_FLAG_CHECK(Int64)
	ELSE_FLAG_CHECK(UInt64)
	ELSE_FLAG_CHECK(Float)
	ELSE_FLAG_CHECK(Double)

	ELSE_FLAG_CHECK(Enum)

	ELSE_FLAG_CHECK(Str)
	ELSE_FLAG_CHECK(Name)
	ELSE_FLAG_CHECK(Text)

	ELSE_FLAG_CHECK(Struct)
	ELSE_FLAG_CHECK(Map)
	ELSE_FLAG_CHECK(Set)
	ELSE_FLAG_CHECK(Array)

	ELSE_FLAG_CHECK(Delegate)

#if UE_4_23_OR_LATER
	else if (Flag & CASTCLASS_FMulticastInlineDelegateProperty)
	{
		return TypeTraits::ToUnderlying(EGMPPropertyClass::InlineMulticastDelegate);
	}
	else if (Flag & CASTCLASS_FMulticastSparseDelegateProperty)
	{
		return TypeTraits::ToUnderlying(EGMPPropertyClass::SparseMulticastDelegate);
	}
#else
	ELSE_FLAG_CHECK(MulticastDelegate)
#endif

	ELSE_FLAG_CHECK(Interface)

	ELSE_FLAG_CHECK(Object)
	ELSE_FLAG_CHECK(WeakObject)
	ELSE_FLAG_CHECK(LazyObject)
	ELSE_FLAG_CHECK(SoftObject)

	ELSE_FLAG_CHECK(Class)
	ELSE_FLAG_CHECK(SoftClass)

#undef ELSE_FLAG_CHECK
	check(false);
	return -1;
}

FORCEINLINE bool BPLibNotifyMessage(const FString& MessageId, const FGMPObjNamePair& SigPair, FTypedAddresses& Params, uint8 Type, UGMPManager* Mgr)
{
	do
	{
		GMP::FGMPTraceBPGuard Guard(MessageId);

		GMP_CHECK(SigPair.Obj);
		auto World = SigPair.Obj->GetWorld();
		if (!ensureAlwaysMsgf(World, TEXT("no world exist with SigSource:%s"), *GetPathNameSafe(SigPair.Obj)))
			break;

		auto NetMode = World->GetNetMode();
		if (Type == EMessageTypeClient)
		{
			if (NetMode == NM_DedicatedServer || NetMode == NM_ListenServer)
				break;
		}
		else if (Type == EMessageTypeServer)
		{
			if (NetMode == NM_Client)
				break;
		}

		auto SigSource = GMP::FSigSource::FindObjNameFilter(SigPair.Obj, SigPair.TagName);
		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();

		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		return Mgr->GetHub().ScriptNotifyMessage(MessageId, Params, SigSource);
	} while (0);
	return false;
}

static inline FGMPTypedAddr InnerToMessageAddr(const void* Addr, FProperty* Property, uint8 PropertyEnum = 255, uint8 ElementEnum = 255, uint8 KeyEnum = 255)
{
	FGMPTypedAddr MessageAddr = FGMPTypedAddr::FromAddr(Addr);
#if GMP_WITH_TYPENAME
	// if (PropertyEnum == TNumericLimits<decltype(PropertyEnum)>::Max())
	// 	PropertyEnum = GetPropertyCustomIndex(Property);
	MessageAddr.TypeName = Reflection::GetPropertyName(Property, EGMPPropertyClass(PropertyEnum), EGMPPropertyClass(ElementEnum), EGMPPropertyClass(KeyEnum));
#endif
	return MessageAddr;
}

template<typename T = FProperty>
static void AddrFromProperty(FFrame& Stack, RESULT_DECL, uint8 PropertyEnum, uint8 ElementEnum = 255, uint8 KeyEnum = 255)
{
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<T>(nullptr);
	auto* ContainerProp = CastField<T>(Stack.MostRecentProperty);
	if (!ContainerProp)
	{
		Stack.bArrayContextFailed = true;
		return;
	}
	P_FINISH
	P_NATIVE_BEGIN
	*(FGMPTypedAddr*)RESULT_PARAM = InnerToMessageAddr(Stack.MostRecentPropertyAddress, ContainerProp, PropertyEnum, ElementEnum, KeyEnum);
	P_NATIVE_END
}

template<typename T = FProperty>
static void AddrFromProperty(FFrame& Stack, RESULT_DECL)
{
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<T>(nullptr);
	auto* ContainerProp = CastField<T>(Stack.MostRecentProperty);
	if (!ContainerProp)
	{
		Stack.bArrayContextFailed = true;
		return;
	}
	P_FINISH
	P_NATIVE_BEGIN
	*(FGMPTypedAddr*)RESULT_PARAM = FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty);
	P_NATIVE_END
}

bool NetSerializeSingleProperty(FArchive& Ar, FProperty* Prop, void* ItemPtr, UPackageMap* PackageMap)
{
	bool bShouldNetSerialized = !!PackageMap;
	GMP_CHECK_SLOW(!Prop->IsA<FArrayProperty>() && !Prop->IsA<FMapProperty>() && !Prop->IsA<FSetProperty>());

	if (auto StructProp = CastField<FStructProperty>(Prop))
	{
		bShouldNetSerialized &= (StructProp->Struct->StructFlags & STRUCT_NetSerializeNative) != 0;
	}
	if (bShouldNetSerialized)
	{
		Prop->NetSerializeItem(Ar, PackageMap, ItemPtr);
	}
	else
	{
#if UE_4_21_OR_LATER
		Prop->SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), ItemPtr);
#else
		Prop->SerializeItem(Ar, ItemPtr);
#endif
	}
	return ensureWorld(PackageMap, !Ar.GetError());
}

int32 InitializeFunctionParameters(UFunction* Function, void* p)
{
	int32 NumParamsEvaluated = 0;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++NumParamsEvaluated)
	{
		FProperty* LocalProp = *It;
#if WITH_EDITOR
		if (!LocalProp || It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			int32 Count = NumParamsEvaluated;
			for (TFieldIterator<FProperty> It2(Function); Count > 0 && It2->HasAnyPropertyFlags(CPF_Parm); ++It2, --Count)
			{
				It2->DestroyValue_InContainer(p);
			}
			return -1;
		}
#endif
		LocalProp->InitializeValue_InContainer(p);
	}
	return NumParamsEvaluated;
}

void DestroyFunctionParameters(UFunction* Function, void* p)
{
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(p);
	}
}
#if GMP_DEBUGGAME
static bool bLogGMPBPExecution = false;
static FAutoConsoleVariableRef CVar_DrawAbilityVisualizer(TEXT("GMP.LogGMPBPExecution"), bLogGMPBPExecution, TEXT("log each blueprint gmp exectuion"), ECVF_Default);
#endif
extern bool IsGMPModuleInited();
}  // namespace GMP

bool UGMPBPLib::UnlistenMessage(const FString& MessageId, UObject* Listener, UGMPManager* Mgr, UObject* Obj)
{
	using namespace GMP;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
		Mgr->GetHub().ScriptUnbindMessage(MessageId, Listener ? Listener : Obj);
	}
	return true;
}

bool UGMPBPLib::UnlistenMessageByKey(const FString& MessageId, UObject* Listener, UGMPManager* Mgr)
{
	return UnlistenMessage(MessageId, Listener, Mgr);
}

UPackageMap* UGMPBPLib::GetPackageMap(APlayerController* PC)
{
	return PC && PC->GetNetConnection() ? PC->GetNetConnection()->PackageMap : nullptr;
}

bool UGMPBPLib::HasAnyListeners(FName InMsgKey, UGMPManager* Mgr)
{
	using namespace GMP;
	if (!IsGMPModuleInited())
		return false;

	Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
	return Mgr->GetHub().IsAlive(InMsgKey);
}

bool UGMPBPLib::NotifyMessageByKeyRet(const FString& MessageId, const FGMPObjNamePair& SigSource, TArray<FGMPTypedAddr>& Params, uint8 Type, UGMPManager* Mgr)
{
	using namespace GMP;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		FTypedAddresses Arr(Params);
		return BPLibNotifyMessage(MessageId, SigSource, Arr, Type, Mgr);
	}
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	return false;
#endif
}

void UGMPBPLib::ResponseMessage(FGMPKey RspKey, TArray<FGMPTypedAddr>& Params, UObject* SigSource, UGMPManager* Mgr)
{
	using namespace GMP;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		FTypedAddresses Arr(Params);
		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
		Mgr->GetHub().ScriptResponseMessage(RspKey, Arr, SigSource);
	}
}

DEFINE_FUNCTION(UGMPBPLib::execResponseMessageVariadic)
{
	using namespace GMP;

	P_GET_STRUCT(FGMPKey, RspKey);
	P_GET_OBJECT(UObject, SigSource);
	P_GET_OBJECT(UGMPManager, Mgr);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Error, TEXT("version not supported"));
	P_FINISH
	return;
#else

	FTypedAddresses Params;
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif

		Params.Add(FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty));
	}
	P_FINISH

	P_NATIVE_BEGIN
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensureWorld(Stack.Object, IsGMPModuleInited()))
#endif
	{
		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		Mgr->GetHub().ScriptResponseMessage(RspKey, Params, SigSource);
	}
	P_NATIVE_END
#endif
}

FGMPTypedAddr UGMPBPLib::ListenMessageByKey(FName MessageKey, const FGMPScriptDelegate& Delegate, int32 Times, int32 Order, uint8 Type, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
#if GMP_TRACE_MSG_STACK
	FString MsgStr = MessageKey.ToString();
	GMP::FGMPTraceBPGuard Guard(MsgStr);
#endif
	using namespace GMP;

	FGMPTypedAddr ret;
	ret.Value = 0;
	do
	{
		const UObject* Listener = Delegate.GetUObject();
		UWorld* World = IsValid(Listener) ? Listener->GetWorld() : nullptr;
		if (!ensureAlwaysMsgf(World, TEXT("no world exist with Listener:%s"), *GetPathNameSafe(Listener)))
			break;

		if (!ensureWorld(World, Listener->FindFunction(Delegate.GetFunctionName())))
		{
			FFrame::KismetExecutionMessage(TEXT("Delegate.Event Is Invalid"), ELogVerbosity::Error);
			break;
		}

		auto NetMode = World->GetNetMode();
		if (Type == EMessageTypeClient)
		{
			if (NetMode == NM_DedicatedServer && NetMode == NM_ListenServer)
				break;
		}
		else if (Type == EMessageTypeServer)
		{
			if (NetMode == NM_Client)
				break;
		}

		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
		auto SigSource = GMP::FSigSource::MakeObjNameFilter(SigPair.Obj, SigPair.TagName);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (Mgr->GetHub().IsAlive(MessageKey, Listener, SigSource))
		{
			auto DebugStr = FString::Printf(TEXT("%s<-%s"), *MessageKey.ToString(), *Delegate.ToString<UObject>());
			const bool AssetFlag = false;
			ensureWorldMsgf(World, AssetFlag, TEXT("%s"), *DebugStr);
		}
#endif
		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		auto Id = Mgr->GetHub().ScriptListenMessage(SigSource,
													MessageKey,
													Listener,
													[Delegate](FMessageBody& Msg) {
#if GMP_DEBUGGAME
														if (bLogGMPBPExecution)
															GMP_LOG(TEXT("Execute %s"), *Delegate.ToString<UObject>());
#endif
														auto Arr = Msg.Parameters();
														Delegate.ExecuteIfBound(Msg.GetSigSource(), Msg.MessageKey(), Msg.Sequence(), Arr);
													},
													{Times, Order});
		if (!Id)
			break;
		ret.Value = Id;
	} while (0);
	return ret;
}

FGMPTypedAddr UGMPBPLib::ListenMessageByKeyValidate(const TArray<FName>& ArgNames, FName MessageKey, const FGMPScriptDelegate& Delegate, int32 Times, int32 Order, uint8 Type, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	using namespace GMP;
	Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
	const FArrayTypeNames* OldParams = nullptr;
	{
		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		if (!Mgr->GetHub().IsSignatureCompatible(false, MessageKey, FArrayTypeNames(ArgNames), OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Listen %s"), *MessageKey.ToString());
			return FGMPTypedAddr{0};
		}
	}
#endif
	return ListenMessageByKey(MessageKey, Delegate, Times, Order, Type, Mgr, SigPair);
}

FGMPTypedAddr UGMPBPLib::ListenMessageViaKey(UObject* Listener, FName MessageKey, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
#if GMP_TRACE_MSG_STACK
	FString MsgStr = MessageKey.ToString();
	GMP::FGMPTraceBPGuard Guard(MsgStr);
#endif
	using namespace GMP;
	FGMPTypedAddr ret;
	ret.Value = 0;
	do
	{
		UWorld* World = Listener ? Listener->GetWorld() : nullptr;
		if (!ensureAlwaysMsgf(World, TEXT("no world exist with Listener:%s"), *GetPathNameSafe(Listener)))
			break;

		auto Function = Listener->FindFunction(EventName);
		if (!ensureWorld(World, Function))
		{
			FFrame::KismetExecutionMessage(TEXT("Event Is Invalid"), ELogVerbosity::Error);
			break;
		}

		auto NetMode = World->GetNetMode();
		if (Type == EMessageTypeClient)
		{
			if (NetMode == NM_DedicatedServer && NetMode == NM_ListenServer)
				break;
		}
		else if (Type == EMessageTypeServer)
		{
			if (NetMode == NM_Client)
				break;
		}

		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
		auto SigSource = GMP::FSigSource::MakeObjNameFilter(SigPair.Obj ? SigPair.Obj : (UObject*)World, SigPair.TagName);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (Mgr->GetHub().IsAlive(MessageKey, Listener, SigSource))
		{
			auto DebugStr = FString::Printf(TEXT("existed %s<-%s.%s"), *MessageKey.ToString(), *GetNameSafe(Listener), *EventName.ToString());
			static bool AssetFlag = false;
			ensureWorldMsgf(World, AssetFlag, TEXT("%s"), *DebugStr);
			break;
		}
#endif
		//GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		auto Id = Mgr->GetHub().ScriptListenMessage(
			SigSource,
			MessageKey,
			Listener,
			[Listener, Function, BodyDataMask](FMessageBody& Msg) {
				int32 OutCnt = 0;
				TArray<FGMPTypedAddr> InnerArr;
				auto Params = Msg.MakeFullParameters(BodyDataMask, OutCnt, InnerArr);
#if GMP_WITH_DYNAMIC_CALL_CHECK
#if GMP_DEBUGGAME
				if (bLogGMPBPExecution)
					GMP_LOG(TEXT("Execute %s.%s"), *GetNameSafe(Listener), *Function->GetName());
#endif

				int32 PropIdx = 0;
				for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
				{
					const bool bIsInput = !(PropIt->HasAnyPropertyFlags(CPF_ReturnParm) || (PropIt->HasAnyPropertyFlags(CPF_OutParm) && !PropIt->HasAnyPropertyFlags(CPF_ReferenceParm) && !PropIt->HasAnyPropertyFlags(CPF_ConstParm)));
					if (!ensureWorld(Listener, bIsInput && Params.IsValidIndex(PropIdx)))
						return;

					if (PropIdx >= OutCnt)
					{
						UEnum* EnumPtr = nullptr;
						auto ByteProp = CastField<FByteProperty>(*PropIt);
						if (ByteProp)
						{
							EnumPtr = ByteProp->GetIntPropertyEnum();
						}
						else if (auto EnumProp = CastField<FEnumProperty>(*PropIt))
						{
							ByteProp = CastField<FByteProperty>(EnumProp->GetUnderlyingProperty());
							ensureWorld(Listener, ByteProp || EnumProp->GetUnderlyingProperty()->IsEnum());
							EnumPtr = EnumProp->GetEnum();
						}

						if (EnumPtr)
						{
							ensureWorld(Listener, EnumPtr->GetCppForm() == UEnum::ECppForm::EnumClass);
							ensureWorld(Listener, Params[PropIdx].TypeName == TClass2Name<uint8>::GetFName() || Params[PropIdx].TypeName == Class2Name::TTraitsEnumBase::GetFName(EnumPtr, 1) || Params[PropIdx].TypeName == *EnumPtr->CppType);
						}
					}
					++PropIdx;
				}
#endif
				CallMessageFunction(Listener, Function, Params);
			},
			{Times, Order});
		if (!Id)
			break;
		ret.Value = Id;
	} while (0);
	return ret;
}

FGMPTypedAddr
	UGMPBPLib::ListenMessageViaKeyValidate(const TArray<FName>& ArgNames, UObject* Listener, FName MessageKey, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	using namespace GMP;
	Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
	{
		const FArrayTypeNames* OldParams = nullptr;
		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		if (!Mgr->GetHub().IsSignatureCompatible(false, MessageKey, FArrayTypeNames(ArgNames), OldParams))
		{
			ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Listen %s"), *MessageKey.ToString());
			return FGMPTypedAddr{0};
		}
	}
#endif
	return ListenMessageViaKey(Listener, MessageKey, EventName, Times, Order, Type, BodyDataMask, Mgr, SigPair);
}

static FGMPKey RequestMessageImpl(FGMPKey& RspKey, FName EventName, const FString& MessageKey, const FGMPObjNamePair& SigPair, GMP::FTypedAddresses& Params, uint8 Type, UGMPManager* Mgr)
{
	GMP::FGMPTraceBPGuard Guard(MessageKey);

	using namespace GMP;
	RspKey = 0;
	do
	{
		auto SigSource = GMP::FSigSource::FindObjNameFilter(SigPair.Obj, SigPair.TagName);
		if (!SigSource)
			break;

		auto Sender = SigPair.Obj;
		GMP_CHECK(Sender);
		UWorld* World = IsValid(Sender) ? Sender->GetWorld() : nullptr;
		if (!ensureAlwaysMsgf(World, TEXT("no world exist with Sender:%s"), *GetPathNameSafe(Sender)))
			break;

		auto Function = Sender->FindFunction(EventName);
		if (!ensureWorld(World, Function))
		{
			FFrame::KismetExecutionMessage(TEXT("Event Is Invalid"), ELogVerbosity::Error);
			break;
		}

		auto NetMode = World->GetNetMode();
		if (Type == EMessageTypeClient)
		{
			if (NetMode == NM_DedicatedServer && NetMode == NM_ListenServer)
				break;
		}
		else if (Type == EMessageTypeServer)
		{
			if (NetMode == NM_Client)
				break;
		}

		Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (Mgr->GetHub().IsResponseOn(RspKey))
		{
			auto DebugStr = FString::Printf(TEXT("%s<-%s.%s"), *MessageKey, *GetNameSafe(Sender), *EventName.ToString());
			static bool AssetFlag = false;
			ensureWorldMsgf(World, AssetFlag, TEXT("%s"), *DebugStr);
			break;
		}
		auto RspLambda = [Sender, Function](FMessageBody& RspBody) {
			TArray<FGMPTypedAddr> RspParams{RspBody.GetParams()};
#if GMP_DEBUGGAME
			if (bLogGMPBPExecution)
				GMP_LOG(TEXT("Execute %s.%s"), *GetNameSafe(Sender), *Function->GetName());
#endif
			int32 PropIdx = 0;
			for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
			{
				const bool bIsInput = !(PropIt->HasAnyPropertyFlags(CPF_ReturnParm) || (PropIt->HasAnyPropertyFlags(CPF_OutParm) && !PropIt->HasAnyPropertyFlags(CPF_ReferenceParm) && !PropIt->HasAnyPropertyFlags(CPF_ConstParm)));
				if (!ensureWorld(Sender, bIsInput && RspParams.IsValidIndex(PropIdx)))
					return;

				UEnum* EnumPtr = nullptr;
				auto ByteProp = CastField<FByteProperty>(*PropIt);
				if (ByteProp)
				{
					EnumPtr = ByteProp->GetIntPropertyEnum();
				}
				else if (auto EnumProp = CastField<FEnumProperty>(*PropIt))
				{
					ByteProp = CastField<FByteProperty>(EnumProp->GetUnderlyingProperty());
					ensureWorld(Sender, ByteProp || EnumProp->GetUnderlyingProperty()->IsEnum());
					EnumPtr = EnumProp->GetEnum();
				}

				if (EnumPtr)
				{
					ensureWorld(Sender, EnumPtr->GetCppForm() == UEnum::ECppForm::EnumClass);
					ensureWorld(Sender, RspParams[PropIdx].TypeName == TClass2Name<uint8>::GetFName() || RspParams[PropIdx].TypeName == Class2Name::TTraitsEnumBase::GetFName(EnumPtr, 1) || RspParams[PropIdx].TypeName == *EnumPtr->CppType);
				}
				++PropIdx;
			}
			UGMPBPLib::CallMessageFunction(Sender, Function, RspParams);
		};
#else
		auto RspLambda = [Sender, Function](FMessageBody& RspBody) {
			TArray<FGMPTypedAddr> RspParams{RspBody.GetParams()};
			UGMPBPLib::CallMessageFunction(Sender, Function, RspParams);
		};
#endif

		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(GMP::FMessageHub::GetBlueprintTagType());
		RspKey = Mgr->GetHub().ScriptRequestMessage(MessageKey, Params, MoveTemp(RspLambda), Sender);
	} while (0);
	return RspKey;
}

bool UGMPBPLib::RequestMessageRet(FGMPKey& RspKey, FName EventName, const FString& MessageKey, const FGMPObjNamePair& SigPair, TArray<FGMPTypedAddr>& Params, uint8 Type, UGMPManager* Mgr)
{
	GMP::FTypedAddresses RspParams{Params};
	return RequestMessageImpl(RspKey, EventName, MessageKey, SigPair, RspParams, Type, Mgr).IsValid();
}

bool execRequestMessageVariadicRetGet(FFrame& Stack, RESULT_DECL)
{
	using namespace GMP;
	P_GET_STRUCT_REF(FGMPKey, RspKey);
	PARAM_PASSED_BY_VAL(EventName, FNameProperty, FName);
	P_GET_PROPERTY(FStrProperty, MessageKey);
	P_GET_STRUCT_REF(FGMPObjNamePair, SigSource);
	P_GET_PROPERTY(FByteProperty, Type);
	P_GET_OBJECT(UGMPManager, Mgr);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Error, TEXT("version not supported"));
	P_FINISH
	return false;
#else

	FTypedAddresses Params;
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif

		Params.Add(FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty));
	}
	P_FINISH
	P_NATIVE_BEGIN
	return RequestMessageImpl(RspKey, EventName, MessageKey, SigSource, Params, Type, Mgr).IsValid();
	P_NATIVE_END
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execRequestMessageVariadicRet)
{
	*(bool*)RESULT_PARAM = execRequestMessageVariadicRetGet(Stack, RESULT_PARAM);
}
DEFINE_FUNCTION(UGMPBPLib::execRequestMessageVariadic)
{
	execRequestMessageVariadicRetGet(Stack, RESULT_PARAM);
}

void UGMPBPLib::InnerSet(FFrame& Stack, uint8 PropertyEnum /*= -1*/, uint8 ElementEnum /*= -1*/, uint8 KeyEnum /*= -1*/)
{
	using namespace GMP;
	Stack.MostRecentProperty = nullptr;
#if 0
	TArray<FGMPTypedAddr>* ArrayAddr = (TArray<FGMPTypedAddr>*)Stack.MostRecentPropertyAddress;
	FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);
	if (!ensureWorld(Stack.Object, ArrayProperty && ArrayAddr))
	{
		Stack.bArrayContextFailed = true;
		return;
	}
#else
	P_GET_TARRAY_REF(FGMPTypedAddr, MsgArr);
	TArray<FGMPTypedAddr>* ArrayAddr = &MsgArr;
#endif
	P_GET_PROPERTY(FIntProperty, Index);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* InProperty = CastField<FProperty>(Stack.MostRecentProperty);

	P_FINISH
	void* ItemPtr = Stack.MostRecentPropertyAddress;

#if GMP_WITH_DYNAMIC_TYPE_CHECK
	if (!ensureWorld(Stack.Object, ArrayAddr->IsValidIndex(Index) && (*ArrayAddr)[Index].TypeName == Reflection::GetPropertyName(InProperty, EGMPPropertyClass(PropertyEnum), EGMPPropertyClass(ElementEnum), EGMPPropertyClass(KeyEnum))))
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid Param"), ELogVerbosity::Warning, TEXT("TypeError"));
		return;
	}
#endif

	P_NATIVE_BEGIN
	if (ArrayAddr->IsValidIndex(Index))
	{
		InProperty->CopyCompleteValueFromScriptVM((*ArrayAddr)[Index].ToAddr(), ItemPtr);
	}
	else
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to access index %d from msg array of length %d in '%s'!"), Index, ArrayAddr->Num(), *GetPathNameSafe(Stack.Object)),
									   ELogVerbosity::Warning,
									   TEXT("OutOfBoundsWarning"));
	}
	P_NATIVE_END
}

void UGMPBPLib::InnerGet(FFrame& Stack, uint8 PropertyEnum, uint8 ElementEnum, uint8 KeyEnum)
{
	using namespace GMP;
	Stack.MostRecentProperty = nullptr;
#if 0
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	TArray<FGMPTypedAddr>* ArrayAddr = (TArray<FGMPTypedAddr>*)Stack.MostRecentPropertyAddress;
	FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);
	if (!ensureWorld(Stack.Object, ArrayProperty && ArrayAddr))
	{
		Stack.bArrayContextFailed = true;
		return;
	}
#else
	P_GET_TARRAY_REF(FGMPTypedAddr, MsgArr);
	TArray<FGMPTypedAddr>* ArrayAddr = &MsgArr;
#endif
	P_GET_PROPERTY(FIntProperty, Index);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* OutProperty = CastField<FProperty>(Stack.MostRecentProperty);

	P_FINISH
	void* ItemPtr = Stack.MostRecentPropertyAddress;

#if GMP_WITH_DYNAMIC_TYPE_CHECK
	if (!ensureWorld(Stack.Object, ArrayAddr->IsValidIndex(Index) && (*ArrayAddr)[Index].TypeName == Reflection::GetPropertyName(OutProperty, EGMPPropertyClass(PropertyEnum), EGMPPropertyClass(ElementEnum), EGMPPropertyClass(KeyEnum))))
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid Param"), ELogVerbosity::Warning, TEXT("TypeError"));
		return;
	}
#endif

	P_NATIVE_BEGIN
	if (ArrayAddr->IsValidIndex(Index))
	{
		OutProperty->CopyCompleteValueToScriptVM(ItemPtr, (*ArrayAddr)[Index].ToAddr());
	}
	else
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Attempted to access index %d from msg array of length %d in '%s'!"), Index, ArrayAddr->Num(), *GetPathNameSafe(Stack.Object)),
									   ELogVerbosity::Warning,
									   TEXT("OutOfBoundsWarning"));
	}
	P_NATIVE_END
}

//////////////////////////////////////////////////////////////////////////
bool execNotifyMessageByKeyVariadicGet(FFrame& Stack, RESULT_DECL)
{
	using namespace GMP;
	P_GET_PROPERTY(FStrProperty, MessageId);
	P_GET_STRUCT_REF(FGMPObjNamePair, SigSource);
	P_GET_PROPERTY(FByteProperty, Type);
	P_GET_OBJECT(UGMPManager, Mgr);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Error, TEXT("version not supported"));
	P_FINISH
	return false;
#else

	FTypedAddresses Params;
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif

		Params.Add(FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty));
	}
	P_FINISH

	P_NATIVE_BEGIN
	return BPLibNotifyMessage(MessageId, SigSource, Params, Type, Mgr);
	P_NATIVE_END
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execNotifyMessageByKeyVariadicRet)
{
	*(bool*)RESULT_PARAM = execNotifyMessageByKeyVariadicGet(Stack, RESULT_PARAM);
}
DEFINE_FUNCTION(UGMPBPLib::execNotifyMessageByKeyVariadic)
{
	execNotifyMessageByKeyVariadicGet(Stack, RESULT_PARAM);
}

DEFINE_FUNCTION(UGMPBPLib::execAddrFromVariadic)
{
	using namespace GMP;
	AddrFromProperty<FProperty>(Stack, RESULT_PARAM);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrFromWild)
{
	using namespace GMP;
	P_GET_PROPERTY(FByteProperty, Enum);
	AddrFromProperty<FProperty>(Stack, RESULT_PARAM, Enum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrFromArray)
{
	using namespace GMP;
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	AddrFromProperty<FArrayProperty>(Stack, RESULT_PARAM, (uint8)EGMPPropertyClass::Array, ElementEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrFromSet)
{
	using namespace GMP;
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	AddrFromProperty<FSetProperty>(Stack, RESULT_PARAM, (uint8)EGMPPropertyClass::Set, ElementEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrFromMap)
{
	using namespace GMP;
	P_GET_PROPERTY(FByteProperty, ValueEnum);
	P_GET_PROPERTY(FByteProperty, KeyEnum);
	AddrFromProperty<FMapProperty>(Stack, RESULT_PARAM, (uint8)EGMPPropertyClass::Map, ValueEnum, KeyEnum);
}

void GMPBPLib_SetVariadic(FGMPTypedAddr& Any, FFrame& Stack, uint8 PropertyEnum = -1, uint8 ElementEnum = -1, uint8 KeyEnum = -1)
{
	using namespace GMP;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* InProperty = CastField<FProperty>(Stack.MostRecentProperty);

	P_FINISH
	void* ItemPtr = Stack.MostRecentPropertyAddress;

#if GMP_WITH_DYNAMIC_TYPE_CHECK
	if (!ensureWorld(Stack.Object, Any.TypeName == Reflection::GetPropertyName(InProperty)))
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid Param"), ELogVerbosity::Warning, TEXT("TypeError"));
		return;
	}
#endif

	P_NATIVE_BEGIN
	InProperty->CopyCompleteValueFromScriptVM(Any.ToAddr(), ItemPtr);
	P_NATIVE_END
}

DEFINE_FUNCTION(UGMPBPLib::execSetVariadic)
{
	using namespace GMP;
	UGMPManager* Mgr = FMessageUtils::GetManager();
	Stack.StepCompiledIn<FObjectProperty>(&Mgr);
	GMP_CHECK(Mgr);
	auto& Params = Mgr->GetHub().GetCurrentMessageBody()->GetParams();
	Stack.MostRecentProperty = nullptr;
	P_GET_PROPERTY(FIntProperty, Index);
#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Fatal, TEXT("version not supported"));
	P_FINISH
	return;
#else

	if (Params.IsValidIndex(Index))
	{
		Stack.bArrayContextFailed = true;
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Index:%d Out Of Array Range:%d!"), Index, Params.Num()), ELogVerbosity::Warning, TEXT("OutOfBoundsWarning"));
		return;
	}
	GMPBPLib_SetVariadic(Params[Index], Stack);
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execSetValue)
{
	InnerSet(Stack);
}

DEFINE_FUNCTION(UGMPBPLib::execSetWild)
{
	P_GET_PROPERTY(FByteProperty, PropertyEnum);
	InnerSet(Stack, PropertyEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execSetArray)
{
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	InnerSet(Stack, (uint8)EGMPPropertyClass::Array, ElementEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execSetMap)
{
	P_GET_PROPERTY(FByteProperty, KeyEnum);
	P_GET_PROPERTY(FByteProperty, ValueEnum);
	InnerSet(Stack, (uint8)EGMPPropertyClass::Map, ValueEnum, KeyEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execSetSet)
{
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	InnerSet(Stack, (uint8)EGMPPropertyClass::Set, ElementEnum);
}

DEFINE_FUNCTION(UGMPBPLib::execAddrToWild)
{
	P_GET_PROPERTY(FByteProperty, PropertyEnum);
	InnerGet(Stack, PropertyEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrToArray)
{
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	InnerGet(Stack, (uint8)EGMPPropertyClass::Array, ElementEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrToMap)
{
	P_GET_PROPERTY(FByteProperty, KeyEnum);
	P_GET_PROPERTY(FByteProperty, ValueEnum);
	InnerGet(Stack, (uint8)EGMPPropertyClass::Map, ValueEnum, KeyEnum);
}
DEFINE_FUNCTION(UGMPBPLib::execAddrToSet)
{
	P_GET_PROPERTY(FByteProperty, ElementEnum);
	InnerGet(Stack, (uint8)EGMPPropertyClass::Set, ElementEnum);
}

//////////////////////////////////////////////////////////////////////////

bool UGMPBPLib::MessageToArchive(FArchive& Ar, UFunction* Function, const TArray<FGMPTypedAddr>& Params, UPackageMap* PackageMap)
{
	GMP_CHECK(Ar.IsSaving());
	bool bSucc = true;
	int32 Index = 0;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm) || !Params.IsValidIndex(Index))
		{
			bSucc = false;
			break;
		}

		if (!NetSerializeProperty(Ar, *It, Params[Index].ToAddr(), PackageMap))
		{
			bSucc = false;
			break;
		}
		++Index;
	}
	return bSucc;
}

bool UGMPBPLib::ArchiveToFrame(FArchive& ArToLoad, UFunction* Function, void* FramePtr, UPackageMap* PackageMap)
{
	using namespace GMP;

	GMP_CHECK(ArToLoad.IsLoading());

	if (InitializeFunctionParameters(Function, FramePtr) < 0)
		return false;

	bool bSucc = true;
	int32 Index = 0;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			bSucc = false;
			break;
		}
		FProperty* Prop = *It;
		if (!NetSerializeProperty(ArToLoad, Prop, Prop->ContainerPtrToValuePtr<void>(FramePtr), PackageMap))
		{
			bSucc = false;
			break;
		}
		++Index;
	}
	if (!bSucc)
		DestroyFunctionParameters(Function, FramePtr);
	return bSucc;
}

bool UGMPBPLib::MessageToFrame(UFunction* Function, void* FramePtr, const TArray<FGMPTypedAddr>& Params)
{
	using namespace GMP;
	if (InitializeFunctionParameters(Function, FramePtr) < 0)
		return false;

	bool bSucc = true;
	int32 Index = 0;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			bSucc = false;
			break;
		}
		FProperty* Prop = *It;

#if GMP_WITH_DYNAMIC_TYPE_CHECK
		if (Params[Index].TypeName != NAME_GMPSkipValidate && !(ensure(FNameSuccession::IsTypeCompatible(Reflection::GetPropertyName(Prop, true), Params[Index].TypeName))))
		{
			bSucc = false;
			break;
		}
#endif

		Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(FramePtr), Params[Index].ToAddr());
		++Index;
	}
	if (!bSucc)
		DestroyFunctionParameters(Function, FramePtr);
	return bSucc;
}

void UGMPBPLib::CallFunctionPacked(UObject* Obj, FName FuncName, TArray<FGMPTypedAddr>& Params)
{
	CallMessageFunction(Obj, Obj ? Obj->FindFunction(FuncName) : nullptr, Params);
}

DEFINE_FUNCTION(UGMPBPLib::execCallFunctionVariadic)
{
	P_GET_OBJECT(UObject, Obj);
	P_GET_PROPERTY(FNameProperty, FuncName);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Fatal, TEXT("version not supported"));
	P_FINISH
	return;
#else
	TArray<FGMPTypedAddr> MsgArr;

	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);
#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif

		MsgArr.Add(FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty));
	}
	P_FINISH

	P_NATIVE_BEGIN
	CallMessageFunction(Obj, Obj ? Obj->FindFunction(FuncName) : nullptr, MsgArr);
	P_NATIVE_END
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execMessageFromVariadic)
{
	Stack.MostRecentProperty = nullptr;
	TArray<FGMPTypedAddr>& MsgArr = Stack.StepCompiledInRef<FArrayProperty, TArray<FGMPTypedAddr>>(nullptr);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Fatal, TEXT("version not supported"));
	P_FINISH
	return;
#else
	P_NATIVE_BEGIN
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);
#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif

		MsgArr.Add(FGMPTypedAddr::FromAddr(Stack.MostRecentPropertyAddress, Stack.MostRecentProperty));
	}
	P_NATIVE_END
	P_FINISH
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execMakeLiteralInt)
{
	P_GET_PROPERTY_REF(FByteProperty, Value);
	P_FINISH

	P_NATIVE_BEGIN
	*(int32*)Z_Param__Result = Value;
	P_NATIVE_END
}

DEFINE_FUNCTION(UGMPBPLib::execMakeLiteralByte)
{
	P_GET_PROPERTY_REF(FIntProperty, Value);
	P_FINISH

	P_NATIVE_BEGIN
	*(uint8*)Z_Param__Result = static_cast<uint8>(Value);
	P_NATIVE_END
}

bool UGMPBPLib::NetSerializeProperty(FArchive& Ar, FProperty* Prop, void* ItemPtr, UPackageMap* PackageMap)
{
	using namespace GMP;
	GMP_CHECK(CastField<FArrayProperty>(Prop) || !(CastField<FMapProperty>(Prop) || CastField<FSetProperty>(Prop)));

	bool bOutSuccess = true;
	if (auto ArrProp = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper ScriptArr(ArrProp, ItemPtr);
		const auto MaxArrayNum = Serializer::MaxNetArrayNum;
		uint32 ArrayNum = 0u;
		if (Ar.IsSaving())
		{
			ArrayNum = ScriptArr.Num();
			if (!ensureWorld(PackageMap, ArrayNum <= MaxArrayNum))
			{
				// Overflow. This is on the saving side, so the calling code is exceeding the limit and needs to be fixed.
				bOutSuccess = false;
				ArrayNum = MaxArrayNum;
			}
		}
		const uint32 NumBits = FMath::CeilLogTwo(MaxArrayNum) + 1;
		Ar.SerializeBits(&ArrayNum, NumBits);

		if (auto ByteProp = CastField<FByteProperty>(ArrProp->Inner))
		{
			if (Ar.IsLoading())
				ScriptArr.EmptyAndAddUninitializedValues(ArrayNum);
			Ar.SerializeBits(ScriptArr.GetRawPtr(0), ArrayNum * sizeof(uint8) * 8);
			if (!ensureWorld(PackageMap, !Ar.GetError()))
				return false;
		}
		else
		{
			if (Ar.IsLoading())
				ScriptArr.EmptyAndAddValues(ArrayNum);
			for (auto i = 0u; i < ArrayNum; ++i)
			{
				if (!NetSerializeSingleProperty(Ar, ArrProp->Inner, ScriptArr.GetRawPtr(i), PackageMap))
				{
					ScriptArr.EmptyValues();
					return false;
				}
			}
		}
	}
	else
	{
		if (!NetSerializeSingleProperty(Ar, Prop, ItemPtr, PackageMap))
			return false;
	}
	return bOutSuccess;
}

bool UGMPBPLib::CallEventFunction(UObject* Obj, const FName FuncName, const TArray<uint8>& Buffer, UPackageMap* PackageMap, EFunctionFlags VerifyFlags)
{
	using namespace GMP;
	UFunction* Function = Obj ? Obj->FindFunction(FuncName) : nullptr;
	if (!ensureAlways(Function))
		return false;
	if (!Function->HasAllFunctionFlags(VerifyFlags))
		return false;

	auto p = FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
	FMemory::Memzero(p, Function->ParmsSize);
	FGMPNetBitReader Reader{PackageMap, const_cast<uint8*>(Buffer.GetData()), Buffer.Num() * 8};
	if (ArchiveToFrame(Reader, Function, p, PackageMap))
	{
		Obj->ProcessEvent(Function, p);
		DestroyFunctionParameters(Function, p);
		return true;
	}
	return false;
}

bool UGMPBPLib::CallEventDelegate(UObject* Obj, const FName EventName, const TArray<uint8>& Buffer, UPackageMap* PackageMap)
{
	using namespace GMP;
	FMulticastDelegateProperty* Prop = Obj ? FindFProperty<FMulticastDelegateProperty>(Obj->GetClass(), EventName) : nullptr;
	const FMulticastScriptDelegate* DelegateAddr = Prop ? Prop->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Obj) : nullptr;
	if (!ensureAlways(DelegateAddr))
		return false;

	auto Function = Prop->SignatureFunction;
	auto p = FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
	FMemory::Memzero(p, Function->ParmsSize);
	FGMPNetBitReader Reader{PackageMap, const_cast<uint8*>(Buffer.GetData()), Buffer.Num() * 8};
	if (ArchiveToFrame(Reader, Function, p, PackageMap))
	{
		DelegateAddr->ProcessMulticastDelegate<UObject>(p);
		DestroyFunctionParameters(Function, p);
		return true;
	}
	return false;
}

DECLARE_CYCLE_STAT(TEXT("Blueprint Time(GMP)"), STAT_BlueprintTimeGMP, STATGROUP_Game);

bool UGMPBPLib::CallMessageFunction(UObject* Obj, UFunction* Function, const TArray<FGMPTypedAddr>& Params, uint64 WritebackFlags)
{
	checkf(!Obj->IsUnreachable(), TEXT("%s  Function: '%s'"), *Obj->GetFullName(), *Function->GetPathName());
	checkf(!FUObjectThreadContext::Get().IsRoutingPostLoad, TEXT("Cannot call UnrealScript (%s - %s) while PostLoading objects"), *Obj->GetFullName(), *Function->GetFullName());

#if WITH_EDITORONLY_DATA
	// Cannot invoke script events when the game thread is paused for debugging.
	if (GIntraFrameDebuggingGameThread)
	{
		if (GFirstFrameIntraFrameDebugging)
		{
			GMP_WARNING(TEXT("Cannot call UnrealScript (%s - %s) while stopped at a breakpoint."), *Obj->GetFullName(), *Function->GetFullName());
		}

		return false;
	}
#endif  // WITH_EDITORONLY_DATA

	using namespace GMP;
	if (!ensureAlways(Obj && Function))
		return false;

#if DO_BLUEPRINT_GUARD || PER_FUNCTION_SCRIPT_STATS
	FBlueprintContextTracker& BlueprintContextTracker = FBlueprintContextTracker::Get();
	const int32 ProcessEventDepth = BlueprintContextTracker.GetScriptEntryTag();
	BlueprintContextTracker.EnterScriptContext(Obj, Function);
	ON_SCOPE_EXIT
	{
		BlueprintContextTracker.ExitScriptContext();
	};
#endif

#if PER_FUNCTION_SCRIPT_STATS
	static auto GMaxFunctionStatDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("bp.MaxFunctionStatDepth"));
	auto GMaxFunctionStatDepthCnt = GMaxFunctionStatDepth->GetInt();
	const bool bShouldTrackFunction = (GMaxFunctionStatDepthCnt == -1 || ProcessEventDepth < GMaxFunctionStatDepthCnt)
#if !UE_5_01_OR_LATER
									  && Stats::IsThreadCollectingData()
#endif
		;
	FScopeCycleCounterUObject FunctionScope(bShouldTrackFunction ? Function : nullptr);
#endif  // PER_FUNCTION_SCRIPT_STATS

#if STATS || ENABLE_STATNAMEDEVENTS
	static auto GVerboseScriptStats = IConsoleManager::Get().FindConsoleVariable(TEXT("bp.VerboseStats"));
	const bool bShouldTrackObject = !!GVerboseScriptStats->GetInt()
#if !UE_5_01_OR_LATER
									&& Stats::IsThreadCollectingData()
#endif
		;
	FScopeCycleCounterUObject ContextScope(bShouldTrackObject ? Obj : nullptr);
#endif

	void* Parms = nullptr;
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
	// Fast path for ubergraph calls
	int32 EventGraphParams;
	if (Function->EventGraphFunction != nullptr)
	{
		// Call directly into the event graph, skipping the stub thunk function
		EventGraphParams = Function->EventGraphCallOffset;
		Parms = &EventGraphParams;
		Function = Function->EventGraphFunction;

		// Validate assumptions required for this optimized path (EventGraphFunction should have only been filled out if these held)
		GMP_CHECK_SLOW(Function->ParmsSize == sizeof(EventGraphParams));
		GMP_CHECK_SLOW(Function->FirstPropertyToInit == nullptr);
		GMP_CHECK_SLOW(Function->PostConstructLink == nullptr);
	}
#endif

	if (!Parms)
	{
		Parms = FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
		FMemory::Memzero(Parms, Function->ParmsSize);
		if (!ensureAlways(MessageToFrame(Function, Parms, Params)))
			return false;
	}
	GMP_CHECK_SLOW((Function->ParmsSize == 0) || (Parms != nullptr));

	uint8* Frame = nullptr;
#if defined(USE_UBER_GRAPH_PERSISTENT_FRAME) && USE_UBER_GRAPH_PERSISTENT_FRAME
	if (Function->HasAnyFunctionFlags(FUNC_UbergraphFunction))
	{
		Frame = Function->GetOuterUClassUnchecked()->GetPersistentUberGraphFrame(Obj, Function);
	}
#endif
	const bool bUsePersistentFrame = (NULL != Frame);
	if (!bUsePersistentFrame)
	{
		Frame = (uint8*)FMemory_Alloca_Aligned(Function->PropertiesSize, Function->GetMinAlignment());
		// zero the local property memory
		FMemory::Memzero(Frame + Function->ParmsSize, Function->PropertiesSize - Function->ParmsSize);
	}

	// initialize the parameter properties
	FMemory::Memcpy(Frame, Parms, Function->ParmsSize);

	// Create a new local execution stack.
	FFrame NewStack(Obj, Function, Frame, nullptr, Reflection::GetFunctionChildProperties(Function));
	GMP_CHECK_SLOW(NewStack.Locals || Function->ParmsSize == 0);

	struct FFlagRestorer
	{
		FFlagRestorer(FProperty& InProp)
			: Prop(&InProp)
			, Flag(InProp.PropertyFlags)
		{
			InProp.SetPropertyFlags(CPF_OutParm | CPF_ReferenceParm);
		}
		~FFlagRestorer()
		{
			if (Prop)
				Prop->PropertyFlags = Flag;
		}

		FFlagRestorer(FFlagRestorer&& Other)
			: Prop(Other.Prop)
			, Flag(Other.Flag)
		{
			Other.Prop = nullptr;
		}

	private:
		FFlagRestorer& operator=(FFlagRestorer&& Other) = delete;
		FFlagRestorer(const FFlagRestorer&) = delete;
		FFlagRestorer& operator=(const FFlagRestorer&) = delete;
		FProperty* Prop;
		EPropertyFlags Flag;
	};

	// if (ensureWorld(Obj, Function->HasAnyFunctionFlags(FUNC_HasOutParms)))
	if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
	{
		FOutParmRec** LastOut = &NewStack.OutParms;
		GMP_CHECK(Function->NumParms <= 64);
		uint64 Idx = 1;
		for (FProperty* Property = (FProperty*)(Function->ChildProperties); Property && (Property->PropertyFlags & (CPF_Parm)) == CPF_Parm; Property = (FProperty*)Property->Next)
		{
			// this is used for optional parameters - the destination address for out parameter values is the address of the calling function
			// so we'll need to know which address to use if we need to evaluate the default parm value expression located in the new function's bytecode
			if (!Property->HasAnyPropertyFlags(CPF_OutParm))
				continue;

			ensureWorld(Obj, Property->HasAnyPropertyFlags(CPF_OutParm));
			CA_SUPPRESS(6263)
			FOutParmRec* Out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
			// set the address and property in the out param info
			// note that since C++ doesn't support "optional out" we can ignore that here
			Out->PropAddr = Property->ContainerPtrToValuePtr<uint8>(Parms);
			Out->Property = Property;

			// add the new out param info to the stack frame's linked list
			if (*LastOut)
			{
				(*LastOut)->NextOutParm = Out;
				LastOut = &(*LastOut)->NextOutParm;
			}
			else
			{
				*LastOut = Out;
			}
			Idx <<= 1;
		}

		// set the next pointer of the last item to NULL to mark the end of the list
		if (*LastOut)
		{
			(*LastOut)->NextOutParm = nullptr;
		}
	}

	if (!bUsePersistentFrame)
	{
		for (FProperty* LocalProp = Function->FirstPropertyToInit; LocalProp != NULL; LocalProp = (FProperty*)LocalProp->Next)
		{
			LocalProp->InitializeValue_InContainer(NewStack.Locals);
		}
	}

	const bool bHasReturnParam = Function->ReturnValueOffset != MAX_uint16;
	uint8* ReturnValueAddress = bHasReturnParam ? ((uint8*)Parms + Function->ReturnValueOffset) : nullptr;
	// Call native function or UObject::ProcessInternal.
	Function->Invoke(Obj, NewStack, ReturnValueAddress);

	if (!bUsePersistentFrame)
	{
		static auto GetElementSize = [](FProperty* Prop) {
#if UE_5_05_OR_LATER
			return Prop->GetElementSize();
#else
			return Prop->ElementSize;
#endif
		};
		// Destroy local variables except function parameters.!! see also UObject::CallFunctionByNameWithArguments
		// also copy back constructed value params here so the correct copy is destroyed when the event function returns
		for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
		{
			if (!P->IsInContainer(Function->ParmsSize))
			{
				P->DestroyValue_InContainer(NewStack.Locals);
			}
			else if (!(P->PropertyFlags & CPF_OutParm))
			{
				FMemory::Memcpy(P->ContainerPtrToValuePtr<uint8>(Parms), P->ContainerPtrToValuePtr<uint8>(NewStack.Locals), P->ArrayDim * GetElementSize(P));
			}
		}
	}
	return true;
}

bool UGMPBPLib::ArchiveToMessage(const TArray<uint8>& Buffer, GMP::FTypedAddresses& Params, const TArray<FProperty*>& Props, UPackageMap* PackageMap)
{
	using namespace GMP;
	GMP_CHECK(Params.Num() == Props.Num());

	FGMPNetBitReader Reader{PackageMap, const_cast<uint8*>(Buffer.GetData()), Buffer.Num() * 8};
	for (auto i = 0; i < Props.Num(); ++i)
	{
		auto& Prop = Props[i];
		if (!ensureWorld(PackageMap, UGMPBPLib::NetSerializeProperty(Reader, Prop, Params[i].InitializeValue(Prop), PackageMap)))
		{
			for (auto j = i; j >= 0; --j)
			{
				Props[j]->DestroyValue_InContainer(Params[j].ToAddr());
			}
			return false;
		}
	}
	return true;
}

UWorld* UBlueprintableObject::GetWorld() const
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (auto Outer = GetOuter())
		{
			if (!Outer->HasAnyFlags(RF_BeginDestroyed) && !Outer->IsUnreachable())
				return Outer->GetWorld();
		}
	}

#if WITH_EDITOR
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext(false).World();
	}
#endif
	return nullptr;
}

namespace FormatPlaceholdersExtracter
{

/** Token representing a literal string inside the string */
struct FStringLiteral
{
	FStringLiteral(const FStringToken& InString)
		: String(InString)
		, Len(UE_PTRDIFF_TO_INT32(InString.GetTokenEndPos() - InString.GetTokenStartPos()))
	{
	}
	/** The string literal token */
	FStringToken String;
	/** Cached length of the string */
	int32 Len;
};

/** Token representing a user-defined token, such as {Argument} */
struct FFormatSpecifier
{
	FFormatSpecifier(const FStringToken& InIdentifier, const FStringToken& InEntireToken)
		: Identifier(InIdentifier)
		, EntireToken(InEntireToken)
		, Len(UE_PTRDIFF_TO_INT32(Identifier.GetTokenEndPos() - Identifier.GetTokenStartPos()))
	{
	}

	/** The identifier part of the token */
	FStringToken Identifier;
	/** The entire token */
	FStringToken EntireToken;
	/** Cached length of the identifier */
	int32 Len;
};

/** Token representing a user-defined index token, such as {0} */
struct FIndexSpecifier
{
	FIndexSpecifier(int32 InIndex, const FStringToken& InEntireToken)
		: Index(InIndex)
		, EntireToken(InEntireToken)
	{
	}

	/** The index of the parsed token */
	int32 Index;
	/** The entire token */
	FStringToken EntireToken;
};

/** Token representing an escaped character */
struct FEscapedCharacter
{
	FEscapedCharacter(TCHAR InChar)
		: Character(InChar)
	{
	}

	/** The character that was escaped */
	TCHAR Character;
};

DEFINE_EXPRESSION_NODE_TYPE(FormatPlaceholdersExtracter::FStringLiteral, 0x7F2A8B44, 0x1C9F62E5, 0x9B3E471A, 0x85D60C93)
DEFINE_EXPRESSION_NODE_TYPE(FormatPlaceholdersExtracter::FFormatSpecifier, 0x2E7B5F19, 0xA4C83B76, 0x6D25E8F2, 0x49B1A037)
DEFINE_EXPRESSION_NODE_TYPE(FormatPlaceholdersExtracter::FIndexSpecifier, 0x91C4D682, 0x5E31B9A8, 0x3F7C0E45, 0xB68F92D1)
DEFINE_EXPRESSION_NODE_TYPE(FormatPlaceholdersExtracter::FEscapedCharacter, 0x63B82C97, 0xF2459D13, 0x8A1E6B4C, 0x47D35E80)

using namespace ExpressionParser;
static FExpressionError GenerateErrorMsg(const FStringToken& Token)
{
	FFormatOrderedArguments Args;
	Args.Add(FText::FromString(FString(Token.GetTokenEndPos()).Left(10) + TEXT("...")));
	return FExpressionError(FText::Format(NSLOCTEXT("GMP", "InvalidTokenDefinition", "Invalid token definition at '{0}'"), Args));
}
TOptional<FExpressionError> ParseIndex(FExpressionTokenConsumer& Consumer, bool bEmitErrors)
{
	auto& Stream = Consumer.GetStream();

	TOptional<FStringToken> OpeningChar = Stream.ParseSymbol(TEXT('{'));
	if (!OpeningChar.IsSet())
	{
		return TOptional<FExpressionError>();
	}

	FStringToken& EntireToken = OpeningChar.GetValue();

	// Optional whitespace
	Stream.ParseToken([](TCHAR InC) { return FChar::IsWhitespace(InC) ? EParseState::Continue : EParseState::StopBefore; }, &EntireToken);

	// The identifier itself
	TOptional<int32> Index;
	Stream.ParseToken(
		[&](TCHAR InC) {
			if (FChar::IsDigit(InC))
			{
				if (!Index.IsSet())
				{
					Index = 0;
				}
				Index.GetValue() *= 10;
				Index.GetValue() += InC - '0';
				return EParseState::Continue;
			}
			return EParseState::StopBefore;
		},
		&EntireToken);

	if (!Index.IsSet())
	{
		// Not a valid token
		if (bEmitErrors)
		{
			return GenerateErrorMsg(EntireToken);
		}
		else
		{
			return TOptional<FExpressionError>();
		}
	}

	// Optional whitespace
	Stream.ParseToken([](TCHAR InC) { return FChar::IsWhitespace(InC) ? EParseState::Continue : EParseState::StopBefore; }, &EntireToken);

	if (!Stream.ParseSymbol(TEXT('}'), &EntireToken).IsSet())
	{
		// Not a valid token
		if (bEmitErrors)
		{
			return GenerateErrorMsg(EntireToken);
		}
		else
		{
			return TOptional<FExpressionError>();
		}
	}

	// Add the token to the consumer. This moves the read position in the stream to the end of the token.
	Consumer.Add(EntireToken, FIndexSpecifier(Index.GetValue(), EntireToken));
	return TOptional<FExpressionError>();
}
static TOptional<FExpressionError> ParseSpecifier(FExpressionTokenConsumer& Consumer, bool bEmitErrors)
{
	auto& Stream = Consumer.GetStream();

	TOptional<FStringToken> OpeningChar = Stream.ParseSymbol(TEXT('{'));
	if (!OpeningChar.IsSet())
	{
		return TOptional<FExpressionError>();
	}

	FStringToken& EntireToken = OpeningChar.GetValue();

	// Optional whitespace
	Stream.ParseToken([](TCHAR InC) { return FChar::IsWhitespace(InC) ? EParseState::Continue : EParseState::StopBefore; }, &EntireToken);

	// The identifier itself
	TOptional<FStringToken> Identifier = Stream.ParseToken(
		[](TCHAR InC) {
			if (FChar::IsWhitespace(InC) || InC == '}')
			{
				return EParseState::StopBefore;
			}
			else if (FChar::IsIdentifier(InC))
			{
				return EParseState::Continue;
			}
			else
			{
				return EParseState::Cancel;
			}
		},
		&EntireToken);

	if (!Identifier.IsSet())
	{
		// Not a valid token
		// Not a valid token
		if (bEmitErrors)
		{
			return GenerateErrorMsg(EntireToken);
		}
		else
		{
			return TOptional<FExpressionError>();
		}
	}

	// Optional whitespace
	Stream.ParseToken([](TCHAR InC) { return FChar::IsWhitespace(InC) ? EParseState::Continue : EParseState::StopBefore; }, &EntireToken);

	if (!Stream.ParseSymbol(TEXT('}'), &EntireToken).IsSet())
	{
		// Not a valid token
		if (bEmitErrors)
		{
			return GenerateErrorMsg(EntireToken);
		}
		else
		{
			return TOptional<FExpressionError>();
		}
	}

	// Add the token to the consumer. This moves the read position in the stream to the end of the token.
	Consumer.Add(EntireToken, FFormatSpecifier(Identifier.GetValue(), EntireToken));
	return TOptional<FExpressionError>();
}

static const TCHAR EscapeChar = TEXT('`');

/** Parse an escaped character */
static TOptional<FExpressionError> ParseEscapedChar(FExpressionTokenConsumer& Consumer, bool bEmitErrors)
{
	static const TCHAR* ValidEscapeChars = TEXT("{`");

	TOptional<FStringToken> Token = Consumer.GetStream().ParseSymbol(EscapeChar);
	if (!Token.IsSet())
	{
		return TOptional<FExpressionError>();
	}

	// Accumulate the next character into the token
	TOptional<FStringToken> EscapedChar = Consumer.GetStream().ParseSymbol(&Token.GetValue());
	if (!EscapedChar.IsSet())
	{
		return TOptional<FExpressionError>();
	}

	// Check for a valid escape character
	const TCHAR Character = *EscapedChar->GetTokenStartPos();
	if (FCString::Strchr(ValidEscapeChars, Character))
	{
		// Add the token to the consumer. This moves the read position in the stream to the end of the token.
		Consumer.Add(Token.GetValue(), FEscapedCharacter(Character));
		return TOptional<FExpressionError>();
	}
	else if (bEmitErrors)
	{
		FString CharStr;
		CharStr += Character;
		FFormatOrderedArguments Args;
		Args.Add(FText::FromString(CharStr));
		return FExpressionError(FText::Format(NSLOCTEXT("GMP", "InvalidEscapeCharacter", "Invalid escape character '{0}'"), Args));
	}
	else
	{
		return TOptional<FExpressionError>();
	}
}

/** Parse anything until we find an unescaped { */
static TOptional<FExpressionError> ParseLiteral(FExpressionTokenConsumer& Consumer, bool bEmitErrors)
{
	// Include a leading { character - if it was a valid argument token it would have been picked up by a previous token definition
	bool bFirstChar = true;
	TOptional<FStringToken> Token = Consumer.GetStream().ParseToken([&](TCHAR C) {
		if (C == '{' && !bFirstChar)
		{
			return EParseState::StopBefore;
		}
		else if (C == EscapeChar)
		{
			return EParseState::StopBefore;
		}
		else
		{
			bFirstChar = false;
			// Keep consuming
			return EParseState::Continue;
		}
	});

	if (Token.IsSet())
	{
		// Add the token to the consumer. This moves the read position in the stream to the end of the token.
		Consumer.Add(Token.GetValue(), FStringLiteral(Token.GetValue()));
	}
	return TOptional<FExpressionError>();
}

const FTokenDefinitions& GetNamedDefinitions()
{
	static FTokenDefinitions NamedDefinitions;
	if (TrueOnFirstCall([] {}))
	{
		NamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseSpecifier(Consumer, false); });
		NamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseEscapedChar(Consumer, false); });
		NamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseLiteral(Consumer, false); });
	}
	return NamedDefinitions;
}
const FTokenDefinitions& GetStrictNamedDefinitions()
{
	static FTokenDefinitions StrictNamedDefinitions;
	if (TrueOnFirstCall([] {}))
	{
		StrictNamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseSpecifier(Consumer, true); });
		StrictNamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseEscapedChar(Consumer, true); });
		StrictNamedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseLiteral(Consumer, true); });
	}
	return StrictNamedDefinitions;
}
const FTokenDefinitions& GetOrderedDefinitions()
{
	static FTokenDefinitions OrderedDefinitions;
	if (TrueOnFirstCall([] {}))
	{
		OrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseIndex(Consumer, false); });
		OrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseEscapedChar(Consumer, false); });
		OrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseLiteral(Consumer, false); });
		OrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseEscapedChar(Consumer, true); });
	}
	return OrderedDefinitions;
}
const FTokenDefinitions& GetStrictOrderedDefinitions()
{
	static FTokenDefinitions StrictOrderedDefinitions;
	if (TrueOnFirstCall([] {}))
	{
		StrictOrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseIndex(Consumer, true); });
		StrictOrderedDefinitions.DefineToken([](FExpressionTokenConsumer& Consumer) { return ParseLiteral(Consumer, true); });
	}
	return StrictOrderedDefinitions;
}

auto ExtractNames(const TCHAR* Expr, bool bStrict = true)
{
	return ExpressionParser::Lex(Expr, bStrict ? GetStrictNamedDefinitions() : GetNamedDefinitions());
}
auto ExtractOrders(const TCHAR* Expr, bool bStrict = true)
{
	return ExpressionParser::Lex(Expr, bStrict ? GetStrictOrderedDefinitions() : GetOrderedDefinitions());
}

void AppendPropPairToString(TPair<FProperty*, uint8*>& Pair, FString& Out)
{
	auto CurProp = Pair.Key;
	auto CurAddr = Pair.Value;
	if (auto StrProp = CastField<FStrProperty>(CurProp))
	{
		Out.Append(*reinterpret_cast<FString*>(CurAddr));
	}
	else if (auto NameProp = CastField<FNameProperty>(CurProp))
	{
		Out.Append(reinterpret_cast<FName*>(CurAddr)->ToString());
	}
	else if (auto TextProp = CastField<FTextProperty>(CurProp))
	{
		Out.Append(reinterpret_cast<FText*>(CurAddr)->ToString());
	}
	else if (auto BoolProp = CastField<FBoolProperty>(CurProp))
	{
		Out.Append(BoolProp->GetPropertyValue(CurAddr) ? TEXT("true") : TEXT("false"));
	}
	else if (auto EnumProp = CastField<FEnumProperty>(CurProp))
	{
		auto EnumVal = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(CurAddr);
		auto EnumStr = EnumProp->GetEnum()->GetNameStringByValue(EnumVal);
		Out.Append(EnumStr);
	}
	else if (auto NumProp = CastField<FNumericProperty>(CurProp))
	{
		if (NumProp->IsFloatingPoint())
			Out.Append(LexToString(NumProp->GetFloatingPointPropertyValue(CurAddr)));
		else if (NumProp->IsA<FUInt64Property>())
			Out.Append(LexToString(NumProp->GetUnsignedIntPropertyValue(CurAddr)));
		else
			Out.Append(LexToString(NumProp->GetSignedIntPropertyValue(CurAddr)));
	}
	else if (auto ObjProp = CastField<FObjectPropertyBase>(CurProp))
	{
		auto Obj = ObjProp->GetObjectPropertyValue(CurAddr);
		Out.Append(GetNameSafe(Obj));
	}
	else  // if (auto StructProp = CastField<FStructProperty>(CurProp))
	{
		CurProp->ExportText_Direct(Out, CurAddr, nullptr, nullptr, PPF_None);
	}
}
}  // namespace FormatPlaceholdersExtracter

DEFINE_FUNCTION(UGMPBPLib::execFormatStringByOrder)
{
	P_GET_PROPERTY_REF(FStrProperty, FmtStr);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Fatal, TEXT("version not supported"));
	P_FINISH
	return;
#else
	P_NATIVE_BEGIN
	TArray<TPair<FProperty*, uint8*>> Props;
	const auto InExpression = *FmtStr;
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);
#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif
		Props.Add(TPair<FProperty*, uint8*>{Stack.MostRecentProperty, Stack.MostRecentPropertyAddress});
	}
	P_FINISH
	TValueOrError<TArray<FExpressionToken>, FExpressionError> Result = FormatPlaceholdersExtracter::ExtractOrders(InExpression);
	if (!Result.IsValid())
	{
		FFrame::KismetExecutionMessage(TEXT("FmtStr Invalid"), ELogVerbosity::Error, TEXT("FmtStr Invalid"));
		*(FString*)RESULT_PARAM = FmtStr;
	}
	else
	{
		using namespace FormatPlaceholdersExtracter;
		TArray<FExpressionToken>& Tokens = Result.GetValue();
		if (Tokens.Num() == 0)
		{
			*(FString*)RESULT_PARAM = FmtStr;
		}
		else
		{
			FString Formatted;
			Formatted.Reserve(UE_PTRDIFF_TO_INT32(Tokens.Last().Context.GetTokenEndPos() - InExpression));
			auto Index = 0;
			for (const FExpressionToken& Token : Tokens)
			{
				auto CurIndex = Index++;
				if (const FStringLiteral* Literal = Token.Node.Cast<FStringLiteral>())
				{
					Formatted.AppendChars(Literal->String.GetTokenStartPos(), Literal->Len);
				}
				else if (const FEscapedCharacter* Escaped = Token.Node.Cast<FEscapedCharacter>())
				{
					Formatted.AppendChar(Escaped->Character);
				}
				else if (const FIndexSpecifier* IndexToken = Token.Node.Cast<FIndexSpecifier>())
				{
					if (Props.IsValidIndex(IndexToken->Index))
					{
						AppendPropPairToString(Props[IndexToken->Index], Formatted);
					}
					else
					{
						// No replacement found, so just add the original token string
						const int32 Length = UE_PTRDIFF_TO_INT32(IndexToken->EntireToken.GetTokenEndPos() - IndexToken->EntireToken.GetTokenStartPos());
						Formatted.AppendChars(IndexToken->EntireToken.GetTokenStartPos(), Length);
					}
				}
			}
			*(FString*)RESULT_PARAM = MoveTemp(Formatted);
		}
	}
	P_NATIVE_END
#endif
}

DEFINE_FUNCTION(UGMPBPLib::execFormatStringByName)
{
	P_GET_PROPERTY_REF(FStrProperty, FmtStr);
	P_GET_TARRAY_REF(FString, Names);

#if !GMP_WITH_VARIADIC_SUPPORT
	FFrame::KismetExecutionMessage(TEXT("version not supported"), ELogVerbosity::Fatal, TEXT("version not supported"));
	P_FINISH
	return;
#else
	P_NATIVE_BEGIN
	TArray<TPair<FProperty*, uint8*>> Props;
	const auto InExpression = *FmtStr;
	while (Stack.PeekCode() != EX_EndFunctionParms)
	{
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentProperty = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);
#if GMP_DEBUGGAME
		ensureAlways(Stack.MostRecentProperty && Stack.MostRecentPropertyAddress);
#endif
		Props.Add(TPair<FProperty*, uint8*>{Stack.MostRecentProperty, Stack.MostRecentPropertyAddress});
	}
	P_FINISH
	TValueOrError<TArray<FExpressionToken>, FExpressionError> Result = FormatPlaceholdersExtracter::ExtractNames(InExpression);
	if (!Result.IsValid())
	{
		FFrame::KismetExecutionMessage(TEXT("FmtStr Invalid"), ELogVerbosity::Error, TEXT("FmtStr Invalid"));
		*(FString*)RESULT_PARAM = FmtStr;
	}
	else
	{
		using namespace FormatPlaceholdersExtracter;
		TArray<FExpressionToken>& Tokens = Result.GetValue();
		if (Tokens.Num() == 0)
		{
			*(FString*)RESULT_PARAM = FmtStr;
		}
		else
		{
			// This code deliberately tries to reallocate as little as possible
			FString Formatted;
			Formatted.Reserve(UE_PTRDIFF_TO_INT32(Tokens.Last().Context.GetTokenEndPos() - InExpression));
			auto Index = 0;
			for (const FExpressionToken& Token : Tokens)
			{
				if (const FStringLiteral* Literal = Token.Node.Cast<FStringLiteral>())
				{
					Formatted.AppendChars(Literal->String.GetTokenStartPos(), Literal->Len);
				}
				else if (const FEscapedCharacter* Escaped = Token.Node.Cast<FEscapedCharacter>())
				{
					Formatted.AppendChar(Escaped->Character);
				}
				else if (const FFormatSpecifier* FormatToken = Token.Node.Cast<FFormatSpecifier>())
				{
					TPair<FProperty*, uint8*>* Prop = nullptr;
					for (auto i = 0; i < Names.Num(); ++i)
					{
						auto& Name = Names[i];
						if (Name.Len() == FormatToken->Len && FCString::Strnicmp(FormatToken->Identifier.GetTokenStartPos(), *Name, FormatToken->Len) == 0 && Props.IsValidIndex(i))
						{
							Prop = &Props[i];
							break;
						}
					}

					if (Prop)
					{
						AppendPropPairToString(*Prop, Formatted);
					}
					else
					{
						const int32 Length = UE_PTRDIFF_TO_INT32(FormatToken->EntireToken.GetTokenEndPos() - FormatToken->EntireToken.GetTokenStartPos());
						Formatted.AppendChars(FormatToken->EntireToken.GetTokenStartPos(), Length);
					}
				}
			}
			*(FString*)RESULT_PARAM = MoveTemp(Formatted);
		}
	}
	P_NATIVE_END
#endif
}

FString UGMPBPLib::FormatStringByNameLegacy(const FString& FmtStr, const TMap<FString, FString>& InArgs)
{
	FStringFormatNamedArguments Arguments;
	Arguments.Reserve(InArgs.Num());
	for (auto& Pair : InArgs)
		Arguments.Add(Pair.Key, Pair.Value);
	return FString::Format(*FmtStr, Arguments);
}
bool UGMPBPLib::IsListenServer(UObject* InCtx)
{
	return InCtx && InCtx->GetWorld() && InCtx->GetWorld()->GetNetMode() == NM_ListenServer;
}
