//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPTypeTraits.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/StructOnScope.h"

#include "GMPUnion.generated.h"

USTRUCT(BlueprintType, BlueprintInternalUseOnly, meta = (HasNativeMake = "/Script/GMP.GMPStructLib:MakeStructUnion"))
struct FGMPStructUnion
{
	GENERATED_BODY()
public:
	FGMPStructUnion() {}
	~FGMPStructUnion() { Reset(); }

	FGMPStructUnion(const FGMPStructUnion& InOther)
		: ScriptStruct(InOther.ScriptStruct)
		, ArrayNum(-FMath::Abs(InOther.ArrayNum))
		, DataPtr(InOther.DataPtr)
	{
	}
	FGMPStructUnion& operator=(const FGMPStructUnion& InOther)
	{
		if (this != &InOther)
		{
			Reset();
			ScriptStruct = InOther.ScriptStruct;
			ArrayNum = -FMath::Abs(InOther.ArrayNum);
			DataPtr = InOther.DataPtr;
		}
		return *this;
	}
	FGMPStructUnion(FGMPStructUnion&& InOther)
	{
		ScriptStruct = InOther.ScriptStruct;
		ArrayNum = InOther.ArrayNum;
		DataPtr = MoveTemp(InOther.DataPtr);
		InOther.Reset();
	}
	FGMPStructUnion& operator=(FGMPStructUnion&& InOther)
	{
		if (this != &InOther)
		{
			Reset();
			ScriptStruct = InOther.ScriptStruct;
			ArrayNum = InOther.ArrayNum;
			DataPtr = MoveTemp(InOther.DataPtr);
			InOther.Reset();
		}
		return *this;
	}

	template<typename T, typename = std::enable_if_t<!std::is_base_of<FGMPStructUnion, std::decay_t<T>>::value>>
	explicit FGMPStructUnion(T&& In)
	{
		static_assert(!std::is_base_of<FGMPStructUnion, std::decay_t<T>>::value, "err");
		SetDynamicStruct(std::forward<T>(In));
	}

	int32 GetArrayNum() const { return FMath::Abs(ArrayNum); }
	uint8* GetDynData(uint32 Index) const
	{
		if (ScriptStruct.IsValid() && Index < static_cast<uint32>(GetArrayNum()))
			return const_cast<uint8*>(DataPtr.Get()) + Index * ScriptStruct->GetStructureSize();
		return nullptr;
	}
	uint8* GetDynData() const { return const_cast<uint8*>(DataPtr.Get()); }

	uint8* GetDynamicStructAddr(const UScriptStruct* InStructType = nullptr, uint32 ArrayIdx = 0) const
	{
		auto Addr = GetDynData(ArrayIdx);
		return (Addr && (!InStructType || ScriptStruct->IsChildOf(InStructType))) ? Addr : nullptr;
	}

	template<typename T>
	T* GetStruct(uint32 Index = 0) const
	{
		return reinterpret_cast<std::decay_t<T>*>(GetDynamicStructAddr(::StaticScriptStruct<T>(), Index));
	}

	template<typename T>
	bool GetDynamicStruct(T& Data, uint32 Index = 0) const
	{
		if (auto* StructPtr = GetStruct<T>(Index))
		{
			Data = *StructPtr;
			return true;
		}

		return false;
	}

	bool IsValid(const UScriptStruct* InStructType = nullptr, uint32 ArrayIdx = 0) const { return !!GetDynamicStructAddr(InStructType, ArrayIdx); }

	FName GetTypeName() const { return ScriptStruct.IsValid() ? ScriptStruct->GetFName() : NAME_None; }
	UScriptStruct* GetType() const { return const_cast<UScriptStruct*>(ScriptStruct.Get()); }
	UScriptStruct* GetTypeAndNum(int32& OutNum) const
	{
		OutNum = GetArrayNum();
		auto StructType = GetType();
		ensure(!OutNum || StructType);
		return StructType;
	}

	GMP_API void AddStructReferencedObjects(FReferenceCollector& Collector);
	GMP_API bool Identical(const FGMPStructUnion* Other, uint32 PortFlags = 0) const;
	GMP_API bool Serialize(FArchive& Ar);
	GMP_API bool Serialize(FStructuredArchive::FRecord Record);
	GMP_API bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	GMP_API bool ExportTextItem(FString& ValueStr, const FGMPStructUnion& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const;
	GMP_API bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* OwnerObject, FOutputDevice* ErrorText);

	bool IsStructView() const { return ArrayNum < 0; }
#if 1
	// Key for TSet/TMap
	friend bool operator==(const FGMPStructUnion& Lhs, const FGMPStructUnion& Rhs) { return Lhs.ScriptStruct == Rhs.ScriptStruct; }
	friend bool operator==(const FGMPStructUnion& Lhs, const UScriptStruct* InStructType) { return Lhs.GetType() == InStructType; }
	friend uint32 GetTypeHash(const FGMPStructUnion& Struct) { return GetTypeHash(Struct.ScriptStruct.Get()); }
#endif

	static FGMPStructUnion MakeStructView(const UScriptStruct* InScriptStruct, void* InDataPtr, int32 Num = 1) { return FGMPStructUnion(InScriptStruct, InDataPtr, Num); }

	FGMPStructUnion Duplicate() const;

	GMP_API static const TCHAR* GetTypePropertyName();
	GMP_API static const TCHAR* GetCountPropertyName();
	GMP_API static const TCHAR* GetDataPropertyName();

	FGMPStructUnion(const UScriptStruct* InScriptStruct, uint32 NewArrayNum)
	{
		if (ensure(InScriptStruct))
		{
			EnsureMemory(InScriptStruct, NewArrayNum);
		}
	}

	template<typename T>
	T& SetStructRefChecked(T&& Data, uint32 Index = 0)
	{
		checkf(ScriptStruct.Get() == ::StaticScriptStruct<T>(), TEXT("must be same type"));
		return SetDynamicStruct(MoveTemp(Data), Index);
	}
	template<typename T>
	T& GetStructRefChecked(uint32 Index = 0)
	{
		checkf(ScriptStruct.Get() == ::StaticScriptStruct<T>(), TEXT("must be same type"));
		return GetStructRef<T>(Index);
	}

	template<typename T>
	T& SetDynamicStruct(T&& Data, uint32 Index = 0)
	{
		return *reinterpret_cast<std::decay_t<T>*>(EnsureMemory(::StaticScriptStruct<T>(), Index + 1)) = std::forward<T>(Data);
	}
	template<typename T>
	T& GetStructRef(uint32 Index = 0)
	{
		return *reinterpret_cast<std::decay_t<T>*>(EnsureMemory(::StaticScriptStruct<T>(), Index + 1));
	}

	static auto ScopeStackStruct(uint8* MemFromStack, UScriptStruct* InStructType, int32 InArrayNum = 1) { return FStackStructOnScope(MemFromStack, InStructType, InArrayNum); }

private:
	struct FStackStructOnScope
	{
		FStackStructOnScope(uint8* MemFromStack, UScriptStruct* InStructType, int32 InArrayNum)
			: StructMem(MemFromStack)
			, StructType(InStructType)
			, ArrayNum(InArrayNum)
		{
			StructType->InitializeStruct(StructMem, ArrayNum);
		}
		~FStackStructOnScope() { StructType->DestroyStruct(StructMem, ArrayNum); }

	protected:
		uint8* StructMem;
		UScriptStruct* StructType;
		const int32 ArrayNum;
	};
	FGMPStructUnion(const UScriptStruct* InScriptStruct, void* InDataPtr, int32 InNum)
		: ScriptStruct(InScriptStruct)
		, ArrayNum(-FMath::Abs(InNum))
		, DataPtr(TSharedPtr<uint8>((uint8*)InDataPtr, [](uint8*) {}))
	{
		GMP_CHECK(InNum >= 1);
	}

	UPROPERTY(BlueprintReadOnly, Category = "GMP|Union", meta = (AllowPrivateAccess = true))
	TWeakObjectPtr<const UScriptStruct> ScriptStruct;

	UPROPERTY(BlueprintReadOnly, Category = "GMP|Union", meta = (AllowPrivateAccess = true))
	int32 ArrayNum = 0;

	TSharedPtr<uint8> DataPtr;

	void Reset()
	{
		int32 TmpArrNum = 0;
		auto StructType = GetTypeAndNum(TmpArrNum);
		if (StructType && DataPtr.GetSharedReferenceCount() == 1)
		{
			auto StructureSize = StructType->GetStructureSize();
			auto Ptr = GetDynData();
			for (auto i = 0; i < TmpArrNum; ++i)
			{
				StructType->DestroyStruct(Ptr);
				Ptr += StructureSize;
			}
		}
		ScriptStruct = nullptr;
		DataPtr = nullptr;
		ArrayNum = 0;
	}

	GMP_API uint8* EnsureMemory(const UScriptStruct* InScriptStruct, int32 NewArrayNum = 0, bool bShrink = false);
	GMP_API void InitFrom(const UScriptStruct* InScriptStruct, uint8* InStructAddr, int32 NewArrayNum = 1, bool bShrink = false);
	GMP_API void InitFrom(FFrame& Stack);

	friend class UGMPDynStructStorage;
	friend class UGMPStructLib;
	friend struct FGMPStructTuple;
	friend class UQuestVariantData;
	GMP_API void ViewFrom(const UScriptStruct* InScriptStruct, uint8* InStructAddr, int32 NewArrayNum = 1);
};

template<>
struct TStructOpsTypeTraits<FGMPStructUnion> : public TStructOpsTypeTraitsBase2<FGMPStructUnion>
{
	enum
	{
		WithCopy = true,
		WithIdentical = true,
		WithSerializer = true,
		WithNetSerializer = true,
		WithImportTextItem = true,
		WithExportTextItem = true,
		WithAddStructReferencedObjects = true,
	};
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GMP_API FGMPStructTuple
{
	GENERATED_BODY()
public:
	template<typename T>
	auto& SetDynamicStruct(T&& Data)
	{
		return FindOrAddByStruct(::StaticScriptStruct<T>()).SetDynamicStruct(std::forward<T>(Data));
	}
	template<typename T>
	auto& GetStructRef() const
	{
		return *reinterpret_cast<std::decay_t<T>*>(FindOrAddByStruct(::StaticScriptStruct<T>()).GetDynData());
	}

	template<typename T>
	bool GetDynamicStruct(T& Data) const
	{
		auto Find = FindByStruct(::StaticScriptStruct<T>());
		return Find && Find->GetDynamicStruct(Data);
	}

	template<typename T>
	T* GetStruct() const
	{
		auto Find = FindByStruct(::StaticScriptStruct<T>());
		return Find ? Find->template GetStruct<T>() : nullptr;
	}

	void ClearStruct(const UScriptStruct* InStructType);

	uint8* GetDynamicStructAddr(const UScriptStruct* InStructType = nullptr, uint32 ArrayIdx = 0) const
	{
		auto StructUnion = FindByStruct(InStructType);
		return StructUnion ? StructUnion->GetDynamicStructAddr(InStructType, ArrayIdx) : nullptr;
	}

protected:
	FGMPStructUnion* FindByStruct(const UScriptStruct* InStructType) const;
	FGMPStructUnion& FindOrAddByStruct(const UScriptStruct* InStructType, bool* bAlreadySet = nullptr);

	UPROPERTY(BlueprintReadOnly, Category = "GMP|Union")
	TSet<FGMPStructUnion> StructTuple;
	friend class UGMPStructLib;
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GMP_API FGMPStructBase
{
	GENERATED_BODY()
public:
	virtual ~FGMPStructBase() {}
	virtual UScriptStruct* GetScriptStruct() const { return FGMPStructBase::StaticStruct(); }

	friend FArchive& operator<<(FArchive& Ar, FGMPStructBase& InStruct);
};

UCLASS()
class GMP_API UGMPStructLib final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "GMP|StructUnion", CustomThunk, meta = (CustomStructureParam = "InStructVal"))
	static FGMPStructUnion MakeStructUnion(const FGMPStructBase& InStructVal);
	DECLARE_FUNCTION(execMakeStructUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InStructVal"))
	static FGMPStructUnion MakeStructView(const FGMPStructBase& InStructVal);
	DECLARE_FUNCTION(execMakeStructView);

	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InVal"))
	static void SetStructUnion(UPARAM(ref) FGMPStructUnion& InStruct, UScriptStruct* InType, const FGMPStructBase& InVal);
	DECLARE_FUNCTION(execSetStructUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "OutVal"))
	static bool GetStructUnion(const FGMPStructUnion& InStruct, UScriptStruct* InType, FGMPStructBase& OutVal);
	DECLARE_FUNCTION(execGetStructUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static void ClearStructUnion(UPARAM(ref) FGMPStructUnion& InStruct);
	DECLARE_FUNCTION(execClearStructUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|MemberUnion", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InVal"))
	static void SetGMPUnion(UObject* InObj, FName MemberName, UScriptStruct* InType, const FGMPStructBase& InVal);
	DECLARE_FUNCTION(execSetGMPUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|MemberUnion", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "OutVal"))
	static bool GetGMPUnion(UObject* InObj, FName MemberName, UScriptStruct* InType, FGMPStructBase& OutVal);
	DECLARE_FUNCTION(execGetGMPUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|MemberUnion", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static void ClearGMPUnion(UObject* InObj, FName MemberName);
	DECLARE_FUNCTION(execClearGMPUnion);

	UFUNCTION(BlueprintCallable, Category = "GMP|Tuple", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InVal"))
	static void SetStructTuple(UPARAM(ref) FGMPStructTuple& InStruct, UScriptStruct* InType, const FGMPStructBase& InVal);
	DECLARE_FUNCTION(execSetStructTuple);

	UFUNCTION(BlueprintCallable, Category = "GMP|Tuple", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "OutVal"))
	static bool GetStructTuple(const FGMPStructTuple& InStruct, UScriptStruct* InType, FGMPStructBase& OutVal);
	DECLARE_FUNCTION(execGetStructTuple);

	UFUNCTION(BlueprintCallable, Category = "GMP|Tuple", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static void ClearStructTuple(UPARAM(ref) FGMPStructTuple& InStruct, UScriptStruct* InType);
	DECLARE_FUNCTION(execClearStructTuple);
};

UCLASS(BlueprintType, editinlinenew, Blueprintable)
class GMP_API UGMPDynStructStorage : public UObject
{
	GENERATED_BODY()
public:
	template<typename T>
	FORCEINLINE auto& SetDynamicStruct(T&& Data, uint32 Index = 0)
	{
		return StructUnion.SetDynamicStruct(Data, Index);
	}
	template<typename T>
	FORCEINLINE auto& GetStructRef(uint32 Index = 0) const
	{
		return StructUnion.template GetStructRef<T>(Index);
	}

	template<typename T>
	FORCEINLINE auto* GetStruct(uint32 Index = 0) const
	{
		return StructUnion.template GetStruct<T>(Index);
	}
	template<typename T>
	FORCEINLINE bool GetDynamicStruct(T& Data, uint32 Index = 0) const
	{
		return StructUnion.GetDynamicStruct(Data, Index);
	}

	UFUNCTION(BlueprintCallable, Category = "GMP|Union")
	bool IsValid(UScriptStruct* InStructType) const { return StructUnion.IsValid(InStructType); }
	UFUNCTION(BlueprintCallable, Category = "GMP|Union")
	UScriptStruct* GetType() const { return StructUnion.GetType(); }
	UFUNCTION(BlueprintCallable, Category = "GMP|Union")
	FName GetTypeName() const { return StructUnion.GetTypeName(); }

public:
	template<typename T>
	static void RegisterType(FName Category = NAME_None)
	{
		RegisterTypeImpl(::StaticScriptStruct<T>(), Category);
	}

protected:
	static void RegisterTypeImpl(UScriptStruct* InStructType, FName Category);

	UPROPERTY()
	FGMPStructUnion StructUnion;

	virtual void BeginDestroy() override;
	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InVal"))
	static void SetDynStruct(UGMPDynStructStorage* InStorage, UScriptStruct* InType, const FGMPStructBase& InVal);
	DECLARE_FUNCTION(execSetDynStruct);
	UFUNCTION(BlueprintCallable, Category = "GMP|Union", CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "OutVal"))
	static bool GetDynStruct(UGMPDynStructStorage* InStorage, UScriptStruct* InType, FGMPStructBase& OutVal);
	DECLARE_FUNCTION(execGetDynStruct);
	friend struct FGMPStructUtils;
};
