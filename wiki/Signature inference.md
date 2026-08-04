# Signature inference

How a tag that was never declared in C++ acquires a recorded signature.

## What it replaces

Without inference, using a tag from script means someone declares it in C++ first. For designers and
technical artists that is a hard dependency on a programmer for something as small as adding one message.

With inference, the first use **is** the declaration.

![signature inference](../docs/img/13-signature-inference.webp)

## Two directions

**Send side.** The tag is unknown, so types come from the values actually passed. Lua numbers are split
into integer and float; boolean, string and userdata map to their reflected equivalents.

**Receive side.** Types come from the callback's declared parameters — but only where the language has
them:

| Backend | Receive-side inference | Why |
|---|---|---|
| AngelScript | yes | named methods expose parm properties |
| C# | yes | generic callbacks carry type tags |
| UnLua / slua | **no** | a lua callback is a dynamic function with no static parameter types |
| Puerts | via codegen declarations rather than runtime inspection | |

So on the lua backends only the send side can teach the table anything.

## What happens after

The signature lands in [[UGMPMeta]], and from there feeds validation ([[Parameter compatibility]]),
Blueprint pin generation ([[UK2Neuron]]) and per-language codegen ([[Transparent rewrite]]).

## Gating

Inference and the validation built on it run under `GMP_WITH_DYNAMIC_CALL_CHECK` — on in Editor and
Development, **compiled out in Shipping**.

The consequence is worth stating plainly: a tag whose signature was never observed in an editor or
development run has nothing recorded for it, so nothing validates it in Shipping either. Inference is a
development-time convenience, not a runtime safety net.

## See also

[[UGMPMeta]] · [[Parameter compatibility]] · [[Transparent rewrite]] · [[Build switches]]
