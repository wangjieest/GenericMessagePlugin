//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPPBSerializer.h"

#include "GMPOneOfBPLib.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "upb/libupb.h"
#include "UnrealCompatibility.h"
#include <variant>

namespace GMP
{
namespace PB
{
	using namespace upb;
	static auto& GetDefPoolMap()
	{
		static TMap<uint8, TUniquePtr<FDefPool>> PoolMap;
		return PoolMap;
	}
	static FDefPool& GetDefPool(uint8 Idx = 0)
	{
		if (!GetDefPoolMap().Contains(Idx))
		{
			GetDefPoolMap().Emplace(Idx, MakeUnique<FDefPool>());	
		}
		return *GetDefPoolMap().FindChecked(Idx);
	}

	FMessageDefPtr FindMessageByName(StringView Sym)
	{
		return GetDefPool().FindMessageByName(Sym);
	}

	bool AddProto(const char* InBuf, uint32 InSize)
	{
		return GetDefPool().AddFile(StringView(InBuf, InSize));
	}

	bool AddProtos(const char* InBuf, uint32 InSize)
	{
		size_t DefCnt = 0;
		auto Arena = FArena();
		if (auto FileProtoSet = google_protobuf_FileDescriptorSet_parse(InBuf, InSize, Arena))
		{
			size_t ProtoCnt = 0;
			auto FileProtos = google_protobuf_FileDescriptorSet_file(FileProtoSet, &ProtoCnt);
			for (auto i = 0; i < ProtoCnt; i++)
			{
				FStatus TmpStatus;
				GetDefPool().AddProto(FileProtos[i], TmpStatus);
				DefCnt += ensure(TmpStatus) ? 1 : 0;
			}
		}
		return DefCnt > 0;
	}
	void ClearProtos()
	{
		GetDefPoolMap().Empty();
	}

	//////////////////////////////////////////////////////////////////////////
	struct PBEnum
	{
		int32_t EnumValue;
	};

	template<typename... TArgs>
	using TValueType = std::variant<std::monostate, bool, int32, uint32, int64, uint64, float, double, TArgs...>;
	template<typename T>
	struct TBaseFieldInfo
	{
		// static FFieldDefPtr GetFieldDef() const { return FieldDef; }
	};
	template<>
	struct TBaseFieldInfo<PBEnum>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Enum;
	};
	template<>
	struct TBaseFieldInfo<bool>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Bool;
	};
	template<>
	struct TBaseFieldInfo<float>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Float;
	};
	template<>
	struct TBaseFieldInfo<double>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Double;
	};
	template<>
	struct TBaseFieldInfo<int32>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Int32;
	};
	template<>
	struct TBaseFieldInfo<uint32>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_UInt32;
	};
	template<>
	struct TBaseFieldInfo<int64>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Int64;
	};
	template<>
	struct TBaseFieldInfo<uint64>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_UInt64;
	};
	union FMessageValue
	{
		bool bool_val;
		float float_val;
		double double_val;
		int32_t int32_val;
		int64_t int64_val;
		uint32_t uint32_val;
		uint64_t uint64_val;
		upb_StringView str_val;
		void* ptr_val;
#if 0
		const upb_Array* array_val;
		const upb_Map* map_val;
		const upb_Message* msg_val;
#endif
		// EXPERIMENTAL: A tagged upb_Message*.  Users must use this instead of
		// msg_val if unlinked sub-messages may possibly be in use.  See the
		// documentation in kUpb_DecodeOption_ExperimentalAllowUnlinked for more
		// information.
		upb_TaggedMessagePtr tagged_msg_val;
	};

	struct FFieldValueReader
	{
		FFieldDefPtr FieldDef;
		FMessageValue MsgVal;
		FFieldValueReader(const upb_Message* InMsg, FFieldDefPtr InField)
			: FieldDef(InField)
		{
			MsgVal.ptr_val = const_cast<upb_Message*>(InMsg);
		}
		const upb_Message* GetMsg() const { return (const upb_Message*)MsgVal.ptr_val; }
		const upb_Array* GetArr() const { return (const upb_Array*)MsgVal.ptr_val; }
		const upb_MiniTable* MiniTable() const { return FieldDef.ContainingType().MiniTable(); }

		//////////////////////////////////////////////////////////////////////////
		bool IsEnum() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Enum; }

		//////////////////////////////////////////////////////////////////////////
		bool IsBool() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Bool; }
		bool IsFloat() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Float; }
		bool IsDouble() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Double; }
		bool IsInt() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Int32; }
		bool IsUint() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_UInt32; }
		bool IsInt64() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Int64; }
		bool IsUint64() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_UInt64; }
		bool IsNumber() const { return IsBool() || IsFloat() || IsDouble() || IsInt() || IsUint() || IsInt64() || IsUint64() || IsEnum(); }

		template<typename T>
		void GetFieldNum(T& Out) const
		{
			if (ensureAlways(FieldDef.GetCType() == TBaseFieldInfo<T>::CType))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					auto DefaultVal = FieldDef.DefaultValue();
					_upb_Message_GetNonExtensionField(GetMsg(), FieldDef.MiniTable(), &DefaultVal, &Out);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					Out = *(const T*)ArrayElmData();
				}
			}
		}

		template<typename T>
		T GetFieldNum() const
		{
			T Ret;
			GetFieldNum(Ret);
			return Ret;
		}
		TValueType<> AsNumber() const
		{
			if (IsBool())
			{
				return GetFieldNum<bool>();
			}
			else if (IsFloat())
			{
				return GetFieldNum<float>();
			}
			else if (IsDouble())
			{
				return GetFieldNum<double>();
			}
			else if (IsInt())
			{
				return GetFieldNum<int32>();
			}
			else if (IsUint())
			{
				return GetFieldNum<uint32>();
			}
			else if (IsInt64())
			{
				return GetFieldNum<int64>();
			}
			else if (IsUint64())
			{
				return GetFieldNum<uint64>();
			}
			else if (IsEnum())
			{
				return GetFieldNum<int32>();
			}
			return std::monostate{};
		}
		template<typename T, typename F>
		T ToNumber(const F& Func) const
		{
			ensure(IsNumber());
			T Ret{};
			std::visit([&](const auto& Item) { Ret = Func(Item); }, AsNumber());
			return Ret;
		}

		template<typename T>
		T ToNumber() const
		{
			ensure(IsNumber());
			T Ret{};
			std::visit([&](const auto& Item) { Ret = VisitVal<T>(Item); }, AsNumber());
			return Ret;
		}
		//////////////////////////////////////////////////////////////////////////
		bool IsBytes() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_Bytes; }

		//////////////////////////////////////////////////////////////////////////
		bool IsString() const { return FieldDef.GetCType() == upb_CType::kUpb_CType_String; }
		template<typename T>
		void GetFieldStr(T& Out) const
		{
			if (ensureAlways(IsString()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					auto val = upb_Message_GetString(GetMsg(), FieldDef.MiniTable(), FieldDef.DefaultValue().str_val);
					Out = StringView(val);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
					if (arr && ensure(FieldDef.GetArrayIdx() < arr->size))
					{
						Out = StringView(*(const upb_StringView*)ArrayElmData());
					}
					else
					{
						Out = {};
					}
				}
			}
		}

		upb_StringView GetFieldBytes() const
		{
			if (ensureAlways(IsString()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_GetString(GetMsg(), FieldDef.MiniTable(), FieldDef.DefaultValue().str_val);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
					if (arr && ensure(FieldDef.GetArrayIdx() < arr->size))
					{
						return (*(const upb_StringView*)ArrayElmData());
					}
				}
			}
			return {};
		}

		template<typename T = StringView>
		T GetFieldStr() const
		{
			T Ret{};
			GetFieldStr(Ret);
			return Ret;
		}
		//////////////////////////////////////////////////////////////////////////
		bool IsArray() const { return FieldDef.IsSequence(); }
		size_t ArraySize() const
		{
			const upb_Array* arr = IsArray() ? upb_Message_GetArray(GetMsg(), FieldDef.MiniTable()) : nullptr;
			return arr ? arr->size : 0;
		}

		const FFieldValueReader ArrayElm(size_t Idx) const
		{
			GMP_CHECK(IsArray());
			const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
			ensureAlways(arr && Idx < arr->size);
			return FFieldValueReader(GetMsg(), FieldDef.GetElementDef(Idx));
		}

		//////////////////////////////////////////////////////////////////////////
		bool IsMessage() const { return FieldDef.IsSubMessage(); }

		//////////////////////////////////////////////////////////////////////////
		bool IsMap() const { return FieldDef.IsMap(); }

		//////////////////////////////////////////////////////////////////////////
		TValueType<StringView, const FFieldValueReader*> DispatchValue() const
		{
			if (FieldDef.GetArrayIdx() < 0)
			{
				GMP_CHECK(IsMessage() || IsArray() || IsMap());
				return this;
			}
			else if (IsString())
			{
				return GetFieldStr<StringView>();
			}
			else if (IsBool())
			{
				return GetFieldNum<bool>();
			}
			else if (IsFloat())
			{
				return GetFieldNum<float>();
			}
			else if (IsDouble())
			{
				return GetFieldNum<double>();
			}
			else if (IsInt())
			{
				return GetFieldNum<int32>();
			}
			else if (IsUint())
			{
				return GetFieldNum<uint32>();
			}
			else if (IsInt64())
			{
				return GetFieldNum<int64>();
			}
			else if (IsUint64())
			{
				return GetFieldNum<uint64>();
			}
			else if (IsEnum())
			{
				return GetFieldNum<int32>();
			}
			return std::monostate{};
		}

		const upb_Message* GetSubMessage() const
		{
			if (ensureAlways(IsMessage()))
			{
				auto DefautVal = FieldDef.DefaultValue();
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_GetMessage(GetMsg(), FieldDef.MiniTable(), &DefautVal);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					return (const upb_Message* const*)ArrayElmData();
				}
			}
			return nullptr;
		}

	protected:
		const void* ArrayElmData(size_t Idx) const
		{
			GMP_CHECK(IsArray());
			const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
			ensureAlways(arr && Idx < arr->size);
			return (const char*)upb_Array_DataPtr(arr) + _upb_Array_ElementSizeLg2(arr) * Idx;
		}
		const void* ArrayElmData() const
		{
			GMP_CHECK(FieldDef.GetArrayIdx() >= 0);
			return ArrayElmData(FieldDef.GetArrayIdx());
		}
		template<typename V>
		static V VisitVal(const std::monostate& Val)
		{
			return {};
		}
		template<typename V>
		static V VisitVal(const PBEnum& Val)
		{
			return V(Val.EnumValue);
		}
		template<typename V, typename T>
		static std::enable_if_t<std::is_arithmetic<T>::value, V> VisitVal(const T& Val)
		{
			return V(Val);
		}
	};

	struct FFieldValueWriter : public FFieldValueReader
	{
		FDynamicArena Arena;
		FFieldValueWriter(upb_Message* InMsg, FFieldDefPtr InField, upb_Arena* InArena = nullptr)
			: FFieldValueReader(InMsg, InField)
			, Arena(InArena)
		{
		}
		FFieldValueWriter(FFieldValueReader& Ref, upb_Arena* InArena = nullptr)
			: FFieldValueReader(Ref)
			, Arena(InArena)
		{
		}
		upb_Arena* GetArena() { return *Arena; }

		upb_Message* GetMsg() { return (upb_Message*)MsgVal.ptr_val; }
		upb_Array* GetArr() { return (upb_Array*)MsgVal.ptr_val; }

		template<typename T>
		bool SetFieldNum(const T& In)
		{
			if (ensureAlways(FieldDef.GetCType() == TBaseFieldInfo<T>::CType))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					_upb_Message_SetNonExtensionField(GetMsg(), FieldDef.MiniTable(), &In);
					return true;
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					*(T*)ArrayElmData() = In;
					return true;
				}
			}
			return false;
		}

		template<typename T>
		bool SetFieldStr(const T& In)
		{
			if (ensureAlways(IsString()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_SetString(GetMsg(), FieldDef.MiniTable(), AllocStrView(In), Arena);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					*(upb_StringView*)ArrayElmData() = AllocStrView(In);
					return true;
				}
			}
			return false;
		}

		template<typename T>
		bool SetFieldBytes(const T& In)
		{
			if (ensureAlways(IsBytes()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_SetString(GetMsg(), FieldDef.MiniTable(), AllocStrView(In), Arena);
				}
				else
				{
					*(upb_StringView*)ArrayElmData() = AllocStrView(In);
					return true;
				}
			}
			return false;
		}

		bool SetFieldMessage(upb_Message* SubMsgRef)
		{
			if (ensureAlways(IsMessage()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					upb_Message_SetMessage(GetMsg(), MiniTable(), FieldDef.MiniTable(), SubMsgRef);
					return true;
				}
				else
				{
					*(upb_Message**)ArrayElmData() = SubMsgRef;
					return true;
				}
			}
			return false;
		}

		FFieldValueWriter ArrayElm(size_t Idx)
		{
			ArrayElmData(Idx);
			return FFieldValueWriter(GetMsg(), FieldDef.GetElementDef(Idx));
		}

	protected:
		template<typename T>
		upb_StringView AllocStrView(const T & In)
		{
			if constexpr (std::is_same_v<T, upb_StringView> || std::is_same_v<T, StringView>)
			{
				return In;
			}
			else
			{
				return upb_StringView(StringView(In, *Arena));
			}
		}
		void* ArrayElmData(size_t Idx)
		{
			GMP_CHECK(IsArray());
			return upb_Message_ResizeArrayUninitialized(GetMsg(), FieldDef.MiniTable(), Idx, Arena);
		}
		void* ArrayElmData()
		{
			GMP_CHECK(FieldDef.GetArrayIdx() >= 0);
			return ArrayElmData(FieldDef.GetArrayIdx());
		}
		FFieldValueWriter(FFieldValueWriter& Ref, int32_t Idx)
			: FFieldValueReader(Ref.GetMsg(), Ref.FieldDef.GetElementDef(Idx))
			, Arena(*Ref.Arena)
		{
		}
	};

	namespace Serializer
	{
		uint32 PropToField(FFieldValueWriter& Value, FProperty* Prop, const void* Addr);
		uint32 PropToField(FMessageDefPtr& MsgDef, FStructProperty* StructProp, const void* StructAddr, upb_Arena* Arena, upb_Message* MsgPtr = nullptr)
		{
			auto MsgRef = MsgPtr ? MsgPtr : upb_Message_New(MsgDef.MiniTable(), Arena);

			uint32 Ret = false;
			for (FFieldDefPtr FieldDef : MsgDef.Fields())
			{
				auto Prop = StructProp->Struct->FindPropertyByName(FieldDef.Name().ToFName());
				if (Prop)
				{
					FFieldValueWriter ValRef(MsgRef, FieldDef, Arena);
					Ret += PropToField(ValRef, Prop, Prop->ContainerPtrToValuePtr<void>(StructAddr));
				}
				else
				{
					UE_LOG(LogGMP, Warning, TEXT("Field %s not found in struct %s"), *FieldDef.Name().ToFStringData(), *StructProp->GetName());
				}
			}
			return Ret;
		}

		uint32 UStructToPBImpl(UScriptStruct* Struct, const void* StructAddr, char** OutBuf, size_t* OutSize, FArena& Arena)
		{
			FString StructName = Struct->GetName();
			GMP::Serializer::StripUserDefinedStructName(StructName);
			auto MsgDef = FindMessageByName(StringView(StructName, Arena));
			uint32 Ret = 0;
			if (MsgDef)
			{
				auto MsgRef = upb_Message_New(MsgDef.MiniTable(), Arena);
				Ret = PropToField(MsgDef, GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), StructAddr, Arena, MsgRef);
				upb_EncodeStatus Status = upb_Encode(MsgRef, MsgDef.MiniTable(), 0, Arena, OutBuf, OutSize);
			}
			else
			{
				UE_LOG(LogGMP, Warning, TEXT("Message %s not found"), *StructName);
			}
			return Ret;
		}
		uint32 UStructToPBImpl(FArchive& Ar, UScriptStruct* Struct, const void* StructAddr)
		{
			FArena Arena;
			char* OutBuf = nullptr;
			size_t OutSize = 0;
			auto Ret = UStructToPBImpl(Struct, StructAddr, &OutBuf, &OutSize, Arena);
			if (OutSize && OutBuf)
			{
				Ar.Serialize(OutBuf, OutSize);
			}
			return Ret;
		}
		uint32 UStructToPBImpl(TArray<uint8>& Out, UScriptStruct* Struct, const void* StructAddr)
		{
			TMemoryWriter<32> Writer(Out);
			return UStructToPBImpl(Writer, Struct, StructAddr);
		}
		uint32 PropToMessage(FFieldValueWriter& Writer, FStructProperty* StructProp, const void* StructAddr)
		{
			uint32 Ret = 0;
			if (ensureAlways(Writer.IsMessage()))
			{
				auto SubMsgDef = Writer.FieldDef.MessageSubdef();
				auto SubMsgRef = upb_Message_New(SubMsgDef.MiniTable(), Writer.GetArena());
				Ret = PropToField(SubMsgDef, StructProp, StructAddr, Writer.GetArena(), SubMsgRef);
				Writer.SetFieldMessage(SubMsgRef);
			}
			return Ret;
		}
	}  // namespace Serializer

#if WITH_GMPVALUE_ONEOF
	static auto FriendGMPValueOneOf = [](const FGMPValueOneOf& In) -> decltype(auto) {
		struct FGMPValueOneOfFriend : public FGMPValueOneOf
		{
			using FGMPValueOneOf::Value;
			using FGMPValueOneOf::Flags;
		};
		return const_cast<FGMPValueOneOfFriend&>(static_cast<const FGMPValueOneOfFriend&>(In));
	};
	struct FPBValueHolder
	{
		FFieldValueReader Reader;
		FDynamicArena Arena;
		FPBValueHolder(const upb_Message* InMsg, FFieldDefPtr InField, upb_Arena* InArena = nullptr)
			: Reader(InMsg, InField)
			, Arena(InArena)
		{
		}
	};
#endif

	namespace Deserializer
	{
		uint32 FieldToProp(const FFieldValueReader& InVal, FProperty* Prop, void* Addr);
		uint32 FieldToProp(const FMessageDefPtr& MsgDef, const upb_Message* MsgRef, FStructProperty* StructProp, void* StructAddr)
		{
			uint32 Ret = false;
			for (FFieldDefPtr FieldDef : MsgDef.Fields())
			{
				auto Prop = StructProp->Struct->FindPropertyByName(FieldDef.Name().ToFName());
				if (Prop)
				{
					Ret += FieldToProp(FFieldValueReader(MsgRef, FieldDef), Prop, Prop->ContainerPtrToValuePtr<void>(StructAddr));
				}
				else
				{
					UE_LOG(LogGMP, Warning, TEXT("Field %s not found in struct %s"), *FieldDef.Name().ToFStringData(), *StructProp->GetName());
				}
			}
			return Ret;
		}

		uint32 UStructFromPBImpl(TArrayView<const uint8> In, UScriptStruct* Struct, void* StructAddr)
		{
			FString StructName = Struct->GetName();
			GMP::Serializer::StripUserDefinedStructName(StructName);
			FDynamicArena Arena;
			auto MsgDef = FindMessageByName(StringView(StructName, Arena));
			if (MsgDef)
			{
				upb_Message* MsgRef = upb_Message_New(MsgDef.MiniTable(), Arena);
				upb_DecodeStatus Status = upb_Decode((const char*)In.GetData(), In.Num(), MsgRef, MsgDef.MiniTable(), nullptr, 0, Arena);
				return FieldToProp(MsgDef, MsgRef, GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), StructAddr);
			}
			else
			{
				UE_LOG(LogGMP, Warning, TEXT("Message %s not found"), *StructName);
			}
			return 0;
		}
		uint32 UStructFromPBImpl(FArchive& Ar, UScriptStruct* Struct, void* StructAddr)
		{
			TArray64<uint8> Buf;
			Buf.AddUninitialized(Ar.TotalSize());
			Ar.Serialize(Buf.GetData(), Buf.Num());
			return UStructFromPBImpl(Buf, Struct, StructAddr);
		}

		uint32 MessageToProp(const FFieldValueReader& Reader, FStructProperty* StructProp, void* StructAddr)
		{
			uint32 Ret = 0;
			if (ensureAlways(Reader.IsMessage()))
			{
				auto MsgDef = Reader.FieldDef.MessageSubdef();
				auto MsgRef = Reader.GetSubMessage();
#if WITH_GMPVALUE_ONEOF
				if (StructProp->Struct == FGMPValueOneOf::StaticStruct())
				{
					auto Ref = MakeShared<FPBValueHolder>(nullptr, Reader.FieldDef);
					Ref->Reader.MsgVal.ptr_val = upb_Message_DeepClone(MsgRef, MsgDef.MiniTable(), Ref->Arena);
					auto OneOf = (FGMPValueOneOf*)StructAddr;
					auto& Holder = FriendGMPValueOneOf(*OneOf);
					Holder.Value = MoveTemp(Ref);
					Holder.Flags = 0;
					Ret = 1;
				}
				else
#endif
				{
					Ret = FieldToProp(MsgDef, MsgRef, StructProp, StructAddr);
				}
			}
			return Ret;
		}
	}  // namespace Deserializer

	namespace Detail
	{

		template<typename WriterType>
		bool WriteToPB(WriterType& Writer, FProperty* Prop, const void* Value);
		template<typename ReaderType>
		bool ReadFromPB(const ReaderType& Val, FProperty* Prop, void* Value);

		using StringView = upb::StringView;
		namespace Internal
		{
			//////////////////////////////////////////////////////////////////////////
			struct FValueVisitorBase
			{
				static FString ExportText(FProperty* Prop, const void* Value)
				{
					FString StringValue;
#if UE_5_02_OR_LATER
					Prop->ExportTextItem_Direct(StringValue, Value, NULL, NULL, PPF_None);
#else
					Prop->ExportTextItem(StringValue, Value, NULL, NULL, PPF_None);
#endif
					return StringValue;
				}
				static void ImportText(const TCHAR* Str, FProperty* Prop, void* Addr, int32 ArrIdx)
				{
#if UE_5_02_OR_LATER
					Prop->ImportText_Direct(Str, Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), nullptr, PPF_None);
#else
					Prop->ImportText(Str, Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), PPF_None, nullptr);
#endif
				}
				static bool CanHoldWithDouble(uint64 u)
				{
					volatile double d = static_cast<double>(u);
					return (d >= 0.0) && (d < static_cast<double>((std::numeric_limits<uint64>::max)())) && (u == static_cast<uint64>(d));
				}
				static bool CanHoldWithDouble(int64 i)
				{
					volatile double d = static_cast<double>(i);
					return (d >= static_cast<double>((std::numeric_limits<int64>::min)())) && (d < static_cast<double>((std::numeric_limits<int64>::max)())) && (i == static_cast<int64>(d));
				}
				template<typename WriterType>
				static FORCEINLINE void WriteVisitStr(WriterType& Writer, const FString& Str)
				{
					Writer.SetFieldStr(Str);
				}
			};

			template<typename P>
			struct TValueVisitorDefault : public FValueVisitorBase
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, P* Prop, const void* Addr, int32 ArrIdx)
				{
					//
				}

				static FORCEINLINE void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					//
				}
				static FORCEINLINE void ReadVisit(const std::monostate& Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					//
				}
				template<typename T>
				static FORCEINLINE std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					//
				}
				template<typename ReaderType>
				static FORCEINLINE void ReadVisit(const ReaderType* Ptr, P* Prop, void* Addr, int32 ArrIdx)
				{
					//
				}
			};

			template<typename P>
			struct TValueVisitor : public TValueVisitorDefault<FProperty>
			{
				using TValueVisitorDefault<FProperty>::ReadVisit;
			};

			template<>
			struct TValueVisitor<FBoolProperty> : public TValueVisitorDefault<FBoolProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FBoolProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					bool BoolVal = Prop->GetPropertyValue(Value);
					Writer.SetFieldNum(BoolVal);
				}
				using TValueVisitorDefault<FBoolProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FBoolProperty* Prop, void* Addr, int32 ArrIdx)
				{
					ensure(Val == 0 || Val == 1);
					Prop->SetPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), !!Val);
				}

				static void ReadVisit(const StringView& Val, FBoolProperty* Prop, void* Addr, int32 ArrIdx) { Prop->SetPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), FCStringAnsi::ToBool(Val)); }
			};
			template<>
			struct TValueVisitor<FEnumProperty> : public TValueVisitorDefault<FEnumProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FEnumProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					auto IntVal = Prop->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
					Writer.SetFieldNum((int32)IntVal);
				}

				using TValueVisitorDefault<FEnumProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FEnumProperty* Prop, void* Addr, int32 ArrIdx)
				{
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), (int64)Val);
				}

				static void ReadVisit(const StringView& Val, FEnumProperty* Prop, void* Addr, int32 ArrIdx)
				{
					const UEnum* Enum = Prop->GetEnum();
					check(Enum);
					int64 IntValue = Enum->GetValueByNameString(Val);
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx), IntValue);
				}
			};
			template<>
			struct TValueVisitor<FNumericProperty> : public TValueVisitorDefault<FNumericProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FNumericProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto IntVal = Prop->GetSignedIntPropertyValue(Value);
						Writer.SetFieldNum((int32)IntVal);
					}
					else if (Prop->IsFloatingPoint())
					{
						const bool bIsDouble = Prop->IsA<FDoubleProperty>();
						if (bIsDouble)
						{
							double d = CastFieldChecked<FDoubleProperty>(Prop)->GetPropertyValue(Value);
							Writer.SetFieldNum(d);
						}
						else
						{
							float f = CastFieldChecked<FFloatProperty>(Prop)->GetPropertyValue(Value);
							Writer.SetFieldNum(f);
						}
					}
					else if (Prop->IsA<FUInt64Property>())
					{
						uint64 UIntVal = Prop->GetUnsignedIntPropertyValue(Value);
						Writer.SetFieldNum(UIntVal);
					}
					else if (Prop->IsA<FInt64Property>())
					{
						int64 IntVal = Prop->GetSignedIntPropertyValue(Value);
						Writer.SetFieldNum(IntVal);
					}
					else if (Prop->IsA<FIntProperty>())
					{
						int32 IntVal = Prop->GetSignedIntPropertyValue(Value);
						Writer.SetFieldNum(IntVal);
					}
					else if (Prop->IsA<FUInt32Property>())
					{
						uint32 IntVal = Prop->GetUnsignedIntPropertyValue(Value);
						Writer.SetFieldNum(IntVal);
					}
					else
					{
						ensureAlways(false);
					}
				}

				using TValueVisitorDefault<FNumericProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FNumericProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (Prop->IsFloatingPoint())
					{
						if (auto FloatProp = CastField<FFloatProperty>(Prop))
							FloatProp->SetPropertyValue(Value, (float)Val);
						else
							Prop->SetFloatingPointPropertyValue(Value, (double)Val);
					}
					else
					{
						Prop->SetIntPropertyValue(Value, (int64)Val);
					}
				}

				static void ReadVisit(const StringView& Val, FNumericProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto EnumVal = EnumDef->GetValueByNameString(Val);
						Prop->SetIntPropertyValue(Value, EnumVal);
					}
					else
					{
						Prop->SetNumericPropertyValueFromString(Value, Val.ToFStringData());
					}
				}
			};

			template<typename P>
			struct TNumericValueVisitor
			{
				using NumericType = typename P::TCppType;
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, P* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					auto Val = Prop->GetPropertyValue(Value);
					using TargetType = std::conditional_t<sizeof(NumericType) < sizeof(int32), int32, NumericType>;
					Writer.SetFieldNum((TargetType)Val);
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					Prop->SetPropertyValue(Value, Val);
				}
				static void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto* ValuePtr = Prop->template ContainerPtrToValuePtr<NumericType>(Addr, ArrIdx);
					LexFromString(*ValuePtr, Val.ToFStringData());
				}
				static FORCEINLINE void ReadVisit(const std::monostate& Val, P* Prop, void* Addr, int32 ArrIdx) {}
				template<typename ReaderType>
				static FORCEINLINE void ReadVisit(const ReaderType* Ptr, P* Prop, void* Addr, int32 ArrIdx)
				{
				}
			};
			template<>
			struct TValueVisitor<FFloatProperty> : public TNumericValueVisitor<FFloatProperty>
			{
			};
			template<>
			struct TValueVisitor<FDoubleProperty> : public TNumericValueVisitor<FDoubleProperty>
			{
			};
			template<>
			struct TValueVisitor<FInt8Property> : public TNumericValueVisitor<FInt8Property>
			{
			};
			template<>
			struct TValueVisitor<FInt16Property> : public TNumericValueVisitor<FInt16Property>
			{
			};
			template<>
			struct TValueVisitor<FIntProperty> : public TNumericValueVisitor<FIntProperty>
			{
			};
			template<>
			struct TValueVisitor<FInt64Property> : public TNumericValueVisitor<FInt64Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt16Property> : public TNumericValueVisitor<FUInt16Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt32Property> : public TNumericValueVisitor<FUInt32Property>
			{
			};
			template<>
			struct TValueVisitor<FUInt64Property> : public TNumericValueVisitor<FUInt64Property>
			{
			};
			template<>
			struct TValueVisitor<FByteProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FByteProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					auto Val = Prop->GetPropertyValue(Value);
					using TargetType = std::conditional_t<sizeof(Val) < sizeof(int32), int32, decltype(Val)>;
					Writer.SetFieldNum((TargetType)Val);
				}
				static FORCEINLINE void ReadVisit(const std::monostate& Val, FByteProperty* Prop, void* Addr, int32 ArrIdx) {}
				static FORCEINLINE void ReadVisit(bool bVal, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					uint8 Val = bVal ? 1 : 0;
					Prop->SetPropertyValue(Value, Val);
				}
				template<typename ReaderType>
				static FORCEINLINE void ReadVisit(const ReaderType* Ptr, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<uint8>(Addr, ArrIdx);
					if (ensureAlways(Val >= 0 && Val <= (std::numeric_limits<uint8>::max)()))
						Prop->SetPropertyValue(Value, (uint8)Val);
				}

				static void ReadVisit(const StringView& Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = Prop->template ContainerPtrToValuePtr<uint8>(Addr, ArrIdx);
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())  // TEnumAsByte
					{
						auto EnumVal = EnumDef->GetValueByNameString(Val);
						if (ensureAlways(EnumVal >= 0 && EnumVal <= (std::numeric_limits<uint8>::max)()))
							Prop->SetPropertyValue(ValuePtr, (uint8)EnumVal);
					}
					else
					{
						LexFromString(*ValuePtr, Val.ToFStringData());
					}
				}
			};
			template<>
			struct TValueVisitor<FStrProperty> : public TValueVisitorDefault<FStrProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FStrProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					FValueVisitorBase::WriteVisitStr(Writer, Prop->GetPropertyValue_InContainer(Addr, ArrIdx));
				}

				using TValueVisitorDefault<FStrProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FStrProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FString* Str = Prop->template ContainerPtrToValuePtr<FString>(Addr, ArrIdx);
					*Str = LexToString(Val);
				}
				static void ReadVisit(const StringView& Val, FStrProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FString* Str = Prop->template ContainerPtrToValuePtr<FString>(Addr, ArrIdx);
					*Str = Val.ToFString();
				}
			};
			template<>
			struct TValueVisitor<FNameProperty> : public TValueVisitorDefault<FNameProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FNameProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					FName Name = Prop->GetPropertyValue_InContainer(Addr, ArrIdx);
					FValueVisitorBase::WriteVisitStr(Writer, Name.ToString());
				}

				using TValueVisitorDefault<FNameProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FNameProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FName* Name = Prop->template ContainerPtrToValuePtr<FName>(Addr, ArrIdx);
					*Name = Val.ToFName(FNAME_Add);
				}
			};
			template<>
			struct TValueVisitor<FTextProperty> : public TValueVisitorDefault<FTextProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FTextProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					FText Text = Prop->GetPropertyValue_InContainer(Addr, ArrIdx);
					FValueVisitorBase::WriteVisitStr(Writer, Text.ToString());
				}

				using TValueVisitorDefault<FTextProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FTextProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FText* Text = Prop->template ContainerPtrToValuePtr<FText>(Addr, ArrIdx);
					// FValueVisitorBase::ImportText(Val.ToFStringData(), Prop, Addr, ArrIdx);
					*Text = FText::FromString(Val);
				}

				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FTextProperty* Prop, void* Addr, int32 ArrIdx)
				{
#if 0
					FText* Text = Prop->template ContainerPtrToValuePtr<FText>(Addr, ArrIdx);
					*Text = FText::FromString(Ptr->GetFieldStr<FString>());
#endif
				}
			};

			template<>
			struct TValueVisitor<FSoftObjectProperty> : public TValueVisitorDefault<FSoftObjectProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSoftObjectProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					UObject* Obj = Prop->GetObjectPropertyValue(Value);
					FValueVisitorBase::WriteVisitStr(Writer, GIsEditor ? GetPathNameSafe(Obj) : GetPathNameSafe(Obj));
				}
				using TValueVisitorDefault<FSoftObjectProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FSoftObjectProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FSoftObjectPath* OutValue = (FSoftObjectPath*)Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					if (GIsEditor && GWorld)
					{
#if UE_5_02_OR_LATER
						OutValue->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->GetPIEInstanceID()));
#else
						OutValue->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->PIEInstanceID));
#endif
					}
					else
					{
						OutValue->SetPath(Val.ToFStringData());
					}
				}
			};

			template<>
			struct TValueVisitor<FStructProperty> : public TValueVisitorDefault<FStructProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FStructProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					Serializer::PropToMessage(Writer, Prop, Addr);
				}

				using TValueVisitorDefault<FStructProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FStructProperty* Prop, void* Addr, int32 ArrIdx) { FValueVisitorBase::ImportText(Val.ToFStringData(), Prop, Addr, ArrIdx); }
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FStructProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& Reader = *Ptr;
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, ArrIdx);
					Deserializer::MessageToProp(Reader, Prop, Addr);
				}
			};

			template<>
			struct TValueVisitor<FArrayProperty> : public TValueVisitorDefault<FArrayProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FArrayProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);

					if (Writer.IsBytes() && ensureAlways(Prop->IsA<FByteProperty>() || Prop->IsA<FInt8Property>()))
					{
						FScriptArrayHelper Helper(Prop, Value);
						Writer.SetFieldBytes(StringView((const char*)Helper.GetRawPtr(), Helper.Num()));
					}
					else if (ensureAlways(Writer.IsArray()))
					{
						FScriptArrayHelper Helper(Prop, Value);
						for (int32 i = 0; i < Helper.Num(); ++i)
						{
							auto Elm = Writer.ArrayElm(i);
							WriteToPB(Elm, Prop->Inner, Helper.GetRawPtr(i));
						}
					}
				}
				using TValueVisitorDefault<FArrayProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FArrayProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& PBVal = *Ptr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);

					if (PBVal.IsBytes() && ensureAlways(Prop->IsA<FByteProperty>() || Prop->IsA<FInt8Property>()))
					{
						auto View = StringView(PBVal.GetFieldBytes());
						FScriptArrayHelper Helper(Prop, OutValue);
						Helper.Resize(View.size());
						FMemory::Memcpy(Helper.GetRawPtr(), View.data(), View.size());
					}
					else if (ensure(PBVal.IsArray()))
					{
						auto ItemsToRead = FMath::Max((int32)PBVal.ArraySize(), 0);
						FScriptArrayHelper Helper(Prop, OutValue);
						Helper.Resize(ItemsToRead);
						for (auto i = 0; i < Helper.Num(); ++i)
						{
							ReadFromPB(PBVal.ArrayElm(i), Prop->Inner, Helper.GetRawPtr(i));
						}
					}
					else
					{
						FScriptArrayHelper Helper(Prop, OutValue);
						Helper.Resize(1);
						ReadFromPB(PBVal, Prop->Inner, Helper.GetRawPtr(0));
					};
				}
			};
			template<>
			struct TValueVisitor<FSetProperty> : public TValueVisitorDefault<FSetProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSetProperty* Prop, const void* Addr, int32 ArrIdx)
				{
#if 0
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
#if 0
					Writer.StartArray();
#endif
					FScriptSetHelper Helper(Prop, Value);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						if (Helper.IsValidIndex(i))
						{
							auto Elm = Writer.ArrayElm(i);
							WriteToPB(Elm, Prop->ElementProp, Helper.GetElementPtr(i));
						}
					}
#endif
				}
				using TValueVisitorDefault<FSetProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FSetProperty* Prop, void* Addr, int32 ArrIdx)
				{
#if 0
					auto& PBVal = *Ptr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					if (ensure(PBVal.IsArray()))
					{
						FScriptSetHelper Helper(Prop, OutValue);
						for (auto i = 0; i < PBVal.ArraySize(); ++i)
						{
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							ReadFromPB(PBVal.ArrayElm(i), Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						}
						Helper.Rehash();
					}
					else
					{
						FScriptSetHelper Helper(Prop, OutValue);
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						ReadFromPB(PBVal, Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						Helper.Rehash();
					}
#endif
				}
			};
			template<>
			struct TValueVisitor<FMapProperty> : public TValueVisitorDefault<FMapProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FMapProperty* Prop, const void* Addr, int32 ArrIdx)
				{
#if 0
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto Value = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
#if 0
					Writer.StartObject();
#endif
					FScriptMapHelper Helper(Prop, Value);
					for (int32 i = 0; i < Helper.Num(); ++i)
					{
						if (Helper.IsValidIndex(i))
						{
							FString StrVal = FValueVisitorBase::ExportText(Prop->KeyProp, Helper.GetKeyPtr(i));
							Writer.Key(*StrVal, StrVal.Len());
							WriteToPB(Writer, Prop->ValueProp, Helper.GetValuePtr(i));
						}
					}
#endif
				}
				using TValueVisitorDefault<FMapProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(ReaderType* Ptr, FMapProperty* Prop, void* Addr, int32 ArrIdx)
				{
#if 0
					auto& PBVal = *Ptr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					auto OutValue = Prop->template ContainerPtrToValuePtr<void>(Addr, 0);
					FScriptMapHelper Helper(Prop, OutValue);
					if (ensure(PBVal.IsMap()))
					{
						PBUtils::ForEachObjectPair(PBVal, [&](const StringView& InName, const Type& InVal) -> bool {
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							TValueVisitor<FProperty>::ReadVisit(InName, Prop->KeyProp, Helper.GetKeyPtr(NewIndex), 0);
							ReadFromPB(InVal, Prop->ValueProp, Helper.GetValuePtr(NewIndex));
							return false;
						});
						Helper.Rehash();
					}
#endif
				}
			};

			template<typename P>
			struct TValueDispatcher
			{
				template<typename WriterType>
				static bool Write(WriterType& Writer, P* Prop, const void* Value)
				{
					if (ensureAlways(Prop->ArrayDim <= 1))
					{
						TValueVisitor<P>::WriteVisit(Writer, Prop, Value, 0);
					}
					return true;
				}

				template<typename ReaderType>
				static bool Read(const ReaderType& Val, P* Prop, void* Addr)
				{
					int32 i = 0;
					auto Visitor = [&](auto&& Elm) { TValueVisitor<P>::ReadVisit(std::forward<decltype(Elm)>(Elm), Prop, Addr, i); };
					if (Val.IsArray() && !CastField<FArrayProperty>(Prop) && !CastField<FSetProperty>(Prop))
					{
						int32 ItemsToRead = FMath::Clamp((int32)Val.ArraySize(), 0, Prop->ArrayDim);
						for (; i < ItemsToRead; ++i)
						{
							std::visit(Visitor, Val.ArrayElm(i).DispatchValue());
						}
					}
					else
					{
						std::visit(Visitor, Val.DispatchValue());
					}
					return true;
				}
			};
		}  // namespace Internal

		template<typename WriterType>
		bool WriteToPB(WriterType& Writer, FProperty* Prop, const void* Value)
		{
			return GMP::Serializer::Traits::ForeachProp([](auto& OutVal, auto* InProp, const void* InVal) -> bool { return Internal::TValueDispatcher<std::decay_t<decltype(*InProp)>>::Write(OutVal, InProp, InVal); }, Writer, Prop, Value);
		}
		template<typename ReaderType>
		bool ReadFromPB(const ReaderType& Reader, FProperty* Prop, void* Value)
		{
			return GMP::Serializer::Traits::ForeachProp([](const auto& InVal, auto* InProp, void* OutVal) -> bool { return Internal::TValueDispatcher<std::decay_t<decltype(*InProp)>>::Read(InVal, InProp, OutVal); }, Reader, Prop, Value);
		}
	}  // namespace Detail

	namespace Serializer
	{
		uint32 PropToField(FFieldValueWriter& Value, FProperty* Prop, const void* Addr)
		{
			uint32 Ret = 0;
			Detail::WriteToPB(Value, Prop, Addr);
			return Ret;
		}
	}  // namespace Serializer
	namespace Deserializer
	{
		uint32 FieldToProp(const FFieldValueReader& Value, FProperty* Prop, void* Addr)
		{
			uint32 Ret = 0;
			Detail::ReadFromPB(Value, Prop, Addr);
			return Ret;
		}
	}  // namespace Deserializer
}  // namespace PB
}  // namespace GMP

DEFINE_FUNCTION(UGMPOneOfUtils::execAsStruct)
{
	P_GET_STRUCT_REF(FGMPValueOneOf, OneOf);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	void* OutData = Stack.MostRecentPropertyAddress;
	FProperty* OutProp = Stack.MostRecentProperty;
	P_GET_PROPERTY(FNameProperty, SubKey);
	P_GET_UBOOL(bConsumeOneOf);
	P_FINISH

	P_NATIVE_BEGIN
	*(bool*)RESULT_PARAM = AsValueImpl(OneOf, OutProp, OutData, SubKey);
	if (bConsumeOneOf)
		OneOf.Clear();
	P_NATIVE_END
}

void UGMPOneOfUtils::ClearOneOf(UPARAM(ref) FGMPValueOneOf& OneOf)
{
	OneOf.Clear();
}

bool UGMPOneOfUtils::AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey)
{
	using namespace GMP::PB;
	bool bRet = false;
	do
	{
		auto OneOfPtr = &FriendGMPValueOneOf(In);

		if (!OneOfPtr->IsValid())
			break;
#if WITH_GMPVALUE_ONEOF
		if (OneOfPtr->Flags == 0 && Prop->IsA<FStructProperty>())
		{
			auto Ptr = StaticCastSharedPtr<FPBValueHolder>(OneOfPtr->Value);
			const FFieldValueReader& Reader = Ptr->Reader;

			Deserializer::MessageToProp(Reader, CastFieldChecked<FStructProperty>(Prop), Out);
			bRet = true;
		}
		else
		{
			ensure(false);
		}
#endif
	} while (false);
	return bRet;
}

int32 UGMPOneOfUtils::IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue)
{
	int32 RetIdx = INDEX_NONE;
	using namespace GMP::PB;
	do
	{
		auto OneOfPtr = &FriendGMPValueOneOf(In);

		if (!OneOfPtr->IsValid())
			break;

#if WITH_GMPVALUE_ONEOF
		if (OneOfPtr->Flags == 0)
		{
			auto Ptr = StaticCastSharedPtr<FPBValueHolder>(OneOfPtr->Value);
			const FFieldValueReader& Reader = Ptr->Reader;
			if (auto SubFieldDef = Reader.FieldDef.MessageSubdef().FindFieldByNumber(Idx))
			{
				Deserializer::FieldToProp(FFieldValueReader(Reader.GetSubMessage(), SubFieldDef), GMP::TClass2Prop<FGMPValueOneOf>::GetProperty(), &OutValue);
			}
		}
		else
		{
			ensure(false);
		}
#endif
	} while (false);
	return RetIdx;
}
