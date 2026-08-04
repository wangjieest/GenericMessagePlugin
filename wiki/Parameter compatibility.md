# Parameter compatibility

The rule that decides whether a listener written against an older version of a message still matches.

## The rule

**A listener may drop trailing arguments. A prefix must match by type.**

After this send:

```cpp
SendMessage(MSGKEY("ABC"), a, b, c);   // TypeA, TypeB, TypeC
```

all four of these listeners are compatible:

```cpp
ListenMessage(MSGKEY("ABC"), this, [](TypeA a, TypeB b, TypeC c){});
ListenMessage(MSGKEY("ABC"), this, [](TypeA a, TypeB b){});
ListenMessage(MSGKEY("ABC"), this, [](TypeA a){});
ListenMessage(MSGKEY("ABC"), this, [](){});
```

These are not:

```cpp
ListenMessage(MSGKEY("ABC"), this, [](TypeB b){});              // wrong type at position 0
ListenMessage(MSGKEY("ABC"), this, [](TypeA a, TypeC c){});     // gap; position 1 must be TypeB
ListenMessage(MSGKEY("ABC"), this, [](TypeA, TypeB, TypeC, T4){});  // longer than the send
```

The semantics mirror default function arguments — the listener sees a prefix of what was sent.

![a tag growing trailing parameters while older listeners keep matching](../docs/img/13-signature-inference.webp)

## Why it exists

So a message can gain a parameter without a coordinated edit. Append to the end of the send and every
existing listener keeps compiling and keeps working; only the listeners that want the new value are
touched.

This is the migration path for a system whose whole point is that sender and listener do not share a
header. Without it, adding one argument would mean finding every listener first.

## Consequences worth planning for

- **Order your parameters by stability.** Things you might add later go at the end. A parameter inserted in
  the middle is a breaking change, and nothing will tell you at C++ compile time — the mismatch surfaces
  as a validation failure at runtime, in the editor.
- **Removing or reordering is always breaking.** Only appending is safe.
- **A zero-argument listener matches everything on that tag.** Useful for "something happened" handlers,
  and a trap if you meant to receive a value and mistyped the lambda.

## Where it is enforced

The recorded signature lives in [[UGMPMeta]]. Comparison happens in the hub when a listener registers and
when a message is sent, under `GMP_WITH_DYNAMIC_CALL_CHECK` — on in Editor and Development, compiled out in
Shipping. A mismatch warns and aborts that dispatch rather than calling with wrong types.

Blueprint gets the same rule earlier: node pins come from the recorded signature, so an incompatible
connection fails at Blueprint compile time. See [[UK2Neuron]].

## See also

[[Signature inference]] · [[UGMPMeta]] · [[ListenMessage family]] · [[Class2Name]]
