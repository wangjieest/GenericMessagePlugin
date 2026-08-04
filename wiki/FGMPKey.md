# FGMPKey

Identity of one listener registration, and the carrier of its ordering.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPKey.h:52`

```cpp
USTRUCT()
struct FGMPKey
{
    GENERATED_BODY()
public:
    FGMPKey(int64 In = 0) : Key(In) {}

    UPROPERTY()
    int64 Key;

    FORCEINLINE auto GetKey() const { return Key; }
    FORCEINLINE bool IsValid() const { return !!Key; }
    operator int64() const { return Key; }
    explicit operator bool() const { return IsValid(); }

    GMP_API static FGMPKey NextGMPKey();
    GMP_API static FGMPKey NextGMPKey(GMP::FGMPListenOptions Options);
};
```

## What it is for

Every `ListenMessage` returns one. Two uses:

- **Unregister a single listener** — `UnListenMessage(FMSGKEYFind(...), Key)`. Without the key you can only
  unregister *all* of an owner's listeners.
- **Ordering** — the `Order` from `FGMPListenOptions` is encoded into the high bits by
  `NextGMPKey(Options)`.

![order decides sequence, ties fall back to registration](../docs/img/02-times-order.webp)

## Why order lives in the key

Sorting listeners by `FGMPKey` sorts them by `Order` first and by creation sequence second, because the low
bits are a monotonically increasing counter. That gives two properties for free:

- lower `Order` runs first
- **equal `Order` keeps registration order**, since the counter breaks the tie

No separate ordering structure is maintained, and the pre-fire sort is a plain integer sort.

## Validity

`0` is the invalid key; `IsValid()` and `explicit operator bool()` both test that. A failed registration
returns an invalid key rather than throwing, so a caller that cares must check.

The implicit `operator int64()` makes the value easy to log and compare, but also means an unchecked key
converts silently to `0` in arithmetic contexts — prefer `IsValid()` to `!Key` in code others will read.

## See also

[[ListenMessage family]] · [[FSigHandle]] · [[FGMPExtra]]
