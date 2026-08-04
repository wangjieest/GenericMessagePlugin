# MSGKEY

The macro that turns a string literal into a compile-time key type.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPMessageKey.h`

```cpp
#define MSGKEY(str) GMP::TMSGKEYTyped<C_STRING_TYPE(str)>{ ... }
```

There are three expansions in the header, selected by build configuration; they differ in how much
source-location and name data is carried. Line 96 is the baked form, 142 carries
`UE_LOG_SOURCE_FILE(__FILE__)` and `__LINE__`, 144 carries the name only.

![sender and listener share only the key](../docs/img/16-key-contract.webp)

## Why the argument must be a literal

`C_STRING_TYPE("Common.Action")` converts the characters into a **type**. Three things depend on that:

| Consequence | Why it needs a literal |
|---|---|
| The editor can collect every call site | a literal is findable in source; a computed string is not |
| The key can be baked to a static store | only a compile-time type can index a template static — see [[Key baking]] |
| The panel can jump back to the line | the macro captures `__FILE__` / `__LINE__` at that site |

Passing a runtime `FString` or `FName` does not fail to compile everywhere — but it silently opts out of
all three. If a tag is missing from the tag tree, this is the first thing to check.

## Forms

| Form | Use |
|---|---|
| `MSGKEY("Tag")` | normal send and listen |
| `MSGKEY_SLOT("Tag")` | resolves to the static store slot, for the baked direct-send path |
| `MSGKEY_TYPE` | the underlying compile-time type, when you need to name it |
| `FMSGKEYFind` | look up an existing key **without** registering a new one — used by `UnListenMessage` |

`FMSGKEYFind` is the one to reach for in teardown paths, where registering a tag as a side effect of
unregistering a listener would be wrong.

## Naming

Dotted names (`Common.Action`, `Player.Hurt`) group the tag tree in the editor. The dots are presentational
grouping, **not** a hierarchy with lookup semantics: `Player` and `Player.Hurt` are unrelated keys, and
listening to one does not hear the other.

## In Shipping

`GMP_WITH_STATIC_MSGKEY` defaults to `!WITH_EDITOR`, so the key string itself is dropped from the runtime
in a packaged build. Code that expects to read a tag's name back at runtime should not assume it is there.

## See also

[[Key baking]] · [[UGMPMeta]] · [[Jump tracing]] · [[Build switches]]
