//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// JsonDom — thin forwarding shell. The DOM is now arena-backed (JsonArena.h): a per-document bump arena
// holds FArenaNode/FArenaObj, and call sites use FJsonRef handles (FJsonObjectPtr/FJsonValuePtr) plus
// zero-copy views (FJsonArrayView / object KV iteration) instead of the old MakeShared<FJsonValue> DOM.
// The legacy heavy FJsonValue/FJsonObject classes are gone (single-DOM end state). EJson, the handle
// typedefs, and the `sj::` alias all live in / route through JsonArena.h.
#pragma once

#ifndef UNREAL_JSONDOM_H
#define UNREAL_JSONDOM_H

#include "JsonArena.h"   // EJson + FArenaNode/FArenaObj/FArenaDoc + FJsonRef + views + FJsonDoc factory

// The `sj::` alias every call site uses; not lifted to global (would collide with UE's ::FJsonObject).
#ifndef UNREAL_SJ_ALIAS_DEFINED
#define UNREAL_SJ_ALIAS_DEFINED 1
namespace JSONDOM_ALIAS = JSONDOM_NAMESPACE;
#endif

#endif // UNREAL_JSONDOM_H
