//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMP/GMPSignalsImpl.h"

template<typename... TArgs>
using TGMPSignal = GMP::TSignal<false, TArgs...>;

using FGMPSignal = TGMPSignal<>;
