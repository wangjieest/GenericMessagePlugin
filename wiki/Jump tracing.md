# Jump tracing

Decoupling removes the compile-time link between sender and listener. Tracing puts back the ability to
answer "who sent this, and from where".

![jump tracing](../docs/img/12-jump-trace.webp)

## Where the location comes from

Every script send and listen records the call site's file and line, taken from the debug facility **each
language engine already maintains**:

| Backend | Source |
|---|---|
| UnLua / slua | the standard lua `debug` library |
| Puerts | v8 `StackTrace` |
| AngelScript | the active context |
| C# | compiler-injected `CallerFilePath` |

GMP patches none of these engines. It reads what they already track, which is why enabling tracing does not
require a modified script runtime.

For C++ call sites the location comes from `__FILE__` / `__LINE__` carried by one expansion of
[[MSGKEY]].

## What consumes it

The MessageTag panel. For a selected tag it lists the script call sites, the Blueprint nodes and the assets
referencing it, side by side. Clicking a script entry opens that file at that line in the IDE.

## Gating

Tracing is editor-only. It is compiled out along with the message-key strings in a shipping build
(`GMP_WITH_STATIC_MSGKEY` defaults to `!WITH_EDITOR`), so it costs nothing at runtime in a packaged game
and is unavailable there.

## Caveat on lua

The recorded frame is the first frame that is not GMP's own lua glue. For a call made through a wrapper of
your own, the location is the wrapper, not the code that called the wrapper — one level of indirection is
skipped, not all of them.

## See also

[[MSGKEY]] · [[Transparent rewrite]] · [[UGMPMeta]]
