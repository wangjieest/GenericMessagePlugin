# FSigSource

What a message is sent *from*, and what a listener filters *on*. Not restricted to `UObject`.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPSignals.inl`

```cpp
#define GMP_SIG_BASE_ALIGN 8
struct GMP_API alignas(GMP_SIG_BASE_ALIGN) ISigSource
{
    ~ISigSource();
};

struct FSigSource
{
    using AddrType = intptr_t;
    enum EAddrMask : AddrType
    {
        EObject  = 0x00,
        ESignal  = 0x01,
        External = 0x02,
        ExtKey   = 0x04,
        EAll     = GMP_SIG_BASE_ALIGN - 1
    };
    explicit FSigSource(std::nullptr_t = nullptr) {}
};
```

![the layers a source is matched against](../docs/img/01-dispatch-layers.webp)

## It is a tagged pointer

`FSigSource` is one pointer-sized value. Because `ISigSource` is `alignas(8)`, the low three bits of any
source address are always zero, and GMP uses them to record **what kind** of source it is:

| Tag | Meaning |
|---|---|
| `EObject` | a `UObject`; lifetime tracked by weak-pointer staleness |
| `ESignal` | derives from `ISigSource`; its destructor unregisters |
| `External` | a registered external type; lifetime is the caller's problem |
| `ExtKey` | an external key form |

Practical implication: **a source costs one pointer**, there is no wrapper allocation, and comparison is an
integer compare. That is why object-level filtering is cheap enough to do inside the store.

## Three ways to be a source

**1 — Be a `UObject`.** Nothing to do.

**2 — Derive from `ISigSource`.** The destructor unregisters, so lifetime is automatic. Use this for your
own long-lived C++ objects.

**3 — Register an external type** with the `GMP_EXTERNAL_SIGSOURCE` macro / `TExternalSigSource`
specialisation. After that **any address of that type can be a source**, including plain data structures
that others "subscribe to the changes of". This is how the script backends attach a language runtime — the
Puerts isolate is used directly as a source.

## Lifetime, per kind

| Kind | Cleanup |
|---|---|
| `UObject` | automatic; stale objects are skipped on dispatch |
| `ISigSource` | automatic, in the destructor |
| external | **manual** — you must call the cleanup when the object dies |

Missing the manual cleanup for an external source is the failure mode to watch for: the address may be
reused by a later allocation, and messages would then match an unrelated object.

## See also

[[Dispatch layers]] · [[NotifyMessage family]] · [[StoreObjectMessage]]
