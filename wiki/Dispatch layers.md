# Dispatch layers

Which listeners hear a send.

## The three layers

```cpp
NotifyObjectMessage(Actor, KEY, ...);   // narrow: this source
NotifyWorldMessage (World, KEY, ...);   // this world
NotifyMessage      (       KEY, ...);   // global
```

![dispatch layers](../docs/img/01-dispatch-layers.webp)

## The rule

A send at a narrow layer reaches listeners at that layer **and every broader one**. A send at a broad layer
does **not** reach narrower listeners.

| Send at | Reaches object listeners on that source | Reaches world listeners | Reaches global listeners |
|---|---|---|---|
| Object | yes | yes (that object's world) | yes |
| World | no | yes | yes |
| Global | no | no | yes |

The comparison is: a listener matches if its recorded source equals the send's source, equals the send's
world, or is the "any" source.

## Why the World layer exists

Under PIE several worlds run in one process. Listening at world level means a message from one PIE instance
never reaches the other, without writing an instance check. If your process only ever has one world, you
can ignore this layer entirely.

Note that `NotifyWorldMessage` accepts either a `UWorld*` or any object with a world context.

## Choosing a layer

- **Object** — the message is about a specific thing, and listeners care about that thing. Highest
  selectivity, cheapest dispatch, because non-matching listeners are filtered in the store rather than in
  your callback.
- **World** — the message is about a session or a level.
- **Global** — genuine process-wide events. Easy to over-use; a global tag that half the project listens to
  is a fan-out you cannot see from any single call site.

## Filtering is not done in your callback

Object-level dispatch reads like an extra leading key:

```cpp
// in spirit only
NotifyMessage(KEY, Obj, P1, P2);
ListenMessage(KEY, this, [Obj](UObject* In, Type1& P1){ if (In != Obj) return; ... });
```

The real implementation matches inside the store, so unmatched listeners are never invoked and never cost
you a call.

## See also

[[FSigSource]] · [[NotifyMessage family]] · [[ListenMessage family]]
