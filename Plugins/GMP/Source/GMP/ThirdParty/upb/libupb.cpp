#include "libupb.h"

#include "upb/base/internal/log2.h"
#include "upb/mini_descriptor/internal/encode.h"

#include <string>

#if defined(__UNREAL__)
#include "HAL/UnrealMemory.h"
#include "upb/mem/alloc.h"
#include "upb/mem/arena.h"
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
UPB_EXPORT struct upb_Arena* _upb_global_arena = nullptr;
struct upb_Arena* get_upb_global_arena()
{
	return _upb_global_arena;
}
bool set_upb_global_arena(struct upb_Arena* in)
{
	bool bfreed = !!_upb_global_arena;
	if (bfreed)
		upb_Arena_Free(_upb_global_arena);
	_upb_global_arena = in;
	return bfreed;
}
#include "upb/port/undef.inc"
#endif  // __UNREAL__

namespace upb
{
FStackedArena::FStackedArena()
{
	_arena = upb_Arena_New();
	std::swap(_arena, _upb_global_arena);
}
FStackedArena::~FStackedArena()
{
	std::swap(_arena, _upb_global_arena);
	upb_Arena_Free(_arena);
}

class StringAppender
{
public:
	StringAppender(upb_MtDataEncoder* e) { e->end = buf_ + sizeof(buf_); }

	template<class T>
	bool operator()(T&& func)
	{
		char* end = func(buf_);
		if (!end)
			return false;
		// C++ does not guarantee that string has doubling growth behavior, but
		// we need it to avoid O(n^2).
		str_.reserve(upb_Log2CeilingSize(str_.size() + (end - buf_)));
		str_.append(buf_, end - buf_);
		return true;
	}

	const char* GetData(size_t* size) const
	{
		if (size)
			*size = str_.size();
		return str_.c_str();
	}

private:
	char buf_[16];
	std::string str_;
};

class FMtDataEncoderImpl : public StringAppender
{
public:
	FMtDataEncoderImpl()
		: StringAppender(&encoder_)
	{
	}

	upb_MtDataEncoder* GetEncoder() { return &encoder_; }

private:
	friend class FMtDataEncoder;
	upb_MtDataEncoder encoder_;
};

FMtDataEncoder::FMtDataEncoder() = default;
FMtDataEncoder::~FMtDataEncoder() = default;

template<class F>
bool FMtDataEncoder::Append(F&& func)
{
	return (*Encoder_)(std::forward<F>(func));
}

bool FMtDataEncoder::StartMessage(uint64_t msg_mod)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_StartMessage(encoder, buf, msg_mod); });
}

bool FMtDataEncoder::PutField(upb_FieldType type, uint32_t field_num, uint64_t field_mod)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_PutField(encoder, buf, type, field_num, field_mod); });
}

bool FMtDataEncoder::StartOneof()
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_StartOneof(encoder, buf); });
}

bool FMtDataEncoder::PutOneofField(uint32_t field_num)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_PutOneofField(encoder, buf, field_num); });
}

bool FMtDataEncoder::StartEnum()
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_StartEnum(encoder, buf); });
}

bool FMtDataEncoder::PutEnumValue(uint32_t enum_value)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_PutEnumValue(encoder, buf, enum_value); });
}

bool FMtDataEncoder::EndEnum()
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_EndEnum(encoder, buf); });
}

bool FMtDataEncoder::EncodeExtension(upb_FieldType type, uint32_t field_num, uint64_t field_mod)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_EncodeExtension(encoder, buf, type, field_num, field_mod); });
}

bool FMtDataEncoder::EncodeMap(upb_FieldType key_type, upb_FieldType val_type, uint64_t key_mod, uint64_t val_mod)
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_EncodeMap(encoder, buf, key_type, val_type, key_mod, val_mod); });
}

bool FMtDataEncoder::EncodeMessageSet()
{
	auto encoder = Encoder_->GetEncoder();
	return Append([=](char* buf) { return upb_MtDataEncoder_EncodeMessageSet(encoder, buf); });
}

const char* FMtDataEncoder::GetData(size_t* size) const
{
	return Encoder_->GetData(size);
}
}  // namespace upb
