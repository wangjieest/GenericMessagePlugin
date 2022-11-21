//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "GMP/GMPBPLib.h"
#include "GMP/GMPSerializer.h"
#include "GMP/GMPStructUnion.h"
#include "GMP/GMPTickBase.h"
#include "GMP/GMPUtils.h"
#include "GMP/GMPRpcUtils.h"

namespace GMP
{
class FGMPNetFrameWriter;
class FGMPNetFrameReader;
class FGMPMemoryReader;
class FGMPMemoryWriter;
GMP_API void OnGMPTagReady(FSimpleDelegate Callback);
}  // namespace GMP

namespace GMPReflection = GMP::Reflection;
namespace GMPTypeTraits = GMP::TypeTraits;
namespace GMPClass2Name = GMP::Class2Name;
namespace GMPClass2Prop = GMP::Class2Prop;

template<typename T, bool bExactType = true>
using TGMPClass2Name = GMP::TClass2Name<T, bExactType>;

template<typename T, bool bExactType = true>
using TGMPClass2Prop = GMP::TClass2Prop<T, bExactType>;

using FGMPSignalsHandle = GMP::FSigHandle;
using IGMPSigSource = GMP::ISigSource;
using FGMPSigSource = GMP::FSigSource;

#define GMP_EXTERNAL_SIGSOURCE(T)                     \
	namespace GMP                                     \
	{                                                 \
		template<>                                    \
		struct TExternalSigSource<T> : std::true_type \
		{                                             \
		};                                            \
	}

using FGMPMessageAddr = FGMPTypedAddr;
using FGMPMessageBody = GMP::FMessageBody;
using FGMPMessageHub = GMP::FMessageHub;

using FGMPHelper = GMP::FMessageUtils;
using FGMPNameSuccession = GMP::FNameSuccession;

template<typename Base, int32 INLINE_SIZE = GMP_ATTACHED_FUNCTION_INLINE_SIZE>
using TGMPAttachedCallableStore = GMP::TAttachedCallableStore<Base, INLINE_SIZE>;

// none-copyable functions
template<typename TSig>
using TGMPFunction = GMP::TGMPFunction<TSig>;

template<typename TSig>
using TGMPFunctionRef = GMP::TGMPFunctionRef<TSig>;

// FrameTickUtils
template<typename TTask>
using TGMPFrameTickWorldTask = GMP::TGMPFrameTickWorldTask<TTask>;
template<typename F>
decltype(auto) MakeGMPFrameTickWorldTask(const UObject* InObj, F&& LambdaTask, double MaxDurationTime = 0.0)
{
	return GMP::MakeGMPFrameTickWorldTask(InObj, std::forward<F>(LambdaTask), MaxDurationTime);
}

#define GMP_STATIC_PROPERTY(CLASS, NAME) GMPClass2Name::TTraitsUStruct<CLASS>::GetUStruct()->FindPropertyByName(#NAME)
#define GMP_MEMBER_PROPERTY(CLASS, NAME) GMPClass2Name::TTraitsUStruct<CLASS>::GetUStruct()->FindPropertyByName(#NAME)
#define GMP_UFUNCTION_CHECKED(CLASS, NAME) CLASS::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(CLASS, NAME))
