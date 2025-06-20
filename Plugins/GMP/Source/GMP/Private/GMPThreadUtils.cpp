//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPThreadUtils.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GMPStruct.h"

#if PLATFORM_APPLE || PLATFORM_ANDROID
#include "HAL/UESemaphore.h"

#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#endif

#define GMP_HAS_ANDROID_UITHREAD (PLATFORM_ANDROID && 0)
#if GMP_HAS_ANDROID_UITHREAD
#include "Android/AndroidApplication.h"
#if defined(LAUNCH_API)
#include "Android/AndroidJNI.h"
#endif
#include <jni.h>
extern "C" JNIEXPORT void JNICALL Java_com_epicgames_GameActivity_nativeRunNativeTFunction(JNIEnv* Env, jobject Thiz, jlong FuncPtr)
{
	if (FuncPtr != 0)
	{
		TFunction<void()>* TaskPtr = reinterpret_cast<TFunction<void()>*>(FuncPtr);
		(*TaskPtr)();
		delete TaskPtr;
	}
}
#endif

void GMP::Internal::RunOnUIThreadImpl(TFunction<void()> Func, bool bWait)
{
	static auto GetSemaphore = []() -> FSemaphore& {
		static thread_local FSemaphore semaphore(0, 1);
		return semaphore;
	};

#if PLATFORM_APPLE
	if ([NSThread isMainThread])  // dispatch_get_main_queue() == dispatch_get_current_queue()
	{
		Func();
		return;
	}

	dispatch_async(dispatch_get_main_queue(), ^{
		Func();
		if (bWait)
		{
			GetSemaphore().Release();
		}
	});
	if (bWait)
	{
		GetSemaphore().Acquire();
	}
#elif GMP_HAS_ANDROID_UITHREAD
	if (IsInGameThread() /*GMainThreadId == GGameThreadId*/)
	{
		Func();
		return;
	}

	JNIEnv* Env = FAndroidApplication::GetJavaEnv(true);
	jobject Activity = FAndroidApplication::GetGameActivityThis();
	if (!Env || !Activity)
	{
		Func();
		return;
	}
	auto TaskHolder = MakeUnique<TUniqueFunction<void()>>([Func, bWait] {
		Func();
		if (bWait)
		{
			GetSemaphore().Release();
		}
	});
	auto TaskPtr = UnqiueTask.Get();
	jlong FuncPtr = reinterpret_cast<jlong>(TaskPtr);
	jclass ActivityClass = Env->GetObjectClass(Activity);
	jmethodID PostTFunctionToUIThread = Env->GetMethodID(ActivityClass, "PostTFunctionToUIThread", "(J)V");
	if (PostTFunctionToUIThread)
	{
		Env->CallVoidMethod(Activity, PostTFunctionToUIThread, FuncPtr);
		FAndroidApplication::CheckJavaException(Env);
	}
	else
	{
		(*TaskPtr)();
	}
	Env->DeleteLocalRef(ActivityClass);
	if (bWait)
	{
		GetSemaphore().Acquire();
	}
#else
	Func();
#endif
}
#if PLATFORM_APPLE
void GMP::Internal::IsInUIThread()
{
	return [NSThread isMainThread];
}
#endif
#endif

namespace GMP
{
namespace Internal
{
	bool DelayExec(const UObject* InObj, FTimerDelegate Delegate, float InDelay, bool bEnsureExec)
	{
		InDelay = FMath::Max(InDelay, 0.00001f);
		auto World = GEngine->GetWorldFromContextObject(InObj, InObj ? EGetWorldErrorMode::LogAndReturnNull : EGetWorldErrorMode::ReturnNull);
#if WITH_EDITOR
		if (bEnsureExec && (!World || !World->IsGameWorld()))
		{
			if (GEditor && GEditor->IsTimerManagerValid())
			{
				FTimerHandle TimerHandle;
				GEditor->GetTimerManager()->SetTimer(TimerHandle, MoveTemp(Delegate), InDelay, false);
			}
			else
			{
				auto Holder = World ? NewObject<UGMPPlaceHolder>(World) : NewObject<UGMPPlaceHolder>();
				Holder->SetFlags(RF_Standalone);
				if (World)
					World->PerModuleDataObjects.Add(Holder);
				else
					Holder->AddToRoot();

				GEditor->OnPostEditorTick().AddWeakLambda(Holder, [Holder, WeakWorld{MakeWeakObjectPtr(World)}, Delegate(MoveTemp(Delegate)), InDelay](float Delta) mutable {
					InDelay -= Delta;
					if (InDelay <= 0.f)
					{
						if (auto OwnerWorld = WeakWorld.Get())
							OwnerWorld->PerModuleDataObjects.Remove(Holder);
						Holder->RemoveFromRoot();

						Delegate.ExecuteIfBound();
						Holder->ClearFlags(RF_Standalone);
						Holder->MarkAsGarbage();
					}
				});
			}
			return true;
		}
		else
#endif
		{
			World = World ? World : GWorld;
			ensure(!bEnsureExec || World);
			if (World)
			{
				FTimerHandle TimerHandle;
				World->GetTimerManager().SetTimer(TimerHandle, MoveTemp(Delegate), InDelay, false);
				return true;
			}
		}
		return false;
	}

}  // namespace Internal
}  // namespace GMP
