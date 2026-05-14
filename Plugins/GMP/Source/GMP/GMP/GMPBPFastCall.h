//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPBPLib.h"
#include "tuplet/tuple.hpp"

template<typename F, typename = void>
struct TGMPBPFastCall;

class FGMPBPFastCallImpl
{
	// Verify that tuplet::tuple layout matches UFunction frame layout at runtime.
	// Both use natural C++ alignment, so they should always match.
	// Returns true if the tuple can be used directly as the function frame.
	template<typename... Ts>
	static bool VerifyTupleLayout(UFunction* Function)
	{
		using TupType = tuplet::tuple<Ts...>;
		if (sizeof(TupType) != Function->ParmsSize)
			return false;

		// Verify each property offset matches the tuple element offset
		TupType* NullTup = nullptr;
		auto Prop = GMP::Reflection::GetFunctionChildProperties(Function);
		bool bMatch = true;
		// Use fold expression to check each element offset
		int Idx = 0;
		const int Dummy[] = {0, ([&] {
			if (!bMatch || !Prop) { bMatch = false; return; }
			// tuplet element offset = offsetof via pointer arithmetic
			// Since tuplet is aggregate, elements are at predictable offsets
			if ((int32)Prop->GetSize() != (int32)sizeof(Ts) || Prop->GetMinAlignment() != (int32)alignof(Ts))
				bMatch = false;
			Prop = CastField<FProperty>((FField*)Prop->Next);
			++Idx;
		}(), 0)...};
		(void)Dummy;
		return bMatch;
	}

	// Slow path: copy args into frame via Property offsets
	template<typename T>
	static void CopyArgToFrame(uint8* Parms, FProperty*& Prop, T& Arg)
	{
		if (Prop)
		{
			Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Parms), &Arg);
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}
	}

	template<typename T>
	static void CopyArgFromFrame(const uint8* Parms, FProperty*& Prop, T& Arg)
	{
		if (Prop)
		{
			if (Prop->HasAnyPropertyFlags(CPF_OutParm))
			{
				Prop->CopyCompleteValue(&Arg, Prop->ContainerPtrToValuePtr<void>(Parms));
			}
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}
	}

	template<typename... TArgs>
	static void InvokeBlueprintEvent(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		using namespace GMP;
		GMP_CHECK_SLOW(InObj && Function);

		// Use tuplet::tuple as the parameter frame — aggregate layout matches struct layout.
		// This eliminates per-property CopyCompleteValue calls when layout is verified.
		using TupType = tuplet::tuple<std::decay_t<TArgs>...>;
		TupType LocalsOnStack{Args...};

		// tuplet::tuple uses aggregate layout (same as struct) with natural alignment,
		// which should always match UE's FProperty layout (also natural alignment).
		// sizeof check is a fast short-circuit; full verify only if sizes match.
		const bool bLayoutMatch = (sizeof(TupType) == Function->ParmsSize);
		uint8* Parms = nullptr;

		if (bLayoutMatch)
		{
			// Fast path: tuple IS the frame — zero per-property copy
			Parms = reinterpret_cast<uint8*>(&LocalsOnStack);
		}
		else
		{
			// Slow path: allocate proper frame and copy via Property offsets
			Parms = (uint8*)FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
			FMemory::Memzero(Parms, Function->ParmsSize);
			auto Prop = Reflection::GetFunctionChildProperties(Function);
			const int Dummy[] = {0, (CopyArgToFrame(Parms, Prop, Args), 0)...};
			(void)Dummy;
		}

#if GMP_WITH_DYNAMIC_CALL_CHECK
		{
			// Reuse LocalsOnStack (tuplet::tuple) for type name validation.
			// MakeNames only uses type info (std::tuple_element_t / std::tuple_size),
			// both of which tuplet::tuple has std:: specializations for.
			const auto& ArgNames = Hub::DefaultTraits::MakeNames(LocalsOnStack);
			if (!ensure(ArgNames.Num() == Function->NumParms))
				return;

			auto FuncProp = Reflection::GetFunctionChildProperties(Function);
			for (auto& TypeName : ArgNames)
			{
				if (!ensure(FuncProp && Reflection::EqualPropertyName(FuncProp, TypeName)))
					return;
				FuncProp = CastField<FProperty>((FField*)FuncProp->Next);
			}
		}
#endif

		auto FuncFirstProp = Reflection::GetFunctionChildProperties(Function);
		const bool bReturnVoid = (Function->ReturnValueOffset == MAX_uint16);
		uint8* ReturnValueAddress = !bReturnVoid ? (Parms + Function->ReturnValueOffset) : nullptr;

		// Allocate execution frame
		uint8* FrameMemory = nullptr;
		if (Function->HasAnyFunctionFlags(FUNC_UbergraphFunction))
		{
			FrameMemory = Function->GetOuterUClassUnchecked()->GetPersistentUberGraphFrame(InObj, Function);
		}
		const bool bUsePersistentFrame = (FrameMemory != nullptr);
		if (!bUsePersistentFrame)
		{
			FrameMemory = (uint8*)FMemory_Alloca_Aligned(Function->PropertiesSize, Function->GetMinAlignment());
			FMemory::Memzero(FrameMemory + Function->ParmsSize, Function->PropertiesSize - Function->ParmsSize);
		}
		FMemory::Memcpy(FrameMemory, Parms, Function->ParmsSize);

		FFrame NewStack(InObj, Function, FrameMemory, nullptr, FuncFirstProp);

		// Set up OutParms pointing to the ORIGINAL caller args for direct writeback
		if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
		{
			void* ArgAddrs[] = {(&Args)...};
			int32 ArgIdx = 0;

			FOutParmRec** LastOut = &NewStack.OutParms;
			for (FProperty* Property = FuncFirstProp;
				 Property && (Property->PropertyFlags & CPF_Parm) == CPF_Parm;
				 Property = CastField<FProperty>((FField*)Property->Next))
			{
				if (Property->HasAnyPropertyFlags(CPF_OutParm) && ArgIdx < (int32)UE_ARRAY_COUNT(ArgAddrs))
				{
					CA_SUPPRESS(6263)
					FOutParmRec* Out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
					Out->PropAddr = reinterpret_cast<uint8*>(ArgAddrs[ArgIdx]);
					Out->Property = Property;

					if (*LastOut)
					{
						(*LastOut)->NextOutParm = Out;
						LastOut = &(*LastOut)->NextOutParm;
					}
					else
					{
						*LastOut = Out;
					}
				}
				++ArgIdx;
			}
			if (*LastOut)
			{
				(*LastOut)->NextOutParm = nullptr;
			}
		}

		// Initialize local properties
		if (!bUsePersistentFrame)
		{
			for (FProperty* LocalProp = Function->FirstPropertyToInit; LocalProp; LocalProp = CastField<FProperty>((FField*)LocalProp->Next))
			{
				LocalProp->InitializeValue_InContainer(NewStack.Locals);
			}
		}

		// Invoke
		Function->Invoke(InObj, NewStack, ReturnValueAddress);

		// Copy back out params from frame to original args
		{
			auto Prop = FuncFirstProp;
			const int Dummy[] = {0, (CopyArgFromFrame(FrameMemory, Prop, Args), 0)...};
			(void)Dummy;
		}

		// Destroy local variables (non-parameter properties)
		if (!bUsePersistentFrame)
		{
			for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
			{
				if (!P->IsInContainer(Function->ParmsSize))
				{
					P->DestroyValue_InContainer(NewStack.Locals);
				}
			}
		}
	}

	template<typename F, typename V>
	friend struct TGMPBPFastCall;
};

template<typename R, typename... TArgs>
struct TGMPBPFastCall<R(TArgs...), std::enable_if_t<!GMP::TypeTraits::IsSameV<void, R>>>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args, R& ReturnVal)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args..., ReturnVal);
	}
};
template<typename... TArgs>
struct TGMPBPFastCall<void(TArgs...), void>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args...);
	}
};
