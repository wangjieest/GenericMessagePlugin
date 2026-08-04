# GMP · GenericMessagePlugin

**English** | [简体中文](README_CN.md)

**One messaging system for C++, Blueprint, and five scripting languages.**

Sender and listener share a string, not a header — so you can delete a module and nothing is waiting to break the build.

[Unreal Marketplace](https://www.unrealengine.com/marketplace/en-US/product/genericmessageplugin-gmp) · [Archived README](README_old.md) · [Measured dispatch stack](docs/dispatch-stack-measured.md)

---

## Why

Wire modules together with plain UE delegates and you will run into the same three things:

- Static signatures live in a shared header; the more translation units include it, the wider one edit ripples
- The more plugin-shaped the project gets, the harder the dependency graph is to reason about
- Blueprint is a second system on the side — Interface plus Dispatcher, wired case by case

Which adds up to: you want to delete a module, and the compiler says no. **The coupling lives in the build graph. You think you are splitting logic; you are splitting translation units.**

![coupling through the build graph](docs/img/15-coupling.webp)

## Two lines

```cpp
FGMPHelper::NotifyMessage(MSGKEY("Common.Action"), P1, P2);

FGMPHelper::ListenMessage(MSGKEY("Common.Action"), this,
    [this](Type1& P1, Type2& P2){ /* ... */ });
```

C++, Blueprint and scripts all use the same key.

![one key, no shared header](docs/img/16-key-contract.webp)

The usual first reaction: a string has no type checking, what happens when I get it wrong? Signatures are collected and validated in the editor, Blueprint nodes grow their pins from that table, and scripts get completion and red squiggles. **Nothing about safety is missing — the checking just happens somewhere else.**

## What it does

![capability map](docs/img/17-capability-map.webp)


# Part I · Using it

*What the plugin gives you, and how it reads at the call site.*

## Messaging

### Three dispatch layers

```cpp
NotifyObjectMessage(Actor, KEY, ...);   // per object
NotifyWorldMessage (World, KEY, ...);   // per world
NotifyMessage      (       KEY, ...);   // global
```

Send from an Actor and all three kinds of listener hear it: the ones watching that Actor, the ones watching its World, and the global ones. **Not the other way around** — a World-level broadcast does not reach a listener bound to one specific Actor.

The World layer isolates PIE instances for free; you never have to work out which instance you are in.

![dispatch layers](docs/img/01-dispatch-layers.webp)

The source is not limited to `UObject`. Derive from `ISigSource`, or register with `GMP_EXTERNAL_SIGSOURCE`, and **any memory address can be a message source** — including your own data structures, so others can "subscribe to it changing".

### Times and order

```cpp
ListenMessage(KEY, this, cb, { .Times = 3, .Order = -10 });
```

- `Times` defaults to -1 (forever). Set N and the listener unlistens itself after N calls.
- `Order` defaults to 0, lower runs first; equal orders keep registration order.

Order is packed into the high bits of the GMPKey and sorted once before firing — no extra structure to maintain. The sort is stable, so ties are naturally FIFO.

![times and order](docs/img/02-times-order.webp)

### Request / response

```cpp
// pass a callback as the last argument and the send becomes a request
SendObjectMessage(Src, KEY, Args..., [](FResult& r){ /* reply arrived */ });

// take FGMPResponder& as the last parameter and GMP knows at compile time
ListenObjectMessage(Src, KEY, this,
    [](FArgs& a, FGMPResponder& Rsp){ Rsp.Response(FResult{...}); });
```

Request and reply are matched by an incrementing Seq; the callback is one-shot and destroyed when used. No more defining two keys for one async query.

![request and response](docs/img/03-request-response.webp)

### Sticky messages

```cpp
StoreObjectMessage(Actor, MSGKEY("game.ready"), Data);  // keep the latest
OnceObjectMessage (Actor, MSGKEY("boot.done"),  Data);  // deliver exactly once

// a listener registered later still gets it immediately
ListenObjectMessage(Actor, MSGKEY("game.ready"), this, [](FData& d){ ... });
```

This is for the ordering problem: the broadcast happened before anyone was listening. `Store` keeps the latest value and is a good fit for state; `Once` is consumed by the first listener and then deleted, which fits "initialization finished" style notifications.

Payloads are packed into a GC-safe table indexed by signal and source. `ExactObjName` lets one object carry several independent sticky messages.

![sticky messages](docs/img/04-store-message.webp)

### A stored array is a table

When the stored value is a single `TArray` of `USTRUCT`, a listener can take the whole array, one fixed row, or every row that changed:

```cpp
StoreObjectMessage(Obj, MSGKEY("Inv.Items"), MyItems);   // TArray<FItem>, the same call as above

// the list itself: capacity and structure
ListenObjectMessage(Obj, MSGKEY("Inv.Items"), this,
    [this](const TArray<FItem>& All, const FGMPStoreUpdate& U){ ... });   // a reference, not a copy

// one row widget: fires only when position 5 now shows something else
ListenObjectMessage(Obj, MSGKEY("Inv.Items"), 5, this,
    [this](int32 Id, const FString& Name, int32 Count){ ... });
```

Storing the array again publishes what actually differs, so the sender never has to describe its own edit. To edit in place instead, `TGMPStoredArray<FItem>` behaves like the array, accumulates the changes and fires once when it goes out of scope. This is also the second way to say “the same key, a different one of these”: the first is a different source per instance, and a table adds one key with N rows — which fits instances that come and go, since a row consumer holds a position rather than an object.

![a stored array is a table](docs/img/26-collection-shapes.webp)

The row form expands the element's members positionally through reflection, so the receiving module never includes the type. A trailing `const FGMPStoreUpdate&` is what opts a lambda into any of this — every other listener is untouched. The row index doubles as the switch for being told when that row goes away: `5` follows slot 5 quietly, `GMP::WithRemoval(5)` also reports it disappearing, `GMP::AllRows` takes every changed row. Blueprint and the script backends get the same forms — right-click a listen node and switch it to *Row*, or call `ListenRowMessage` from Lua and TypeScript.

A table not worth keeping — one recomputed every tick — can just be sent: a plain `SendObjectMessage` of a `TArray` reaches the `AllRows` listeners too, reading the table off the sender's own argument. Only that form, and always as a full reload: with nothing stored there is no previous table to diff against, so a slot subscription would fire on every send rather than only for its own row, and is left alone instead. Details in [Collection messages](https://github.com/wangjieest/GenericMessagePlugin/wiki/Collection-messages).

### Parameter compatibility: listeners may drop trailing arguments

After `SendMessage(MSGKEY("ABC"), a, b, c)`, all of these are compatible:

```cpp
ListenMessage(MSGKEY("ABC"), this, [](TypeA a, TypeB b, TypeC c){});
ListenMessage(MSGKEY("ABC"), this, [](TypeA a, TypeB b){});
ListenMessage(MSGKEY("ABC"), this, [](TypeA a){});
ListenMessage(MSGKEY("ABC"), this, [](){});
```

The semantics mirror default function arguments: **you can append parameters to an existing message without touching the listeners already out there.**

### Signature inference

You do not have to declare the tag in C++ first — the first use records it. Inference works from both ends:

- **Send side**: for an unregistered tag, types are inferred from the actual script values (lua numbers split into integer and float; boolean, string and userdata map across)
- **Receive side**: statically typed backends infer from the callback parameters — AngelScript named methods expose parm properties, C# generic callbacks carry type tags. Lua callbacks are dynamic and carry no static types, so they cannot be inferred

Once it is in the table, validation, IntelliSense and codegen all follow. This runs under Editor and Development (`GMP_WITH_DYNAMIC_CALL_CHECK`) and is compiled out entirely in Shipping.

![signature inference](docs/img/13-signature-inference.webp)

---

## Blueprint

### Self-describing, self-validating

**Self-describing** — drop a GMP message node in a graph, pick a Tag from the dropdown, and the node grows that tag's parameter pins from the signature table: right types, right names, right defaults. Change the tag and the pins rebuild on the spot. The pin layout is a projection of the signature; there is nothing to configure by hand.

**Self-validating** — because the pin types come from that same table, connecting the wrong type is rejected at Blueprint compile time rather than at runtime. The check runs in the uncook-stage Blueprint compile, so the compiled asset carries no extra validation payload. Under Editor and Development there is a second, runtime consistency check (`GMP_WITH_DYNAMIC_CALL_CHECK`): a signature that contradicts the recorded one warns and aborts that dispatch; a compatible one updates the table.

![message node](docs/img/10-message-node.webp)

### Neuron: an extensible node base

The message node is just one of them. Underneath is **Neuron**, a base for self-describing K2Nodes — pins carry a PersistentGuid, so rebuilding a node does not lose pin identity and the wires you already connected stay connected.

What grows on top of it:

| Node | What it does |
|---|---|
| **NeuronAction** | Point it at an async factory function and the node expands itself: inputs are the spawn params, every callback delegate becomes its own output exec pin (with its own data pins), plus a Cancel |
| **GenericInvoker** | Walk a member chain to a target, read a member or call a function. ExpandNode flattens the chain to FName literals, so **the compiled Blueprint holds no hard reference to the target class** |
| **StructUnion family** | Set/Get StructUnion, StructTuple, DynStructOnScope — packing and reading heterogeneous data |
| FormatStr / EventGraphFunction / DerefParam | Small, frequently used |

GMP ships a NeuronAction of its own — `UGMPJsonHttpUtils`, opted in right on the class with `meta = (NeuronAction)`:

![NeuronAction: the HTTP node GMP ships](docs/img/11-neuron-action.webp)

`CustomStructureParam` makes both the request and response bodies wildcards, typed by whatever you plug in; the response JSON is deserialized straight into your struct **before** the exec pin fires — no hand-written parsing, and no proxy object to keep alive.

---

## Scripting

### Five languages, same source

UnLua, slua, Puerts, AngelScript, C#.

```lua
-- write it the way you always did
NotifyObjectMessage(self, "Player.Hurt", dmg, causer)
```

At load or compile time that line is rewritten into a strongly typed, key-baked call. Four languages, four hook points:

| Backend | When it is rewritten |
|---|---|
| UnLua / slua | Load time, on the text (`FUnLuaDelegates::CustomLoadLuaFile` / `setLoadFileDelegate`) |
| AngelScript | Pre-compile preprocessor (`OnPostProcessCode`) |
| Puerts | AST transform inside tsc |
| C# | No rewrite needed — it is statically typed, and generic `MsgTag<T...>` lets the compiler pin the types |

![transparent rewrite](docs/img/05-script-rewrite.webp)

Designers keep writing the same generic `NotifyObjectMessage`, unchanged. What actually runs is the strongly typed function generated at compile time, on the baked-key fast path.

### IntelliSense

The signature table is codegen'd into each language's own declaration form. Wrong type, red squiggle, right where you typed it. Change the signature and it regenerates.

![IntelliSense](docs/img/14-intellisense.webp)

### Jump tracing

Every script send and listen records the call site's file and line using the debug facility **each language engine already maintains** — the standard debug library for lua, v8 StackTrace for Puerts, the active context for AngelScript, compiler-injected CallerFilePath for C#. **GMP patches none of them; it only reads what they already track.**

Click in the MessageTag panel and your IDE opens that file on that line. The same panel lists the Blueprint nodes and assets that reference the tag, side by side.

![jump tracing](docs/img/12-jump-trace.webp)

---

## Interop with existing code

**RefEvent** — call a Blueprint event from C++ and get a result back. A Blueprint CustomEvent is void and has no return value, so the out parameter is written straight into the caller's stack variable:

```cpp
int32 out = -1;
TGMPBPFastCall<void(int32, int32&)>::FastInvoke(Obj, Func, 21, out);  // out == 42
```

It takes the compile-time signature-matching fast path, not the `ProcessEvent` reflection route.

**InlineHook** — inline hooks for arbitrary non-virtual functions, on Windows, Linux and Android.

What both have in common: **the other side does not have to change.**

![RefEvent](docs/img/06-refevent.webp)

### The small things

![handy bits](docs/img/19-handy-bits.webp)

- `FSigHandle` — RAII, unlistens on destruction, safe for non-UObject owners
- The `CreateWeakLambda` family — `this` plus a lambda in one line; smart pointers too (`CreateSPLambda`)
- `LocalSharedStorage` — named shared data scoped to a World, type safe
- `RpcMessageUtils` — a MSGKEY is the RPC interface, riding UE's own network serialization
- `GMPArchive` / `GMPJson` / Protobuf (upb) / YAML — all bridged to UStruct reflection; `FGMPValueOneOf` for dynamic access
- `TGMPNativeInterface` — messaging over native interfaces
- `Class2Name` / `Class2Prop` — type to name to `FProperty*`; saves a lot of work when writing libraries and support code

---

## Install

1. Drop `Plugins/GMP` into your project's `Plugins/` folder (or the engine's `Engine/Plugins/`)
2. Enable GMP in your `.uproject`
3. Regenerate project files and build

Also available on the [Unreal Marketplace](https://www.unrealengine.com/marketplace/en-US/product/genericmessageplugin-gmp).


# Part II · Under the hood

*Why none of the above costs you anything at runtime. The numbers here were measured, not estimated.*

## Where one message goes

The naive path is `"Common.Action"` → `FName` → `TMap` hash lookup → store → dispatch. Messages are high frequency — hundreds or thousands a frame is normal — and hashing every single time does not pay. **And that string is already known at compile time.**

![lookup vs baked](docs/img/07-key-lookup-vs-baked.webp)

## Key baking

![key baking](docs/img/18-key-baking.webp)

```cpp
C_STRING_TYPE("Common.Action")   // a compile-time type, not a runtime string
GetKeySlot<KeyT>().GetStore()    // the one static store for this process
```

Monolithic: a single field read. Modular: resolved once on the first call, then cached in the slot. **Either way the hash lookup happens zero times per send.** The slot is a Meyers singleton, so vague linkage cannot hand out two of them.

## How deep the dispatch stack actually is (measured)

A unit test calls `FPlatformStackWalk::CaptureStackBackTrace` inside the listener and counts the GMP frames between the send call and the callback:

| Build | by-name (FName + lookup) | by-store (baked key) |
|---|---|---|
| Unoptimized (DebugGame) | 9 | 7 |
| Optimized (Development) | 4 | **3** |

Most of those 9 unoptimized frames are type-erasure scaffolding — the adapter, the dispatch lambda, `FlexBackendThunk`, `TGMPFunction::operator()`, the unpack thunk. With optimization on, the compiler eats all five and the dispatch loop lands directly on your callback.

![measured dispatch stack](docs/img/08-inline-fire.webp)

Of the remaining 3, `GMPFireWithSigSourceDirectRaw` is exported with `GMP_API`; in a modular build that is a DLL boundary the optimizer cannot inline through. `GMP_WITH_INLINE_FIRE=1` plus monolithic tears that wall down: `GMP_API` expands to nothing, the dispatch loop sits FORCEINLINE in the header and expands into the caller, and the first two of those three frames go away with it.

> Raw symbolized stacks and the exact commands are in [docs/dispatch-stack-measured.md](docs/dispatch-stack-measured.md). Both rows are modular builds; INLINE_FIRE requires monolithic.

## The last hop

A listener holds a thunk function pointer and an object address, **not a vtable**:

```cpp
reinterpret_cast<R(*)(void*, Args...)>(GetCallable())(GetObj(), Args...);
```

That sits in tail position, so -O2 makes it a sibling call — a plain `jmp`, no new stack frame. Small lambdas live inline in the 16-byte slot via SBO and never touch the heap.

What is left is one indirect jump whose target is only known at runtime — which is the floor for the observer pattern itself. The sender is not supposed to know who is listening.

![the last hop](docs/img/20-tail-call.webp)

## One stable C ABI

Callbacks from all five languages normalize to the same C signature. After type erasure, what actually crosses the boundary is this bare function pointer:

```cpp
void (*)(void* Self, const FGMPTypedAddr* Params, const FGMPExtra* Extra);
```

Three parameters. `Self` is **the callable's own address** — not a UObject, not a vtable pointer — so this hop is an indirect jump, not a virtual dispatch.

`Params` is the array of per-argument erased addresses. Type names travel on two separate channels; do not conflate them:

```cpp
struct FGMPTypedAddr {           // GMPStruct.h
    uint64 Value = 0;            // always: the address, erased to uint64
#if GMP_WITH_TYPENAME            // = DYNAMIC_TYPE_CHECK || DYNAMIC_CALL_CHECK || TYPE_INFO_EXTENSION
    FName TypeName;              // rides along only in Editor / Development
#endif
};

struct FGMPExtra {               // same file
    int32 Size;                  // argument count
    const FName* TypeNames;      // one static table per signature
    FSigSource Source; FName Key; FGMPKey Seq;
};
```

So **in Shipping `FGMPTypedAddr` collapses to a bare `uint64`** and `Params` really is nothing but an address array — the per-argument names are compiled out entirely. Anything that needs type information reads `Extra->TypeNames` together with `Extra->Size`. Arguments cross the language boundary as a plain C array of addresses, and each language implements exactly this one entry point.

![C ABI hub](docs/img/09-c-abi-hub.webp)

C# takes the contract furthest: it registers a bare function pointer to an `[UnmanagedCallersOnly]` entry, and native calls straight through on fire — **no reflection, no marshalling**.

## What is left of one message

![what remains](docs/img/21-what-remains.webp)

---

## Build switches

| Macro | Default | What it does |
|---|---|---|
| `GMP_SIGNAL_BACKEND_FLEX` | `1` | Pluggable signal backend; storage, ABI and handler concerns are orthogonal policies, so the backend swaps without touching call sites |
| `GMP_WITH_DIRECT_SIGNAL` | `1` | Typed direct-send path |
| `GMP_STATIC_STORE_MONOLITHIC` | `IS_MONOLITHIC` | Precondition for baking a key down to a static store |
| `GMP_WITH_STATIC_STORE` | derived | `DIRECT_SIGNAL && STATIC_STORE_MONOLITHIC` |
| `GMP_WITH_INLINE_FIRE` | `0` | Inlines the dispatch loop into the caller; **only takes effect under monolithic** (`GMP_WITH_INLINE_FIRE_ENABLED = INLINE_FIRE && STATIC_STORE`) |
| `GMP_WITH_DYNAMIC_CALL_CHECK` | `1` in Editor/Dev, `0` in Shipping | Signature consistency checks and signature inference |
| `GMP_WITH_STATIC_MSGKEY` | `!WITH_EDITOR` | Drops the MsgKey string from the runtime |
| `GMP_SLUA_STATIC_BIND` / `GMP_UNLUA_STATIC_BIND` / `GMP_PUERTS_STATIC_BIND` / `GMP_CSHARP_STATIC_BIND` | `0` | Codegen'd static binding per script backend |

`GMP_WITH_INLINE_FIRE` is off by default: the default build keeps the cross-DLL boundary clean and avoids code-size growth, paying one constant out-of-line call per fire. The extreme path is a deliberate opt-in for monolithic shipping builds.

Every configuration — modular and monolithic, default backend and Flex backend, inline and out-of-line fire — runs the same unit-test suite, **69 tests, all green**. The layered switches trade binary size for overhead, never correctness.

---

---

## Further reading

- [Archived README](README_old.md) — the pre-rewrite document, kept verbatim. Still the only place that walks through the original design reasoning: object-oriented message passing, type erasure, `FSigSource`, `FMessageBody`, `Class2Name` / `Class2Prop`. Its performance section is superseded by the measurements above
- [Measured dispatch stack](docs/dispatch-stack-measured.md) — raw symbolized frames and how to reproduce them

## License

See [LICENSE](LICENSE).
