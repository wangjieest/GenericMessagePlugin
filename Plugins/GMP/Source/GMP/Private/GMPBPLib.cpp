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
#if WITH_EDITOR
#include "UnrealEd.h"
#endif
#include "HAL/IConsoleManager.h"

//////////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY(LogGMP);
namespace GMP
{
int32 GetPropertyCustomIndex(FProperty* Property)
{
	EClassCastFlags Flag = GetPropertyCastFlags(Property);

#define ELSE_FLAG_CHECK(Mask) \
	else if (Flag & CASTCLASS_F##Mask##Property) { return TypeTraits::ToUnderlying(EGMPPropertyClass::Mask); }

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
	else if (Flag & CASTCLASS_FMulticastInlineDelegateProperty) { return TypeTraits::ToUnderlying(EGMPPropertyClass::InlineMulticastDelegate); }
	else if (Flag & CASTCLASS_FMulticastSparseDelegateProperty) { return TypeTraits::ToUnderlying(EGMPPropertyClass::SparseMulticastDelegate); }
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

FORCEINLINE void BPLibNotifyMessage(const FString& MessageId, const FGMPObjNamePair& SigPair, FTypedAddresses& Params, uint8 Type, UGMPManager* Mgr)
{
	do
	{
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
		Mgr->GetHub().ScriptNotifyMessage(MessageId, Params, SigSource);
	} while (0);
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
static FAutoConsoleVariableRef CVar_DrawAbilityVisualizer(TEXT("x.LogGMPBPExecution"), bLogGMPBPExecution, TEXT("log each blueprint gmp exectuion"), ECVF_Default);
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

void UGMPBPLib::NotifyMessageByKey(const FString& MessageId, const FGMPObjNamePair& SigSource, TArray<FGMPTypedAddr>& Params, uint8 Type, UGMPManager* Mgr)
{
	using namespace GMP;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		FTypedAddresses Arr(Params);
		BPLibNotifyMessage(MessageId, SigSource, Arr, Type, Mgr);
	}
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
		Mgr->GetHub().ScriptResponeMessage(RspKey, Arr, SigSource);
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
		Mgr->GetHub().ScriptResponeMessage(RspKey, Params, SigSource);
	}
	P_NATIVE_END
#endif
}

FGMPTypedAddr UGMPBPLib::ListenMessageByKey(FName MessageKey, const FGMPScriptDelegate& Delegate, int32 Times, int32 Order, uint8 Type, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
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
		auto Id = Mgr->GetHub().ScriptListenMessage(
			SigSource,
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
	if (!Mgr->GetHub().IsSignatureCompatible(false, MessageKey, FArrayTypeNames(ArgNames), OldParams, false))
	{
		ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Listen %s"), *MessageKey.ToString());
		return FGMPTypedAddr{0};
	}
#endif
	return ListenMessageByKey(MessageKey, Delegate, Times, Order, Type, Mgr, SigPair);
}

FGMPTypedAddr UGMPBPLib::ListenMessageViaKey(UObject* Listener, FName MessageKey, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
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
		auto SigSource = GMP::FSigSource::MakeObjNameFilter(SigPair.Obj, SigPair.TagName);
#if GMP_WITH_DYNAMIC_CALL_CHECK
		if (Mgr->GetHub().IsAlive(MessageKey, Listener, SigSource))
		{
			auto DebugStr = FString::Printf(TEXT("%s<-%s.%s"), *MessageKey.ToString(), *GetNameSafe(Listener), *EventName.ToString());
			static bool AssetFlag = false;
			ensureWorldMsgf(World, AssetFlag, TEXT("%s"), *DebugStr);
			break;
		}
#endif
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

FGMPTypedAddr UGMPBPLib::ListenMessageViaKeyValidate(const TArray<FName>& ArgNames, UObject* Listener, FName MessageKey, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& SigPair)
{
#if GMP_WITH_DYNAMIC_CALL_CHECK
	using namespace GMP;
	Mgr = Mgr ? Mgr : FMessageUtils::GetManager();
	const FArrayTypeNames* OldParams = nullptr;
	if (!Mgr->GetHub().IsSignatureCompatible(false, MessageKey, FArrayTypeNames(ArgNames), OldParams, false))
	{
		ensureAlwaysMsgf(false, TEXT("SignatureMismatch On Listen %s"), *MessageKey.ToString());
		return FGMPTypedAddr{0};
	}
#endif
	return ListenMessageViaKey(Listener, MessageKey, EventName, Times, Order, Type, BodyDataMask, Mgr, SigPair);
}

static FGMPKey RequestMessageImpl(FGMPKey& RspKey, FName EventName, const FString& MessageKey, UObject* Sender, GMP::FTypedAddresses& Params, uint8 Type, UGMPManager* Mgr)
{
	using namespace GMP;
	RspKey = 0;
	do
	{
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

		RspKey = Mgr->GetHub().ScriptRequestMessage(MessageKey, Params, MoveTemp(RspLambda), Sender);
	} while (0);
	return RspKey;
}

void UGMPBPLib::RequestMessage(FGMPKey& RspKey, FName EventName, const FString& MessageKey, UObject* Listener, TArray<FGMPTypedAddr>& Params, uint8 Type, UGMPManager* Mgr)
{
	GMP::FTypedAddresses RspParams{Params};
	RequestMessageImpl(RspKey, EventName, MessageKey, Listener, RspParams, Type, Mgr);
}

DEFINE_FUNCTION(UGMPBPLib::execRequestMessageVariadic)
{
	using namespace GMP;
	P_GET_STRUCT_REF(FGMPKey, RspKey);
	PARAM_PASSED_BY_VAL(EventName, FNameProperty, FName);
	P_GET_PROPERTY(FStrProperty, MessageKey);
	P_GET_OBJECT(UObject, SigSource);
	P_GET_PROPERTY(FByteProperty, Type);
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
	RequestMessageImpl(RspKey, EventName, MessageKey, SigSource, Params, Type, Mgr);
	P_NATIVE_END
#endif
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

DEFINE_FUNCTION(UGMPBPLib::execNotifyMessageByKeyVariadic)
{
	using namespace GMP;
	P_GET_PROPERTY(FStrProperty, MessageId);
	P_GET_STRUCT_REF(FGMPObjNamePair, SigSource);
	P_GET_PROPERTY(FByteProperty, Type);
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
	BPLibNotifyMessage(MessageId, SigSource, Params, Type, Mgr);
	P_NATIVE_END
#endif
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
	P_FINISH
	P_NATIVE_END
	CallMessageFunction(Obj, Obj ? Obj->FindFunction(FuncName) : nullptr, MsgArr);
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
	P_FINISH
	P_NATIVE_END
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
			UE_LOG(LogGMP, Warning, TEXT("Cannot call UnrealScript (%s - %s) while stopped at a breakpoint."), *Obj->GetFullName(), *Function->GetFullName());
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
	ON_SCOPE_EXIT { BlueprintContextTracker.ExitScriptContext(); };
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
		// Destroy local variables except function parameters.!! see also UObject::CallFunctionByNameWithArguments
		// also copy back constructed value parms here so the correct copy is destroyed when the event function returns
		for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
		{
			if (!P->IsInContainer(Function->ParmsSize))
			{
				P->DestroyValue_InContainer(NewStack.Locals);
			}
			else if (!(P->PropertyFlags & CPF_OutParm))
			{
				FMemory::Memcpy(P->ContainerPtrToValuePtr<uint8>(Parms), P->ContainerPtrToValuePtr<uint8>(NewStack.Locals), P->ArrayDim * P->ElementSize);
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

DEFINE_FUNCTION(UGMPBPLib::execFormatStringVariadic)
{
	FStringFormatOrderedArguments Arguments;

	P_GET_PROPERTY_REF(FStrProperty, FmtStr);

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

		auto CurProp = Stack.MostRecentProperty;
		if (auto StrProp = CastField<FStrProperty>(CurProp))
		{
			Arguments.Add(*reinterpret_cast<FString*>(Stack.MostRecentPropertyAddress));
		}
		else if (auto NameProp = CastField<FNameProperty>(CurProp))
		{
			Arguments.Add(reinterpret_cast<FName*>(Stack.MostRecentPropertyAddress)->ToString());
		}
		else if (auto TextProp = CastField<FTextProperty>(CurProp))
		{
			Arguments.Add(reinterpret_cast<FText*>(Stack.MostRecentPropertyAddress)->ToString());
		}
		else if (auto EnumProp = CastField<FEnumProperty>(CurProp))
		{
			auto EnumVal = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Stack.MostRecentPropertyAddress);
#if 0
			Arguments.Add(EnumVal);
#else
			auto EnumStr = EnumProp->GetEnum()->GetNameStringByValue(EnumVal);
			Arguments.Add(EnumStr);
#endif
		}
		else if (auto NumProp = CastField<FNumericProperty>(CurProp))
		{
			if (NumProp->IsFloatingPoint())
				Arguments.Add(NumProp->GetFloatingPointPropertyValue(Stack.MostRecentPropertyAddress));
			else if (NumProp->IsA<FUInt64Property>())
				Arguments.Add(NumProp->GetUnsignedIntPropertyValue(Stack.MostRecentPropertyAddress));
			else
				Arguments.Add(NumProp->GetSignedIntPropertyValue(Stack.MostRecentPropertyAddress));
		}
		else if (auto ObjProp = CastField<FObjectPropertyBase>(CurProp))
		{
			auto Obj = ObjProp->GetObjectPropertyValue(Stack.MostRecentPropertyAddress);
			Arguments.Add(GetNameSafe(Obj));
		}
		else  // if (auto StructProp = CastField<FStructProperty>(CurProp))
		{
			FString Str;
			CurProp->ExportText_Direct(Str, Stack.MostRecentPropertyAddress, nullptr, nullptr, PPF_None);
		}
	}
	P_FINISH

	*(FString*)RESULT_PARAM = FString::Format(*FmtStr, Arguments);
	P_NATIVE_END
#endif
}

FString UGMPBPLib::FormatStringByName(const FString& FmtStr, const TMap<FString, FString>& InArgs)
{
	FStringFormatNamedArguments Arguments;
	Arguments.Reserve(InArgs.Num());
	for (auto& Pair : InArgs)
		Arguments.Add(Pair.Key, Pair.Value);
	return FString::Format(*FmtStr, Arguments);
}
