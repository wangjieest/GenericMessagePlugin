//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoSerializerEditor.h"

#if defined(GMP_WITH_UPB) && 0
#if WITH_EDITOR
#include "Editor.h"
#include "GMPEditorUtils.h"
#include "GMPProtoUtils.h"
#include "HAL/PlatformFile.h"
#include "Misc/FileHelper.h"
#include "UnrealCompatibility.h"
#include "upb/libupb.h"
#include "upb/util/def_to_proto.h"
#include "upb/wire/types.h"

#include <cmath>

// Must be last
#include "upb/port/def.inc"

namespace GMP
{
namespace Proto
{

}  // namespace Proto
}  // namespace GMP

#include "upb/port/undef.inc"
#endif  // WITH_EDITOR
#endif
