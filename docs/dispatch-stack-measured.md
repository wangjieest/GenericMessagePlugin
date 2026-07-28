# Measured dispatch stack

Numbers on this page were captured, not estimated. A unit test (`Test_DispatchStack`, tagged `TSTK`, in
`GMPTests.cpp`) calls `FPlatformStackWalk::CaptureStackBackTrace` from **inside the listener body** and
counts the GMP frames sitting between the send call and the callback.

The same listener is hit two ways:

- **by-name** — `SendObjectMessage(MSGKEY(...), ...)`, the FName plus `TMap` lookup path
- **by-store** — `MSGKEY_SLOT(...)` plus `SendObjectMessageDirect(...)`, the baked-key path

Reproduce:

```
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll \
       <YourEditor> Win64 <DebugGame|Development> -Project=<Your.uproject> -Module=GMP -WaitMutex

Engine/Binaries/Win64/UnrealEditor[-Win64-DebugGame]-Cmd.exe \
       <Your.uproject> -run=GMPUnitTest -unattended -nopause -nosplash -nullrhi -NoShaderCompile
```

## Results

| Build | by-name (FName + lookup) | by-store (baked key) |
|---|---|---|
| Unoptimized (DebugGame) | 9 | 7 |
| Optimized (Development) | 4 | **3** |

Frame counts exclude the listener callback itself.

Both rows are **modular** builds. `GMP_WITH_INLINE_FIRE` only takes effect when `GMP_WITH_STATIC_STORE`
is on, and that requires `GMP_STATIC_STORE_MONOLITHIC` (`IS_MONOLITHIC`). What it removes is the first two
of the three optimized frames: the cross-module `GMP_API` wrapper disappears, and the dispatch loop inlines
into the caller.

Going from 9 to 3 is two separate effects:

- **key baking** merges the three hub frames (`SendObjectMessageWrapperEx` → `NotifyMessageImpl` →
  `FireMsgBodyAdapt`) into a single `NotifyMessageDirectRaw`
- **the optimizer** inlines away the type-erasure scaffolding: the dispatch lambda, `FlexBackendThunk`,
  `TGMPFunction::operator()` and the unpack thunk

What survives optimization is the work that actually has to happen: find the store, walk the matched
listeners, call them.

---

## DebugGame (unoptimized)

**by-name** — 9 frames

1. `GMP::FMessageHub::SendObjectMessageWrapperEx<0,int>()`
2. `GMP::FMessageHub::NotifyMessageImpl()`
3. `GMP::FireMsgBodyAdapt()`
4. `GMP::GMPFireWithSigSourceDirectRaw()`
5. `GMP::FSignalUtils::FireWithSigSourceCore<0, ...FireWithSigSourceRaw<0>...<lambda_1> >()`
6. `GMP::FSignalUtils::FireWithSigSourceRaw<0>::<lambda_1>::operator()()`
7. `GMP::FlexSig::TFlexBackendThunk<GMP::TGMPFunction<void __cdecl(FGMPTypedAddr const*, GMP::FGMPExtra const*)>, FGMPTypedAddr const*, GMP::FGMPExtra const*>::FlexThunk()`
8. `GMP::TGMPFunction<void __cdecl(FGMPTypedAddr const*, GMP::FGMPExtra const*)>::operator()()`
9. `GMP::Hub::RawUnpackThunk<std::tuple<int>, ...Test_DispatchStack...<lambda_1> >()`

**by-store (baked key)** — 7 frames

1. `GMP::FMessageHub::NotifyMessageDirectRaw()`
2. `GMP::GMPFireWithSigSourceDirectRaw()`
3. `GMP::FSignalUtils::FireWithSigSourceCore<0, ...>()`
4. `GMP::FSignalUtils::FireWithSigSourceRaw<0>::<lambda_1>::operator()()`
5. `GMP::FlexSig::TFlexBackendThunk<...>::FlexThunk()`
6. `GMP::TGMPFunction<...>::operator()()`
7. `GMP::Hub::RawUnpackThunk<...>()`

## Development (optimized)

**by-name** — 4 frames

1. `GMP::FMessageHub::SendObjectMessageWrapperEx<0,int>()`
2. `GMP::FMessageHub::NotifyMessageImpl()`
3. `GMP::GMPFireWithSigSourceDirectRaw()`
4. `GMP::FSignalUtils::FireWithSigSourceCore<0, ...>()`

**by-store (baked key)** — 3 frames

1. `GMP::FMessageHub::NotifyMessageDirectRaw()`
2. `GMP::GMPFireWithSigSourceDirectRaw()` — `GMP_API`, the export the optimizer cannot inline through
3. `GMP::FSignalUtils::FireWithSigSourceCore<0, ...>()` — the matching loop

Symbols are as the platform stack walker printed them; template arguments are elided above where they only
repeat the enclosing signature.
