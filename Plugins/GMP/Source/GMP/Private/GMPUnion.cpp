//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPUnion.h"

#if UE_5_05_OR_LATER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#include "GMPClass2Prop.h"
#include "GMPReflection.h"
#include "Misc/AsciiSet.h"
#include "Misc/ScopeExit.h"

#if UE_4_24_OR_LATER
#include "Net/Core/PushModel/PushModel.h"
#endif
#include "GMPRpcProxy.h"

#if WITH_EDITOR
namespace GMP
{
namespace StructUnionUtils
{
	static TMap<TWeakObjectPtr<UScriptStruct>, FName> RegClasses;

	GMP_API bool MatchGMPStructUnionCategory(const UScriptStruct* InStruct, FName Category)
	{
		if (InStruct->IsA<UUserDefinedStruct>())
		{
			return true;
		}
		if (!RegClasses.Num() && Category.IsNone() && InStruct && InStruct->IsChildOf(FGMPStructBase::StaticStruct()) && !InStruct->HasMetaData(TEXT("BlueprintInternalUseOnly")))
			return true;

		TWeakObjectPtr<UScriptStruct> Key = const_cast<UScriptStruct*>(InStruct);
		auto Found = RegClasses.Find(Key);
		return Found && (Category.IsNone() || *Found == Category);
	}
}  // namespace StructUnionUtils
}  // namespace GMP
#endif

#define GMP_STACK_STRUCT_ARRAY(Type, Val, ArrayNum)                                                          \
	auto Val = (uint8*)FMemory_Alloca_Aligned(Type->GetStructureSize() * ArrayNum, Type->GetMinAlignment()); \
	auto GMPStructScope = FGMPStructUnion::ScopeStackStruct(Val, Type, ArrayNum)

#define GMP_STACK_STRUCT(Type, Val) GMP_STACK_STRUCT_ARRAY(Type, Val, 1)

FArchive& operator<<(FArchive& Ar, FGMPStructBase& InStruct)
{
	if (Ar.IsLoading())
	{
		FString StructPath;
		Ar << StructPath;
		if (ensureAlways(!StructPath.IsEmpty()))
		{
			UScriptStruct* ScriptStructPtr = FindObject<UScriptStruct>(nullptr, *StructPath, false);
			if (!ensure(ScriptStructPtr && ScriptStructPtr->IsChildOf(InStruct.GetScriptStruct())))
			{
				Ar.SetError();
				return Ar;
			}
			ScriptStructPtr->InitializeStruct(&InStruct);
			ScriptStructPtr->SerializeItem(Ar, &InStruct, nullptr);
		}
	}
	else
	{
		FString StructPath;
		UScriptStruct* ScriptStructPtr = InStruct.GetScriptStruct();
		if (ensureAlways(ScriptStructPtr))
		{
			StructPath = ScriptStructPtr->GetPathName();
			Ar << StructPath;
			ScriptStructPtr->SerializeItem(Ar, &InStruct, nullptr);
		}
		else
		{
			Ar << StructPath;
		}
	}

	return Ar;
}

void UGMPDynStructStorage::RegisterTypeImpl(UScriptStruct* InStructType, FName Category)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (InStructType)
			GMP::StructUnionUtils::RegClasses.FindOrAdd(InStructType) = Category;
	}
#endif
}

void UGMPDynStructStorage::BeginDestroy()
{
	StructUnion.Reset();
	Super::BeginDestroy();
}

DEFINE_FUNCTION(UGMPStructLib::execClearStructUnion)
{
	P_GET_STRUCT_REF(FGMPStructUnion, DynStruct);
	DynStruct.Reset();
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execSetStructUnion)
{
	P_GET_STRUCT_REF(FGMPStructUnion, DynStruct);
	P_GET_OBJECT(UScriptStruct, StructType);
	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	Stack.StepCompiledIn<FStructProperty>(DynStruct.EnsureMemory(StructType, ArrayNum));
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execMakeStructUnion)
{
	reinterpret_cast<FGMPStructUnion*>(RESULT_PARAM)->InitFrom(Stack);
	P_FINISH
}
DEFINE_FUNCTION(UGMPStructLib::execMakeStructView)
{
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);

	if (ensureWorld(Stack.Object, Stack.MostRecentPropertyAddress))
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty))
		{
			reinterpret_cast<FGMPStructUnion*>(RESULT_PARAM)->ViewFrom(StructProp->Struct, Stack.MostRecentPropertyAddress);
		}
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execGetStructUnion)
{
	P_GET_STRUCT_REF(FGMPStructUnion, DynStruct);
	P_GET_OBJECT(UScriptStruct, StructType);

	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	GMP_STACK_STRUCT_ARRAY(StructType, StructMem, ArrayNum);

	Stack.StepCompiledIn<FStructProperty>(StructMem);
	if (auto Ptr = DynStruct.GetDynamicStructAddr(StructType, ArrayNum - 1))
	{
		StructType->CopyScriptStruct(Stack.MostRecentPropertyAddress, Ptr);
		*(bool*)RESULT_PARAM = true;
	}
	else
	{
		*(bool*)RESULT_PARAM = false;
	}
	P_FINISH
}
DEFINE_FUNCTION(UGMPStructLib::execClearGMPUnion)
{
	P_GET_OBJECT(UObject, InObj);
	P_GET_STRUCT(FName, MemberName);
	FStructProperty* Prop = InObj ? FindFProperty<FStructProperty>(InObj->GetClass(), MemberName) : nullptr;
	if (ensure(Prop && Prop->Struct == FGMPStructUnion::StaticStruct()))
	{
		auto DynStruct = Prop->ContainerPtrToValuePtr<FGMPStructUnion>(InObj);
		DynStruct->Reset();
#if UE_4_24_OR_LATER && WITH_PUSH_MODEL
		MARK_PROPERTY_DIRTY(InObj, Prop);
#endif
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execSetGMPUnion)
{
	P_GET_OBJECT(UObject, InObj);
	P_GET_STRUCT(FName, MemberName);
	P_GET_OBJECT(UScriptStruct, StructType);
	GMP_CHECK_SLOW(StructType);

	FStructProperty* Prop = InObj ? FindFProperty<FStructProperty>(InObj->GetClass(), MemberName) : nullptr;
	if (ensure(Prop && Prop->Struct == FGMPStructUnion::StaticStruct()))
	{
		auto DynStruct = Prop->ContainerPtrToValuePtr<FGMPStructUnion>(InObj);
		uint32 ArrayNum = 1;
		Stack.StepCompiledIn<FStructProperty>(DynStruct->EnsureMemory(StructType, ArrayNum));
#if UE_4_24_OR_LATER && WITH_PUSH_MODEL
		MARK_PROPERTY_DIRTY(InObj, Prop);
#endif
	}
	else
	{
		P_GET_STRUCT_REF(FGMPStructUnion, Tmp);
	}

	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execGetGMPUnion)
{
	P_GET_OBJECT(UObject, InObj);
	P_GET_STRUCT(FName, MemberName);
	P_GET_OBJECT(UScriptStruct, StructType);

	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	GMP_STACK_STRUCT_ARRAY(StructType, StructMem, ArrayNum);

	Stack.StepCompiledIn<FStructProperty>(StructMem);
	uint8* Ptr = nullptr;

	FStructProperty* Prop = InObj ? FindFProperty<FStructProperty>(InObj->GetClass(), MemberName) : nullptr;
	if (ensure(Prop && Prop->Struct == FGMPStructUnion::StaticStruct()))
	{
		auto DynStruct = Prop->ContainerPtrToValuePtr<FGMPStructUnion>(InObj);
		Ptr = DynStruct->GetDynamicStructAddr(StructType, ArrayNum - 1);
	}

	if (Ptr)
	{
		StructType->CopyScriptStruct(Stack.MostRecentPropertyAddress, Ptr);
		*(bool*)RESULT_PARAM = true;
	}
	else
	{
		*(bool*)RESULT_PARAM = false;
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPDynStructStorage::execSetDynStruct)
{
	P_GET_OBJECT(UGMPDynStructStorage, Storage);
	P_GET_OBJECT(UScriptStruct, StructType);
	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	if (ensure(Storage))
	{
		Stack.StepCompiledIn<FStructProperty>(Storage->StructUnion.EnsureMemory(StructType, ArrayNum));
	}
	else
	{
		GMP_STACK_STRUCT_ARRAY(StructType, StructMem, ArrayNum);
		Stack.StepCompiledIn<FStructProperty>(StructMem);
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPDynStructStorage::execGetDynStruct)
{
	P_GET_OBJECT(UGMPDynStructStorage, Storage);
	P_GET_OBJECT(UScriptStruct, StructType);

	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	GMP_STACK_STRUCT_ARRAY(StructType, StructMem, ArrayNum);

	Stack.StepCompiledIn<FStructProperty>(StructMem);
	if (auto Ptr = ensure(Storage) ? Storage->StructUnion.GetDynamicStructAddr(StructType, ArrayNum - 1) : nullptr)
	{
		StructType->CopyScriptStruct(Stack.MostRecentPropertyAddress, Ptr);
		*(bool*)RESULT_PARAM = true;
	}
	else
	{
		*(bool*)RESULT_PARAM = false;
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execSetStructTuple)
{
	P_GET_STRUCT_REF(FGMPStructTuple, StructTuple);
	P_GET_OBJECT(UScriptStruct, StructType);
	GMP_CHECK_SLOW(StructType);
	Stack.StepCompiledIn<FStructProperty>(StructTuple.FindOrAddByStruct(StructType).GetDynData());
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execGetStructTuple)
{
	P_GET_STRUCT_REF(FGMPStructTuple, StructTuple);
	P_GET_OBJECT(UScriptStruct, StructType);

	GMP_CHECK_SLOW(StructType);
	uint32 ArrayNum = 1;
	GMP_STACK_STRUCT_ARRAY(StructType, StructMem, ArrayNum);

	Stack.StepCompiledIn<FStructProperty>(StructMem);
	if (uint8* Ptr = StructTuple.GetDynamicStructAddr(StructType, ArrayNum - 1))
	{
		StructType->CopyScriptStruct(Stack.MostRecentPropertyAddress, Ptr);
		*(bool*)RESULT_PARAM = true;
	}
	else
	{
		*(bool*)RESULT_PARAM = false;
	}
	P_FINISH
}

DEFINE_FUNCTION(UGMPStructLib::execClearStructTuple)
{
	P_GET_STRUCT_REF(FGMPStructTuple, StructTuple);
	P_GET_OBJECT(UScriptStruct, StructType);
	GMP_CHECK_SLOW(StructType);
	StructTuple.ClearStruct(StructType);
	P_FINISH
}

const TCHAR* FGMPStructUnion::GetTypePropertyName()
{
	return TEXT("UnionType");
}

const TCHAR* FGMPStructUnion::GetCountPropertyName()
{
	return TEXT("UnionCount");
}

const TCHAR* FGMPStructUnion::GetDataPropertyName()
{
	return TEXT("UnionData");
}

namespace GMP
{
namespace Class2Prop
{
	UGMPPropertiesContainer* GMPGetMessagePropertiesHolder();
}
}  // namespace GMP

FGMPStructUnion FGMPStructUnion::From(FName MsgKey, const FGMPPropStackRefArray& Arr, int32 InFlags)
{
	FGMPStructUnion Data;
	if (Arr.Num() > 0)
	{
		Data.Flags = InFlags;
		int32 Cnt = -1;
		static auto GetPropCnt = [](const UStruct* InStruct) {
			int32 Cnt = 0;
			for (TFieldIterator<FProperty> It(InStruct); It; ++It)
			{
				++Cnt;
			}
			return Cnt;
		};
		auto Holder = GMP::Class2Prop::GMPGetMessagePropertiesHolder();
		UScriptStruct* InScriptStruct = Holder->FindScriptStructByName(MsgKey);
		if (!InScriptStruct || GetPropCnt(InScriptStruct) <= Arr.Num())
		{
			InScriptStruct = GMP::Class2Prop::MakeRuntimeStruct(Holder, MsgKey, [&]() -> const FProperty* { return Arr.IsValidIndex(++Cnt) ? Arr[Cnt].GetProp() : nullptr; });
			Holder->AddScriptStruct(MsgKey, InScriptStruct);
		}
		auto Mem = Data.EnsureMemory(InScriptStruct);
		Cnt = 0;
		for (TFieldIterator<FProperty> It(InScriptStruct); It; ++It)
		{
			It->CopyCompleteValue(It->ContainerPtrToValuePtr<void>(Mem), Arr[Cnt++].GetAddr());
		}
	}
	return Data;
}

FGMPStructUnion FGMPStructUnion::Duplicate() const
{
	FGMPStructUnion Data;
	if (IsValid())
		ScriptStruct->CopyScriptStruct(Data.EnsureMemory(ScriptStruct.Get(), GetArrayNum()), GetDynData(), GetArrayNum());
	return Data;
}

void FGMPStructUnion::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	if (ArrayNum <= 0)
		return;

	int32 TmpArrNum = FMath::Abs(ArrayNum);
	if (auto StructType = GetType())
	{
		if (auto Ops = StructType->GetCppStructOps())
		{
			if (ensure(Ops) && !Ops->IsPlainOldData() && Ops->HasAddStructReferencedObjects())
			{
				auto StructureSize = StructType->GetStructureSize();
				for (auto i = 0; i < TmpArrNum; ++i)
					Ops->AddStructReferencedObjects()(GetDynData() + i * StructureSize, Collector);
			}
		}
		else if (auto BPStructType = Cast<UUserDefinedStruct>(StructType))
		{
			auto StructureSize = BPStructType->GetStructureSize();
			for (auto i = 0; i < TmpArrNum; ++i)
			{
				FVerySlowReferenceCollectorArchiveScope CollectorScope(Collector.GetVerySlowReferenceCollectorArchive(), BPStructType);
				BPStructType->SerializeBin(FStructuredArchiveFromArchive(CollectorScope.GetArchive()).GetSlot(), GetDynData() + i * StructureSize);
			}
		}
	}
}

bool FGMPStructUnion::Identical(const FGMPStructUnion* Other, uint32 PortFlags /*= 0*/) const
{
	if (ScriptStruct != Other->ScriptStruct || GetArrayNum() != Other->GetArrayNum())
		return false;
	if (GetDynData() == Other->GetDynData())
		return true;
	GMP_CHECK_SLOW(!ArrayNum || ScriptStruct.Get());
	for (auto i = 0; i < GetArrayNum(); ++i)
	{
		auto StructureSize = i * ScriptStruct->GetStructureSize();
		if (!ScriptStruct->CompareScriptStruct(GetDynData() + StructureSize, Other->GetDynData() + StructureSize, PortFlags))
			return false;
	}
	return true;
}

bool FGMPStructUnion::Serialize(FArchive& Ar)
{
#if 0
	return Serialize(FStructuredArchiveFromArchive(Ar).GetSlot().EnterRecord());
#else
	Ar.UsingCustomVersion(GMP::CustomVersion::VersionGUID());
	Ar << ScriptStruct;
	int32 TmpArrNum = 0;
	if (auto StructType = GetTypeAndNum(TmpArrNum))
	{
		Ar << TmpArrNum;
		EnsureMemory(StructType, TmpArrNum, Ar.IsLoading());

		auto StructureSize = StructType->GetStructureSize();
		for (auto i = 0; i < TmpArrNum; ++i)
			StructType->SerializeItem(Ar, GetDynData() + i * StructureSize, nullptr);
	}
	return true;
#endif
}

bool FGMPStructUnion::Serialize(FStructuredArchive::FRecord Record)
{
	auto& UnderlayArchive = Record.GetUnderlyingArchive();
	UnderlayArchive.UsingCustomVersion(GMP::CustomVersion::VersionGUID());
	Record << SA_VALUE(GetTypePropertyName(), ScriptStruct);
	int32 TmpArrNum = 0;
	auto StructType = GetTypeAndNum(TmpArrNum);
	if (ensure(StructType))
	{
#if UE_5_01_OR_LATER
		auto SlotArray = Record.EnterArray(GetDataPropertyName(), TmpArrNum);
#else
		auto SlotArray = Record.EnterArray(SA_FIELD_NAME(GetDataPropertyName()), TmpArrNum);
#endif
		EnsureMemory(StructType, TmpArrNum, UnderlayArchive.IsLoading());

		auto StructureSize = StructType->GetStructureSize();
		for (auto i = 0; i < TmpArrNum; ++i)
			StructType->SerializeItem(SlotArray.EnterElement(), GetDynData() + i * StructureSize, nullptr);
	}

	return true;
}

bool FGMPStructUnion::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	int32 TmpArrNum = GetArrayNum();
	Ar << TmpArrNum;
	if (TmpArrNum > 0)
	{
		Ar << ScriptStruct;
		auto StructType = const_cast<UScriptStruct*>(ScriptStruct.Get());
		if (ensure(StructType))
		{
			EnsureMemory(StructType, TmpArrNum, true);
			auto StructProp = GMP::Class2Prop::TTraitsStructBase::GetProperty(StructType);
			for (auto i = 0; i < TmpArrNum; ++i)
				StructProp->NetSerializeItem(Ar, Map, GetDynData(i));
		}
		else
		{
			Reset();
		}
	}
	else if (Ar.IsLoading())
	{
		Reset();
	}
	bOutSuccess = true;
	return true;
}

bool FGMPStructUnion::ExportTextItem(FString& ValueStr, const FGMPStructUnion& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	ValueStr += TEXT("(");
	ON_SCOPE_EXIT
	{
		ValueStr += TEXT(")");
	};
	ValueStr.Appendf(TEXT("%s=%s"), GetTypePropertyName(), *GetTypeName().ToString());
	int32 TmpArrNum = 0;
	if (auto StructType = GetTypeAndNum(TmpArrNum))
	{
		ValueStr.Appendf(TEXT(",%s=("), GetDataPropertyName());
		ON_SCOPE_EXIT
		{
			ValueStr += TEXT(")");
		};
		for (auto i = 0; i < TmpArrNum; ++i)
		{
			StructType->ExportText(ValueStr, GetDynData(i), &DefaultValue, Parent, PortFlags, ExportRootScope);
		}
	}

	return true;
}
namespace GMP
{
namespace StructUnion
{
	constexpr FAsciiSet Whitespaces(" \t");
	constexpr FAsciiSet Delimiters("=([.");
	constexpr FAsciiSet PairStartDelimiters("{[(");
	constexpr FAsciiSet PairEndDelimiters("}]), ");
	static auto SkipWhitespace(const TCHAR*& Str)
	{
		while (FChar::IsWhitespace(*Str))
		{
			Str++;
		}
	};
	static auto IsPropertyValueSpecified(const TCHAR* Buffer)
	{
		return Buffer && *Buffer && *Buffer != TCHAR(',') && *Buffer != TCHAR(')');
	};

	static bool ReadContext(const TCHAR*& Str, FOutputDevice* Warn)
	{
		SkipWhitespace(Str);

		int32 Index = INDEX_NONE;
		bool bInStr = false;
		TArray<TCHAR, TInlineAllocator<16>> BraceStacks;
		const TCHAR* Start = Str;
		const TCHAR* End = nullptr;
		if (PairStartDelimiters.Contains(*Str))
		{
			while (*Str)
			{
				auto Ch = *Str++;
				switch (Ch)
				{
					case '\\':
					{
						if (bInStr)
						{
							Str++;
						}
						break;
					}
					case '"':
					{
						bInStr = !bInStr;
						if (!bInStr && !BraceStacks.Num())
						{
							End = Str;
						}
						break;
					}
					case '(':  // 40
						--Ch;
					case '<':  // 60
					case '[':  // 91
					case '{':  // 123
					{
						if (!bInStr)
						{
							BraceStacks.Push(Ch + 2);
						}
						break;
					}
					case ')':  // 41
					case '>':  // 62
					case ']':  // 93
					case '}':  // 125
					{
						if (!bInStr)
						{
							ensure(BraceStacks.Num() && BraceStacks.Pop() == Ch);
							if (!BraceStacks.Num())
							{
								End = Str;
							}
						}
						break;
					}
					default:
						break;
				}
				if (End)
					break;
			}
		}
		else if (*Start == '"')
		{
			while (*Str && *Str != '"')
				++Str;
			End = Str;
		}
		else
		{
			while (!PairEndDelimiters.Contains(*Str++))
				;
			End = Str;
		}

		if (!ensure(End))
		{
			Warn->Logf(ELogVerbosity::Warning, TEXT("Missing ')' in default properties subscript: %s"), Start);
		}
		return !!End;
	}

	static bool FindKeyValuePair(const TCHAR*& Str, TMap<FName, FStringView>& KeyValuePairs, FOutputDevice* ErrorText)
	{
		// strip leading whitespace
		auto StartStr = Str;
		const TCHAR* Start = FAsciiSet::Skip(Str, Whitespaces);
		// find first delimiter
		Str = FAsciiSet::FindFirstOrEnd(Start, Delimiters);
		// check if delimiter was found...
		if (*Str)
		{
			// strip trailing whitespace
			int32 Len = Str - Start;
			while (Len > 0 && Whitespaces.Contains(Start[Len - 1]))
			{
				--Len;
			}
			const FName PropertyName(Len, Start);
			SkipWhitespace(Str);

			if (*Str++ != '=')
			{
				ErrorText->Logf(ELogVerbosity::Warning, TEXT("Missing '=' in default properties assignment: %s"), Start);
				return false;
			}
			// strip whitespace after =
			SkipWhitespace(Str);

			// read context
			auto ContextStart = Str;
			if (ReadContext(Str, ErrorText))
			{
				KeyValuePairs.Add(PropertyName, FStringView(ContextStart, Str - ContextStart));
				return true;
			}
		}
		return false;
	};
}  // namespace StructUnion
}  // namespace GMP

bool FGMPStructUnion::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* OwnerObject, FOutputDevice* ErrorText)
{
	using namespace GMP::StructUnion;
	do
	{
		SkipWhitespace(Buffer);
		if (*Buffer++ == TCHAR('('))
		{
			TMap<FName, FStringView> DefinedProperties;
			// Parse all properties.
			while (Buffer && *Buffer != '\0' && *Buffer != ')')
			{
				if (!FindKeyValuePair(Buffer, DefinedProperties, ErrorText))
					break;
				SkipWhitespace(Buffer);
			}
			auto Type = DefinedProperties.Find(GetTypePropertyName());
			if (!Type)
			{
				ErrorText->Logf(TEXT("FGMPStructUnion::ImportText (%s): Missing UnionType"), Buffer);
				break;
			}
			if (auto StructType = GMP::Reflection::DynamicStruct(Type->GetData()))
			{
				if (auto Data = DefinedProperties.Find(GetDataPropertyName()))
				{
					TArray<FStringView> DefinedArray;
					auto InnerStr = Data->GetData();
					auto StartStr = InnerStr;
					while (ReadContext(InnerStr, ErrorText))
					{
						DefinedArray.Add(FStringView(StartStr, InnerStr - StartStr));
						SkipWhitespace(InnerStr);
						if (*InnerStr != ',' && *InnerStr != ')')
							break;
					}

					EnsureMemory(StructType, DefinedArray.Num());
					for (auto i = 0; i < DefinedArray.Num(); ++i)
					{
						StructType->ImportText(DefinedArray[i].GetData(), GetDynData(i), OwnerObject, PortFlags, ErrorText, StructType->GetName(), true);
					}
				}
				else
				{
					EnsureMemory(StructType, 1);
				}
				return true;
			}
		}
		else
		{
			ErrorText->Logf(TEXT("FGMPStructUnion::ImportText (%s): Missing opening parenthesis: %s"), Buffer);
			return false;
		}
	} while (false);
	Reset();
	return false;
}

uint8* FGMPStructUnion::EnsureMemory(const UScriptStruct* NewStructPtr, int32 NewArrayNum, bool bShrink)
{
	GMP_CHECK_SLOW(NewStructPtr);
	int32 OldArrNum = 0;
	auto OldStructType = GetTypeAndNum(OldArrNum);
	NewArrayNum = NewArrayNum != 0 ? FMath::Abs(NewArrayNum) : FMath::Max(1, OldArrNum);

	auto NewStructureSize = NewStructPtr->GetStructureSize();
	auto NewMemSize = FMath::Max(1, NewArrayNum * NewStructureSize);
	auto OldStructureSize = !OldStructType ? 0 : OldStructType->GetStructureSize();
	uint8* Ptr = GetDynData();
	if (ArrayNum < 0 || (OldStructType != NewStructPtr) || !OldStructType || NewArrayNum > OldArrNum || (bShrink && NewArrayNum < OldArrNum))
	{
		auto OldPtr = Ptr;

		// Construct New
		Ptr = static_cast<uint8*>(FMemory::Malloc(NewMemSize));
		auto NewDataPtr = TSharedPtr<uint8>(Ptr, [](uint8* Ptr) { FMemory::Free(Ptr); });
		for (auto i = 0; i < NewArrayNum; ++i)
			NewStructPtr->InitializeStruct(Ptr + i * NewStructureSize);

		// Copy to New Address
		if (OldStructType == NewStructPtr)
		{
			for (auto i = 0; i < OldArrNum; ++i)
				NewStructPtr->CopyScriptStruct(Ptr + i * NewStructureSize, OldPtr + i * NewStructureSize);
		}
		// Destroy If Possible
		if (DataPtr.GetSharedReferenceCount() == 1 && ensure(OldStructType))
		{
			for (auto i = 0; i < OldArrNum; ++i)
				OldStructType->DestroyStruct(OldPtr + i * OldStructureSize);
		}
		DataPtr = NewDataPtr;
		ArrayNum = NewArrayNum;
	}
	ScriptStruct = NewStructPtr;
	return Ptr;
}
void FGMPStructUnion::ViewFrom(const UScriptStruct* InScriptStruct, uint8* InStructAddr, int32 NewArrayNum /*= 1*/)
{
	this->operator=(FGMPStructUnion(InScriptStruct, InStructAddr, NewArrayNum));
}

void FGMPStructUnion::InitFrom(const UScriptStruct* InScriptStruct, uint8* InStructAddr, int32 NewArrayNum, bool bShrink)
{
	EnsureMemory(InScriptStruct, NewArrayNum, bShrink);
	for (auto i = 0; i < NewArrayNum; ++i)
	{
		InScriptStruct->CopyScriptStruct(GetDynData(i), InStructAddr);
	}
}

void FGMPStructUnion::InitFrom(FFrame& Stack)
{
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);

	if (ensureWorld(Stack.Object, Stack.MostRecentPropertyAddress))
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty))
		{
			InitFrom(StructProp->Struct, Stack.MostRecentPropertyAddress);
		}
		else if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Stack.MostRecentProperty))
		{
			if (FStructProperty* ElmProp = CastField<FStructProperty>(ArrProp->Inner))
			{
				FScriptArrayHelper ArrayHelper(ArrProp, Stack.MostRecentPropertyAddress);
				InitFrom(ElmProp->Struct, Stack.MostRecentPropertyAddress, ArrayHelper.Num());
			}
		}
	}
}

void FGMPStructTuple::ClearStruct(const UScriptStruct* InStructType)
{
	auto Hash = GetTypeHash(InStructType);
	if (auto Find = FindByStruct(InStructType))
	{
		StructTuple.RemoveByHash(Hash, InStructType);
	}
}

FGMPStructUnion* FGMPStructTuple::FindByStruct(const UScriptStruct* InStructType) const
{
	auto Hash = GetTypeHash(InStructType);
	return const_cast<FGMPStructUnion*>(StructTuple.FindByHash(Hash, InStructType));
}

FGMPStructUnion& FGMPStructTuple::FindOrAddByStruct(const UScriptStruct* InStructType, bool* bAlreadySet)
{
	auto Find = FindByStruct(InStructType);
	if (!Find)
	{
		FGMPStructUnion StructUnion;
		StructUnion.EnsureMemory(InStructType, 0);
		auto Id = StructTuple.Emplace(MoveTemp(StructUnion));
		Find = &StructTuple[Id];
	}
	else if (bAlreadySet)
	{
		*bAlreadySet = true;
	}

	return *Find;
}
