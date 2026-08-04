# FGMPStructUnion

A value whose `UScriptStruct` type is decided at runtime, that still behaves like a first-class UE value.

**Declared in** `Plugins/GMP/Source/GMP/Classes/GMPUnion.h:144`

```cpp
USTRUCT(BlueprintType, BlueprintInternalUseOnly,
        meta = (HasNativeMake = "/Script/GMP.GMPStructLib:MakeStructUnion"))
struct FGMPStructUnion
{
    UScriptStruct* GetScriptStruct() const;
    FName          GetTypeName() const;
    bool           IsValid(const UScriptStruct* InType = nullptr, uint32 ArrayIdx = 0) const;

    template<typename T>
    bool GetDynamicStruct(T& Data, uint32 Index = 0) const;

    GMP_API bool Serialize(FArchive& Ar);
    GMP_API bool Serialize(FStructuredArchive::FRecord Record);
    GMP_API bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
    GMP_API bool Identical(const FGMPStructUnion* Other, uint32 PortFlags = 0) const;
    GMP_API void AddStructReferencedObjects(FReferenceCollector& Collector);
};
```

## What makes it more than a blob

The last four methods, via `TStructOpsTypeTraits`:

| Method | Consequence |
|---|---|
| `Serialize` | works in save games and asset serialisation |
| `NetSerialize` | replicates, with `UPackageMap` handling object references |
| `Identical` | property comparison and delta detection behave correctly |
| `AddStructReferencedObjects` | **objects inside the held struct are visible to the garbage collector** |

Without the last one a stored payload holding an object reference would be collected out from under you.
This is why [[StoreObjectMessage]] can safely keep a message alive indefinitely.

## Array form

It holds either one value or **N values of the same runtime type** — `GetDynamicStruct` and `IsValid` both
take an index, and `GetArrayNum()` bounds it. That is what lets a stored or serialised message carry a list
without a second container type.

![a stored message is a StructUnion keyed by source](../docs/img/04-store-message.webp)

## Where it is used internally

```cpp
struct FGMPStoreSourceMsgs : public TMap<FSigSource, FGMPStructUnion>   // GMPUnion.h:526
```

This is the container behind [[StoreObjectMessage]]: keyed by source, holding the copied payload.

## Reading a value back

`GetDynamicStruct<T>(Data)` is checked — it returns false rather than reinterpreting if the held type is not
`T`. Check the result; do not assume.

`IsValid(InType)` tests the held type without extracting.

## Blueprint

Access goes through the StructUnion node family — Set/Get StructUnion, StructTuple, DynStructOnScope. See
[[UK2Neuron]].

## See also

[[StoreObjectMessage]] · [[GMPArchive]] · [[FRpcMessageUtils]]
