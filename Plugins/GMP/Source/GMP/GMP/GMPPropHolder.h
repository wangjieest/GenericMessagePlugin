//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPClass2Prop.h"
#include "GMPPropHolder.generated.h"

UCLASS()
class UGMPPlaceHolder final : public UObject
{
	GENERATED_BODY()
public:
};

struct GMP_API FGMPPropStackRef
{
	uint8* Addr = nullptr;
	const FProperty* Prop = nullptr;

	FGMPPropStackRef() = default;
	FGMPPropStackRef(const void* InAddr, const FProperty* InProp = nullptr)
		: Addr(static_cast<uint8*>(const_cast<void*>(InAddr)))
		, Prop(InProp)
	{
	}
	const uint8* GetAddr() const { return Addr; }
	uint8* GetAddr() { return Addr; }
	const FProperty* GetProp() const { return Prop; }

	template<typename T>
	static FGMPPropStackRef MakePropStackRef(const T& Val)
	{
		const FProperty* Prop = GMP::TClass2Prop<T, true>::GetProperty();
		return FGMPPropStackRef(std::addressof(Val), Prop);
	}
	using FGMPPropStackRefArray = TArray<FGMPPropStackRef, TInlineAllocator<GMP_MSG_HOLDER_DEFAULT_INLINE_SIZE>>;
	static FGMPPropStackRefArray MakePropStackRefArray(const void* InAddr, const UScriptStruct* InStruct)
	{
		FGMPPropStackRefArray Ret;
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			Ret.Emplace(It->ContainerPtrToValuePtr<void>(InAddr), *It);
		}
		return Ret;
	}
	template<typename StructType>
	FORCEINLINE static FGMPPropStackRefArray MakePropStackRefArray(const StructType& InData)
	{
		return MakePropStackRefArray(std::addressof(InData), GMP::TypeTraits::StaticStruct<StructType>());
	}
};
using FGMPPropStackRefArray = FGMPPropStackRef::FGMPPropStackRefArray;

struct GMP_API FGMPPropStackHolder : public FGMPPropStackRef
{
	FGMPPropStackHolder() = default;
	FGMPPropStackHolder(const FProperty* InProp, const void* InAddr)
		: FGMPPropStackRef(InAddr, InProp)
	{
		Prop->InitializeValue_InContainer(Addr);
	}
	FGMPPropStackHolder(const FGMPPropStackHolder&) = delete;
	FGMPPropStackHolder(FGMPPropStackHolder&& Rhs)
	{
		Prop = Rhs.Prop;
		Addr = Rhs.Addr;
		Rhs.Prop = nullptr;
		Rhs.Addr = nullptr;
	}
	~FGMPPropStackHolder()
	{
		if (Prop && Addr)
		{
			Prop->DestroyValue_InContainer(Addr);
		}
	}

	template<typename T>
	static FGMPPropStackHolder MakePropStackHolder(const T& Val)
	{
		FProperty* Prop = GMP::TClass2Prop<T, true>::GetProperty();
		return FGMPPropStackHolder(Prop, std::addressof(Val));
	}
};

using FGMPPropStackHolderArray = TArray<FGMPPropStackHolder, TInlineAllocator<GMP_MSG_HOLDER_DEFAULT_INLINE_SIZE>>;

struct GMP_API FGMPPropHeapHolder
{
protected:
	using FuncAddRefOrNot = void (*)(void*, FReferenceCollector&);
	FuncAddRefOrNot AddRefOrNot = [](void* InAddr, FReferenceCollector& Collector) {};
	const FProperty* Prop = nullptr;
	size_t Addr[1];

public:
	const void* GetAddr() const { return Addr; }
	void* GetAddr() { return Addr; }
	const FProperty* GetProp() const { return Prop; }
	~FGMPPropHeapHolder()
	{
		if (GetProp() && GetAddr())
		{
			GetProp()->DestroyValue(GetAddr());
		}
	}

	using FGMPPropHeapHolderArray = TIndirectArray<FGMPPropHeapHolder, TInlineAllocator<GMP_MSG_HOLDER_DEFAULT_INLINE_SIZE>>;

	template<typename T>
	static FGMPPropHeapHolder* MakePropHolder(const T& InVal)
	{
		const FProperty* Prop = GMP::TClass2Prop<T, true>::GetProperty();
		FuncAddRefOrNot AddRefOrNot = nullptr;
		if constexpr (std::is_base_of<UObjectBase, std::remove_pointer_t<T>>::value)
		{
			AddRefOrNot = &FGMPPropHeapHolder::AddReferencedObject;
		}
		else
		{
			AddRefOrNot = [](void* This, FReferenceCollector& Collector) { AddStructReferencedObjectsOrNot<T>(MutableThis(This)->GetAddr(), Collector); };
		}
		return MakePropHolder(Prop, std::addressof(InVal), AddRefOrNot);
	}

	static FGMPPropHeapHolder* MakePropHolder(const FProperty* InProp, const void* InAddr, TOptional<FuncAddRefOrNot> AddRefOrNot = {})
	{
		auto Ptr = (FGMPPropHeapHolder*)FMemory::Malloc(FMath::Max<SIZE_T>(sizeof(FGMPPropHeapHolder), InProp->GetSize() + 2 * sizeof(void*)), InProp->GetMinAlignment());
		Ptr->Prop = InProp;
		Ptr->AddRefOrNot = nullptr;
		if (!AddRefOrNot.IsSet())
		{
			if (InProp->IsA<FObjectProperty>())
			{
				AddRefOrNot = &FGMPPropHeapHolder::AddReferencedObject;
			}
			else if (auto StructProp = CastField<FStructProperty>(InProp))
			{
				if (auto AddStructRef = StructProp->Struct->GetCppStructOps()->AddStructReferencedObjects())
				{
					AddRefOrNot = [](void* Ptr, FReferenceCollector& Collector) {
						auto This = MutableThis(Ptr);
						auto AddStructRef = ::CastFieldChecked<FStructProperty>(This->GetProp())->Struct->GetCppStructOps()->AddStructReferencedObjects();
						AddStructRef(This->GetAddr(), Collector);
					};
				}
			}
		}
		Ptr->AddRefOrNot = AddRefOrNot.Get(nullptr);
		InProp->InitializeValue(Ptr->GetAddr());
		if (InAddr)
		{
			InProp->CopyCompleteValue(Ptr->GetAddr(), InAddr);
		}
		return Ptr;
	}
	void AddStructReferencedObjects(FReferenceCollector& Collector)
	{
		if (AddRefOrNot && Prop)
		{
			const_cast<FProperty*>(Prop)->AddReferencedObjects(Collector);
			AddRefOrNot(GetAddr(), Collector);
		}
	}
	static FGMPPropHeapHolderArray From(const FGMPPropStackRefArray& Arr)
	{
		FGMPPropHeapHolderArray Ret;
		Ret.Reserve(Arr.Num());
		for (auto& Ref : Arr)
		{
			Ret.Add(FGMPPropHeapHolder::MakePropHolder(Ref.GetProp(), Ref.GetAddr()));
		}
		return Ret;
	}

private:
	FGMPPropHeapHolder() = default;

	template<typename T>
	FORCEINLINE static const T* CastFieldChecked(void* Ptr)
	{
		return ::CastFieldChecked<T>(MutableThis(Ptr)->Prop);
	}
	static void AddReferencedObject(void* Ptr, FReferenceCollector& Collector)
	{
		auto This = MutableThis(Ptr);
		auto ObjPtr = ::CastFieldChecked<FObjectProperty>(This->GetProp())->GetObjectPtrPropertyValuePtr(This->GetAddr());
		Collector.AddReferencedObject(*ObjPtr);
	}
	FORCEINLINE static FGMPPropHeapHolder* MutableThis(void* Ptr) { return static_cast<FGMPPropHeapHolder*>(Ptr); }
	FORCEINLINE static FGMPPropHeapHolder* MutableThis(const FGMPPropHeapHolder* Ptr) { return const_cast<FGMPPropHeapHolder*>(Ptr); }
};
using FGMPPropHeapHolderArray = FGMPPropHeapHolder::FGMPPropHeapHolderArray;
