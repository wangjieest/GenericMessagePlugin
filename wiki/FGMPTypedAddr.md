# FGMPTypedAddr

One argument, type-erased: an address, plus its type name when the build keeps type names.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPStruct.h:127`

```cpp
USTRUCT(BlueprintType, meta = (HiddenByDefault = true))
struct GMP_API FGMPTypedAddr
{
    GENERATED_BODY()
public:
    UPROPERTY()
    uint64 Value = 0;

#if GMP_WITH_TYPENAME
    FName TypeName;
#endif
};
```

## Fields

| Field | Always present | Meaning |
|---|---|---|
| `Value` | yes | The argument's address, converted to an integer by `HorribleFromAddr`. **Not a copy of the value** — the argument still lives on the sender's stack for the duration of the dispatch. |
| `TypeName` | only under `GMP_WITH_TYPENAME` | The reflected type name from `Class2Name`, used for validation and by script backends. |

`GMP_WITH_TYPENAME` is `GMP_WITH_DYNAMIC_TYPE_CHECK || GMP_WITH_DYNAMIC_CALL_CHECK ||
GMP_WITH_TYPE_INFO_EXTENSION` (`GMPMacros.h`), which is on in Editor and Development and off in Shipping.

**So in Shipping the struct is layout-identical to a bare `uint64`**, and an argument array really is
nothing but an array of addresses.

## Constructors

```cpp
FGMPTypedAddr(const void* Addr, FName InName);
FGMPTypedAddr(const void* Addr, const FProperty* Prop);   // name via Reflection::GetPropertyName
FGMPTypedAddr(const void* Addr);                          // no name
```

The `FProperty*` form is the one to use when you are bridging from reflection — see [[Class2Name]].

## Lifetime — read this before storing one

The struct holds an **address, not a value**. It is valid for the duration of the dispatch and no longer.
Capturing an `FGMPTypedAddr` and reading it after the send returns is a use-after-free.

Anything that has to outlive the dispatch must copy the value out. That is exactly what
[[StoreObjectMessage]] does, and why it packs into an [[FGMPStructUnion]] rather than keeping the address.

## Where you meet it

Listeners written in C++ never see this type — the thunk decodes the array back into typed parameters
before your lambda runs. You meet it when writing a backend or a bridge, where the callback signature is
the raw one:

![Params is the first array argument of the raw callback](../docs/img/09-c-abi-hub.webp)


```cpp
void (*)(void* Self, const FGMPTypedAddr* Params, const FGMPExtra* Extra);
```

`Params` is an array of `Extra->Size` entries. See [[FGMPRawSig]].

## Type names travel on two channels

Do not assume `TypeName` is the only source:

| Channel | Available | Granularity |
|---|---|---|
| `FGMPTypedAddr::TypeName` | Editor / Development only | per argument |
| `FGMPExtra::TypeNames` | always | one static table per signature |

Code that must work in Shipping reads [[FGMPExtra]]`::TypeNames` together with `Size`.

## See also

[[FGMPExtra]] · [[FGMPRawSig]] · [[FMessageBody]] · [[Class2Name]] · [[Build switches]]
