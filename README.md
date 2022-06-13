# GenericMessage

# 背景

UE自带的Delegate

````C++
DECLARE_DELEGATE_OneParam(FDelegateTypeName, Type)
DECLARE_DELEGATE_TwoParams(FDelegateTypeName, Type1, Type2)
...
```

```C++
DECLARE_MULTICAST_DELEGATE_OneParam(FDelegateTypeName, Type)
DECLARE_MULTICAST_DELEGATE_TwoParams(FDelegateTypeName, Type1, Type2)
...
````

需要前置声明，使用时候需要引入对头文件的依赖，模块间协作需要引入共同依赖。

常见的消息分发与响应：

```C++
#include "XXXDelegates.h"

FWorldDelegates::OnWorldXXX().Broadcast(...);
````

```C++
#include "XXXDelegates.h"

FWorldDelegates::OnWorldXXX().AddWeakLambda(this, [this](...){...});
```

当需要维护这个公共头文件的时候，不停的改动可能会导致大范围的代码重编。

如果想要对蓝图或者其他脚本语言暴露，得用上DynamicDelegate。

```C++
DECLARE_DYNAMIC_DELEGATE_OneParam(FDelegateTypeName, Type, Name)
DECLARE_DYNAMIC_DELEGATE_TwoParams(FDelegateTypeName, Type1, Name1, Type2, Name2)
...
```

而DynamicDelegate的使用，对于C++er来说并不是那么友好，如果要绑定成员回调必须使用UFUNCTION，需要在类声明部分添加，打断性同样比较大


**那么，有没有一种可以同时支持C++以及蓝图等脚本语言的消息机制呢？**

# 使用GMP

首先，直接集成GMP插件，[Github](https://github.com/wangjieest/GenericMessagePlugin) ，[官方商城](https://www.unrealengine.com/marketplace/en-US/product/genericmessageplugin-gmp)

借助于GameplayTags和RPC的灵感，以及基于UE Editor的工作流，GMP应运而生。

```C++
// 基于名字约束的消息通信，可选的运行期动态检查

// 对于广播只需要一行代码
FGMPHelper::SendMessage(MSGKEY("World.Hello"), Param1, Param2, Param3);


// 回调同样也是"一行"代码
FGMPHelper::ListenMesage(MSGKEY("World.Hello"), this, [this] (Type1 P1, Type2 P2 ){
    ...
});


```


在蓝图里同样可以轻松使用，在NotifyMessage或者ListenMessage节点中，通过GameplayTags的下拉选择需要的Key，剩下的蓝图节点自动为你展开对应的引脚。
![image](https://user-images.githubusercontent.com/2570757/168963671-872b70ae-d8a3-4ad0-bc19-3e444ce4b29c.png)
![image](https://i.loli.net/2020/05/01/Tglj7zZHaiQ9x85.gif)

蓝图添加事件
![image](https://i.loli.net/2020/05/01/eHxvFhskKrcpaV8.gif)

调试当前EventGraph时，蓝图节点上还会展示对应的监听对象以及基于当前Key事件的历史记录。
![image](https://i.loli.net/2020/05/01/2d76hwVL3JXmp8s.gif)


# TODO：GMP的更多特性介绍
1. 签名兼容检查，例如在Notify的尾部新增参数类型，是可以和之前完整保持兼容的
2. PIE情况下，支持消息的World隔离，例如客户端消息不能直接发到DS去处理
3. 针对单个Object对象的事件分发（非侵入式的为对象添加**虚函数**）
4. 针对非UObject的支持（Interface/SigSource/SigHandler）
5. 保留MSGKEY的反射到运行时，更好的支持多脚本兼容（Unlua/Puerts）
6. RPC支持，让任意Replicated对象可以进行RPC，避免在其他地方手写转发代码




