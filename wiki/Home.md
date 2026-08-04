# GenericMessagePlugin

Reference wiki. One page per named thing, plus a short concept page for each rule that does not belong to
any single type. Written to be **looked up**, not read front to back — for the guided introduction use the
[README](https://github.com/wangjieest/GenericMessagePlugin/blob/main/README.md) or the
[site](https://wangjieest.github.io/GenericMessagePlugin/)
([中文](https://wangjieest.github.io/GenericMessagePlugin/index-cn.html)).

Every entry states where the thing is declared, with file and line. Line numbers drift; the symbol name
does not, so search for that if a link lands in the wrong place.

![capability map](../docs/img/17-capability-map.webp)

## Concepts

| Page | Answers |
|---|---|
| [[Why a string key]] | What the string convention buys, and what it costs |
| [[Dispatch layers]] | Which listeners hear a send, and which do not |
| [[Parameter compatibility]] | When a listener signature still matches after the message changes |
| [[Signature inference]] | How an undeclared tag gets a recorded signature |
| [[Key baking]] | How a compile-time string becomes a direct store pointer |
| [[Transparent rewrite]] | How script calls become typed calls without editing the script |
| [[Jump tracing]] | How a decoupled message finds its way back to a source line |

## Sending and listening

| Entry | Kind |
|---|---|
| [[NotifyMessage family]] | API — every send form and what distinguishes them |
| [[ListenMessage family]] | API — every listen form, plus `Times` and `Order` |
| [[StoreObjectMessage]] | API — sticky and once-only delivery |
| [[Collection messages]] | API — a stored `TArray` as a table: whole, one slot, or every changed row |
| [[MSGKEY]] | macro |
| [[FSigSource]] | type — the source of a message, `ISigSource` |
| [[FSigHandle]] | type — RAII listener lifetime |
| [[FGMPKey]] | type — listener identity, order encoding |
| [[FGMPResponder]] | type — the reply half of request/response |

## Dispatch internals

| Entry | Kind |
|---|---|
| [[FGMPTypedAddr]] | type — one erased argument |
| [[FGMPExtra]] | type — the call context |
| [[FMessageBody]] | type — the local message body |
| [[TGMPFunction]] | type — the type-erased callable |
| [[FGMPRawSig]] | type — the C ABI callback contract |

## Reflection and data

| Entry | Kind |
|---|---|
| [[UGMPMeta]] | class — the collected signature table |
| [[Class2Name]] | template — type to name, type to `FProperty*` |
| [[FGMPStructUnion]] | type — a value of a runtime-decided struct type |
| [[GMPArchive]] | types — memory and network archives |
| [[FRpcMessageUtils]] | class — MSGKEY as an RPC interface |

## Editor

| Entry | Kind |
|---|---|
| [[UK2Neuron]] | class — the self-describing K2Node base |
| [[UK2NeuronAction]] | class — async factory to a single node |
| [[UGMPJsonHttpUtils]] | class — the HTTP NeuronAction GMP ships |

## Build

[[Build switches]] — every macro, its default, and what it changes.

## Articles

Long-form write-ups on the GitHub Pages site — the reasoning and trade-offs behind a feature, where the pages
above are the reference for it.

| Article | About |
|---|---|
| [Collection messages](https://wangjieest.github.io/GenericMessagePlugin/article-collection-messages.html) ([中文](https://wangjieest.github.io/GenericMessagePlugin/article-collection-messages-cn.html)) | Row dispatch off a plain send, and what a stored copy buys on top of it |
| [Inlined dispatch](https://wangjieest.github.io/GenericMessagePlugin/article-inline-fire.html) ([中文](https://wangjieest.github.io/GenericMessagePlugin/article-inline-fire-cn.html)) | Collapsing a send to four frames: compile-time store resolution and a reference-passing ABI |
