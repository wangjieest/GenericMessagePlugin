# UGMPMeta

The collected signature table: for each tag, the parameter types it has been used with.

**Declared in** `Plugins/GMP/Source/GMP/Private/GMPMeta.h:80`

```cpp
UCLASS(defaultconfig, config = GMPMeta)
class UGMPMeta : public UObject
{
public:
    GMP_API static const TArray<FName>* GetTagMeta(const UObject* WorldCtx, FName MsgTag);
    GMP_API static const TArray<FName>* GetSvrMeta(const UObject* WorldCtx, FName MsgTag);

    UPROPERTY()        TMap<FName, FGMPTagTypes> GMPTypes;
    UPROPERTY(Config)  TArray<FGMPTagMetaBase>   MessageTagsList;
};
```

## What it holds

`GetTagMeta` returns the recorded parameter type names for a tag, in order — the array that
[[Parameter compatibility]] compares against. `GetSvrMeta` is the server-side counterpart.

Two storages, deliberately:

| Property | Config-backed | Role |
|---|---|---|
| `GMPTypes` | no | the live table used during a session |
| `MessageTagsList` | yes | what serialises to `DefaultGMPMeta.ini` |

## It is a config object

`config = GMPMeta` with `defaultconfig` means the collected table writes out to `DefaultGMPMeta.ini`.

**Staging that ini with a build carries the signatures into the packaged game.** That is what lets a script
backend validate types at runtime without the editor present. If script-side type validation is expected in
a shipped build and is not happening, an unstaged ini is the first thing to check.

## How it gets filled

Editor-time collection from C++ call sites, Blueprint nodes and script usage, plus
[[Signature inference]] for tags never declared in C++. Filling and comparison are gated on
`GMP_WITH_DYNAMIC_CALL_CHECK` — on in Editor and Development.

![the table feeding each language's declarations](../docs/img/14-intellisense.webp)

## What reads it

- listener registration and dispatch validation
- Blueprint node pin generation — see [[UK2Neuron]]
- per-language codegen — see [[Transparent rewrite]]

## Caveat

The table records what has been **observed**, not what is declared. A tag exercised only in a code path no
editor run ever took has nothing recorded for it, and nothing to validate against.

## See also

[[Signature inference]] · [[Parameter compatibility]] · [[Class2Name]] · [[MSGKEY]]
