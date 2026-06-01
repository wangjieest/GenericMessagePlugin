//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/MemberReference.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/ObjectMacros.h"

#include "GMPInvokerTypes.generated.h"

// How a resolved chain level can be consumed downstream.
UENUM()
enum class EGMPChainNodeKind : uint8
{
	None,          // unresolved / empty placeholder level
	ObjectMember,  // object/weak/soft object property -> can descend
	StructMember,  // struct property -> can descend
	Leaf,          // scalar/enum/string/container/object-leaf -> terminates the chain
};

// What the node does once the member chain reaches the endpoint object.
UENUM()
enum class EGMPInvokeEndpoint : uint8
{
	GetMember,     // read the last member's value (wildcard output)
	CallFunction,  // call a function on the endpoint object
};

// One level of the member chain. MemberRef (name + guid + parent) is the only
// persistent source of truth; everything else is a cache recomputed on resolve.
USTRUCT()
struct FGMPMemberChainLink
{
	GENERATED_BODY()

	UPROPERTY()
	FMemberReference MemberRef;

	UPROPERTY()
	FEdGraphPinType CachedPinType;

	UPROPERTY()
	EGMPChainNodeKind Kind = EGMPChainNodeKind::None;

	UPROPERTY()
	FName OwnerTypeName;   // name only -> weak dependency

	UPROPERTY()
	bool bResolveFailed = false;
};

// Cached shape of one function parameter, used to (re)build pins.
USTRUCT()
struct FGMPCallParamCache
{
	GENERATED_BODY()

	UPROPERTY()
	FName ParamName;

	UPROPERTY()
	FEdGraphPinType PinType;

	UPROPERTY()
	bool bIsOutput = false;   // CPF_OutParm && !CPF_ReturnParm && !CPF_ConstParm

	UPROPERTY()
	bool bIsReturn = false;   // CPF_ReturnParm
};

// Shared classification helpers, used by the node and its details customization.
namespace GMPMemberChainUtils
{
	// Only blueprint-visible, non-deprecated members are offered in the dropdowns.
	inline bool IsExposedToBlueprint(const FProperty* Prop)
	{
		return Prop && Prop->HasAnyPropertyFlags(CPF_BlueprintVisible) && !Prop->HasAnyPropertyFlags(CPF_Deprecated);
	}

	inline EGMPChainNodeKind ClassifyKind(const FProperty* Prop)
	{
		if (CastField<FObjectProperty>(Prop) || CastField<FWeakObjectProperty>(Prop) || CastField<FSoftObjectProperty>(Prop))
			return EGMPChainNodeKind::ObjectMember;
		if (CastField<FStructProperty>(Prop))
			return EGMPChainNodeKind::StructMember;
		return EGMPChainNodeKind::Leaf;
	}

	// Container type to descend into for a descendable property; null for leaves.
	inline UStruct* NextOwnerOf(const FProperty* Prop)
	{
		if (auto* ObjProp = CastField<FObjectPropertyBase>(Prop))
			return ObjProp->PropertyClass;
		if (auto* StructProp = CastField<FStructProperty>(Prop))
			return StructProp->Struct;
		return nullptr;
	}
}
