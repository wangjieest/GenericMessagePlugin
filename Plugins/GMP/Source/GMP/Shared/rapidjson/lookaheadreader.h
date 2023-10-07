#include "reader.h"

#include <variant>

namespace rapidjson
{

template<typename SourceEncoding, typename TargetEncoding, typename StackAllocator = CrtAllocator>
class LookaheadReader
{
public:
	using Ch = typename SourceEncoding::Ch;
	using CCh = std::add_const_t<Ch>;

	bool Null()
	{
		st_ = kHasNull;
		v_.SetNull();
		return true;
	}
	bool Bool(bool b)
	{
		st_ = kHasBool;
		v_.SetBool(b);
		return true;
	}
	bool Int(int32_t i)
	{
		st_ = kHasNumber;
		v_.SetInt(i);
		return true;
	}
	bool Uint(uint32_t u)
	{
		st_ = kHasNumber;
		v_.SetUint(u);
		return true;
	}
	bool Int64(int64_t i)
	{
		st_ = kHasNumber;
		v_.SetInt64(i);
		return true;
	}
	bool Uint64(uint64_t u)
	{
		st_ = kHasNumber;
		v_.SetUint64(u);
		return true;
	}
	bool Double(double d)
	{
		st_ = kHasNumber;
		v_.SetDouble(d);
		return true;
	}
	bool RawNumber(const Ch*, SizeType, bool) { return false; }
	bool String(const Ch* str, SizeType length, bool)
	{
		st_ = kHasString;
		v_.SetString(str, length);
		return true;
	}
	bool StartObject()
	{
		st_ = kEnteringObject;
		return true;
	}
	bool Key(const Ch* str, SizeType length, bool)
	{
		st_ = kHasKey;
		v_.SetString(str, length);
		return true;
	}
	bool EndObject(SizeType)
	{
		st_ = kExitingObject;
		return true;
	}
	bool StartArray()
	{
		st_ = kEnteringArray;
		return true;
	}
	bool EndArray(SizeType)
	{
		st_ = kExitingArray;
		return true;
	}

	struct StringView
	{
		CCh* = Data = nullptr;
		uint32 Len = 0;
		CCh* operator*() const { return Data; }
		operator CCh*() const { return Data; }
		explicit operator uint32() const { return Len; }

		bool IsValid() const { return Data; }
		explicit operator bool() const { return IsValid(); }
	};

protected:
	LookaheadReader(Ch* str)
		: v_()
		, st_(kInit)
		, r_()
		, ss_(str)
	{
		r_.IterativeParseInit();
		ParseNext();
	}

	void ParseNext()
	{
		if (r_.HasParseError())
		{
			st_ = kError;
			return;
		}

		static constexpr int32_t parseFlags = kParseDefaultFlags | (UsingInsituStream ? kParseInsituFlag : 0);
		r_.IterativeParseNext<parseFlags>(ss_, *this);
	}

protected:
	enum ELookaheadState
	{
		kInit,
		kError,
		kHasNull,
		kHasBool,
		kHasNumber,
		kHasString,
		kHasKey,
		kEnteringObject,
		kExitingObject,
		kEnteringArray,
		kExitingArray
	};

	GenericValue<TargetEncoding> v_;
	ELookaheadState st_;
	GenericReader<SourceEncoding, TargetEncoding, StackAllocator> r_;
	enum
	{
		UsingInsituStream = !std::is_const<Ch>::value && std::is_same<SourceEncoding, TargetEncoding>::value;
	};
	using GenericStringStreamType = std::conditional_t<UsingInsituStream, GenericInsituStringStream<SourceEncoding>, GenericStringStream<SourceEncoding>>;
	GenericStringStreamType<SourceEncoding> ss_;
};

template<typename SourceEncoding, typename TargetEncoding, typename StackAllocator = CrtAllocator>
class LookaheadReader : protected LookaheadReader<SourceEncoding, TargetEncoding, StackAllocator>
{
public:
	using Ch = typename SourceEncoding::Ch;
	using CCh = typename SourceEncoding::CCh;
	using StringView = typename LookaheadReader::StringView;

	LookaheadReader(Ch* str)
		: LookaheadReader(str)
	{
	}

	bool EnterObject()
	{
		if (st_ != kEnteringObject)
		{
			st_ = kError;
			return false;
		}

		ParseNext();
		return true;
	}

	bool EnterArray()
	{
		if (st_ != kEnteringArray)
		{
			st_ = kError;
			return false;
		}

		ParseNext();
		return true;
	}

	CCh* NextObjectKey()
	{
		if (st_ == kHasKey)
		{
			CCh* result = v_.GetString();
			ParseNext();
			return result;
		}

		if (st_ != kExitingObject)
		{
			st_ = kError;
			return nullptr;
		}

		ParseNext();
		return nullptr;
	}

	bool NextArrayValue()
	{
		if (st_ == kExitingArray)
		{
			ParseNext();
			return false;
		}

		if (st_ == kError || st_ == kExitingObject || st_ == kHasKey)
		{
			st_ = kError;
			return false;
		}

		return true;
	}

	int32_t GetInt()
	{
		if (st_ != kHasNumber || !v_.IsInt())
		{
			st_ = kError;
			return 0;
		}

		int32_t result = v_.GetInt();
		ParseNext();
		return result;
	}
	uint32_t GetUInt()
	{
		if (st_ != kHasNumber || !v_.IsUInt())
		{
			st_ = kError;
			return 0;
		}

		uint32_t result = v_.GetUInt();
		ParseNext();
		return result;
	}
	int64_t GetInt64()
	{
		if (st_ != kHasNumber || !v_.IsInt64())
		{
			st_ = kError;
			return 0;
		}

		int64_t result = v_.GetInt64();
		ParseNext();
		return result;
	}
	uint64_t GetUInt64()
	{
		if (st_ != kHasNumber || !v_.IsUInt64())
		{
			st_ = kError;
			return 0;
		}

		uint64_t result = v_.GetUInt64();
		ParseNext();
		return result;
	}
	double GetDouble()
	{
		if (st_ != kHasNumber)
		{
			st_ = kError;
			return 0.;
		}

		double result = v_.GetDouble();
		ParseNext();
		return result;
	}

	CCh* GetString(uint32_t* OutLen = nullptr)
	{
		if (st_ != kHasString)
		{
			st_ = kError;
			return 0;
		}

		CCh* result = v_.GetString();
		if (OutLen)
			*OutLen = v_.GetStringLength();
		ParseNext();
		return result;
	}

	bool GetBool()
	{
		if (st_ != kHasBool)
		{
			st_ = kError;
			return false;
		}

		bool result = v_.GetBool();
		ParseNext();
		return result;
	}

	void GetNull()
	{
		if (st_ != kHasNull)
		{
			st_ = kError;
			return;
		}

		ParseNext();
	}

	void SkipObject() { SkipOut(1); }
	void SkipArray() { SkipOut(1); }
	void SkipValue() { SkipOut(0); }

	auto* PeekValue() const -> decltype(&v_)
	{
		if (st_ >= kHasNull && st_ <= kHasKey)
		{
			return &v_;
		}

		return nullptr;
	}

	bool PeekArrayType() const { return PeekType() == kArrayType; }
	bool PeekObjectType() const { return PeekType() == kObjectType; }

	bool PeekNumberType() const { return PeekType() == kNumberType; }
	bool PeekStringType() const { return PeekType() == kStringType; }
	int32_t PeekType() const
	{
		if (st_ >= kHasNull && st_ <= kHasKey)
		{
			return v_.GetType();
		}

		if (st_ == kEnteringArray)
		{
			return kArrayType;
		}

		if (st_ == kEnteringObject)
		{
			return kObjectType;
		}

		return -1;
	}
	bool IsValid() const { return st_ != kError; }

	template<typename StrView = StringView>
	StrView GetStringView()
	{
		if (st_ != kHasString)
		{
			st_ = kError;
			return {};
		}

		CCh* result = v_.GetString();
		auto Len = v_.GetStringLength();
		ParseNext();
		return StrView(result, Len);
	}

	bool PeekLeafType() const { return PeekValue() != nullptr; }

	template<typename StrView = StringView>
	std::variant<std::monostate, bool, int32_t, uint32_t, int64_t, uint64_t, double, StrView> GetLeafValue()
	{
		if (auto Val = PeekValue())
		{
			if (Val->IsString())
				return StrView(Val->GetString(), Val->GetStringLength());
			else if (Val->IsBool())
				return Val->GetBool();
			else if (Val->IsDouble())
				return Val->GetDouble();
			else if (Val->IsInt())
				return Val->GetInt();
			else if (Val->IsUint())
				return Val->GetUint();
			else if (Val->IsInt64())
				return Val->GetInt64();
			else
				return Val->GetUint64();
		}

		return std::monostate{};
	}

	std::variant<std::monostate, bool, int32_t, uint32_t, int64_t, uint64_t, double> GetNumber()
	{
		if (auto Val = PeekValue())
		{
			if (Val->IsBool())
				return Val->GetBool();
			else if (Val->IsDouble())
				return Val->GetDouble();
			else if (Val->IsInt())
				return Val->GetInt();
			else if (Val->IsUint())
				return Val->GetUint();
			else if (Val->IsInt64())
				return Val->GetInt64();
			else
				return Val->GetUint64();
		}

		return std::monostate{};
	}

protected:
	void SkipOut(int32_t depth)
	{
		do
		{
			if (st_ == kEnteringArray || st_ == kEnteringObject)
			{
				++depth;
			}
			else if (st_ == kExitingArray || st_ == kExitingObject)
			{
				--depth;
			}
			else if (st_ == kError)
			{
				return;
			}

			ParseNext();
		} while (depth > 0);
	}
};

#if 0
int test()
{
	using namespace std;

	char json[] =
		" { \"hello\" : \"world\", \"t\" : true , \"f\" : false, \"n\": null,"
		"\"i\":123, \"pi\": 3.1416, \"a\":[-1, 2, 3, 4, \"array\", []], \"skipArrays\":[1, 2, [[[3]]]], "
		"\"skipObject\":{ \"i\":0, \"t\":true, \"n\":null, \"d\":123.45 }, "
		"\"skipNested\":[[[[{\"\":0}, {\"\":[-9.87]}]]], [], []], "
		"\"skipString\":\"zzz\", \"reachedEnd\":null, \"t\":true }";

	LookaheadReader r(json);

	RAPIDJSON_ASSERT(r.PeekType() == kObjectType);

	r.EnterObject();
	while (const char* key = r.NextObjectKey())
	{
		if (0 == strcmp(key, "hello"))
		{
			RAPIDJSON_ASSERT(r.PeekType() == kStringType);
			cout << key << ":" << r.GetString() << endl;
		}
		else if (0 == strcmp(key, "t") || 0 == strcmp(key, "f"))
		{
			RAPIDJSON_ASSERT(r.PeekType() == kTrueType || r.PeekType() == kFalseType);
			cout << key << ":" << r.GetBool() << endl;
			continue;
		}
		else if (0 == strcmp(key, "n"))
		{
			RAPIDJSON_ASSERT(r.PeekType() == kNullType);
			r.GetNull();
			cout << key << endl;
			continue;
		}
		else if (0 == strcmp(key, "pi"))
		{
			RAPIDJSON_ASSERT(r.PeekType() == kNumberType);
			cout << key << ":" << r.GetDouble() << endl;
			continue;
		}
		else if (0 == strcmp(key, "a"))
		{
			RAPIDJSON_ASSERT(r.PeekType() == kArrayType);

			r.EnterArray();

			cout << key << ":[ ";
			while (r.NextArrayValue())
			{
				if (r.PeekType() == kNumberType)
				{
					cout << r.GetDouble() << " ";
				}
				else if (r.PeekType() == kStringType)
				{
					cout << r.GetString() << " ";
				}
				else
				{
					r.SkipArray();
					break;
				}
			}

			cout << "]" << endl;
		}
		else
		{
			cout << key << ":skipped" << endl;
			r.SkipValue();
		}
	}

	return 0;
}
#endif

}  // namespace rapidjson
