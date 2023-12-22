// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_GENERATED_CODE_MACROS_H_
#define UPB_GENERATED_CODE_MACROS_H_

// IWYU pragma: begin_exports
#include "upb/message/array.h"
// IWYU pragma: end_exports


#ifdef __cplusplus
#include "upb/port/def.inc"

#ifndef DEFAULT_ARENA_PARAMETER
extern UPB_API struct upb_Arena* get_upb_global_arena();
extern UPB_API bool set_upb_global_arena(struct upb_Arena*);

#define DEFAULT_ARENA_PARAMETER = get_upb_global_arena()
#endif

#ifndef UPB_VALID_ARENA
UPB_INLINE struct upb_Arena* upb_valid_arena(struct upb_Arena* arena) { return arena; }
#define UPB_VALID_ARENA(x) upb_valid_arena(x)
#endif

#ifndef UPB_ITERATOR_SUPPORT
template<typename T>
struct upb_range_t
{
protected:
	struct upb_iterator
	{
		const struct upb_Array* _arr;
		size_t _index;

		upb_iterator(const struct upb_Array * arr, size_t index) : _arr(arr), _index(index) {}
		upb_iterator& operator++() { ++_index; return *this; }
		explicit operator bool() const { return _arr && _index < _arr->size; }
		bool operator!() const { return !(bool)*this; }
		T* operator->() const { return ((T*const*)_upb_array_constptr(_arr))[_index]; }
		T& operator*() const { return *(this->operator->()); }
		bool operator==(const upb_iterator& rhs) const { return _arr == rhs._arr && _index == rhs._index; }
		bool operator!=(const upb_iterator& rhs) const { return !(*this == rhs); }
	};
	const struct upb_Array * _arr;
public:
	upb_range_t(const struct upb_Array * arr) : _arr(arr) {}
	upb_iterator begin() const { return upb_iterator(_arr, 0); }
	upb_iterator end() const { return upb_iterator(_arr, _arr->size); }

};
#define UPB_ITERATOR_SUPPORT(name, type) upb_range_t<type> name() const	{ return upb_range_t<type>(_##name##_upb_array(nullptr)); }
#endif

#include "upb/port/undef.inc"

#endif  // __cplusplus

#endif  // UPB_GENERATED_CODE_MACROS_H_
