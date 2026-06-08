//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// GMP unit-test commandlet declaration (implementation in GMPUnitTestCommandlet.cpp).
// UCLASS must live in a header so UnrealHeaderTool scans it and emits the .generated.h.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Engine/World.h"
#include "UObject/Interface.h"

#include "GMPUnitTestCommandlet.generated.h"

// Headless test runner shared by the commandlet and (indirectly) CI. Defined in GMPTests.cpp,
// which holds all the Test_* bodies + UE automation entries. The commandlet's Main() forwards here.
namespace GMPUnitTest
{
	int32 RunAllGMPTests(const FString& Params);
}

UCLASS()
class UGMPUnitTestCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};

// A concrete, throwaway UObject used as a message source / listener holder (no world needed).
// MUST be concrete: NewObject<UObject>() on the abstract UObject base trips a handled ensure
// ("Class which was marked abstract was trying to be loaded"), which UE Automation treats as a
// test failure (commandlet tolerated it as a mere log). Use this concrete subclass instead.
UCLASS()
class UGMPTestProbe : public UObject
{
	GENERATED_BODY()
public:
	// --- targets for GMPBPFastCall.h (TGMPBPFastCall / InvokeBlueprintEvent) ---
	// Pure-in params (POD): exercises the fast path (tuple == frame) + a return value.
	UFUNCTION()
	int32 FastCallAddInts(int32 A, int32 B)
	{
		LastA = A;
		LastB = B;
		return A + B;
	}

	// Out param (int32&): exercises CopyArgFromFrame / OutParm writeback to the caller arg.
	UFUNCTION()
	void FastCallScaleOut(int32 In, UPARAM(ref) int32& OutDouble)
	{
		LastA = In;
		OutDouble = In * 2;
	}

	// void, no params: exercises the void/zero-arg overload.
	UFUNCTION()
	void FastCallTick()
	{
		++TickCount;
	}

	// Non-POD params + non-POD return value: exercises the placement-construct/C++-destroy
	// fast-path branch (bAllPOD == false), plus non-POD ref writeback through OutParm.
	// Out is appended to; the return value is a separately-built FString.
	UFUNCTION()
	FString FastCallAppendStr(const FString& In, UPARAM(ref) FString& Out)
	{
		LastStr = In;
		Out += In;                       // non-POD ref writeback to the caller's FString&
		return FString(TEXT("R:")) + In; // non-POD return value
	}

	int32 LastA = 0;
	int32 LastB = 0;
	int32 TickCount = 0;
	FString LastStr;
};

// Probe whose source object has a real world source for leveled dispatch tests.
UCLASS()
class UGMPWorldProbe : public UGMPTestProbe
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override { return TestWorld; }

	UPROPERTY()
	UWorld* TestWorld = nullptr;
};

// Native interface for testing typed-direct interface-param StoreMessage replay (R011 fix): the interface subobject
// pointer (IGMPTestInterface*) must NOT equal the UObject* -- so a wrong reinterpret reads the object, the fix reads
// the interface via FScriptInterface::GetInterface.
UINTERFACE()
class UGMPTestInterface : public UInterface
{
	GENERATED_BODY()
};
class IGMPTestInterface
{
	GENERATED_BODY()
public:
	virtual int32 GMPTestMagic() const = 0;
};

// Impl object: a non-zero first member before the interface vtable ensures IGMPTestInterface* (interface subobject)
// differs from UObject* (object base), exercising the FScriptInterface two-pointer layout.
UCLASS()
class UGMPTestInterfaceImpl : public UObject, public IGMPTestInterface
{
	GENERATED_BODY()
public:
	virtual int32 GMPTestMagic() const override { return 4242; }
};
