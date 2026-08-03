# 把一次消息发送压到 4 层栈：GMP 的 C++ 直发优化

本文记录 GMP（GenericMessagePlugin，UE 下的解耦消息插件）的一轮优化：重点是 C++ 到 C++ 这一层的查找开销和调用栈开销。

## 一、先说结论

先把对比的两个版本说清楚。

**老版本**：一次 `SendObjectMessage` 发出去，要穿过多个独立的派发函数帧，沿途还有一连串运行时开销——按名查 store、按 source 查出监听器列表、参数打包与类型转换、签名兼容性检查。

**新版本**：在 C++ 层，编译期已知（`MSGKEY_SLOT`）、构建是 monolithic、且打开 `GMP_WITH_INLINE_FIRE=1` 时，整条 send + fire 被内联进调用方，store 在编译期就解析好（不再查表），控制流直接跳到监听器回调——只剩 4 层栈：调用方 → 消息分发 → 监听器 thunk → 你的回调。

## 二、背景：解耦的固有成本

UE 自带的 Delegate 简单也够快，但它要求前向声明、头文件依赖，跨模块协作得共享依赖。

GMP 的卖点是「解耦」——发送方和接收方不需要互相知道对方的类型，靠一个 key 在中间的 store 里牵线。代价是：解耦天然引入了一层「按 key 找 store → 遍历 store 里匹配的监听器 → 逐个调用」的间接。

在老版本里，这层间接是实打实的运行时开销：

```
// 老版本一次 send 的真实链路
SendObjectMessage
  → SendObjectMessageWrapper          // 运行时：参数打包 + 类型转换(InterfaceParamConvert) + 签名兼容性检查
  → 按 FName 查 store(GetSig)          // 运行时查表
  → call NotifyMessageImpl            // ← out-of-line 派发帧（在 .cpp 里）
       → SignalPtr->FireWithSigSource
       → call OnFireWithSigSource      // ← 又一个 out-of-line 派发帧
            // 运行时：按 source 查出匹配的监听器 key 数组(GetKeysBySrc)，逐个取 handler
            → Invoker(Elem) → InvokeSlot
                 → 你的 lambda
```

老路径的成本不只是「调用栈深」，更是散落在沿途的一连串运行时开销：①参数打包 + 类型转换；②签名兼容性检查；③按 FName 查 store；④按 source 查出监听器 key 数组再遍历；⑤还要穿过两个独立的 out-of-line 派发帧。每一项单看都不大，叠在一起就是「解耦」要付的税。

这轮优化要回答的问题是：在「保持解耦语义」的前提下，能不能把这层间接的运行时成本尽量精简？答案是分几个方向同时推进的。

## 三、这轮优化做了什么

### 3.1 编译期 typed direct send：把「查 store」提前到编译期

这是整轮优化的地基。新增了一套「直发」API（`SendObjectMessageDirect` / `ListenObjectMessageDirect`）和 `MSGKEY_SLOT` 机制。

核心思想：**当 key 编译期已知时，把「按名查 store」从运行时提前到编译期完成。** 在 monolithic 构建下，slot 解析到一个 per-type 的静态信号存储（static signal store），`GetStore()` 退化成一次直接的字段读取，而不是运行时查表。

控制这个行为的宏链是：

```cpp
// GMPMacros.h
#define GMP_STATIC_STORE_MONOLITHIC IS_MONOLITHIC
#define GMP_WITH_STATIC_STORE (GMP_WITH_DIRECT_SIGNAL && GMP_STATIC_STORE_MONOLITHIC)
```

它依赖 `IS_MONOLITHIC`——这就是为什么这套极致优化只在 shipping/独立构建下成立：editor 是 modular 的，跨 DLL 边界拿不到编译期静态 store，会自动回退到 by-name 慢路。

这一步先干掉了老路径里「运行时按名查 store」那块成本。

### 3.2 可插拔信号后端 FlexSignal：干净、可替换的派发地基

派发核心换成了 FlexSignal（由 `GMP_SIGNAL_BACKEND_FLEX` 控制，默认就是 1）。它的精髓是把三个正交维度拆成可插拔的 policy：

- 存储策略（store 怎么组织）
- ABI 策略（参数地址怎么打包）
- handler 策略（回调怎么调用）

正交意味着后端实现可以整体替换而不动任何调用点。日常路径在这里就已经是一次轻量的、几乎不分配的 store 遍历。

一个值得记的架构判据：**正交维度（ABI / Storage / Handler）抽成 policy；但不兼容的整体架构（比如两套生命周期模型）就别硬揉，各守其位。**

### 3.3 消息 key 类型安全收窄：让编译器有资格选最优路径

这一项不是直接的性能改动，却是「极致优化」能成立的前提。

核心规则：**C++ 侧的 Send / Listen 严格只接受 `MSGKEY`（编译期 key），不再接受裸 `FName`。** 实现上让 MSGKEY 宏产物的 `operator FName` 变成 explicit，禁止隐式降级；删掉运行期的 FName 重载，逼调用方走编译期 key；脚本侧 API 仍收 FName，由库内吸收转换。

意义在于：**只有当 key 的类型在编译期确定，slot direct send 那条静态 store 直发路径才有资格被编译器选中。** 类型收窄不是为了严格而严格，是为了给编译器足够信息去走最优路径。

### 3.4 生命周期与热路径精修

还有两块收尾工作：一是引入全局连接池，让监听器仅凭它的 `FGMPKey`（全局唯一）就能被断开，而且只有 connect/disconnect 的冷路径碰这个池，fire 热路径完全不受影响；二是把 fire 的排序逻辑统一、对齐 inline 与 out-of-line 两条路径的行为（比如「监听器多于一个才排序」的守卫）。

## 四、压轴：调用栈怎么一层层塌下来

前面三项把「查 store」「选最优路径」都铺好了，这一节讲最关键的：一次发送的调用栈是怎么从老版本那一摞，一层层塌到只剩 4 层的。

而能塌到这么薄，靠的不是某一个内联开关，而是底层一个通用 ABI——它才是地基；内联只是地基铺好后顺手削掉的最后一跳。

### 地基：边界上传的是「引用（指针）」

解耦的核心矛盾是：发送方不知道监听器的签名，可监听器又是各种各样的强类型 lambda（参数个数、类型都不同）。怎么让派发核心「不认识签名也能调」？

GMP 的答案，也是整个 ABI 设计：**支持回写参数，在边界上不传参数的值，只传参数的地址（引用降成指针）。**

每注册一个监听器，编译期就为它生成一个专属的 thunk——一个签名固定的小函数，内部把 `void*` 形式的参数地址 `reinterpret_cast` 回真实类型的引用，再调用那个 lambda。派发核心存的、fire 时调的，永远是这个固定签名的 thunk：

```cpp
// 派发核心眼里，每个监听器都长这一个样子，与它的真实签名无关：
void Thunk(void* self, const void* a0, const void* a1);
//          ↑装着lambda  ↑参数的地址；不是值，是指针
```

发送侧取实参的地址递进去，接收侧把地址还原成引用透传给 lambda：

```cpp
// 发送侧：把引用降成指针穿过边界（不拷贝实参本体）
static void Dispatch(Thunk thunk, void* self, const A0& a0, const A1& a1) {
    thunk(self, &a0, &a1);
}
// 接收侧 thunk 内部：把指针还原回强类型的引用，再调真正的 lambda
T& arg = *reinterpret_cast<T*>(const_cast<void*>(a0));
```

「传引用而非值」是这套 ABI 一切好处的源头，至少带来三件事：

**1. 边界零拷贝。** 不管参数是 4 字节的 int 还是几百字节的大结构体，穿过 ABI 边界的永远只是一个 8 字节指针，实参本体一直待在发送方栈上。参数再大，派发成本不变。

**2. 类型擦除只发生在边界这一跳，两端都不丢类型。** 发送方编译期知道真类型（所以能取地址），接收方 thunk 编译期也知道真类型（所以能 `reinterpret_cast` 还原）——中间那一段才是 `void*`。类型信息在两端各自完整，边界只是个「地址通道」。

也正因为传的是地址而非值，所有不同签名才能被压成同一个 `(self, a0, a1)`：值会因类型而异，地址永远是一个指针。这是「归一化」能成立的前提。

**3. 支持参数回写。** 这是传引用最直接的红利——监听器拿到的是引用 `T&`，对它的修改会写回发送方的原始对象：

```cpp
// 监听器声明成引用参数，就能原地改写发送方的变量
ListenObjectMessageDirect(Slot, Src, &H, [](int32& V) { V *= 10; });
int32 X = 7;
SendObjectMessageDirect(Slot, FSigSource(Src), X);  // X 以引用传入
// fire 之后 X == 70 —— 监听器写回了发送方的 X
```

监听器想回写就把参数声明成引用 `T&`，不想回写就声明成值 `T`（适配层会把 `T&` 安全地 `static_cast` 成值，不触碰发送方）——要不要回写由监听器自己的签名决定，这是普通「值广播」的消息系统做不到的能力。

（一个边界：回写只对实时 fire 有效；StoreMessage 这类延迟重放是值快照，因为发送方那时已经不在了。）

### 可插拔的两套 ABI

「参数怎么到达 thunk」这个维度本身也是可插拔的 policy，有两套实现：

- **RawAddr ABI**（GMP 直发路径用）：`void(self, a0, a1)`，最多两个参数、无类型指纹，字节级兼容 GMP 原有的 `GMPInvokeRaw`——所以新后端能无缝接到老的直发管线上；
- **Paddrs ABI**（默认通用路径）：`void(self, paddrs[], Num)`，带类型指纹校验、支持任意参数个数和「减参前缀」匹配。

两套都遵循同一个根原则：**边界传地址**。这就是为什么 FlexSignal 能「整体替换后端而不动调用点」——调用点只跟这个统一 ABI 打交道，底下换谁都行。

这层「传引用」的归一化 ABI 才是整套优化的承重墙。

### 栈层级：一层层塌下来

有了这个地基，调用栈的塌缩就顺理成章了：

```
老版本（多帧 + 一路查找转换）
   业务代码 →〔查 store〕→ 派发帧① NotifyMessageImpl
            → 派发帧② OnFireWithSigSource〔按 source 查监听器〕→ thunk → lambda

  ↓ ① 编译期 typed direct：key 编译期解析到静态 store，「查 store」整块消失

  ↓ ② 静态直发路径：两个老派发帧合并成一个轻量的直发派发（默认就到这）
   业务代码 → 派发帧 GMPFireWithSigSourceDirectRaw → thunk → lambda

  ↓ ③ 打开 INLINE_FIRE：把这最后一个派发帧也内联进调用方
   业务代码（send+fire 已全内联） → call thunk → lambda     ← 只剩 3 层
```

## 五、撑起灵活性的另外两个设计

前面讲的是「快」。但 GMP 之所以好用，还在于两个让它「灵活」的设计——它们和性能优化是同一套地基上长出来的，值得单独说。

### 双 Key：消息身份 与 监听器身份 分开

很多消息系统只有一个 key——消息名。GMP 用两类正交的 key：

- **消息 key**（`FName`，由编译期 MSGKEY 产生）：回答「这是哪条消息、落在哪个 store」——是消息的身份；
- **监听器 key**（`FGMPKey`，一个全局唯一、递增分配的 int64 句柄）：回答「这是哪一个具体的监听器实例」——是某次监听的身份。

```cpp
struct FGMPKey {
    int64 Key;                       // 全局唯一句柄
    static FGMPKey NextGMPKey();     // 每注册一个监听器，分配一个新的
};
```

这个分离带来几个直接好处：

- 同一条消息上可以挂任意多个监听器，每个有自己独立的 `FGMPKey`，互不干扰、可单独管理；
- 凭监听器 key 就能精确断开某一个监听，不需要持有 store、也不需要消息名——前面提到的「全局连接池仅凭 FGMPKey 断开」就是靠它；`FGMPKey` 全局唯一，做这件事天然安全；
- fire 的顺序是确定的：派发前按 `FGMPKey` 排序，因为 key 是递增分配的，注册早的先收到，行为可预测。

一句话：消息名管「发给谁这类」，`FGMPKey` 管「具体是哪一个」，两个维度解耦，管理监听器就有了精确的抓手。

### 可扩展 SigSource：消息源不必是 UObject

GMP 的派发是分级的——一次对象消息会按「源对象 → 源对象的 World → 全局」几个层级投递，让监听器可以按 source 精确过滤「我只关心这个对象发来的」。承载这个「源」的就是 `FSigSource`。

它的实现是一个 tagged pointer：把来源对象的地址存进一个 `intptr_t`，利用地址按 `GMP_SIG_BASE_ALIGN` 对齐后低位必然空闲的特性，用低位的标记位区分来源的种类：

```cpp
struct FSigSource {
    using AddrType = intptr_t;
    enum EAddrMask {
        EObject  = 0x00,   // 普通 UObject
        ESignal  = 0x01,   // signal 实例
        External = 0x02,   // 外部的非 UObject 对象
        ExtKey   = 0x04,   // 对象 + Name 组成的复合 key
    };
    AddrType Addr;         // 高位是地址，低位是标记
};
```

关键在于它没有把「源」写死成 UObject。构造函数对类型只有一个开放的约束：

```cpp
template<typename T>
FSigSource(const T* InPtr) {
    static_assert(alignof(T) >= GMP_SIG_BASE_ALIGN &&
        (TExternalSigSource<T>::value          // ← 任意类型特化这个 trait 即可
         || std::is_base_of<UObject, T>::value
         || std::is_base_of<ISigSource, T>::value), "err");
}
```

也就是说，任意一个自定义类型，只要对齐达标、并特化 `TExternalSigSource<T>` 为真（或继承 `ISigSource`），就能直接当消息源用，不必是 UObject。还配了 `OnSourceRemoved` 回调，让外部类型能接入 GMP 的生命周期清理（源销毁时自动摘掉相关监听）。

再加上 ExtKey 那一类「对象 + Name 复合 key」，连「同一个对象上按名字细分的多个子源」都能表达。

这让 GMP 的「按源分级派发」不被 UObject 绑死——纯 C++ 对象、第三方类型、甚至自定义的逻辑实体，都能成为消息的发送源与过滤维度。

## 小结

这轮 GMP 优化的内核是一句话：**在保持解耦语义的前提下，用编译期信息把运行时间接一层层削掉。**

- **typed direct send + slot**：让 key 在编译期解析到静态 store，`GetStore()` 退化成字段直读，干掉运行时查表
- **FlexSignal 后端**：干净、可替换的派发地基（三正交 policy）
- **msgkey 类型收窄**：收紧契约，让编译器有资格选最优路径

相比之前的老代码（一次 send 要穿过多个独立派发帧，沿途还有按名查 store、按 source 查监听器、参数打包转换、签名检查这一连串运行时开销），极致配置下不仅省去了查询开销，一次 typed send 也只剩下极限的 4 层栈：调用方 → 检查分发 → 监听器 thunk → 你的 lambda。