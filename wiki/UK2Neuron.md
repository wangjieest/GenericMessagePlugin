# UK2Neuron

The base class for self-describing Blueprint nodes.

**Declared in** `Plugins/GMP/Source/GMPEditor/GMPEditor/Public/K2Neuron.h:278`

```cpp
class K2NEURON_API UK2Neuron : public UK2Node
```

## What it provides

Pin layout generated from reflection or from the [[UGMPMeta]] signature table, rather than declared by
hand — and, critically, **pin identity that survives regeneration**.

Each pin carries a **PersistentGuid**. When a node rebuilds — a tag changed, a signature changed, the asset
reloaded — pins are matched by guid rather than by name or index, so the wires you already connected stay
connected.

Without that, every signature change would silently disconnect the graph, and a self-describing node would
be more trouble than a hand-written one.

## The message nodes

`UK2Node_NotifyMessage` and `UK2Node_ListenMessage`, over a shared message-node base.

![message node](../docs/img/10-message-node.webp)

Pick a tag; the node grows that tag's parameter pins with the right types, names and defaults. Change the
tag and the pins rebuild.

Because pin types come from the recorded signature, **a wrong connection fails at Blueprint compile time**.
The check runs in the uncook-stage Blueprint compile, so the compiled asset carries no extra validation
payload; under Editor and Development a runtime consistency check backs it up.

## The rest of the family

| Node | What it does |
|---|---|
| [[UK2NeuronAction]] | an async factory function expanded into a single node |
| GenericInvoker | walks a member chain to read a member or call a function |
| StructUnion group | Set/Get StructUnion, StructTuple, DynStructOnScope — see [[FGMPStructUnion]] |
| FormatStr, EventGraphFunction, DerefParam | smaller utilities |

![a void Blueprint event writing back through an out parameter](../docs/img/06-refevent.webp)

### GenericInvoker is worth a note

`ExpandNode` flattens the member chain into `FName` literals, so **the compiled Blueprint holds no hard
reference to the target class**. Same stance as the messaging core: you can delete that class's module and
the Blueprint still compiles. The cost is that the chain is resolved by name at runtime, so a rename that
the editor does not catch becomes a runtime failure.

## See also

[[UK2NeuronAction]] · [[UGMPMeta]] · [[Parameter compatibility]]
