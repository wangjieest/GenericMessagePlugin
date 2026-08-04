# NotifyMessage family

Every form of sending a message.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPUtils.h` (`FGMPHelper`)

```cpp
template<typename KeyT, typename... TArgs>
static auto NotifyObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);

template<typename KeyT, typename... TArgs>
static auto NotifyWorldMessage(const UWorld* InWorld,      const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
template<typename KeyT, typename... TArgs>
static auto NotifyWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);

template<typename KeyT, typename... TArgs>
static auto SendObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
template<typename KeyT, typename... TArgs>
static auto SendWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
```

Plus the global `NotifyMessage(K, Args...)` with no source.

![which listeners a send reaches](../docs/img/01-dispatch-layers.webp)

## Notify or Send

Both dispatch. The distinction is what the **last argument** may be:

- `Send*` accepts a trailing callable, which turns the call into a request and routes the reply back to it.
  See [[FGMPResponder]].
- `Notify*` is the plain fire-and-forget form.

If you are not doing request/response, either name works; `Notify` states the intent.

## Layer

`Object` / `World` / global selects the dispatch layer, not the delivery mechanism —
see [[Dispatch layers]].

## Arguments are passed by address

Arguments are not copied into a queue. Their addresses are packed into an [[FGMPTypedAddr]] array valid for
the duration of the dispatch, and every matching listener runs **synchronously before the call returns**.

Two consequences:

- Passing a temporary is fine — it outlives the dispatch.
- A listener that stores a pointer to a parameter and reads it later is reading freed memory. To keep a
  value, copy it, or use [[StoreObjectMessage]].

## Reentrancy

Because dispatch is synchronous, a listener may send another message, including on the same tag. Listener
lists tolerate mutation during dispatch (a listener may unlisten itself, and `Times` reaching zero does
exactly that). Unbounded recursion on the same tag is still your problem to avoid.

## See also

[[ListenMessage family]] · [[Dispatch layers]] · [[FSigSource]] · [[StoreObjectMessage]]
