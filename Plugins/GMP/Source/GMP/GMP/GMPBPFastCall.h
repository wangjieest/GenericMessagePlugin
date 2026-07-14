//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPBPLib.h"
#include "tuplet/tuple.hpp"

#include <type_traits>
#include <utility>
#include <tuple>

template<typename F, typename = void>
struct TGMPBPFastCall;

// ============================================================================
// C++ -> Blueprint zero-copy FastCall.
//
// Direction: C++ (compile-time known signature) actively calls a UFunction on a
// Blueprint object. This is the OPPOSITE direction of the production FastCall in
// GMPBPLib.cpp (which is BP->listener, message receive). The two are unrelated.
//
// Core idea (validated against UE5 source, see evidence comments below): a
// tuplet::tuple<TArgs...> has the SAME aggregate memory layout as a UFunction's
// parameter frame (both natural-aligned, declaration order). So a stack tuple can
// BE the UFunction frame directly -- Invoke reads/writes Stack.Locals+offset and
// lands inside the tuple. This bypasses every per-property reflection copy.
//
// Admission gates (must all hold to take the fast path):
//   C1  per-field layout match (offset+size+align), verified once & cached.
//   C4  NOT FUNC_UbergraphFunction (ubergraph uses a PersistentFrame; not ours).
// Branch selectors (both fast, only differ in HOW the frame is sourced):
//   C2  all args trivially-copyable (compile-time). POD -> tuple == frame, no dtor.
//   C3  ParmsSize == PropertiesSize, i.e. no locals (runtime, cached).
//
//   | C2 POD | C3 no-local | FramePtr                | write             | dtor                    |
//   |  yes   |    yes      | &stack tuple            | tuple IS frame    | none (POD)              |
//   |  yes   |    no       | alloca(PropertiesSize)  | memcpy parm region| local-var init/destroy  |
//   |  no    |    yes      | alloca(PropertiesSize)  | placement-new tup | C++ dtor on parm region |
//   |  no    |    no       | alloca(PropertiesSize)  | placement-new tup | parm + local destroy    |
//
// Out/ref params (RefEvent): OutParms are redirected to point at the ORIGINAL
// caller args, so the bytecode writes results straight back into the C++ caller's
// stack variables (ScriptCore.cpp:514-538 StepExplicitProperty uses Out->PropAddr
// for CPF_OutParm). This is the ONLY way a void CustomEvent returns a result.
// ============================================================================
class FGMPBPFastCallImpl
{
	// Verify that the tuplet::tuple layout matches the UFunction parameter frame
	// FIELD BY FIELD: each element must land at the same offset, with matching size
	// and alignment, and the totals must match. tuplet::tuple uses multiple
	// inheritance of tuple_elem<I,T> in declaration order, which all major compilers
	// lay out in declaration order with the first base at offset 0 -- but rather than
	// trust that, we measure the actual member offset and compare it to the engine's
	// GetOffset_ForUFunction(). Returns false on any mismatch (-> reflection fallback).
	template<typename... Ts>
	static bool VerifyTupleLayout(UFunction* Function)
	{
		using TupType = tuplet::tuple<Ts...>;
		if (sizeof(TupType) != Function->ParmsSize)
			return false;
		// Walk fields with a compile-time index sequence so get<I> can be indexed.
		return VerifyTupleLayoutImpl<TupType>(Function, std::make_index_sequence<sizeof...(Ts)>{});
	}

	template<typename TupType, size_t... Is>
	static bool VerifyTupleLayoutImpl(UFunction* Function, std::index_sequence<Is...>)
	{
		// Measure each member's real offset without constructing anything: a tuplet::tuple
		// derives from tuplet::tuple_elem<I,T>, so the offset of element I is the offset of
		// that base subobject's `value`. Casting a raw (unconstructed) buffer pointer to the
		// tuple type and then to the base subobject is a pointer (offset) computation only --
		// no object access, no UB from reading uninitialized memory.
		alignas(TupType) uint8 Probe[sizeof(TupType)];
		TupType* Tup = reinterpret_cast<TupType*>(&Probe[0]);
		const uint8* Base = reinterpret_cast<const uint8*>(Tup);

		FProperty* Prop = GMP::Reflection::GetFunctionChildProperties(Function);
		bool bMatch = true;
		const int Dummy[] = {0, ([&] {
			if (!bMatch)
				return;
			if (!Prop || (Prop->PropertyFlags & CPF_Parm) != CPF_Parm)
			{
				bMatch = false;
				return;
			}
			using ElemT = std::tuple_element_t<Is, TupType>;
			using ElemBase = tuplet::tuple_elem<Is, ElemT>;
			ElemBase* ElemPtr = static_cast<ElemBase*>(Tup);
			const int32 TupOffset = (int32)(reinterpret_cast<const uint8*>(&ElemPtr->value) - Base);
			if (TupOffset != Prop->GetOffset_ForUFunction()
				|| (int32)Prop->GetElementSize() != (int32)sizeof(ElemT)
				|| (int32)Prop->GetMinAlignment() != (int32)alignof(ElemT))
			{
				bMatch = false;
				return;
			}
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}(), 0)...};
		(void)Dummy;
		return bMatch;
	}

	// Cache the ADMISSION gates (C1 layout + C4 non-ubergraph) per (template instance,
	// UFunction*). The first call pays the reflection walk; every subsequent call is a
	// TMap lookup. Shipping trusts the dev-verified invariant.
	//
	// NOTE: only C1 and C4 are admission gates. C3 (ParmsSize == PropertiesSize, i.e. no
	// local variables) is NOT a gate -- a target WITH locals still takes the fast path,
	// it just sources the frame from alloca(PropertiesSize) instead of the bare tuple and
	// still constructs its param region in C++ (design §4.1). C1 verifies only the
	// parameter region layout (sizeof(tuple) == ParmsSize), which is independent of the
	// local-variable region, so it composes correctly with locals present.
	//
	// Thread-safety: FastCall is game-thread only (like every GMP listener path), so the
	// static TMap needs no lock.
	template<typename... Ts>
	static bool IsFastCallEligible(UFunction* Function)
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		static TMap<const UFunction*, bool> Cache;
		if (const bool* Hit = Cache.Find(Function))
			return *Hit;
		const bool bOk = !Function->HasAnyFunctionFlags(FUNC_UbergraphFunction)   // C4: not ubergraph
					  && VerifyTupleLayout<Ts...>(Function);                       // C1: field-by-field
		Cache.Add(Function, bOk);
		return bOk;
#endif
	}

	// Slow path: copy one arg into the frame parameter slot via its FProperty offset.
	template<typename T>
	static void CopyArgToFrame(uint8* Parms, FProperty*& Prop, T& Arg)
	{
		if (Prop)
		{
			Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Parms), &Arg);
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}
	}

	// Set up FOutParmRec entries so the bytecode writes CPF_OutParm results straight
	// back into the original caller args (RefEvent / void-event output). Returns nothing;
	// mutates NewStack.OutParms. ArgAddrs are the addresses of the caller's args, in
	// parameter order. Evidence: ScriptCore.cpp:2144-2179 builds this same list, and
	// StepExplicitProperty (514-538) reads Out->PropAddr for CPF_OutParm.
	//
	// IMPORTANT: the FOutParmRec records MUST outlive Function->Invoke, so storage is owned
	// by the CALLER (InvokeBlueprintEvent's stack frame) and passed in -- we must NOT alloca
	// them here, because alloca memory is freed when this helper returns, leaving NewStack.
	// OutParms dangling (that was a real crash). OutRecStorage must have room for >= NumArgs.
	static void SetupOutParms(FFrame& NewStack, FProperty* FuncFirstProp, void* const* ArgAddrs, int32 NumArgs, FOutParmRec* OutRecStorage)
	{
		FOutParmRec** LastOut = &NewStack.OutParms;
		int32 ArgIdx = 0;
		int32 RecIdx = 0;
		for (FProperty* Property = FuncFirstProp;
			 Property && (Property->PropertyFlags & CPF_Parm) == CPF_Parm;
			 Property = CastField<FProperty>((FField*)Property->Next))
		{
			if (Property->HasAnyPropertyFlags(CPF_OutParm) && ArgIdx < NumArgs)
			{
				FOutParmRec* Out = &OutRecStorage[RecIdx++];
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

	// Locate the caller arg address that backs the return-value parameter. We point the VM's
	// RESULT_PARAM straight at the caller's R& so the bytecode's `return <expr>` (ScriptCore.cpp:
	// 1261-1269: Stack.Step(Object, RESULT_PARAM)) writes the result directly into the caller's
	// variable -- same zero-indirection trick as OutParm redirection. The caller's R is an already
	// constructed object, so an assign-style write is valid even for non-POD returns.
	// ArgAddrs are in parameter order (1:1 with the CPF_Parm chain, guaranteed by VerifyTupleLayout).
	static uint8* GetReturnArgAddr(UFunction* Function, FProperty* FuncFirstProp, void* const* ArgAddrs, int32 NumArgs)
	{
		if (Function->ReturnValueOffset == MAX_uint16)
			return nullptr;
		int32 ArgIdx = 0;
		for (FProperty* Property = FuncFirstProp;
			 Property && (Property->PropertyFlags & CPF_Parm) == CPF_Parm;
			 Property = CastField<FProperty>((FField*)Property->Next))
		{
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
				return (ArgIdx < NumArgs) ? reinterpret_cast<uint8*>(ArgAddrs[ArgIdx]) : nullptr;
			++ArgIdx;
		}
		return nullptr;
	}

	template<typename... TArgs>
	static void InvokeBlueprintEvent(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		using namespace GMP;
		GMP_CHECK_SLOW(InObj && Function);

		using TupType = tuplet::tuple<std::decay_t<TArgs>...>;
		// C2: are ALL args trivially-copyable? Decided entirely at compile time.
		constexpr bool bAllPOD = std::conjunction_v<std::is_trivially_copyable<std::decay_t<TArgs>>...>;

		// C1+C3+C4 admission (cached; Shipping == true). False -> reflection fallback.
		const bool bEligible = IsFastCallEligible<std::decay_t<TArgs>...>(Function);

		// +1 so the array is never zero-length (illegal in C++) for the void zero-arg overload.
		void* const ArgAddrs[sizeof...(TArgs) + 1] = {(void*)(&Args)...};
		constexpr int32 NumArgs = (int32)sizeof...(TArgs);
		// FOutParmRec storage owned by THIS stack frame (must outlive Function->Invoke).
		FOutParmRec OutRecStorage[sizeof...(TArgs) + 1];
		auto FuncFirstProp = Reflection::GetFunctionChildProperties(Function);
		// Return value goes straight back into the caller's R& (zero indirection, like OutParms).
		uint8* const ReturnValueAddress = GetReturnArgAddr(Function, FuncFirstProp, ArgAddrs, NumArgs);

		if (bEligible)
		{
			// ---------------- FAST PATH (C++ construct, bypass reflection) ----------------
			if (bAllPOD && (Function->ParmsSize == Function->PropertiesSize))
			{
				// C2 && C3: the stack tuple IS the frame. Zero alloc, zero copy, no dtor.
				// tuplet::tuple is an aggregate; clang's -Wmissing-braces over-warns on {Args...} (GCC/MSVC don't). Suppress locally.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
				TupType Frame{Args...};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
				uint8* FramePtr = reinterpret_cast<uint8*>(&Frame);

#if GMP_WITH_DYNAMIC_CALL_CHECK
				if (!VerifyArgNames(Function, Frame))
					return;
#endif
				FFrame NewStack(InObj, Function, FramePtr, nullptr, FuncFirstProp);
				if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
					SetupOutParms(NewStack, FuncFirstProp, ArgAddrs, NumArgs, OutRecStorage);
				Function->Invoke(InObj, NewStack, ReturnValueAddress);
				// POD: nothing to destroy; Frame is a trivially-destructible stack object.
			}
			else
			{
				// Non-POD and/or has locals: alloca the full frame, placement-CONSTRUCT the
				// param region as a tuple (compile-time member construction == what reflection
				// CopyCompleteValue ends up doing, UnrealType.h:1608, minus the virtual dispatch),
				// then C++-destroy what we constructed. NEVER assign into zero memory (operator=
				// would destroy a non-existent object).
				uint8* FramePtr = (uint8*)FMemory_Alloca_Aligned(Function->PropertiesSize, Function->GetMinAlignment());
				// Zero the local-variable region (engine does the same, ScriptCore.cpp:2123-2127).
				const int32 NonParmsSize = Function->PropertiesSize - Function->ParmsSize;
				if (NonParmsSize > 0)
					FMemory::Memzero(FramePtr + Function->ParmsSize, NonParmsSize);

				// Placement-construct the whole parameter tuple into the param region.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
				TupType* FrameTup = new (FramePtr) TupType{Args...};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if GMP_WITH_DYNAMIC_CALL_CHECK
				if (!VerifyArgNames(Function, *FrameTup))
				{
					FrameTup->~TupType();
					return;
				}
#endif
				FFrame NewStack(InObj, Function, FramePtr, nullptr, FuncFirstProp);
				if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
					SetupOutParms(NewStack, FuncFirstProp, ArgAddrs, NumArgs, OutRecStorage);

				// Initialize local properties (C3 false). FirstPropertyToInit walks the locals.
				for (FProperty* LocalProp = Function->FirstPropertyToInit; LocalProp; LocalProp = CastField<FProperty>((FField*)LocalProp->PostConstructLinkNext))
					LocalProp->InitializeValue_InContainer(NewStack.Locals);

				Function->Invoke(InObj, NewStack, ReturnValueAddress);

				// Destroy locals we initialized (engine equivalent: ScriptCore.cpp:2198-2208).
				for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
				{
					if (!P->IsInContainer(Function->ParmsSize))
						P->DestroyValue_InContainer(NewStack.Locals);
				}
				// Destroy the param tuple we placement-constructed (C++ owns it; the engine does
				// NOT DestroyValue the param region for in-params, ScriptCore.cpp:2194-2208).
				if (!bAllPOD)
					FrameTup->~TupType();
			}
		}
		else
		{
			// ---------------- SLOW PATH (reflection fallback; non-Shipping layout mismatch) ----
			uint8* FramePtr = (uint8*)FMemory_Alloca_Aligned(Function->PropertiesSize, Function->GetMinAlignment());
			FMemory::Memzero(FramePtr, Function->PropertiesSize);
			{
				auto Prop = FuncFirstProp;
				const int Dummy[] = {0, (CopyArgToFrame(FramePtr, Prop, Args), 0)...};
				(void)Dummy;
			}
			FFrame NewStack(InObj, Function, FramePtr, nullptr, FuncFirstProp);
			if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
				SetupOutParms(NewStack, FuncFirstProp, ArgAddrs, NumArgs, OutRecStorage);

			for (FProperty* LocalProp = Function->FirstPropertyToInit; LocalProp; LocalProp = CastField<FProperty>((FField*)LocalProp->PostConstructLinkNext))
				LocalProp->InitializeValue_InContainer(NewStack.Locals);

			Function->Invoke(InObj, NewStack, ReturnValueAddress);

			// Reflection-destroy everything we reflection-constructed (params + locals): the
			// whole frame is engine-owned in this path.
			for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
				P->DestroyValue_InContainer(FramePtr);
		}
	}

#if GMP_WITH_DYNAMIC_CALL_CHECK
	// Optional runtime type-name validation (dev only). Reuses the tuple's compile-time
	// type info via MakeNames (std::tuple_element_t / std::tuple_size specializations).
	template<typename TupType>
	static bool VerifyArgNames(UFunction* Function, TupType& Tup)
	{
		using namespace GMP;
		const auto& ArgNames = Hub::DefaultTraits::MakeNames(Tup);
		if (!ensure(ArgNames.Num() == Function->NumParms))
			return false;
		auto FuncProp = Reflection::GetFunctionChildProperties(Function);
		for (auto& TypeName : ArgNames)
		{
			if (!ensure(FuncProp && Reflection::EqualPropertyName(FuncProp, TypeName)))
				return false;
			FuncProp = CastField<FProperty>((FField*)FuncProp->Next);
		}
		return true;
	}
#endif

	template<typename F, typename V>
	friend struct TGMPBPFastCall;
};

// Blueprint Function with a return value (CPF_ReturnParm). The return value is the
// trailing parameter; it is passed as the last arg so it occupies the trailing tuple slot.
template<typename R, typename... TArgs>
struct TGMPBPFastCall<R(TArgs...), std::enable_if_t<!GMP::TypeTraits::IsSameV<void, R>>>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args, R& ReturnVal)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args..., ReturnVal);
	}
};

// void target (CustomEvent or void Function). Output, if any, flows through out/ref params.
template<typename... TArgs>
struct TGMPBPFastCall<void(TArgs...), void>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args...);
	}
};
