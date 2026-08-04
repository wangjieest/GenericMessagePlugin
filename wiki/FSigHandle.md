# FSigHandle

RAII ownership of listener registrations. Destroying it unregisters everything registered through it.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPSignals.inl:89`

```cpp
// auto disconnect signals
class FSigHandle : public FSigCollection
{
public:
    ~FSigHandle() { DisconnectAll(); }
};
```

Aliased as `FGMPSignalsHandle` in `GMP/Shared/GMPCore.h:45`.

## What problem it solves

A listener outliving its owner is a dangling call. For `UObject` owners GMP can check weak-pointer
staleness, but **a plain C++ class has nothing to check** — so it needs an owner object whose destructor
does the unregistering. That object is `FSigHandle`.

```cpp
class FMyThing            // not a UObject
{
    GMP::FSigHandle Handle;
public:
    FMyThing()
    {
        FGMPHelper::ListenMessage(MSGKEY("Common.Action"), &Handle,
            [this](Type1& P1){ /* ... */ });
    }
    // ~FMyThing destroys Handle, which disconnects the listener
};
```

`GMPSignalsInc.h:28` declares a `GMPSignalHandle` member for classes that want this by convention rather
than by hand.

![FSigHandle among the everyday helpers](../docs/img/19-handy-bits.webp)

## Behaviour

- **Disconnects all**, not one. It is a collection (`FSigCollection`); every registration made against it
  goes away together.
- **Order is not guaranteed** across registrations, and should not be relied on.
- Destroying the handle is the only thing that has to happen. There is no explicit teardown call to forget.
- A handle is not copyable in a meaningful sense — it owns registrations, so treat it as a member, not a
  value to pass around.

## When you do not need it

- The owner is a `UObject` — pass the object itself; staleness is checked on dispatch.
- The owner is shared-pointer managed — `CreateSPLambda` covers it.
- You want to unregister one specific listener rather than all of them — keep the [[FGMPKey]] returned by
  the listen call and unlisten with that.

## Common mistake

Registering with `this` from a non-`UObject` class and expecting it to be safe. It is not; there is nothing
for GMP to check. Either hold an `FSigHandle` or manage the [[FGMPKey]] yourself.

## See also

[[ListenMessage family]] · [[FGMPKey]] · [[FSigSource]]
