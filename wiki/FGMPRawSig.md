# FGMPRawSig

The one callback shape every backend normalises to.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPHub.h:31`

```cpp
using FGMPRawSig = TGMPFunction<void(const FGMPTypedAddr*, const FGMPExtra*)>;
```

## The C++ face and the actual ABI are not the same shape

`FGMPRawSig` is the type you name in C++. What actually crosses the boundary, after
[[TGMPFunction]] erases it, is a **three-argument bare function pointer**:

```cpp
void (*)(void* Self, const FGMPTypedAddr* Params, const FGMPExtra* Extra);
```

| Parameter | What it is |
|---|---|
| `Self` | the **callable's own address** — not a `UObject`, not a vtable pointer |
| `Params` | array of `Extra->Size` erased arguments — see [[FGMPTypedAddr]] |
| `Extra` | the dispatch context — see [[FGMPExtra]] |

Because `Self` is the callable rather than an object with a vtable, the call is an indirect jump and not a
virtual dispatch.

![C ABI hub](../docs/img/09-c-abi-hub.webp)

## Why one shape for everything

C++, Blueprint and all five scripting languages reach the core through this signature. Adding a language
means implementing this single entry point and translating `(Params, Extra)` into that language's values —
not writing a marshalling layer per language.

| Backend | What it does with `(Params, Extra)` |
|---|---|
| UnLua / slua | push onto the lua stack |
| Puerts | convert to v8 values |
| AngelScript | fill AngelScript arguments |
| C# | pass straight through to an `[UnmanagedCallersOnly]` entry |

## The C# variant

C# registers a bare function pointer of its own shape:

```cpp
using FGMPOnFireFn = void (*)(int64 /*cbHandle*/, const FGMPTypedAddr* /*paddrs*/, int32 /*numArgs*/);
```

Structurally the same idea — context first, then arguments — with the context being a managed delegate id
rather than a `void* Self`. Native never dereferences it; it is handed back untouched. No reflection and no
marshalling on that path.

## In Shipping

With `GMP_WITH_TYPENAME` off, `FGMPTypedAddr` is layout-identical to `uint64`, so `Params` is literally an
array of addresses. Type information, if needed, comes from `Extra->TypeNames` and `Extra->Size`.

## See also

[[TGMPFunction]] · [[FGMPTypedAddr]] · [[FGMPExtra]] · [[Transparent rewrite]]
