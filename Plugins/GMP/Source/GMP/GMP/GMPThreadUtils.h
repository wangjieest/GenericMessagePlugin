//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "GMPWorldLocals.h"
#include "TimerManager.h"

#include "HAL/PlatformProcess.h"

namespace GMP
{
enum ERunType
{
	NoWait,
	ForceBlock,
	ForceAsync,
};

namespace Internal
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
	GMP_API void RunOnUIThreadImpl(TFunction<void()> Func, ERunType RunType);
#endif
	GMP_API bool IsInUIThread();
	GMP_API bool DelayExec(const UObject* InObj, FTimerDelegate InDelegate, float InDelay = 0.f, bool bEnsureExec = true);
}  // namespace Internal

template<typename F>
inline void RunOnUIThread(F&& Func, ERunType RunType = ERunType::NoWait)
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
	Internal::RunOnUIThreadImpl(MoveTemp(Func), RunType);
#else
	Func();
#endif
}

template<typename F>
inline void RunOnGameThread(F&& Func, ERunType RunType = ERunType::NoWait)
{
#if PLATFORM_APPLE
	if (RunType != ERunType::ForceAsync && (!Internal::IsInUIThread() && IsInGameThread()))
#else
	if (RunType != ERunType::ForceAsync && IsInGameThread())
#endif
	{
		Func();
	}
	else
	{
		auto Futrue = Async(EAsyncExecution::TaskGraphMainThread, [Func{MoveTemp(Func)}] { Func(); });
		if (RunType == ERunType::ForceBlock)
		{
			Futrue.Consume();
		}
	}
}

template<typename F>
FORCEINLINE void WaitOnGameThread(F&& Func)
{
	return RunOnGameThread(MoveTemp(Func), ERunType::ForceBlock);
}

template<typename F>
FORCEINLINE auto AsyncOnGameThread(F&& Func)
{
	return Async(EAsyncExecution::TaskGraphMainThread, [Func{MoveTemp(Func)}] { Func(); });
}

template<typename F>
FORCEINLINE void WaitOnUIThread(F&& Func)
{
	return RunOnUIThread(MoveTemp(Func), ERunType::ForceBlock);
}
template<typename F>
FORCEINLINE auto AsyncOnUIThread(F&& Func)
{
	TPromise<void> Promise;
	TFuture<void> Future = Promise.GetFuture();
	return RunOnGameThread(
		[Func{MoveTemp(Func)}, Promise{MoveTemp(Promise)}]() mutable {
			Func();
			Promise.SetValue();
		},
		ERunType::ForceAsync);
	return Future;
}

namespace Internal
{
	enum class ERunLambdaOp
	{
		ASYNC_ON_UI,
		ASYNC_ON_GAME,
	};

	template<typename Fun>
	FORCEINLINE auto operator+(ERunLambdaOp Op, Fun&& fn)
	{
		return Op == ERunLambdaOp::ASYNC_ON_UI ? AsyncOnUIThread(Forward<Fun>(fn)) : AsyncOnGameThread(Forward<Fun>(fn));
	}
}  // namespace Internal

#define GMP_ASYNC_ON_UI GMP::Internal::ERunLambdaOp::ASYNC_ON_UI + [=]()
#define GMP_ASYNC_ON_GAME GMP::Internal::ERunLambdaOp::ASYNC_ON_GAME + [=]()

template<typename F>
auto DelayExec(const UObject* InObj, F&& Lambda, float InDelay = 0.f, bool bEnsureExec = true)
{
	if (InObj)
		return Internal::DelayExec(InObj, FTimerDelegate::CreateWeakLambda(const_cast<UObject*>(InObj), Forward<F>(Lambda)), InDelay, bEnsureExec);
	else
		return Internal::DelayExec(InObj, FTimerDelegate::CreateLambda(Forward<F>(Lambda)), InDelay, bEnsureExec);
}

template<typename F>
auto CallOnWorldNextTick(const UObject* InObj, F&& Lambda, bool bEnsureExec = true)
{
	return DelayExec(InObj, Forward<F>(Lambda), 0.f, bEnsureExec);
}

}  // namespace GMP
