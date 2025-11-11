//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPThreadUtils.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GMPStruct.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#if PLATFORM_APPLE || PLATFORM_ANDROID
#include "HAL/UESemaphore.h"

#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#endif

#ifndef GMP_WITH_ANDROID_UI_THREAD
#define GMP_WITH_ANDROID_UI_THREAD 0
#endif

#define GMP_HAS_ANDROID_UITHREAD (GMP_WITH_ANDROID_UI_THREAD)
#if GMP_HAS_ANDROID_UITHREAD
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"  // require Module "Luncher"
#include <jni.h>
extern "C" JNIEXPORT void JNICALL Java_com_epicgames_GameActivity_gmpNativeRunNativeTFunction(JNIEnv* Env, jobject Thiz, jlong FuncPtr)
{
	if (FuncPtr != 0)
	{
		TFunction<void()>* TaskPtr = reinterpret_cast<TFunction<void()>*>(FuncPtr);
		(*TaskPtr)();
		delete TaskPtr;
	}
}

static bool IsOnAndroidUiThread()
{
	static thread_local bool bCached = false;
	static thread_local bool bIsUIThread = false;
	if (bCached)
	{
		return bIsUIThread;
	}

	JNIEnv* Env = FAndroidApplication::GetJavaEnv(true);
	jmethodID mIsOnUiThread = Env ? Env->GetStaticMethodID(FJavaWrapper::GameActivityClassID, "gmpIsOnUiThread", "()Z") : nullptr;
	if (mIsOnUiThread)
	{
		jboolean result = FJavaWrapper::CallStaticBooleanMethod(Env, FJavaWrapper::GameActivityClassID, mIsOnUiThread);
		bIsUIThread = result == JNI_TRUE;
		bCached = true;
		return bIsUIThread;
	}

	static uint32 UIThreadId = 0;
	if (UIThreadId == 0)
	{
		jclass LooperClass = Env->FindClass("android/os/Looper");
		jmethodID GetMainLooper = Env->GetStaticMethodID(LooperClass, "getMainLooper", "()Landroid/os/Looper;");
		jobject MainLooper = Env->CallStaticObjectMethod(LooperClass, GetMainLooper);
		jmethodID GetThread = Env->GetMethodID(LooperClass, "getThread", "()Ljava/lang/Thread;");
		jobject MainThread = Env->CallObjectMethod(MainLooper, GetThread);

		jclass ThreadClass = Env->FindClass("java/lang/Thread");
		jmethodID CurrentThread = Env->GetStaticMethodID(ThreadClass, "currentThread", "()Ljava/lang/Thread;");
		jobject Current = Env->CallStaticObjectMethod(ThreadClass, CurrentThread);

		const bool bCurrentIsUI = Env->IsSameObject(Current, MainThread);
		if (bCurrentIsUI)
		{
			UIThreadId = FPlatformTLS::GetCurrentThreadId();
			bIsUIThread = true;
		}
		bCached = true;
		Env->DeleteLocalRef(LooperClass);
		Env->DeleteLocalRef(MainLooper);
		Env->DeleteLocalRef(MainThread);
		Env->DeleteLocalRef(ThreadClass);
		Env->DeleteLocalRef(Current);
	}
	return bIsUIThread;
}
#endif  // GMP_HAS_ANDROID_UITHREAD

void GMP::Internal::RunOnUIThreadImpl(TFunction<void()> Func, GMP::ERunType RunType)
{
	static auto GetSemaphore = []() -> FSemaphore& {
		static thread_local FSemaphore semaphore(0, 1);
		return semaphore;
	};
	if (RunType != GMP::ForceAsync && GMP::Internal::IsInUIThread())
	{
		Func();
		return;
	}
#if PLATFORM_APPLE
	dispatch_async(dispatch_get_main_queue(), ^{
		Func();
		if (RunType == GMP::ForceBlock)
		{
			GetSemaphore().Release();
		}
	});
	if (RunType == GMP::ForceBlock)
	{
		GetSemaphore().Acquire();
	}
#elif GMP_HAS_ANDROID_UITHREAD
	jmethodID PostTFunctionToUIThread = nullptr;
	JNIEnv* Env = FAndroidApplication::GetJavaEnv(true);
	if (Env)
	{
		PostTFunctionToUIThread = Env->GetMethodID(FJavaWrapper::GameActivityClassID, "gmpPostTFunctionToUIThread", "(J)V");
	}
	if (!PostTFunctionToUIThread)
	{
		Func();
		return;
	}
	auto FuncHolder = new TFunction<void()>([Func, RunType] {
		Func();
		if (RunType == GMP::ForceBlock)
		{
			GetSemaphore().Release();
		}
	});
	jlong FuncPtr = reinterpret_cast<jlong>(FuncHolder);
	Env->CallStaticVoidMethod(FJavaWrapper::GameActivityClassID, PostTFunctionToUIThread, FuncPtr);
	FAndroidApplication::CheckJavaException();
	if (RunType == GMP::ForceBlock)
	{
		GetSemaphore().Acquire();
	}
#else
	if (RunType != GMP::ForceAsync)
	{
		Func();
	}
	else
	{
		auto Future = Async(EAsyncExecution::TaskGraphMainThread, [Func{MoveTemp(Func)}] { Func(); });
		if (RunType == GMP::ForceBlock)
		{
			Future.Consume();
		}
	}
#endif
}
#endif

namespace GMP
{
namespace Internal
{
	bool IsInUIThread()
	{
#if PLATFORM_APPLE
		return [NSThread isMainThread];  // dispatch_get_main_queue() == dispatch_get_current_queue()
#elif PLATFORM_ANDROID && GMP_HAS_ANDROID_UITHREAD
		return IsOnAndroidUiThread();
#else
		return IsInGameThread();  // otherwise the game thread is the UI thread
#endif
	}

	bool DelayExecImpl(const UObject* InObj, FTimerDelegate Delegate, float InDelay, bool bEnsureExec)
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

	TWeakPtr<void> DelayTickerImpl(TDelegate<bool(float)> InTicker, float InDelay, bool bEnsureExec)
	{
		return FTSTicker::GetCoreTicker().AddTicker(InTicker, InDelay);
	}

}  // namespace Internal
}  // namespace GMP
