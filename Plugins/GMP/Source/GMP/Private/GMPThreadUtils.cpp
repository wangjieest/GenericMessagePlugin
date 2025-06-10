//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPThreadUtils.h"

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

void GMP::Internal::RunOnUIThreadImpl(TFunction<void()> Func)
{
	static auto GetSemaphore = []() -> FSemaphore& {
		static FSemaphore semaphore(0, 1);
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
		GetSemaphore().Release();
	});
	GetSemaphore().Acquire();
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
	auto TaskHolder = MakeUnique<TUniqueFunction<void()>>([Func] {
		Func();
		GetSemaphore().Release();
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
	GetSemaphore().Acquire();
#else
	Func();
#endif
}

#endif
