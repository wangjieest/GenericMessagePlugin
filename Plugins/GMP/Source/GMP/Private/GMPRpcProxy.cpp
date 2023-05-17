//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPRpcProxy.h"

#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GMPArchive.h"
#include "GMPBPLib.h"
#include "GMPRpcUtils.h"
#include "GMPWorldLocals.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Stats/Stats2.h"
#include "Templates/SharedPointer.h"
#include "TimerManager.h"
#include "UObject/ObjectMacros.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealCompatibility.h"

#if WITH_EDITOR
// #include "GameDelegates.h"
#include "Editor.h"
#endif

#if UE_4_24_OR_LATER
#include "Serialization/StructuredArchive.h"
#elif UE_4_20_OR_LATER
#include "Serialization/StructuredArchiveFromArchive.h"
#endif

#if UE_4_22_OR_LATER
#include "Engine/LevelStreamingDynamic.h"
#else
#include "Engine/LevelStreamingKismet.h"
#endif

UGMPRpcValidation& GMPRpcValidation(const UObject* WorldContextObj)
{
	return GMP::WorldLocalObject<UGMPRpcValidation>(WorldContextObj);
}

inline auto& GMPRpcProcessors(const UObject* WorldContextObj)
{
	return GMPRpcValidation(WorldContextObj).RPCProcessors;
}

bool UGMPRpcValidation::VerifyRpc(const UObject* Obj, const FName& MessageName, const TArray<FProperty*>& Props)
{
	auto& Processors = GMPRpcProcessors(Obj);
	if (auto Find = Processors.Find(MessageName))
	{
		// Must ExactMatched
		if (!ensureWorldMsgf(Obj, *Find == Props, TEXT("RPC Must ExactMatched :%s "), *MessageName.ToString()))
			return false;
	}
	else
	{
		Processors.Add(MessageName, Props);
	}
	return true;
}

const TArray<FProperty*>* UGMPRpcValidation::Find(const UObject* Obj, const FName& MessageKey)
{
	return GMPRpcProcessors(Obj).Find(MessageKey);
}

int32 UGMPRpcValidation::GetNextPlayerSequence(const APlayerController& PC)
{
	return (++GMPRpcValidation(&PC).PlayerSequenceID);
}

//////////////////////////////////////////////////////////////////////////
const int32 UGMPRpcProxy::MaxByteCount = 1024;

UGMPRpcProxy::UGMPRpcProxy()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bWantsInitializeComponent = true;
#if UE_4_24_OR_LATER
	SetIsReplicatedByDefault(true);
#else
	bReplicates = true;
#endif

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FGameModeEvents::GameModePostLoginEvent.AddLambda([](AGameModeBase* InMode, APlayerController* Player) {
			auto Class = UGMPRpcProxy::StaticClass();
			if (ensureWorld(Player, !Player->FindComponentByClass(Class)))
			{
				UActorComponent* ActorComp = NewObject<UActorComponent>(Player, Class, *FString::Printf(TEXT("_`%s"), *Class->GetName()));
				ActorComp->RegisterComponent();
			}
		});

		static auto BindWorldEvent = [](UWorld* InWorld) {
			if (ensure(/*!GIsEditor || */ InWorld || InWorld->IsGameWorld() && !InWorld->IsPreviewWorld()))
			{
				InWorld->OnWorldBeginPlay.AddLambda([InWorld] {
					GMP::FMessageUtils::GetMessageHub()->SendObjectMessage(MSGKEY("GMP.OnWorldBeginPlay"), GMP::FSigSource::NullSigSrc, InWorld);
					InWorld->GetTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateWeakLambda(InWorld, [InWorld] { GMP::FMessageUtils::GetMessageHub()->SendObjectMessage(MSGKEY("GMP.OnWorldBeginPlayNextTick"), GMP::FSigSource::NullSigSrc, InWorld); }));
				});
			}
		};

#if 0
		FWorldDelegates::OnPostWorldCreation.AddLambda([](UWorld* InWorld) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnPostWorldCreation"), InWorld);
			// BindWorldEvent(InWorld);
		});
#endif

		FWorldDelegates::OnPreWorldInitialization.AddLambda([](UWorld* InWorld, const UWorld::InitializationValues) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnPreWorldInitialization"), InWorld);
			// BindWorldEvent(InWorld);
		});

		FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* InWorld, const UWorld::InitializationValues) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnPostWorldInitialization"), InWorld);
			BindWorldEvent(InWorld);
		});

		FWorldDelegates::OnWorldInitializedActors.AddLambda([](const UWorld::FActorsInitializedParams& Params) {
			GMP::FMessageUtils::NotifyWorldMessage(Params.World, MSGKEY("GMP.OnWorldInitializedActors"), Params.World);
			// BindWorldEvent(Params.World);
		});

		FWorldDelegates::OnWorldBeginTearDown.AddLambda([](UWorld* InWorld) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnPostWorldCleanup"), InWorld);
			// BindWorldEvent(Params.World);
		});

		FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* InWorld, bool bSessionEnded, bool bCleanupResources) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnWorldCleanup"), InWorld, bSessionEnded, bCleanupResources);
			// BindWorldEvent(Params.World);
		});

		FWorldDelegates::OnPostWorldCleanup.AddLambda([](UWorld* InWorld, bool bSessionEnded, bool bCleanupResources) {
			GMP::FMessageUtils::NotifyWorldMessage(InWorld, MSGKEY("GMP.OnPostWorldCleanup"), InWorld, bSessionEnded, bCleanupResources);
			// BindWorldEvent(Params.World);
		});
	}

#if WITH_EDITOR
#define ITS_TYPE_NAME_TEST(x)                                               \
	do                                                                      \
	{                                                                       \
		FString ITS_Name = ITS::TypeStr<x>();                               \
		ensureMsgf(ITS_Name == TEXT(#x), TEXT("ITS_Name : %s"), *ITS_Name); \
	} while (0)

	ITS_TYPE_NAME_TEST(EMessageAuthorityType);
	ITS_TYPE_NAME_TEST(EMouseCaptureMode);
	ITS_TYPE_NAME_TEST(ENetworkFailure::Type);
	ITS_TYPE_NAME_TEST(UGMPRpcProxy);
	ITS_TYPE_NAME_TEST(GMP::FMessageUtils);
#endif
}

void UGMPRpcProxy::InitializeComponent()
{
	using namespace GMP;
	Super::InitializeComponent();
	APlayerController* PC = GetTypedOuter<APlayerController>();
	if (ensureWorld(this, PC))
	{
		if (auto NewPawn = PC->GetPawn())
			FMessageUtils::SendObjectMessage(NewPawn, MSGKEY("GMP.OnPlayerPossessed"), NewPawn);

#if UE_4_20_OR_LATER
		PC->GetOnNewPawnNotifier().Add(CreateWeakLambda(this, [this](APawn* NewPlayer) {
			if (NewPlayer)
				FMessageUtils::SendObjectMessage(NewPlayer, MSGKEY("GMP.OnPlayerPossessed"), NewPlayer);
		}));
#endif
	}
}

void UGMPRpcProxy::BeginPlay()
{
	using namespace GMP;
	Super::BeginPlay();
#if GMP_DEBUGGAME
	bool bTest = false;
	if (bTest)
	{
		TSubclassOf<AActor> c = GetOwner()->GetClass();
		if (GetNetMode() == NM_DedicatedServer)
		{
			FRpcMessageUtils::RecvRPC(Cast<APlayerController>(GetOwner()), this, MSGKEY("RPC.Test"), this, [](int v1, UObject* v2, const TArray<uint8>& v3, TArray<UObject*>& v4, const TSubclassOf<AActor>& c) {
				GMP_DEBUG_LOG(TEXT("simple test"));
			});
		}
		else if (GetNetMode() == NM_Client)
		{
			FRpcMessageUtils::PostRPC(Cast<APlayerController>(GetOwner()), this, MSGKEY("RPC.Test"), 123, this, TArray<uint8>{0x1, 0x2, 0x3, 0x4}, TArray<UObject*>{this, GetOwner()}, c);
		}

		// if last parameter type is FGMPSingleShotInfo, treat as a async server
		FMessageUtils::ListenObjectMessage(this, MSGKEY("ReqRsp.ReqRsp"), this, [](int v1, UObject* v2, const TArray<uint8>& v3, const TArray<UObject*>& v4, const TSubclassOf<AActor>& c, FGMPResponder& Info) {
			GMP_DEBUG_LOG(TEXT("simple ReqRsp test "));
			Info.Response(v1, v2, v3, v4, c);
		});

		// if last parameter type is callable, treat as a async request
		FMessageUtils::NotifyObjectMessage(this,
										   MSGKEY("ReqRsp.ReqRsp"),
										   123,
										   this,
										   TArray<uint8>{0x1, 0x2, 0x3, 0x4},
										   TArray<UObject*>{this, GetOwner()},
										   c,
										   [](int v1, UObject* v2, const TArray<uint8>& v3, TArray<UObject*>& v4, const TSubclassOf<AActor>& c) {
											   //
											   GMP_DEBUG_LOG(TEXT("simple ReqRsp test1"));
										   });
		FMessageUtils::NotifyObjectMessage(this,
										   MSGKEY("ReqRsp.ReqRsp"),
										   123,
										   this,
										   TArray<uint8>{0x1, 0x2, 0x3, 0x4},
										   TArray<UObject*>{this, GetOwner()},
										   c,
										   [](int v1, UObject* v2, const TArray<uint8>& v3, TArray<UObject*>& v4, const TSubclassOf<AActor>& c) {
											   //
											   GMP_DEBUG_LOG(TEXT("simple ReqRsp test2"));
										   });
	}

#endif
}

void UGMPRpcProxy::CallLocalFunction(UObject* InObject, FName InFunctionName, const TArray<uint8>& Buffer)
{
	using namespace GMP;
	UFunction* Function = InObject ? InObject->FindFunction(InFunctionName) : nullptr;
	bool bExist = Function && Function->HasAnyFunctionFlags(FUNC_Native);
	ensureWorldMsgf(InObject, bExist || GetNetMode() == NM_Client, TEXT("Function Error:%s in %s"), *InFunctionName.ToString(), *GetNameSafe(InObject));
	if (!bExist)
		return;

	uint8* Locals = (uint8*)FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
	FMemory::Memzero(Locals, Function->ParmsSize);
	FGMPNetFrameReader Reader{Function, Locals, CastChecked<APlayerController>(GetOwner()), const_cast<uint8*>(Buffer.GetData()), Buffer.Num() * 8};
	if (ensureWorld(InObject, Reader))
	{
		InObject->ProcessEvent(Function, Locals);
	}
}

void UGMPRpcProxy::RPC_Request_Implementation(UObject* InObject, const FString& FuncName, const TArray<uint8>& Buffer)
{
	CallLocalFunction(InObject, *FuncName, Buffer);
}

bool UGMPRpcProxy::RPC_Request_Validate(UObject* InObject, const FString& FuncName, const TArray<uint8>& Buffer)
{
	FName FunName(*FuncName, FNAME_Find);
	UFunction* Func = (FunName.IsValid() && (Buffer.Num() <= MaxByteCount) && IsValid(InObject)) ? InObject->FindFunction(FunName) : nullptr;
	if (!(Func && Func->HasAnyFunctionFlags(FUNC_NetRequest) && Buffer.Num() <= Func->ParmsSize))
	{
		GMP_WARNING(TEXT("RPC_Request_Validate : %s in %s"), *FuncName, *GetNameSafe(InObject));
		return ensure(false);
	}
	return true;
}

void UGMPRpcProxy::RPC_Notify_Implementation(UObject* Object, const FString& FuncName, const TArray<uint8>& Buffer)
{
	CallLocalFunction(Object, *FuncName, Buffer);
}

bool UGMPRpcProxy::CallFunctionRemote(APlayerController* PC, UObject* InObject, FName InFunctionName, TArray<uint8>& Buffer)
{
	if (auto World = GEngine->GetWorldFromContextObject(InObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		bool bClient = World->GetNetMode() != NM_DedicatedServer;
		if (!PC && bClient)
			PC = World->GetFirstPlayerController();

		UGMPRpcProxy* Comp = PC ? PC->FindComponentByClass<UGMPRpcProxy>() : nullptr;
		if (ensureWorldMsgf(InObject, Comp, TEXT("Found No Comp : %s"), *GetNameSafe(PC)))
		{
			if (Comp->ScopedCnt > 0)
				Comp->PendingRPCs.Emplace(InObject, InFunctionName.ToString(), MoveTemp(Buffer), true);
			else if (bClient)
				Comp->RPC_Request(InObject, InFunctionName.ToString(), Buffer);
			else
				Comp->RPC_Notify(InObject, InFunctionName.ToString(), Buffer);
			return true;
		}
	}
	return false;
}

void UGMPRpcProxy::FlushPendingRPCs()
{
	ScopedCnt = 0;
	const bool bClient = (GetNetMode() != NM_DedicatedServer);
	auto Pendings = MoveTemp(PendingRPCs);
	if (bClient)
		Batch_Request(Pendings);
	else
		Batch_Notify(Pendings);
}

void UGMPRpcProxy::DispatchPendingProgress(const TArray<FGMPRpcBatchData>& Batcher)
{
	for (auto& Data : Batcher)
	{
		if (Data.bFunction)
			CallLocalFunction(Data.Obj, *Data.Key, Data.Buff);
		else
			CallLocalMessage(Data.Obj, Data.Key, Data.Buff);
	}
}

bool UGMPRpcProxy::Batch_Request_Validate(const TArray<FGMPRpcBatchData>& Batcher)
{
	return true;
}

void UGMPRpcProxy::Batch_Request_Implementation(const TArray<FGMPRpcBatchData>& Batcher)
{
	DispatchPendingProgress(Batcher);
}

void UGMPRpcProxy::Batch_Notify_Implementation(const TArray<FGMPRpcBatchData>& Batcher)
{
	DispatchPendingProgress(Batcher);
}

//////////////////////////////////////////////////////////////////////////
void UGMPRpcProxy::CallMessageRemote(APlayerController* PC, const UObject* Sender, const FString& MessageStr, TArray<uint8>& Buffer, bool bReliable)
{
	if (auto World = GEngine->GetWorldFromContextObject(Sender, EGetWorldErrorMode::LogAndReturnNull))
	{
		bool bClient = World->GetNetMode() != NM_DedicatedServer;
		if (!PC && bClient)
			PC = World->GetFirstPlayerController();

		UGMPRpcProxy* Comp = PC ? PC->FindComponentByClass<UGMPRpcProxy>() : nullptr;
		if (ensureWorldMsgf(Sender, Comp, TEXT("Found No Comp:%s"), *GetNameSafe(PC)))
		{
			if (Comp->ScopedCnt > 0)
				Comp->PendingRPCs.Emplace(const_cast<UObject*>(Sender), FString(MessageStr), MoveTemp(Buffer), false);
			else if (bClient)
				Comp->Message_Request(Sender, MessageStr, Buffer);
			else if (bReliable)
				Comp->Message_Notify(Sender, MessageStr, Buffer);
			else
				Comp->Unreliable_Notify(Sender, MessageStr, Buffer);
		}
	}
}

void UGMPRpcProxy::Message_Request_Implementation(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer)
{
	CallLocalMessage(InObject, MessageStr, Buffer);
}

bool UGMPRpcProxy::Message_Request_Validate(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer)
{
	FName MessageName(*MessageStr, FNAME_Find);
	bool bValidate = MessageName.IsValid() && (Buffer.Num() <= MaxByteCount && UGMPRpcValidation::Find(this, MessageName));
	return ensureAlwaysMsgf(bValidate, TEXT("Message_Request_Validate : %s with %s"), *MessageStr, *GetNameSafe(InObject));
}

void UGMPRpcProxy::Message_Notify_Implementation(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer)
{
	CallLocalMessage(InObject, MessageStr, Buffer);
}
void UGMPRpcProxy::Unreliable_Notify_Implementation(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer)
{
	CallLocalMessage(InObject, MessageStr, Buffer);
}

bool UGMPRpcProxy::CallLocalMessage(const UObject* InObject, const FString& MessageStr, const TArray<uint8>& Buffer)
{
	using namespace GMP;
	FName MessageName(*MessageStr, FNAME_Find);
	const TArray<FProperty*>* Find = MessageName.IsValid() ? UGMPRpcValidation::Find(this, MessageName) : nullptr;
	if (!ensureWorldMsgf(InObject, Find, TEXT("rpc not registered for %s"), *MessageName.ToString()))
		return false;

	if (!ensureWorldMsgf(InObject, FMessageUtils::GetMessageHub()->IsAlive(MessageName), TEXT("no listener for %s"), *MessageStr))
		return false;

	return LocalBoardcastMessage(MessageStr, *Find, InObject, Buffer);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4750)  // warning C4750: function with _alloca() inlined into a loop
#endif
bool UGMPRpcProxy::LocalBoardcastMessage(const FString& MessageStr, const TArray<FProperty*>& Props, const UObject* Sender, const TArray<uint8>& Buffer)
{
	using namespace GMP;
	bool bSucc = true;
	GMP::FTypedAddresses Params;
	Params.Empty(Props.Num());
	auto PackageMap = UGMPBPLib::GetPackageMap(CastChecked<APlayerController>(GetOwner()));

	struct FFrameOnScope
	{
		FFrameOnScope(const TArray<FProperty*>& Props)
		{
			for (auto Prop : Props)
			{
				LocalTotalSize += Prop->ElementSize;
			}
			Locals = (uint8*)FMemory_Alloca(LocalTotalSize);
		}

		uint8* Alloca(FProperty* InProp)
		{
			auto Ret = Locals;
			Locals += InProp->ElementSize;
			return Ret;
		}

		int32 LocalTotalSize = 0;
		uint8* Locals = nullptr;
	};

#if defined(GMP_USING_SINGLE_ELEMENT_ALLOCATION)
#define AllocaByProp(Prop, Scope) (uint8*)FMemory_Alloca_Aligned(Prop->ElementSize, Prop->GetMinAlignment())
#else
#define AllocaByProp(Prop, Scope) Scope.Alloca(Prop)
#endif

#if 1
	FGMPNetBitReader Reader{PackageMap, const_cast<uint8*>(Buffer.GetData()), Buffer.Num() * 8};
	FFrameOnScope Scope(Props);
	int Index = 0;
	for (; Index < Props.Num();)
	{
		auto* Prop = Props[Index++];
		uint8* Locals = AllocaByProp(Prop, Scope);
		Prop->InitializeValue_InContainer(Locals);
		Add_GetRef(Params).SetAddr(Locals, Prop);

		if (!UGMPBPLib::NetSerializeProperty(Reader, Prop, Locals, PackageMap))
		{
			bSucc = false;
			break;
		}
	}

	if (bSucc)
	{
		FMessageUtils::GetMessageHub()->ScriptNotifyMessage(MessageStr, Params, Sender ? Sender : GetWorld());
	}

	for (--Index; Index >= 0; --Index)
	{
		Props[Index]->DestroyValue_InContainer(Params[Index].ToAddr());
	}

#else

	for (auto Prop : Props)
	{
		uint8* Locals = AllocaByProp(Prop, Scope);
		Prop->InitializeValue_InContainer(Locals);
		Add_GetRef(Params).SetAddr(Locals, Prop);
		Locals = Locals + Prop->ElementSize;
	}

	if (!UGMPBPLib::ArchiveToMessage(Buffer, Params, Props, PackageMap))
		return false;

	FMessageUtils::GetMessageHub()->ScriptNotifyMessage(MessageStr, Params, Sender ? Sender : GetWorld());
	for (auto i = 0; i < Props.Num(); ++i)
	{
		Props[i]->DestroyValue_InContainer(Params[i].ToAddr());
	}

#endif
	return bSucc;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

UGMPNewPawnPossessedBinder::UGMPNewPawnPossessedBinder()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = true;
#if UE_4_24_OR_LATER
	SetIsReplicatedByDefault(true);
#else
	bReplicates = true;
#endif

#if 0 && defined(GENERICSTORAGES_API)
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UDeferredComponentRegistry::AddDeferredComponent(AController::StaticClass(), GetClass());
	}
#endif
}

void UGMPNewPawnPossessedBinder::InitializeComponent()
{
	using namespace GMP;
	Super::InitializeComponent();
	AController* Controller = GetTypedOuter<AController>();
	if (ensureWorld(this, Controller))
	{
#if UE_4_20_OR_LATER
		Controller->GetOnNewPawnNotifier().Add(CreateWeakLambda(this, [this](APawn* NewPawn) {
			if (NewPawn)
				FMessageUtils::SendObjectMessage(NewPawn, MSGKEY("GMP.OnNewPawnPossessed"), NewPawn);
		}));
#endif
	}
}
