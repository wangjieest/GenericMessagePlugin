# StoreObjectMessage / OnceObjectMessage

Messages that survive the moment they were sent, so a listener registering later still receives them.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPUtils.h` (`FGMPHelper`)

```cpp
template<typename KeyT, typename... TArgs>
static auto StoreObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);
template<typename KeyT, typename... TArgs>
static auto OnceObjectMessage (const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args);

static auto RemoveStoredObjectMessage(const UObject* InObj, const MSGKEY_TYPE& K);
```

![sticky messages](../docs/img/04-store-message.webp)

## The problem they solve

A plain send reaches whoever is listening at that instant. If the broadcast happens before the listener
registers, the listener never learns of it — and initialisation order is rarely under your control.

## Which one

| | Kept after delivery | Overwritten by a later send | Use for |
|---|---|---|---|
| `StoreObjectMessage` | yes, indefinitely | yes — latest value wins | **state**: ready / not ready, current phase, latest result |
| `OnceObjectMessage` | no — deleted after the first listener consumes it | n/a | **a notification that must arrive exactly once**: initialisation done, order paid |

`RemoveStoredObjectMessage` clears a stored message explicitly.

## What it does to your control flow

The usual shape without it is two paths — read the current value now, *and* subscribe for later — which
have to agree with each other. With a stored message both collapse into one `ListenObjectMessage`, and
"already happened" versus "about to happen" stops being a distinction the caller has to make.

## Storage

Arguments are **copied** into a GC-safe table keyed by signal and source (the container is
`FGMPStoreSourceMsgs`, a `TMap<FSigSource, FGMPStructUnion>` — see [[FGMPStructUnion]]). This is the
difference from a plain send, which only passes addresses: a stored message owns its payload, so object
references stay alive and are visible to the collector.

When the source object is destroyed, its stored messages go with it.

## Multiple instances, multiple messages

Keys are global, which raises two separate questions:

- *Several instances of the same thing* — use a different **source object** per instance. Each keeps its
  own stored message under the same key.
- *One object needing several independent stored messages* — distinguish them with **`ExactObjName`**.

## Cost

A stored message is a live allocation until it is consumed, overwritten or its source dies. Storing
high-frequency messages is not the intent; store state, send events.

## Storing a table

When the stored value is a single `TArray` of `USTRUCT`, it can also be read as a table: a listener takes the
whole array, one fixed row, or every row that changed, and rewriting the array publishes what actually differs.
Storing and listening are the same two calls as above — see [[Collection messages]].

## See also

[[Collection messages]] · [[NotifyMessage family]] · [[FGMPStructUnion]] · [[FSigSource]]
