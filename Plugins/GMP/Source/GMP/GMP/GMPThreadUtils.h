//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "GMPWorldLocals.h"

#include "HAL/PlatformProcess.h"

namespace GMP
{
namespace Internal
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
	GMP_API void RunOnUIThreadImpl(TFunction<void()> Func, bool bWait = true);
#endif
#if PLATFORM_APPLE
	GMP_API void IsInUIThread();
#endif
	GMP_API bool DelayExec(const UObject* InObj, FTimerDelegate InDelegate, float InDelay = 0.f, bool bEnsureExec = true);
}  // namespace Internal

template<typename F>
inline void RunOnUIThread(F&& Func, bool bWait = true)
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
	Internal::RunOnUIThreadImpl(MoveTemp(Func), bWait);
#else
	Func();
#endif
}

template<typename F>
inline void RunOnGameThread(F&& Func, bool bWait = true)
{
#if PLATFORM_APPLE
	if (!Internal::IsInUIThread() && IsInGameThread())
#else
	if (IsInGameThread())
#endif
	{
		Func();
	}
	else
	{
		auto Futrue = Async(EAsyncExecution::TaskGraphMainThread, [Func{MoveTemp(Func)}] { Func(); });
		if (bWait)
		{
			Futrue.Consume();
		}
	}
}

namespace Internal
{
	enum class ERunLambdaOp
	{
		ON_UI_THREAD,
		ON_GAME_THEAD,
	};

	template<typename Fun>
	FORCEINLINE auto operator+(ERunLambdaOp Op, Fun&& fn)
	{
		return Op == ERunLambdaOp::ON_UI_THREAD ? RunOnUIThread(Forward<Fun>(fn)) : RunOnGameThread(Forward<Fun>(fn));
	}
}  // namespace Internal

#define GMP_RUN_ON_UI GMP::Internal::ERunLambdaOp::ON_UI_THREAD + [&]()
#define GMP_RUN_ON_GAME GMP::Internal::ERunLambdaOp::ON_GAME_THEAD + [&]()

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
