//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Engine/World.h"
#include "TimerManager.h"

namespace GMP
{
template<typename T>
struct TGMPFrameTickBase
{
public:
	TGMPFrameTickBase(double InMaxDuration = 0.013)
		: MaxDurationInFrame(InMaxDuration)
	{
	}

	// 设定每帧最长用时
	void SetMaxDurationInFrame(double In) { MaxDurationInFrame = FMath::Max(0.0, In); }

	// 驱动
	void Tick()
	{
		auto CurTime = FPlatformTime::Seconds();
		if (LastTime != 0.0)
		{
			TickInternal(CurTime - LastTime);
		}
		LastTime = CurTime;
	}
	void TickDelta(float DeltaTime)
	{
		auto CurTime = FPlatformTime::Seconds();
		if (LastTime == 0.0)
			LastTime = CurTime - DeltaTime;
		TickInternal(DeltaTime);
	}

	int64 GetTotalCount() const { return TotalCnt; }
	double GetTotalTime() const { return TotalTime; }

protected:
	// return true or void to continue, false to stop
	bool Step() { return false; }
	void Finish() {}

private:
	void TickInternal(float DeltaTime)
	{
		const double BeginTime = FPlatformTime::Seconds();
		const double EndTime = BeginTime + MaxDurationInFrame;
		double CurTime = BeginTime;

		int32 StepCnt = 0;
		bool bNext = true;
		while (bNext)
		{
			CurTime = FPlatformTime::Seconds();
			using RetType = decltype(std::declval<T>().Step());
			ProcessStep(bNext, std::conditional_t<std::is_same<RetType, bool>::value, std::true_type, std::false_type>{});

			const double NextEndTime = GetNextEndTimePoint(CurTime, BeginTime, ++StepCnt);
			if (!bNext && NextEndTime >= EndTime)
				break;
		}
		LastTime = CurTime;

		TotalCnt += StepCnt;
		TotalTime += CurTime - BeginTime;
	}
	double GetNextEndTimePoint(double InCur, double InBegin, int32 InCnt) const
	{
		GMP_CHECK_SLOW(InCur >= InBegin && InCnt > 0);
		return InCur + (InCur - InBegin) / InCnt;
	}

	void ProcessStep(bool& bContinue, std::true_type)
	{
		bContinue = static_cast<T*>(this)->Step();
		if (!bContinue)
			ProcessFinish();
	}
	void ProcessStep(bool& bContinue, std::false_type) { static_cast<T*>(this)->Step(); }
	void ProcessFinish() { static_cast<T*>(this)->Finish(); }
	double MaxDurationInFrame = 0.001;
	double LastTime = 0.0;

	double TotalTime = 0.0;
	int64 TotalCnt = 0ull;
};

template<typename F>
struct TGMPFrameTickTaskBase : public TGMPFrameTickBase<TGMPFrameTickTaskBase<F>>
{
public:
	TGMPFrameTickTaskBase(F&& InLambda, double MaxDurationTime = 0.013)
		: TGMPFrameTickBase<TGMPFrameTickTaskBase<F>>(MaxDurationTime)
		, Functor(std::forward<F>(InLambda))
	{
	}

protected:
	auto Step() { return Functor(); }
	F Functor;
};

template<typename F>
struct TGMPFrameTickWorldTask final : public TGMPFrameTickTaskBase<F>
{
public:
	TGMPFrameTickWorldTask(const UObject* InCtx, F&& InTask, double MaxDurationTime = 0.013)
		: TGMPFrameTickTaskBase<F>(std::forward<F>(InTask), MaxDurationTime)
	{
		//
		bool bSupported = false;
		WorldObj = InCtx->GetWorldChecked(bSupported);
		WorldObj->GetTimerManager().SetTimer(
			TimeHandle,
			[this] { this->Tick(); },
			0.001f,
			true);
	}
	~TGMPFrameTickWorldTask() { Cancel(); }
	void Cancel()
	{
		if (auto World = WorldObj.Get())
			World->GetTimerManager().ClearTimer(TimeHandle);
	}

protected:
	TWeakObjectPtr<UWorld> WorldObj;
	FTimerHandle TimeHandle;
};

template<typename F>
auto MakeGMPFrameTickWorldTask(const UObject* InObj, F&& Task, double MaxDurationTime = 0.0)
{
	GMP_CHECK_SLOW(InObj);
	return TGMPFrameTickWorldTask<std::decay_t<F>>(InObj, std::forward<F>(Task), MaxDurationTime);
}
}  // namespace GMP
