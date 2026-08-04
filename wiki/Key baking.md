# Key baking

How a string written at a call site stops being a string before the program runs.

## The naive path

```
"Common.Action"  ->  FName  ->  TMap hash lookup  ->  signal store  ->  dispatch
```

Messages are a high-frequency path — hundreds or thousands per frame is ordinary — and the string is
already known when the compiler sees it.

![lookup vs baked](../docs/img/07-key-lookup-vs-baked.webp)

## The baked path

```cpp
C_STRING_TYPE("Common.Action")   // characters become a compile-time type
GetKeySlot<KeyT>().GetStore()    // the one static store for that type, in this process
```

Because the key is a *type*, the store can be a static bound to that type. No hashing, no name.

![key baking](../docs/img/18-key-baking.webp)

| Build | What `GetStore()` compiles to |
|---|---|
| monolithic | a direct field read |
| modular | resolved once on the first call, then cached in the slot |

**Either way the hash lookup happens zero times per send.**

## Why the slot is a Meyers singleton

A function-local static with vague linkage: every translation unit that instantiates the template refers to
the same object, and the initialisation is thread-safe. Without that guarantee two modules could each get
their own store for the same key, and listeners registered through one would be invisible to sends through
the other.

## What you have to do to get it

Nothing, if you use `MSGKEY(...)` with a literal. The typed direct path is on by default
(`GMP_WITH_DIRECT_SIGNAL`). What is **not** on by default is inlining the dispatch loop itself — see
[[Build switches]].

Using a runtime-assembled string instead of a literal opts you out silently: it cannot become a type, so it
takes the lookup path.

## Measured effect

See [[Build switches]] for the frame-count table and the reproduction commands.

## See also

[[MSGKEY]] · [[Build switches]] · [[TGMPFunction]]
