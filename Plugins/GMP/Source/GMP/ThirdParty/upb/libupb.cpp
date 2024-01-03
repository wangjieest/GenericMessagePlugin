#include "libupb.h"

#include "upb/base/internal/log2.h"
#include "upb/mini_descriptor/internal/encode.h"
#include "GMPPBSerializer.h"

namespace upb
{
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
		str_.Reserve(upb_Log2CeilingSize(str_.Num() + (end - buf_)));
		str_.Append(buf_, end - buf_);
		return true;
	}

	const TArray<char>& GetData() const { return str_; }

private:
	char buf_[16];
	TArray<char> str_;
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

const TArray<char>& FMtDataEncoder::GetData() const
{
	return Encoder_->GetData();
}
}  // namespace upb
