# FGMPExtra

The per-dispatch context handed to a raw callback alongside the arguments.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPStruct.h:490`

```cpp
struct FGMPExtra
{
    int32        Size = 0;
    float        DebugSeconds = 0.f;
    const FName* TypeNames = nullptr;
    FSigSource   Source = FSigSource(nullptr);
    FName        Key;
    FGMPKey      Seq = {};
};
```

## Fields

| Field | Meaning |
|---|---|
| `Size` | number of entries in the [[FGMPTypedAddr]] array — the only reliable way to know the argument count |
| `DebugSeconds` | timing information for debug tooling |
| `TypeNames` | pointer to a **static** table of type names, one per argument, built once per signature |
| `Source` | the [[FSigSource]] the message was sent from |
| `Key` | the message key |
| `Seq` | the request sequence when this dispatch is part of a request/response exchange, otherwise zero |

![Extra is the third argument of the raw callback](../docs/img/09-c-abi-hub.webp)

## Why `TypeNames` matters

It is the type-name channel that **survives Shipping**. The per-argument `FGMPTypedAddr::TypeName` exists
only under `GMP_WITH_TYPENAME`, so any code that needs types in a packaged build reads `TypeNames` together
with `Size` instead.

The table is static and shared per signature (built by `FMessageBody::MakeStaticNames`), so it is cheap to
carry — the struct holds a pointer, not a copy.

## Lifetime

Valid for the duration of the dispatch only, like the argument array. Do not store the pointer.

## See also

[[FGMPTypedAddr]] · [[FMessageBody]] · [[FGMPRawSig]] · [[Build switches]]
