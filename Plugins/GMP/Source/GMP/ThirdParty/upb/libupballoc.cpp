//#include "libupb.h"
#include "HAL/UnrealMemory.h"
#include "upb/base/internal/log2.h"
#include "upb/mem/alloc.h"
#include "upb/mem/arena.h"
#include "upb/mini_descriptor/internal/encode.h"
#include "upb/port/def.inc"

extern "C"
{
	static void* upb_global_allocfunc(upb_alloc* alloc, void* ptr, size_t oldsize, size_t size)
	{
		UPB_UNUSED(alloc);
		UPB_UNUSED(oldsize);
		if (size == 0)
		{
			FMemory::Free(ptr);
			return nullptr;
		}
		else
		{
			return FMemory::Realloc(ptr, size);
		}
	}
	upb_alloc upb_alloc_global = {&upb_global_allocfunc};
}
GMP_API struct upb_Arena * _upb_global_arena = nullptr;
GMP_API struct upb_Arena * get_upb_global_arena() 
{
	return _upb_global_arena;
}
GMP_API bool set_upb_global_arena(struct upb_Arena* in)
{
	bool bfreed = !!_upb_global_arena;
	if (bfreed)
		upb_Arena_Free(_upb_global_arena);
	_upb_global_arena = in;
	return bfreed;
}

#include "upb/port/undef.inc"
