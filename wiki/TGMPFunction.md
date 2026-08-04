# TGMPFunction

The type-erased callable the dispatch core stores. Deliberately not a vtable.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPFunction.h`

```cpp
#define GMP_FUNCTION_PREDEFINED_ALIGN_SIZE 16

static constexpr int32_t kInlineSize = GMP_FUNCTION_PREDEFINED_INLINE_SIZE;
static constexpr int32_t kAlignSize  = GMP_FUNCTION_PREDEFINED_ALIGN_SIZE;
```

Invocation (`GMPFunction.h:568`):

```cpp
R operator()(TArgs... Args) const
{
    CheckCallable();
    return reinterpret_cast<R (*)(void*, TArgs...)>(GetCallable())(GetObjectAddress(), Args...);
}
```

## The representation

Two things: a **thunk function pointer** and the **callable's own address**. That is the whole erasure —
there is no virtual base, no vtable pointer, and no dynamic dispatch.

The thunk is generated per concrete callable type and simply casts the `void*` back:

```cpp
// Private/GMPFlexBackend.h:39
static void FlexThunk(void* Self, TArgs... Args) { (*static_cast<Func*>(Self))(static_cast<TArgs>(Args)...); }
```

## Why it matters for cost

| Virtual call | TGMPFunction |
|---|---|
| load vptr → load slot → call | pointer already in hand → one indirect call |

And because the cast-and-call sits in **tail position**, `-O2` turns it into a sibling call: a plain `jmp`,
no new stack frame.

![the last hop](../docs/img/20-tail-call.webp)

## Small buffer optimisation

A callable up to `kInlineSize` bytes lives **inside** the storage, aligned to
`GMP_FUNCTION_PREDEFINED_ALIGN_SIZE` (16). No heap allocation, and the callable is contiguous with the
dispatch data.

The practical rule: **keep listener lambdas small**. `[this]` and one or two pointers stay inline. Capturing
a large struct by value spills to the heap and costs an allocation per registration.

## See also

[[FGMPRawSig]] · [[FGMPTypedAddr]] · [[Key baking]] · [[Build switches]]
