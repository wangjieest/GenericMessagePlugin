//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMP/GMPBPLib.h"
#include "GMP/GMPArchive.h"
#include "GMP/GMPTickBase.h"
#include "GMP/GMPUtils.h"
#include "GMP/GMPRpcUtils.h"
#include "GMP/GMPThreadUtils.h"
#include "GMP/GMPWorldLocals.h"
#include "Classes/GMPUnion.h"
#if GMP_WITH_STATIC_STORE
// The typed-MSGKEY entries on FMessageUtils (declared in GMPUtils.h) have their out-of-line definitions in the
// top-layer GMPHubOpt.h (which pulls the complete slot/store). It MUST be visible everywhere a user TU might
// select a typed overload (the typed entries return 'auto', so even the definition-not-found case surfaces as a
// compile error C3779, not just a link error). GMPCore.h is the single header every standard user TU reaches, so
// inject here. Placed AFTER Classes/GMPUnion.h so FGMPStructUnion is complete (and GMPUnion.h no longer pulls
// GMPCore.h, so there is no cycle). The injected slot header is heavy CoreUObject; that is the accepted cost of a
// transparent zero-source-change upgrade. Gated on GMP_WITH_STATIC_STORE: only the monolithic static-store build
// has typed overloads to define, so modular/editor builds neither inject this heavy header nor pay its cost.
#include "GMP/GMPHubOpt.h"
#endif

namespace GMP
{
class FGMPNetFrameWriter;
class FGMPNetFrameReader;
class FGMPMemoryReader;
class FGMPMemoryWriter;
GMP_API void OnGMPModuleLifetime(FSimpleDelegate Startup, FSimpleDelegate Shutdown = {});
FORCEINLINE void OnGMPTagReady(FSimpleDelegate Callback)
{
	OnGMPModuleLifetime(MoveTemp(Callback));
}
}  // namespace GMP

namespace GMPReflection = GMP::Reflection;
namespace GMPTypeTraits = GMP::TypeTraits;
namespace GMPClass2Name = GMP::Class2Name;
namespace GMPClass2Prop = GMP::Class2Prop;

template<typename T, bool bExactType = true>
using TGMPClass2Name = GMP::TClass2Name<T, bExactType>;

template<typename T, bool bExactType = true>
using TGMPClass2Prop = GMP::TClass2Prop<T, bExactType>;

template<typename T>
using TGMPClassToPropTag = GMP::Class2Prop::TClassToPropTag<T>;

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

template<typename Base, int32 INLINE_SIZE = GMP_FUNCTION_PREDEFINED_INLINE_SIZE>
using TGMPAttachedCallableStore = GMP::TAttachedCallableStore<Base, INLINE_SIZE>;

// none-copyable functions
template<typename TSig>
using TGMPFunction = GMP::TGMPFunction<TSig>;

template<typename TSig>
using TGMPWeakFunction = GMP::TGMPWeakFunction<TSig>;

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
