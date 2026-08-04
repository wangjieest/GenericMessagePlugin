# UK2NeuronAction

Expands an asynchronous factory function into one node, with an output execution pin per callback.

**Declared in** `Plugins/GMP/Source/GMPEditor/GMPEditor/Public/K2NeuronAction.h:15`

```cpp
UCLASS()
class UK2NeuronAction final : public UK2Neuron
{
protected:
    UPROPERTY() UClass* ProxyFactoryClass;
    UPROPERTY() FName   ProxyFactoryFunctionName;
    UPROPERTY() UClass* ProxyClass;
    UPROPERTY() FName   ProxyActivateFunctionName = NAME_None;
};
```

## The shape it produces

| Side | Pins |
|---|---|
| input | exec, the factory function's spawn parameters, plus **Cancel** |
| output | one exec pin **per callback delegate**, each carrying that delegate's own data pins |

So a flow with four possible outcomes is one node with four labelled exits, rather than a node plus a
scatter of Event nodes whose association with it is only visual.

![one async factory expanded into one node](../docs/img/11-neuron-action.webp)

## Opting in

Tag the class or the UFUNCTION with `meta = (NeuronAction)`. GMP ships one working example —
[[UGMPJsonHttpUtils]].

## Relation to the proxy pattern

`ProxyFactoryClass` / `ProxyFactoryFunctionName` / `ProxyClass` / `ProxyActivateFunctionName` are the same
four pieces UE's own async-action nodes use. The difference is what happens at expansion: the delegates
become exec pins on this node instead of requiring separately placed Event nodes bound at runtime.

## Cancel

The Cancel input exec is generated for you. What it does depends on the proxy honouring cancellation — the
node provides the entry point, not the semantics.

## See also

[[UK2Neuron]] · [[UGMPJsonHttpUtils]] · [[FGMPResponder]]
