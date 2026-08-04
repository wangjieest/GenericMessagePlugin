# FMessageBody

The local message context: an [[FGMPExtra]] plus the helpers a listener-side implementation needs.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPStruct.h:500`

```cpp
struct GMP_API FMessageBody : public FGMPExtra
{
    template<typename... Ts>
    static const FArrayTypeNames& MakeStaticNamesImpl()
    {
        static FArrayTypeNames Ret{GMP_TYPE_META(Ts)::GetFName()...};
        return Ret;
    }

    template<typename Tup, size_t... Is>
    static const FArrayTypeNames& MakeStaticNames(Tup*, const std::index_sequence<Is...>&);

    FORCEINLINE auto GetSigSource() const { return Source.TryGetUObject(); }
};
```

![the body arrives as the third argument of the raw callback](../docs/img/09-c-abi-hub.webp)

## What it adds over FGMPExtra

**The static type-name table.** `MakeStaticNamesImpl<Ts...>()` builds one `FArrayTypeNames` per parameter
pack — a function-local static, so it is constructed once and shared by every dispatch of that signature.
This is what `FGMPExtra::TypeNames` points at, and why carrying type names costs a pointer rather than an
allocation per send.

**`GetSigSource()`** narrows the tagged [[FSigSource]] back to a `UObject*`, returning null when the source
is not one. Use this rather than casting the source yourself.

## Where you meet it

It is the body a listener-side adapter receives. Backends and bridges use it; C++ listeners do not, because
the thunk has already decoded the arguments into typed parameters by the time your lambda runs.

## See also

[[FGMPExtra]] · [[FGMPTypedAddr]] · [[Class2Name]]
