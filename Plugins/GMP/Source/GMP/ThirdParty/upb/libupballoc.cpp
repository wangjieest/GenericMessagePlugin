//#include "libupb.h"
#include "HAL/UnrealMemory.h"
#include "upb/base/internal/log2.h"
#include "upb/mem/alloc.h"
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
#include "upb/port/undef.inc"
