# FGMPResponder

The reply half of request/response. Declaring one as a listener's last parameter is what makes that
listener the responder.

**Declared in** `Plugins/GMP/Source/GMP/GMP/GMPHub.h:110`

```cpp
USTRUCT(NotBlueprintable, NotBlueprintType)
struct FGMPResponder
{
    GENERATED_BODY()
public:
    template<typename... TArgs> void Response(TArgs&&... Args) const;

    template<typename... TArgs>
    void ResponseAndCear(TArgs&&... Args) const
    {
        Response(Forward<TArgs&&>(Args)...);
        MsgHub = nullptr;
    }

    operator bool() const { return MsgHub != nullptr; }

    FGMPResponder(GMP::FMessageHub* InMsgHub, FName InMsgId, uint64 InSeq);
    FGMPResponder() = default;

protected:
    mutable GMP::FMessageHub* MsgHub = nullptr;
    // MsgId, Sequence
};
```

## Usage

```cpp
// request: a trailing callable turns the send into a request
SendObjectMessage(Src, KEY, Args..., [](FResult& r){ /* the reply */ });

// responder: FGMPResponder& last marks this listener as the one who answers
ListenObjectMessage(Src, KEY, this,
    [](FArgs& a, FGMPResponder& Rsp){ Rsp.Response(FResult{...}); });
```

![request and response](../docs/img/03-request-response.webp)

## How the halves are matched

Not by the message key — every request on a tag shares it. Each request gets an incrementing **`Sequence`**,
the responder carries it, and the reply is routed by that. The request callback is one-shot and destroyed
after use.

## Detected at compile time

There is no flag to set. The last parameter's type is inspected when the listener is bound, and a
responding listener goes through a different thunk. Writing the parameter is the opt-in.

## Deferring a reply

The responder is copyable and holds no reference to the listener's stack, so it can be captured and
answered later — which is the point, since the interesting case is asynchronous.

`operator bool()` tests whether it can still answer. `ResponseAndCear` answers and then clears the hub
pointer, so a captured copy cannot answer twice. (The spelling of that method is as in the source.)

## Failure modes

- **Never answering** — the request callback is simply never invoked and is destroyed with the pending
  entry. No warning is raised, so a responder path that can bail out early should answer with a failure
  result rather than returning.
- **Answering twice** — use `ResponseAndCear`, or check `operator bool()` before answering.

## See also

[[NotifyMessage family]] · [[ListenMessage family]] · [[FGMPExtra]]
