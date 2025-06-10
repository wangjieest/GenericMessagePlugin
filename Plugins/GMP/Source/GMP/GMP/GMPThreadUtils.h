//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "GMPWorldLocals.h"

#include "HAL/PlatformProcess.h"

namespace GMP
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
namespace Internal
{
	GMP_API void RunOnUIThreadImpl(TFunction<void()> Func);
}
#endif

template<typename F>
inline void RunOnUIThread(F&& Func)
{
#if PLATFORM_APPLE || PLATFORM_ANDROID
	Internal::RunOnUIThreadImpl(MoveTemp(Func));
#else
	Func();
#endif
}

template<typename F>
inline void RunOnGameThread(F&& Func)
{
	if (IsInGameThread())
	{
		Func();
	}
	else
	{
		Async(EAsyncExecution::TaskGraphMainThread, [Func{MoveTemp(Func)}] { Func(); }).Consume();
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

}  // namespace GMP
