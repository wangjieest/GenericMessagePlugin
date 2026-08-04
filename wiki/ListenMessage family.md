# ListenMessage family

Every form of registering a listener.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPUtils.h` (`FGMPHelper`)

```cpp
template<typename KeyT, typename T, typename F>
static FGMPKey ListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f,
                             GMP::FGMPListenOptions Options = {});

template<typename KeyT, typename T, typename F>
static FGMPKey ListenObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f,
                                   GMP::FGMPListenOptions Options = {});

template<typename KeyT, typename T, typename F>
static FGMPKey ListenWorldMessage(const UWorld* InWorld,       const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f,
                                  GMP::FGMPListenOptions Options = {});
template<typename KeyT, typename T, typename F>
static FGMPKey ListenWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f,
                                  GMP::FGMPListenOptions Options = {});
```

Unregistering:

```cpp
static void UnListenMessage(const FMSGKEYFind& K, FGMPKey id);            // one listener
static void UnListenMessage(const FMSGKEYFind& K, const UObject* Owner);  // all of an owner's
```

## The last two arguments are always (owner, callable)

`Listener` is the lifetime owner, `f` is what runs. The owner may be:

| Owner | Lifetime handled by |
|---|---|
| `UObject*` | weak-pointer staleness checked on dispatch |
| `GMP::FSigHandle*` | its destructor — see [[FSigHandle]] |
| shared-pointer types | `CreateSPLambda` and friends |

A raw `this` from a non-`UObject`, non-handle class has **nothing** to check. That is the one combination
to avoid.

## Options

`GMP::FGMPListenOptions` — `Plugins/GMP/Source/GMP/GMP/GMPKey.h:29`:

```cpp
struct FGMPListenOptions : public FGMPListenOrder
{
    int32 Times = -1;                 // -1 = forever
    // int32 Order = 0;               // from FGMPListenOrder, under GMP_WITH_SIGNAL_ORDER
    static FGMPListenOptions Default;
};
```

```cpp
ListenMessage(KEY, this, cb, { .Times = 3, .Order = -10 });
```

- **`Times`** — decremented per call; reaching zero unregisters the listener automatically.
- **`Order`** — lower runs first, default `0`. Equal orders keep registration order, because the value is
  packed into the high bits of the [[FGMPKey]] and the pre-fire sort is stable. `FGMPListenOrder::MinOrder`
  and `MaxOrder` are provided for the extremes.

![times and order](../docs/img/02-times-order.webp)

`Order` is compiled out entirely when `GMP_WITH_SIGNAL_ORDER` is off.

## Callback shape

The callable's parameters determine the signature this listener claims. It may take fewer parameters than
the send provides — see [[Parameter compatibility]]. Declaring `FGMPResponder&` last makes it the
responding listener — see [[FGMPResponder]].

## Return value

An [[FGMPKey]] identifying this registration. Keep it if you intend to unregister that one listener; ignore
it if the owner's lifetime is doing the work.

## See also

[[FSigHandle]] · [[FGMPKey]] · [[Parameter compatibility]] · [[Dispatch layers]]
