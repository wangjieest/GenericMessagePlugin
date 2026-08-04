# Transparent rewrite

How a generic call written in script becomes a typed, key-baked call without the script being edited.

## What the author writes

```lua
NotifyObjectMessage(self, "Player.Hurt", dmg, causer)
```

## What runs

A generated per-tag function with the key already baked and typed argument decoding.

![transparent rewrite](../docs/img/05-script-rewrite.webp)

## Where each backend is intercepted

The hook point is wherever that toolchain can be reached before execution:

| Backend | Stage | Mechanism |
|---|---|---|
| UnLua | load time, on the text | `FUnLuaDelegates::CustomLoadLuaFile` |
| slua | load time, on the text | `setLoadFileDelegate` |
| AngelScript | before compilation | preprocessor hook (`OnPostProcessCode`) |
| Puerts | during compilation | AST transform inside `tsc` |
| C# | not rewritten | statically typed already; generic `MsgTag<T...>` pins types at compile time |

The two lua backends share one rewriter — same lexing problem, so the same implementation handles both.

## Why rewrite instead of a faster generic call

The generic entry point has to look the tag up by name and unpack arguments dynamically, because it cannot
know which tag it is. A generated function knows, so both costs disappear. Rewriting is what lets the
script author keep the readable generic form while the binary gets the specific one.

## Declarations come from the same codegen

| Backend | Emitted |
|---|---|
| UnLua / slua | EmmyLua `---@param` annotations |
| Puerts | `gmp_messages.d.ts` |
| AngelScript | declaration stubs |
| C# | generic `MsgTag<T...>` |

![IntelliSense](../docs/img/14-intellisense.webp)

## Limits worth knowing

- The rewriter matches call **syntax**. A call assembled dynamically — the function fetched into a variable
  first, or the tag built by concatenation — is not rewritten. It still works, on the generic path.
- Static binding per backend is behind its own switch (`GMP_SLUA_STATIC_BIND`, `GMP_UNLUA_STATIC_BIND`,
  `GMP_PUERTS_STATIC_BIND`, `GMP_CSHARP_STATIC_BIND`), all default `0`.

## See also

[[Key baking]] · [[Signature inference]] · [[Build switches]] · [[FGMPRawSig]]
