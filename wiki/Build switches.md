# Build switches

Every macro, its default, and what changes when you move it.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPMacros.h`

## Dispatch

| Macro | Default | Effect |
|---|---|---|
| `GMP_SIGNAL_BACKEND_FLEX` | `1` | Pluggable signal backend; storage, ABI and handler are orthogonal policies, so the backend can be replaced without touching call sites |
| `GMP_WITH_DIRECT_SIGNAL` | `1` | The typed direct-send path |
| `GMP_STATIC_STORE_MONOLITHIC` | `IS_MONOLITHIC` | Precondition for baking a key to a static store; overridable |
| `GMP_WITH_STATIC_STORE` | derived | `GMP_WITH_DIRECT_SIGNAL && GMP_STATIC_STORE_MONOLITHIC` |
| `GMP_WITH_INLINE_FIRE` | `0` | Inlines the dispatch loop into the caller. Only effective under monolithic: `GMP_WITH_INLINE_FIRE_ENABLED = GMP_WITH_INLINE_FIRE && GMP_WITH_STATIC_STORE` |
| `GMP_WITH_SIGNAL_ORDER` | — | Compiles out `FGMPListenOrder::Order` when off; see [[ListenMessage family]] |

## Checking

| Macro | Default | Effect |
|---|---|---|
| `GMP_WITH_DYNAMIC_CALL_CHECK` | `1` Editor/Dev, `0` Shipping | Signature consistency checking and [[Signature inference]] |
| `GMP_WITH_DYNAMIC_TYPE_CHECK` | `1` Editor/Dev, `0` Shipping | Per-argument type validation |
| `GMP_WITH_TYPENAME` | derived | `DYNAMIC_TYPE_CHECK \|\| DYNAMIC_CALL_CHECK \|\| TYPE_INFO_EXTENSION`. When off, `FGMPTypedAddr::TypeName` is gone and the struct is a bare `uint64` |
| `GMP_WITH_STATIC_MSGKEY` | `!WITH_EDITOR` | Drops the message-key string from the runtime, and with it [[Jump tracing]] |
| `GMP_WITH_NO_CLASS_CHECK` | `UE_BUILD_SHIPPING` | Skips class checks |

## Script backends

| Macro | Default |
|---|---|
| `GMP_SLUA_STATIC_BIND` | `0` |
| `GMP_UNLUA_STATIC_BIND` | `0` |
| `GMP_PUERTS_STATIC_BIND` | `0` |
| `GMP_CSHARP_STATIC_BIND` | `0` |

Set in `GMP.Build.cs`. Each enables codegen'd static binding for that backend — see
[[Transparent rewrite]].

## Serialisation

`GMP_WITH_UPB` (Protobuf), `GMP_WITH_YAML`, `GMP_WITH_JSONDOM`, `GMP_WITH_HTTP_PACKAGE`, `GMP_HTTPSERVER` —
also in `GMP.Build.cs`. See [[GMPArchive]].

## Measured effect of the dispatch switches

`Test_DispatchStack` (tagged `TSTK`, in `GMPTests.cpp`) captures a real backtrace inside the listener and
counts the GMP frames between the send call and the callback:

| Build | by-name (FName + lookup) | by-store (baked key) |
|---|---|---|
| Unoptimized (DebugGame) | 9 | 7 |
| Optimized (Development) | 4 | **3** |

![measured dispatch stack](../docs/img/08-inline-fire.webp)

Most unoptimized frames are type-erasure scaffolding — the adapter, the dispatch lambda, `FlexBackendThunk`,
`TGMPFunction::operator()`, the unpack thunk. Optimisation inlines all five away.

Of the remaining three, `GMPFireWithSigSourceDirectRaw` carries `GMP_API`; in a modular build that export is
a boundary the optimiser cannot inline through. Both rows are modular builds; `GMP_WITH_INLINE_FIRE`
requires monolithic and removes the first two of those three.

Raw stacks and reproduction commands:
[dispatch-stack-measured](https://wangjieest.github.io/GenericMessagePlugin/dispatch-stack-measured.html).

![what each stage is reduced to](../docs/img/21-what-remains.webp)

## Why INLINE_FIRE is off by default

The default build keeps the cross-DLL boundary clean and avoids code-size growth, paying one constant
out-of-line call per fire. Inlining trades binary size for that call, and only pays off in a monolithic
shipping build.

Every configuration — modular and monolithic, default and Flex backend, inline and out-of-line fire — runs
the same suite: **69 tests, all green**.

## See also

[[Key baking]] · [[TGMPFunction]] · [[FGMPTypedAddr]] · [[Transparent rewrite]]
