# Four frames per send: collapsing GMP's C++ dispatch

This is a write-up of a round of optimisation in GMP (GenericMessagePlugin, a decoupled messaging plugin for UE), aimed at the lookup and call-stack overhead on the C++-to-C++ path.

## 1. The result first

The two versions being compared:

**Before**: one `SendObjectMessage` crossed several separate dispatch frames, and picked up a string of runtime work along the way — look the store up by name, look the listener list up by source, pack the arguments and convert types, check signature compatibility.

**After**: on the C++ path, when the key is known at compile time (`MSGKEY_SLOT`), the build is monolithic, and `GMP_WITH_INLINE_FIRE=1` is on, the whole send + fire is inlined into the caller, the store is resolved at compile time (no lookup), and control flows straight to the listener callback — four frames: caller → dispatch → listener thunk → your callback.

## 2. Background: what decoupling costs

UE's own delegates are simple and fast enough, but they want forward declarations and header dependencies; collaborating across modules means sharing those dependencies.

GMP's selling point is decoupling — the sender and the receiver never learn each other's types, and a key in a store in the middle connects them. The price is an inherent layer of indirection: find the store by key, walk the listeners in it that match, call them one at a time.

In the old version that indirection was real runtime work:

```
// the actual chain of one send, before
SendObjectMessage
  → SendObjectMessageWrapper          // runtime: argument packing + type conversion + signature check
  → look the store up by FName        // runtime lookup
  → call NotifyMessageImpl            // <- out-of-line dispatch frame (lives in a .cpp)
       → SignalPtr->FireWithSigSource
       → call OnFireWithSigSource      // <- another out-of-line dispatch frame
            // runtime: get the matching listener keys by source, fetch each handler
            → Invoker(Elem) → InvokeSlot
                 → your lambda
```

The cost is not only stack depth. It is the string of runtime work spread along the way: argument packing and type conversion; the signature compatibility check; the store lookup by `FName`; the by-source listener key array and the walk over it; and two separate out-of-line dispatch frames to cross. None of it is large on its own, and together it is the tax decoupling charges.

The question this round set out to answer: **while keeping the decoupled semantics, how much of that indirection can be taken out of the runtime?** The answer came from several directions at once.

## 3. What was done

### 3.1 Compile-time typed direct send: move the store lookup into the compiler

This is the foundation for everything else. A direct-send API (`SendObjectMessageDirect` / `ListenObjectMessageDirect`) and the `MSGKEY_SLOT` mechanism were added.

The idea: **when the key is known at compile time, resolve the store then instead of at runtime.** Under a monolithic build a slot resolves to a per-type static signal store, and `GetStore()` degrades into a direct field read rather than a lookup.

The macro chain that controls it:

```cpp
// GMPMacros.h
#define GMP_STATIC_STORE_MONOLITHIC IS_MONOLITHIC
#define GMP_WITH_STATIC_STORE (GMP_WITH_DIRECT_SIGNAL && GMP_STATIC_STORE_MONOLITHIC)
```

It depends on `IS_MONOLITHIC`, which is why this end of the optimisation only exists in shipping/standalone builds: the editor is modular, a compile-time static store cannot be reached across a DLL boundary, and the by-name slow path is used instead.

That removes the "look the store up by name at runtime" part of the old path.

### 3.2 FlexSignal: a clean, replaceable dispatch backend

The dispatch core became FlexSignal (behind `GMP_SIGNAL_BACKEND_FLEX`, on by default). Its point is splitting three orthogonal dimensions into pluggable policies:

- storage — how the store is organised
- ABI — how argument addresses are packed
- handler — how a callback is invoked

Orthogonal means a backend can be replaced wholesale without touching a single call site. The everyday path is already a light, near-allocation-free walk of the store here.

An architectural rule worth keeping: **turn orthogonal dimensions (ABI / storage / handler) into policies; do not force two incompatible whole architectures together (two lifetime models, say) — let each keep its own place.**

### 3.3 Narrowing the key type: giving the compiler the right to pick the best path

This one is not a performance change directly, but it is what makes the rest possible.

The rule: **Send / Listen on the C++ side accept `MSGKEY` (a compile-time key) only, no bare `FName`.** The `operator FName` on the MSGKEY macro's product became explicit, so no implicit downgrade; the runtime FName overloads were removed, forcing callers onto compile-time keys; script-facing APIs still take FName and the library absorbs the conversion.

Why it matters: **only when the key's type is fixed at compile time is the slot direct-send path eligible to be selected at all.** Narrowing the type is not strictness for its own sake — it is giving the compiler enough information to take the best route.

### 3.4 Lifetime and hot-path cleanup

Two loose ends. A global connection pool lets a listener be disconnected by its `FGMPKey` alone (globally unique), and only the cold connect/disconnect path touches that pool — the fire hot path is untouched. And the fire ordering logic was unified so the inline and out-of-line paths behave identically (the "only sort when there is more than one listener" guard, for instance).

## 4. The main event: how the stack collapses

The three items above laid down the store resolution and path selection. This section is the important one: how a send's call stack goes from that pile down to four frames.

And what makes it possible is not any single inlining switch, but a general ABI underneath — that is the foundation. Inlining only removes the last hop once the foundation is in place.

### The foundation: what crosses the boundary is a reference (a pointer)

The core tension in decoupling: the sender does not know the listener's signature, and listeners are all differently-typed lambdas (different arity, different types). How does a dispatch core call something whose signature it does not know?

GMP's answer, and the whole of its ABI design: **support writing back to arguments; across the boundary do not pass argument values, pass argument addresses (a reference lowered to a pointer).**

Registering a listener generates a dedicated thunk for it at compile time — a small function of fixed signature that `reinterpret_cast`s the `void*` argument addresses back into typed references and calls the lambda. What the dispatch core stores, and what a fire calls, is always that fixed-signature thunk:

```cpp
// to the dispatch core every listener looks exactly like this, whatever its real signature:
void Thunk(void* self, const void* a0, const void* a1);
//          ^ holds the lambda  ^ the argument's address; not a value, a pointer
```

The sending side takes the addresses of the actual arguments and passes them down; the receiving thunk turns them back into references for the lambda:

```cpp
// sending side: a reference lowered to a pointer to cross the boundary (the argument itself is not copied)
static void Dispatch(Thunk thunk, void* self, const A0& a0, const A1& a1) {
    thunk(self, &a0, &a1);
}
// inside the receiving thunk: back into a typed reference, then the real lambda
T& arg = *reinterpret_cast<T*>(const_cast<void*>(a0));
```

"References, not values" is the source of everything this ABI gets, and it buys at least three things:

**1. Zero copying at the boundary.** Whether the argument is a 4-byte int or a several-hundred-byte struct, what crosses is an 8-byte pointer, and the argument itself stays on the sender's stack. Bigger arguments do not cost more to dispatch.

**2. Type erasure happens at that one hop, and neither end loses its types.** The sender knows the real type at compile time (which is how it takes the address); the receiving thunk knows it at compile time too (which is how it casts back). Only the segment between them is `void*`. Type information is intact at both ends; the boundary is just an address channel.

It is also *because* addresses rather than values are passed that all those different signatures can be flattened into one `(self, a0, a1)`: values vary by type, an address is always a pointer. That is the precondition for the normalisation.

**3. Write-back.** The most direct dividend of passing references — a listener receives a `T&`, and what it writes goes back into the sender's object:

```cpp
// declare the parameter as a reference and you write straight into the sender's variable
ListenObjectMessageDirect(Slot, Src, &H, [](int32& V) { V *= 10; });
int32 X = 7;
SendObjectMessageDirect(Slot, FSigSource(Src), X);  // X goes in by reference
// after the fire X == 70 -- the listener wrote back into the sender's X
```

A listener that wants write-back declares a reference `T&`; one that does not declares a value `T` (the adapter `static_cast`s the `T&` down to a value safely, without touching the sender). Whether write-back happens is decided by the listener's own signature, which a plain value-broadcast message system cannot offer.

(One boundary: write-back only applies to a live fire. A delayed replay such as StoreMessage is a value snapshot, because the sender is long gone by then.)

### Two pluggable ABIs

"How arguments reach the thunk" is itself a pluggable dimension, with two implementations:

- **RawAddr ABI** (used by GMP's direct path): `void(self, a0, a1)`, at most two arguments, no type fingerprint, byte-compatible with GMP's existing `GMPInvokeRaw` — which is how the new backend drops onto the old direct pipeline;
- **Paddrs ABI** (the general default): `void(self, paddrs[], Num)`, with type fingerprint checking, any arity, and prefix matching for listeners that drop trailing arguments.

Both obey the same root principle: **addresses cross the boundary**. That is why FlexSignal can be swapped out wholesale without touching call sites — a call site only ever deals with this one ABI.

This normalising, reference-passing ABI is the load-bearing wall of the whole thing.

### The frames, collapsing

With that foundation the collapse follows:

```
before (several frames + lookups and conversions all the way)
   caller →[store lookup]→ dispatch frame 1: NotifyMessageImpl
          → dispatch frame 2: OnFireWithSigSource [listener lookup by source] → thunk → lambda

  ↓ 1. compile-time typed direct: the key resolves to a static store, the lookup disappears entirely

  ↓ 2. static direct path: the two old dispatch frames become one light direct dispatch (the default lands here)
   caller → dispatch frame GMPFireWithSigSourceDirectRaw → thunk → lambda

  ↓ 3. INLINE_FIRE on: that last dispatch frame is inlined into the caller too
   caller (send + fire fully inlined) → call thunk → lambda      <- three frames
```

## 5. Two designs that carry the flexibility

The above is about speed. What makes GMP pleasant to use is two other designs — grown on the same foundation, and worth their own section.

### Two keys: the message's identity and the listener's, kept apart

Many message systems have one key, the message name. GMP has two orthogonal ones:

- **the message key** (`FName`, produced by the compile-time MSGKEY): answers "which message is this, which store does it land in" — the message's identity;
- **the listener key** (`FGMPKey`, a globally unique, monotonically allocated int64 handle): answers "which particular listener instance is this" — the identity of one act of listening.

```cpp
struct FGMPKey {
    int64 Key;                       // globally unique handle
    static FGMPKey NextGMPKey();     // one allocated per listener registration
};
```

The separation buys a few things directly:

- any number of listeners can sit on one message, each with its own `FGMPKey`, independently managed;
- one specific listen can be disconnected by its key alone, without holding the store and without the message name — which is what the global connection pool mentioned earlier relies on, and `FGMPKey` being globally unique makes that safe;
- fire order is deterministic: listeners are sorted by `FGMPKey` before dispatch, and since keys are allocated in increasing order, whoever registered first is called first.

In one line: the message name governs "which class of recipient", `FGMPKey` governs "which one exactly", and having the two decoupled gives you a precise handle on listener management.

### Extensible SigSource: a message source need not be a UObject

GMP's dispatch is layered — an object message is delivered along "the source object → that object's World → global", which lets a listener filter precisely on "only what this object sent". What carries that source is `FSigSource`.

It is a tagged pointer: the source object's address goes into an `intptr_t`, and since addresses aligned to `GMP_SIG_BASE_ALIGN` necessarily leave the low bits free, those bits tag what kind of source it is:

```cpp
struct FSigSource {
    using AddrType = intptr_t;
    enum EAddrMask {
        EObject  = 0x00,   // a plain UObject
        ESignal  = 0x01,   // a signal instance
        External = 0x02,   // an external, non-UObject object
        ExtKey   = 0x04,   // a composite key of object + Name
    };
    AddrType Addr;         // address in the high bits, tag in the low ones
};
```

The important part is that "source" is not hard-wired to UObject. The constructor places one open constraint on the type:

```cpp
template<typename T>
FSigSource(const T* InPtr) {
    static_assert(alignof(T) >= GMP_SIG_BASE_ALIGN &&
        (TExternalSigSource<T>::value          // <- any type may specialise this trait
         || std::is_base_of<UObject, T>::value
         || std::is_base_of<ISigSource, T>::value), "err");
}
```

So any custom type can be a message source, without being a UObject, as long as it is aligned well enough and specialises `TExternalSigSource<T>` to true (or derives from `ISigSource`). An `OnSourceRemoved` hook comes with it, so external types can join GMP's lifetime cleanup and have their listens dropped when the source dies.

Add the ExtKey kind — "object + Name" composites — and even "several named sub-sources on one object" is expressible.

This keeps GMP's layered by-source dispatch from being tied to UObject: plain C++ objects, third-party types, even bespoke logical entities can be senders and filter dimensions.

## Closing

The core of this round is one sentence: **keep the decoupled semantics, and use compile-time information to peel the runtime indirection away a layer at a time.**

- **typed direct send + slot**: the key resolves to a static store at compile time, `GetStore()` degrades to a field read, the runtime lookup is gone
- **the FlexSignal backend**: a clean, replaceable dispatch foundation (three orthogonal policies)
- **narrowing the key type**: tighten the contract so the compiler is allowed to take the best route

Against the old code — one send crossing several separate dispatch frames, with a store lookup by name, a listener lookup by source, argument packing and conversion, and a signature check along the way — the tuned configuration not only drops the lookups but brings a typed send down to four frames: caller → dispatch → listener thunk → your lambda.
