# Collection messages

A stored message whose single parameter is a `TArray` of `USTRUCT` is a **collection**: listeners can take the
whole table, one fixed slot, or every changed row — and a row can be read without depending on the element type.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPStoreCollection.h`

![three ways to take one table](../docs/img/26-collection-shapes.webp)

Nothing new to learn if you do not want it. `StoreObjectMessage` and `ListenObjectMessage` behave exactly as
before for every other shape; a collection is recognised from the stored parameter, not from a new entry point.

## Storing

```cpp
USTRUCT() struct FItem { GENERATED_BODY() UPROPERTY() int32 Id; UPROPERTY() FString Name; UPROPERTY() int32 Count; };

FGMPHelper::StoreObjectMessage(Obj, MSGKEY("Inv.Items"), MyItems);   // TArray<FItem>, existing API
```

The only new requirement is that the element is a `USTRUCT`. Storing the whole table again is diffed against what
is already there with `UScriptStruct::CompareScriptStruct`, so handing GMP the full array is as precise as editing
it in place — a run of differing elements becomes one span, an unchanged table publishes nothing.

To edit without rebuilding the array, use the writer:

```cpp
{
    TGMPStoredArray<FItem> Arr(Obj, MSGKEY("Inv.Items"));
    Arr.GetMutable(5).Count -= 1;
    Arr.Add(FItem{1002, TEXT("Elixir"), 5});
    Arr.RemoveAt(7);
}   // one fire on destruction, spans merged
```

It is stack-only by design — a heap or member instance would defer or lose the notification. `operator[]` is
const; writing goes through `GetMutable(i)`, because a non-const `operator[]` cannot tell whether the caller
actually wrote anything and would have to mark the row anyway.

### Not storing at all

A table that is recomputed every tick — radar blips, currently visible actors, nearby interactables — is not worth
storing. Send it instead:

```cpp
FGMPHelper::SendObjectMessage(Obj, MSGKEY("Radar.Blips"), CurrentBlips);   // nothing is kept
```

The row expansion still applies, but **only `AllRows` listeners are woken**, and the update is always a full
reload. There is no stored table to diff against, so there is no change set — and a slot listener, whose whole
point is *not* firing when someone else's row changed, would fire on every send. It stays quiet instead. There is
no late replay either: a listener arriving after the send gets nothing.

| | `StoreObjectMessage` | `SendObjectMessage` |
|---|---|---|
| a listener arriving late | replayed once | nothing |
| which rows changed | exact ranges | whole table only |
| slot subscription | works | **never fires** |
| `TGMPStoredArray` | works | not applicable |

## What a listener is told

```cpp
struct FGMPStoreRange { int32 Index; int32 Count; };

struct FGMPStoreUpdate
{
    int32 TotalCount;                          // the table size after the change
    TArrayView<const FGMPStoreRange> Ranges;   // which rows now read differently
    int32 PrevTotalCount;                      // the size before it, to tell "gone" from "never existed"
    int32 Row;                                 // row callbacks only: >= 0 this row, < 0 means ~Row is gone
    bool  IsFullReload() const;
    bool  IsRowRemoved() const;                // Row < 0
    int32 GetRow() const;                      // the row number either way
};
```

There is no add/change/remove enum: the two dimensions carry it. The count says whether the table resized, the
index says where — changing row 3 keeps the count, inserting at 3 raises it, removing at 3 lowers it. A resize
widens `Ranges` to the tail, because every row after the first touched one now holds different content.

## Listening

```cpp
ListenObjectMessage(SigSrc, K,        Listener, Lambda);   // whole table
ListenObjectMessage(SigSrc, K, Index, Listener, Lambda);   // by row, see the encoding below
```

`Index` says which row *and* whether removals are wanted, with one `~` rule shared by the callback side:

| `Index` | means | told when the row is gone |
|---|---|---|
| `5` | slot 5 | no |
| `GMP::WithRemoval(5)` (= `~5`) | slot 5 | yes |
| `GMP::AllRows` (= `MAX_int32`) | every changed row | no |
| `GMP::AllRowsWithRemoval` (= `~MAX_int32`) | every changed row | yes |

The callback side reads the same way: `U.Row >= 0` is a live row, `U.Row < 0` means `~U.Row` is gone. Asking for
removals is what costs — shrinking a table from 10 rows to 3 calls a subscriber that wanted them 7 times — so it
is opted into at the listen site rather than inferred.

A lambda ending in `const FGMPStoreUpdate&` opts into the collection; without it the listen is an ordinary
message listen, unchanged. Four shapes:

```cpp
// whole table, strongly typed -- a reference to the stored array, no copy
[](const TArray<FItem>& All, const FGMPStoreUpdate& U){ ... }

// whole table, no dependency on the element type
[](FGMPStoreView View, const FGMPStoreUpdate& U){ ... }

// notification only
[](const FGMPStoreUpdate& U){ ... }

// per row, through the Index overload -- members expand positionally, so no element type is needed
FGMPHelper::ListenObjectMessage(Obj, K, GMP::AllRows, this, [](int32 Row, int32 Id, const FString& Name, int32 Count){ ... });
FGMPHelper::ListenObjectMessage(Obj, K,           5, this, [](int32 Id, const FString& Name, int32 Count){ ... });

// slot 5, told when it goes away -- the trailing update is what tells the two apart
FGMPHelper::ListenObjectMessage(Obj, K, GMP::WithRemoval(5), this,
    [](int32 Id, const FString& Name, int32 Count, const FGMPStoreUpdate& U)
    {
        if (U.IsRowRemoved()) { /* Id/Name/Count are default here -- do not read them */ }
    });
```

A stored table is replayed to a listener that arrives late, as with any [[StoreObjectMessage]]. Unlistening is
the existing `UnbindMessage(K, Listener)` family — a collection listener dies with its `UObject` or
[[FSigHandle]] like any other.

### A fixed slot is a position, not an element

![what wakes a slot](../docs/img/27-collection-wake.webp)

`Index >= 0` means *what is displayed at row N*, which is what a virtual list row widget wants:

| | slot 5 |
|---|---|
| row 5 edited | fires |
| row 3 edited | does not fire |
| insert at 3 | fires — the old row 4 moved into position 5 |
| remove at 3 | fires — the old row 6 moved into position 5 |
| insert or remove at 8 | does not fire |
| the table shrank past 5 | fires with `Row < 0` if the listen asked for removals, otherwise not |

A slot subscription is not invalidated by its position disappearing — the table growing back calls it again. Two
ways to handle the gap, both supported: subscribe plainly and let the whole-table listener destroy the row widget
(whose destruction unlistens), or ask for removals with `WithRemoval` and have the widget react itself — play an
exit animation, clear its display — without depending on someone else to tear it down.

### Reading a row without the type

```cpp
struct FGMPStoreView            // borrowed; valid only for the callback
{
    int32 Num() const;
    template<typename F> void VisitRow(int32 Row, F&& Lambda) const;   // (Id, Name, Count)
    template<typename F> void ForEach (F&& Lambda) const;              // (Row, Id, Name, Count)
    template<typename T> const TArray<T>& As() const;                  // when you do have the type
};
```

Members expand by **declaration order of the element struct**, read through reflection, so a UI module can
consume a table defined by a gameplay module without including its header. A lambda may declare only the first
few members.

**Editor-only members are skipped.** A `WITH_EDITORONLY_DATA` member would otherwise sit in the middle of the
sequence in editor and vanish in shipping, and a positional lambda would read a different field per
configuration. Skipping makes the visible sequence the same everywhere.

## What it is for

![a virtual list across modules](../docs/img/28-collection-virtuallist.webp)

A virtual list recycles a fixed set of row widgets over a moving table, which is exactly the shape this fits: the
list widget follows `TotalCount`, each row widget follows its own position, and the module that draws the rows
never learns the element type. A row widget is not called once its position is past the end, so it never has to
ask whether it still has data — unless it asked for removals, in which case it is told exactly once.

The case it is really aimed at is **instances that come and go at run time** — party members, spawned entities,
inventory rows, a scoreboard. The data side edits an array; nobody allocates a per-instance object for the UI to
hold, and nobody has to tear one down.

## Several instances of the same thing

There are now two ways to say "the same key, but a different one of these", and they answer different questions.

| | one key, several **sources** | one key, one table, several **rows** |
|---|---|---|
| how | a different `FSigSource` per instance, or `ExactObjName` | a row index into the stored array |
| fits | instances that are long-lived objects and own their state | instances that appear and disappear, consumed by recycled widgets |
| identity | the object | the position |
| lifetime | the store dies with its source | rows are just array elements |

Nothing is ever pushed at a row: a row subscribes to *its own position*. That is why a row consumer does not have
to handle "the thing I was showing is gone" — it holds a slot number, not an entity, and by default a slot past
the end is simply not called, leaving the whole-table listener to see `TotalCount` and destroy the widget. A row
that would rather know can ask, with `WithRemoval`.

The cost of that choice: **a position is not an identity.** If what you mean is "the head equipment slot, and I
do not care what other slots do", an index cannot express it — inserting anything before it moves it. That case
wants a key field on the element and is a separate piece of work, deliberately not folded into this one.

## What this is not

It is an observable table, not a view model. There is no object per row, no per-field notification, no write-back
through the binding, and no editor that compiles bindings for you. Those all need a typed object on both ends of
the binding, which is the thing the string key exists to avoid — adding them here would buy back the coupling.

Nothing stops a view model from sitting on top: it can be a collection listener internally, and then it is the
one deciding what a field-level change means for it.

## Scripts

Rows reach the script backends through one shared entry, so all of them see the same `(int32 Row, Item)` pair and
the same `Index` encoding as C++. A removed row arrives with a negative row and a default-constructed item.

```lua
-- slua / UnLua
local key = GMP.ListenRowMessage(WatchedObj, "Inv.Items", WeakObj, 5, function(Row, Item)
    if Row < 0 then return end                      -- ~Row is gone, Item is a default
    Label:SetText(Item.Name)
end)
```

```ts
// Puerts
GMP.ListenRowMessage(WatchedObj, "Inv.Items", WeakObj, GMP.AllRows, (Row: number, Item: FItem) => { ... });
```

AngelScript is typed, so a collection tag additionally generates a funcdef and a per-tag entry into
`Script/GMPMessages.as`, checked at compile time like the other generated bindings:

```angelscript
funcdef void FOnRow_Inv_Items(int Row, FItem Item);
int64 asListenRow_Inv_Items(UObject WatchedObj, UObject WeakObj, int Index, FOnRow_Inv_Items@ cb, int Times = -1);
```

Lifetime is an ordinary GMP listener registered by that shared entry, so `UnbindMessage` and the rest of the
unbind family work on a row listen with no script-specific teardown.

## Blueprint

The whole-table form needs nothing new: the tag's parameter is `TArray<FItem>`, and a listen node already gives
an array pin.

For rows, right-click a listen node on a collection tag — a **Collection** section offers *Whole Table* (the
default) or *Row*. Row mode adds an `Index` input and gives `(Row, Item)` outputs; the node titles itself
`ListenMessageRow`. `Index` is an ordinary pin, so a row widget can subscribe to its own row number at run time.
The section only appears for a tag that carries one array of structs.

`Index` takes the same encoding as C++, and the `Row` output carries it back: a negative `Row` means that row is
gone and `Item` is a default. Branching on `Row < 0` is the blueprint equivalent of `IsRowRemoved()`.

## Cost

Measured with `-run=GMPUnitTest -Bench` (64 rows, best of three, Development editor):

| | |
|---|---|
| dispatch, whole table, one listener | 56 ns |
| dispatch, 64 slot listeners, one wakes | 253 ns |
| `ForEach` per row, compiled offsets | 0.9 ns |
| `ForEach` per row, reflection | 10 ns |
| *for scale:* an ordinary GMP slot send | ~690 ns |

`ForEach` reads rows at compiled offsets when the arguments a lambda declares line up with the element layout —
same offset, same width, same type for each declared member — and falls back to reflection when they do not.
Correctness never depends on the shortcut; a mismatch (an editor-only member shifting the rest, a missing
`UPROPERTY`, a same-width different type) just takes the slower path. A slot or per-row listener makes that
decision once, on its first fire, and remembers it.

## Limits

- The element must be a `USTRUCT`; members that participate must be `UPROPERTY`.
- A removal in the middle of a table published as a whole reports one wide span, not "one row removed" —
  recognising that needs a sequence diff.
- `FGMPStoreView` is borrowed. Do not store it past the callback.
- A slot subscription never fires for a plain `SendObjectMessage` — by design, not a defect. Without a stored
  table there is no change set, so it would fire on every send instead of only when its own row changed.
- Subscribing by name rather than by index is not implemented; the design keeps `TArray` and marks a key field
  with `meta=(GMPKey)` rather than switching to `TMap`, so index and name can coexist.

## See also

Long-form: [Collection messages](https://wangjieest.github.io/GenericMessagePlugin/article-collection-messages.html) — the reasoning and the trade-offs, where this page is the reference.


[[StoreObjectMessage]] · [[ListenMessage family]] · [[FGMPStructUnion]] · [[FSigSource]]
