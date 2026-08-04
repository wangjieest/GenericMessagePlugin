# UGMPJsonHttpUtils

The HTTP NeuronAction GMP ships. Also the reference example of what a [[UK2NeuronAction]] looks like in
source.

**Declared in** `Plugins/GMP/Source/GMP/Private/GMPJsonUtils.h:60`

```cpp
DECLARE_DYNAMIC_DELEGATE_TwoParams(FGMPJsonResponseDelegate, bool, bSucc, int32, RspCode);

UCLASS(meta = (NeuronAction))
class UGMPJsonHttpUtils : public UBlueprintFunctionLibrary
{
    UFUNCTION(BlueprintCallable, CustomThunk, BlueprintInternalUseOnly,
              Category = "GMP|Json|HTTP",
              meta = (DisplayName = "GMPHttpPostRequest", NeuronAction, WorldContext = InCtx,
                      CustomStructureParam = "RequestStruct,ResponseStruct",
                      TimeoutSecs = "60", AutoCreateRefTerm = "Headers",
                      AdvancedDisplay = "Headers,TimeoutSecs"))
    static void HttpPostRequestWild(const UObject* InCtx, const FString& Url,
                                    const TMap<FString, FString>& Headers, float TimeoutSecs,
                                    const FGMPJsonResponseDelegate& OnHttpResponse,
                                    int32 ConvertFlags,
                                    const int32& RequestStruct, int32& ResponseStruct);
};
```

`HttpGetRequestWild` is the same shape without a request body.

## The resulting node

![NeuronAction](../docs/img/11-neuron-action.webp)

| Pin | From |
|---|---|
| `Url`, `Convert Flags` | ordinary parameters |
| `Request Struct`, `Response Struct` | `CustomStructureParam` — **wildcards**, typed by whatever you connect |
| `Headers`, `Timeout Secs` | `AdvancedDisplay`, collapsed by default |
| `On Http Response` + `bSucc`, `RspCode` | the delegate parameter, expanded into an exec pin with its data |

## The response is already decoded when the pin fires

`ResponseStruct` is a ref parameter. The implementation deserialises the response JSON **into your struct**
and only then executes the delegate, so downstream of the exec pin the data is already there. No parsing
node, and no proxy object to keep alive.

## ConvertFlags

`EEJsonEncodeMode` is a bitmask controlling JSON encoding of the request: `BoolAsBoolean`, `EnumAsStr`,
`Int64AsStr`, `UInt64AsStr`, `OverflowAsStr`, `LowerStartCase`, `StandardizeID`. The `Int64AsStr` and
`UInt64AsStr` flags exist because JSON numbers cannot represent the full 64-bit range — set them when
talking to a service that expects string-encoded ids.

## C++ use

The template form takes typed request and response structs directly:

```cpp
template<typename TReq, typename TRsp>
static void HttpPostRequestWild(const UObject* InCtx, const FString& Url,
                                const TMap<FString, FString>& Headers, const TReq& Req,
                                TDelegate<void(bool, int32, const TRsp&)> Rsp,
                                float TimeoutSecs = 60.f,
                                EEJsonEncodeMode ConvertFlags = EEJsonEncodeMode::Default);
```

## See also

[[UK2NeuronAction]] · [[UK2Neuron]] · [[GMPArchive]]
