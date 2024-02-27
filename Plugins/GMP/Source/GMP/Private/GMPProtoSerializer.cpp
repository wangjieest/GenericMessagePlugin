//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPProtoSerializer.h"

#include "GMPProtoUtils.h"
#include "HAL/PlatformFile.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UnrealCompatibility.h"
#include "upb/libupb.h"

#include <variant>

namespace GMP
{
namespace PB
{
	using namespace upb;
#if WITH_EDITOR
	extern void PreInitProtoList(TFunctionRef<void(const FDefPool::FProtoDescType*)> Func);
#else
#endif  // WITH_EDITOR

	int32 DefaultPoolIdx = 0;
	struct FGMPDefPool
	{
		FGMPDefPool() { DefPool.SetPlatform(PLATFORM_64BITS ? kUpb_MiniTablePlatform_64Bit : kUpb_MiniTablePlatform_32Bit); }

		FDefPool DefPool;
		TMap<FName, FMessageDefPtr> MsgDefs_;
		bool AddProto(const FDefPool::FProtoDescType* FileProto)
		{
			FStatus Status;
			auto FileDef = DefPool.AddProto(FileProto, Status);
			MapProtoName(FileDef);
			return Status.IsOk();
		}
		FMessageDefPtr FindMessageByStruct(const UScriptStruct* Struct)
		{
			if (auto ProtoStruct = Cast<UProtoDefinedStruct>(Struct))
			{
				return DefPool.FindMessageByName(StringView::Ref(ProtoStruct->FullName));
			}

			auto Find = MsgDefs_.Find(Struct->GetFName());
			if (ensureAlwaysMsgf(Find, TEXT("unable to match proto message definition : %s"), *Struct->GetPathName()))
			{
				return *Find;
			}

			return DefPool.FindMessageByName(StringView::Ref(Struct->GetName()));
		}

		void MapProtoName(FFileDefPtr FileDef)
		{
			if (!FileDef || FileDef.ToplevelMessageCount() == 0)
				return;

			FArena Arena;
			for (auto i = 0; i < FileDef.ToplevelMessageCount(); ++i)
			{
				auto Msg = FileDef.ToplevelMessage(i);
				if (ensure(Msg))
				{
					FName MsgName = Msg.Name();
#if WITH_EDITOR
					auto Find = MsgDefs_.Find(MsgName);
					ensure(!Find || *Find == Msg);
#endif
					MsgDefs_.Add(MsgName, Msg);

#if 0
					FString FullName = Msg.FullName().ToFString();
					FullName.ReplaceCharInline(TEXT('.'), TEXT('_'), ESearchCase::CaseSensitive);
					MsgDefs_.Add(*FullName, Msg);
#endif
				}
			}
		}
	};

	static auto& GetDefPoolMap()
	{
		static TMap<uint8, TUniquePtr<FGMPDefPool>> PoolMap;
		return PoolMap;
	}

	static TUniquePtr<FGMPDefPool>& ResetDefPool(uint8 Idx = DefaultPoolIdx)
	{
		auto& Ref = GetDefPoolMap().FindOrAdd(Idx);
		Ref = MakeUnique<FGMPDefPool>();
		return Ref;
	}
	static TUniquePtr<FGMPDefPool>& GetDefPool(uint8 Idx = DefaultPoolIdx)
	{
		auto Find = GetDefPoolMap().Find(Idx);
		if (!Find)
		{
			Find = &ResetDefPool(Idx);
#if WITH_EDITOR
			PreInitProtoList([&](const FDefPool::FProtoDescType* Proto) { (*Find)->AddProto(Proto); });
#endif  // WITH_EDITOR
		}
		return *Find;
	}

#if WITH_EDITOR
	FDefPool& ResetEditorPoolPtr()
	{
		return ResetDefPool(DefaultPoolIdx)->DefPool;
	}
#endif

	FMessageDefPtr FindMessageByStruct(const UScriptStruct* Struct)
	{
		return GetDefPool()->FindMessageByStruct(Struct);
	}

	bool AddProto(const char* InBuf, uint32 InSize)
	{
		FArena Arena;
		auto FileProto = FDefPool::ParseProto(StringView(InBuf, InSize), *Arena);
		return GetDefPool()->AddProto(FileProto);
	}

	bool AddProtos(const char* InBuf, uint32 InSize)
	{
		size_t DefCnt = 0;
		auto Arena = FArena();
		auto& Pair = *GetDefPool();
		FDefPool::IteratorProtoSet(FDefPool::ParseProtoSet(upb_StringView_FromDataAndSize(InBuf, InSize), Arena), [&](auto* FileProto) { DefCnt += Pair.AddProto(FileProto) ? 1 : 0; });
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
	template<typename... Ts>
	struct Overload : Ts...
	{
		using Ts::operator()...;
	};
	template<class... Ts>
	Overload(Ts...) -> Overload<Ts...>;

	template<typename... TArgs>
	using TValueType = std::variant<std::monostate, bool, int32, uint32, int64, uint64, float, double, TArgs...>;
	template<typename T>
	struct TBaseFieldInfo
	{
		// static FFieldDefPtr GetFieldDef() const { return FieldDef; }
		// static bool EqualType(upb_CType InType) { return false; }
	};
	template<>
	struct TBaseFieldInfo<bool>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Bool;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Bool;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<float>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Float;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Float;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<double>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Double;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Double;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<int32>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Int32;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_UInt32;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType || InType == upb_CType::kUpb_CType_Enum; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<uint8> : public TBaseFieldInfo<int32>
	{
	};

	template<>
	struct TBaseFieldInfo<uint32>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_UInt32;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Int32;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<int64>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Int64;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_UInt64;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<uint64>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_UInt64;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Int64;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<PBEnum>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_Enum;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Int32;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<upb_StringView>
	{
		static constexpr upb_CType CType = upb_CType::kUpb_CType_String;
		static constexpr upb_CType CompactType = upb_CType::kUpb_CType_Bytes;
		static bool EqualType(upb_CType InType) { return InType == CType || InType == CompactType; }
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsPrimitive() && EqualType(FieldDef.GetCType()); }
	};
	template<>
	struct TBaseFieldInfo<StringView> : public TBaseFieldInfo<upb_StringView>
	{
	};

	template<>
	struct TBaseFieldInfo<upb_Message*>
	{
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsSubMessage(); }
	};

	template<>
	struct TBaseFieldInfo<upb_Map*>
	{
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsMap(); }
	};
	template<>
	struct TBaseFieldInfo<upb_Array*>
	{
		static bool EqualField(FFieldDefPtr FieldDef) { return FieldDef.IsArray(); }
	};

	using FMessageVariant = TValueType<upb_StringView, upb_Message*, upb_Array*, upb_Map*>;
	auto AsVariant(const upb_MessageValue& Val, FFieldDefPtr FieldDef, bool bIgnoreRepeat = false) -> FMessageVariant
	{
		if (FieldDef.IsMap())
		{
			return const_cast<upb_Map*>(Val.map_val);
		}
		else if (FieldDef.IsArray())
		{
			ensure(FieldDef.GetArrayIdx() < 0);
			return const_cast<upb_Array*>(Val.array_val);
		}
		auto CType = FieldDef.GetCType();
		switch (CType)
		{
			// clang-format off
			case kUpb_CType_Bool: return Val.bool_val;
			case kUpb_CType_Float: return Val.float_val;
			case kUpb_CType_Double: return Val.double_val;
			case kUpb_CType_Enum: case kUpb_CType_Int32: return Val.int32_val;
			case kUpb_CType_UInt32: return Val.uint32_val;
			case kUpb_CType_Int64: return Val.int64_val;
			case kUpb_CType_UInt64: return Val.uint64_val;
			case kUpb_CType_String: case kUpb_CType_Bytes:return Val.str_val; 
			case kUpb_CType_Message: default: return const_cast<upb_Message*>(Val.msg_val);
				// clang-format on
		}
	}
	auto ToVariant(const void* InPtr, FFieldDefPtr FieldDef) -> FMessageVariant
	{
		void* Ptr = const_cast<void*>(InPtr);
		auto CType = FieldDef.GetCType();
		switch (CType)
		{
			// clang-format off
			case kUpb_CType_Bool: return *reinterpret_cast<bool*>(Ptr);
			case kUpb_CType_Float: return *reinterpret_cast<float*>(Ptr);
			case kUpb_CType_Double: return *reinterpret_cast<double*>(Ptr);
			case kUpb_CType_Enum: case kUpb_CType_Int32: return *reinterpret_cast<int32*>(Ptr);
			case kUpb_CType_UInt32: return *reinterpret_cast<uint32*>(Ptr);
			case kUpb_CType_Int64: return *reinterpret_cast<int64*>(Ptr);
			case kUpb_CType_UInt64: return *reinterpret_cast<uint64*>(Ptr);
			case kUpb_CType_String: case kUpb_CType_Bytes: return *reinterpret_cast<upb_StringView*>(Ptr);
			case kUpb_CType_Message: default: return *reinterpret_cast<upb_Message**>(Ptr);
				// clang-format on
		}
	}

	bool FromVariant(upb_MessageValue& OutVal, FFieldDefPtr FieldDef, const FMessageVariant& Var)
	{
		bool bRet = false;
		// clang-format off
		std::visit(Overload{
			[&](bool val) { OutVal.bool_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](float val) { OutVal.float_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](double val) { OutVal.double_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](int32 val) { OutVal.int32_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](uint32 val) { OutVal.uint32_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](int64 val) { OutVal.int64_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](uint64 val) { OutVal.uint64_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](upb_StringView val) { OutVal.str_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](upb_Array* val) { OutVal.array_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](upb_Map* val) { OutVal.map_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](upb_Message* val) { OutVal.msg_val = val; bRet = TBaseFieldInfo<decltype(val)>::EqualField(FieldDef); },
			[&](auto) { }
			}, Var);
		// clang-format on
		return ensure(bRet);
	}

	struct FProtoReader
	{
		FFieldDefPtr FieldDef;
		FMessageVariant Var;
		FProtoReader(const FMessageVariant& InVar, FFieldDefPtr InField)
			: FieldDef(InField)
			, Var(InVar)
		{
		}
		FProtoReader(FFieldDefPtr InField, const upb_Message* InMsg)
			: FProtoReader(FMessageVariant(const_cast<upb_Message*>(InMsg)), InField)
		{
		}

		FProtoReader(upb_MessageValue InVal, FFieldDefPtr InField)
			: FieldDef(InField)
			, Var(AsVariant(InVal, InField))
		{
		}

		FProtoReader(FFieldDefPtr InField)
			: FieldDef(InField)
		{
		}

		template<typename T>
		bool GetValue(T& OutVar) const
		{
			if (std::holds_alternative<T>(Var))
			{
				OutVar = std::get<T>(Var);
				return true;
			}
			return false;
		}
		bool IsContainer() const { return std::holds_alternative<upb_Message*>(Var) || std::holds_alternative<upb_Map*>(Var) || std::holds_alternative<upb_Array*>(Var); }

		const upb_Message* GetMsg() const { return std::get<upb_Message*>(Var); }
		const upb_Array* GetArr() const { return std::get<upb_Array*>(Var); }
		const upb_Map* GetMap() const { return std::get<upb_Map*>(Var); }

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
			auto CType = FieldDef.GetCType();
			if (ensureAlways(TBaseFieldInfo<T>::EqualField(FieldDef)))
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
					if (arr && ensure(FieldDef.GetArrayIdx() < upb_Array_Size(arr)))
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
			if (ensureAlways(IsBytes()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_GetString(GetMsg(), FieldDef.MiniTable(), FieldDef.DefaultValue().str_val);
				}
				else if (ensureAlways(FieldDef.GetArrayIdx() < ArraySize()))
				{
					const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
					if (arr && ensure(FieldDef.GetArrayIdx() < upb_Array_Size(arr)))
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
		bool IsArray() const { return FieldDef.IsArray(); }
		bool IsArrayElm() const { return FieldDef.GetArrayIdx() >= 0 && ensure(FieldDef.IsRepeated(true)); }

		const upb_Array* GetSubArray() const { return IsArray() ? upb_Message_GetArray(GetMsg(), FieldDef.MiniTable()) : nullptr; }
		size_t ArraySize() const
		{
			const upb_Array* arr = GetSubArray();
			return arr ? upb_Array_Size(arr) : 0;
		}

		const FProtoReader ArrayElm(size_t Idx) const
		{
			GMP_CHECK(IsArray() && FieldDef.GetArrayIdx() < 0);
			const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
			ensureAlways(arr && Idx < upb_Array_Size(arr));
			return FProtoReader(FieldDef.GetElementDef(Idx), GetMsg());
		}

		//////////////////////////////////////////////////////////////////////////
		bool IsMap() const { return FieldDef.IsMap(); }
		const upb_Map* GetSubMap() const { return IsMap() ? upb_Message_GetMap(GetMsg(), FieldDef.MiniTable()) : nullptr; }
		size_t MapSize() const
		{
			const upb_Map* map = GetSubMap();
			return map ? upb_Map_Size(map) : 0;
		}

		FMessageDefPtr MapEntryDef() const
		{
			GMP_CHECK(IsMap());
			return FieldDef.MapEntrySubdef();
		}

		//////////////////////////////////////////////////////////////////////////
		bool IsMessage() const { return FieldDef.IsSubMessage(); }

		TValueType<StringView, const FProtoReader*> DispatchFieldValue() const
		{
			if (IsContainer())
			{
				auto CType = FieldDef.GetCType();
				if (IsArray() && FieldDef.GetArrayIdx() < 0)
				{
					return this;
				}

				switch (CType)
				{
					case kUpb_CType_Bool:
						return GetFieldNum<bool>();
					case kUpb_CType_Float:
						return GetFieldNum<float>();
					case kUpb_CType_Double:
						return GetFieldNum<double>();
					case kUpb_CType_Enum:
					case kUpb_CType_Int32:
						return GetFieldNum<int32>();
					case kUpb_CType_UInt32:
						return GetFieldNum<uint32>();
					case kUpb_CType_Int64:
						return GetFieldNum<int64>();
					case kUpb_CType_UInt64:
						return GetFieldNum<uint64>();
					case kUpb_CType_String:
						return GetFieldStr<StringView>();
					case kUpb_CType_Bytes:
						return GetFieldBytes();
					case kUpb_CType_Message:
						return this;
					default:
						break;
				}
			}
			else
			{
				TValueType<StringView, const FProtoReader*> Ret = std::monostate{};
				std::visit(Overload{[&](bool val) { Ret = val; },
									[&](float val) { Ret = val; },
									[&](double val) { Ret = val; },
									[&](int32 val) { Ret = val; },
									[&](uint32 val) { Ret = val; },
									[&](int64 val) { Ret = val; },
									[&](uint64 val) { Ret = val; },
									[&](upb_StringView val) { Ret = StringView(val); },
									[&](auto) { ensure(false); }},
						   Var);
				return Ret;
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
					return *(const upb_Message* const*)ArrayElmData();
				}
			}
			return nullptr;
		}

	protected:
		const void* ArrayElmData(size_t Idx) const
		{
			GMP_CHECK(IsArray());
			const upb_Array* arr = upb_Message_GetArray(GetMsg(), FieldDef.MiniTable());
			ensureAlways(arr && Idx < upb_Array_Size(arr));
			return (const char*)upb_Array_DataPtr(arr, Idx);
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

	struct FProtoWriter : public FProtoReader
	{
		FDynamicArena Arena;
		FProtoWriter(FFieldDefPtr InField, upb_Message* InMsg, upb_Arena* InArena)
			: FProtoReader(InField, InMsg)
			, Arena(InArena)
		{
		}
		FProtoWriter(FProtoReader& Ref, upb_Arena* InArena)
			: FProtoReader(Ref)
			, Arena(InArena)
		{
		}
		FProtoWriter(FFieldDefPtr InField, upb_Arena* InArena)
			: FProtoReader(AsVariant(InField.DefaultValue(), InField), InField)
			, Arena(InArena)
		{
		}
		upb_Arena* GetArena() { return *Arena; }

		upb_Message* MutableMsg() const { return std::get<upb_Message*>(Var); }
		upb_Array* MutableArr() const { return std::get<upb_Array*>(Var); }
		upb_Map* MutableMap() const { return std::get<upb_Map*>(Var); }

		template<typename T>
		bool SetFieldNum(const T& In)
		{
			if (!IsContainer())
			{
				Var = In;
				return true;
			}
			auto CType = FieldDef.GetCType();
			if (ensureAlways(TBaseFieldInfo<T>::EqualField(FieldDef)))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					_upb_Message_SetNonExtensionField(MutableMsg(), FieldDef.MiniTable(), &In);
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
			if (!IsContainer())
			{
				Var = AllocStrView(In);
				return true;
			}
			if (ensureAlways(IsString()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_SetString(MutableMsg(), FieldDef.MiniTable(), AllocStrView(In), Arena);
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
			if (!IsContainer())
			{
				Var = AllocStrView(In);
				return true;
			}
			if (ensureAlways(IsBytes()))
			{
				if (FieldDef.GetArrayIdx() < 0)
				{
					return upb_Message_SetString(MutableMsg(), FieldDef.MiniTable(), AllocStrView(In), Arena);
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
					upb_Message_SetMessage(MutableMsg(), MiniTable(), FieldDef.MiniTable(), SubMsgRef);
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

		upb_Array* MutableSubArray()
		{
			GMP_CHECK(IsArray());
			return upb_Message_GetOrCreateMutableArray(MutableMsg(), FieldDef.MiniTable(), Arena);
		}

		upb_Array* EnsureArraySize(size_t size)
		{
			if (size == 0)
				return const_cast<upb_Array*>(GetSubArray());

			upb_Array* arr = MutableSubArray();
			if (!ensureAlways(arr && _upb_Array_ResizeUninitialized(arr, size, Arena)))
				return nullptr;
			return arr;
		}
		FProtoWriter ArrayElm(size_t Idx, upb_Arena* InArena = nullptr)
		{
			EnsureArraySize(Idx + 1);
			return FProtoWriter(FieldDef.GetElementDef(Idx), MutableMsg(), InArena ? InArena : *Arena);
		}

		upb_Map* MutableSubMap()
		{
			GMP_CHECK(IsMap());
			return upb_Message_GetOrCreateMutableMap(MutableMsg(), FieldDef.MessageSubdef().MiniTable(), FieldDef.MiniTable(), Arena);
		}

		bool InsertFieldMapPair(FMessageVariant InKey, FMessageVariant InVal)
		{
			upb_MessageValue Key;
			upb_MessageValue Val;
			if (ensureAlways(IsMap() && FromVariant(Key, FieldDef.MapEntrySubdef().MapKeyDef(), InKey)  //
							 && FromVariant(Val, FieldDef.MapEntrySubdef().MapValueDef(), InVal)))
			{
				upb_MapInsertStatus Status = upb_Map_Insert(MutableSubMap(), Key, Val, Arena);
				return true;
			}
			return false;
		}

	protected:
		template<typename T>
		upb_StringView AllocStrView(const T& In)
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
			upb_Array* arr = EnsureArraySize(Idx + 1);
			return (char*)upb_Array_DataPtr(arr, Idx);
		}

		void* ArrayElmData() { return ArrayElmData(FieldDef.GetArrayIdx()); }
		FProtoWriter(FProtoWriter& Ref, int32_t Idx)
			: FProtoReader(Ref.FieldDef.GetElementDef(Idx), Ref.GetMsg())
			, Arena(*Ref.Arena)
		{
		}
	};

	static FProperty* FindPropertyByField(const UScriptStruct* Struct, FFieldDefPtr FieldDef)
	{
#if 0
		const bool bIsNative = Struct->IsNative();
		if (bIsNative)
		{
			auto FieldName = FieldDef.Name().ToFName();
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				if (It->GetFName() == FieldName)
				{
					return *It;
				}
			}
			UE_LOG(LogGMP, Error, TEXT("FindPropertyByField(%s, %s) Failed"), *GetNameSafe(Struct), *FieldDef.Name().ToFString());
		}
		else
#endif
		{
			auto FieldName = FieldDef.Name().ToFString();
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				auto PropName = It->GetName();
				if (!PropName.StartsWith(FieldName) || PropName.Len() <= FieldName.Len() || PropName[FieldName.Len()] != TEXT('_'))
					continue;

				if (GMP::Serializer::StripUserDefinedStructName(PropName) && PropName == FieldName)
					return *It;
			}
			UE_LOG(LogGMP, Error, TEXT("FindPropertyByField(%s, %s) Failed"), *GetNameSafe(Struct), *FieldName);
		}
		return nullptr;
	}

	int32 EncodeProtoImpl(FProtoWriter& Value, FProperty* Prop, const void* Addr);
	int32 EncodeProtoImpl(FMessageDefPtr& MsgDef, FStructProperty* StructProp, const void* StructAddr, upb_Arena* Arena, upb_Message* MsgPtr = nullptr)
	{
		auto MsgRef = MsgPtr ? MsgPtr : upb_Message_New(MsgDef.MiniTable(), Arena);

		int32 Ret = 0;
		for (FFieldDefPtr FieldDef : MsgDef.Fields())
		{
			auto Prop = FindPropertyByField(StructProp->Struct, FieldDef);
			// Should ensure struct always has the same field as proto?
			if (ensureAlways(Prop))
			{
				FProtoWriter ValRef(FieldDef, MsgRef, Arena);
				Ret += EncodeProtoImpl(ValRef, Prop, Prop->ContainerPtrToValuePtr<void>(StructAddr));
			}
			else
			{
				UE_LOG(LogGMP, Error, TEXT("Field %s not found in struct %s when encode proto"), *FieldDef.Name().ToFStringData(), *StructProp->GetName());
			}
		}
		return Ret;
	}

	int32 DecodeProtoImpl(const FProtoReader& InVal, FProperty* Prop, void* Addr);
	int32 DecodeProtoImpl(const FMessageDefPtr& MsgDef, const upb_Message* MsgRef, FStructProperty* StructProp, void* StructAddr)
	{
		int32 Ret = 0;
		for (FFieldDefPtr FieldDef : MsgDef.Fields())
		{
			auto Prop = FindPropertyByField(StructProp->Struct, FieldDef);
			// Should ensure struct always has the same field as proto?
			if (Prop)
			{
				Ret += DecodeProtoImpl(FProtoReader(FieldDef, MsgRef), Prop, Prop->ContainerPtrToValuePtr<void>(StructAddr));
			}
			else
			{
				UE_LOG(LogGMP, Warning, TEXT("Field %s not found in struct %s when decode proto"), *FieldDef.Name().ToFStringData(), *StructProp->GetName());
			}
		}
		return Ret;
	}

	namespace Serializer
	{
		bool UStructToProtoImpl(const UScriptStruct* Struct, const void* StructAddr, char** OutBuf, size_t* OutSize, FArena& Arena)
		{
			if (auto MsgDef = FindMessageByStruct(Struct))
			{
				auto MsgRef = upb_Message_New(MsgDef.MiniTable(), Arena);
				auto Ret = EncodeProtoImpl(MsgDef, GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), StructAddr, Arena, MsgRef);
				upb_EncodeStatus Status = upb_Encode(MsgRef, MsgDef.MiniTable(), 0, Arena, OutBuf, OutSize);
				if (!ensureAlways(Status == upb_EncodeStatus::kUpb_EncodeStatus_Ok))
					return false;
			}
			else
			{
				UE_LOG(LogGMP, Warning, TEXT("Message %s not found"), *Struct->GetName());
				return false;
			}
			return true;
		}
		bool UStructToProtoImpl(FArchive& Ar, const UScriptStruct* Struct, const void* StructAddr)
		{
			FArena Arena;
			char* OutBuf = nullptr;
			size_t OutSize = 0;
			auto Ret = UStructToProtoImpl(Struct, StructAddr, &OutBuf, &OutSize, Arena);
			if (OutSize && OutBuf)
			{
				Ar.Serialize(OutBuf, OutSize);
			}
			return Ret;
		}
		bool UStructToProtoImpl(TArray<uint8>& Out, const UScriptStruct* Struct, const void* StructAddr)
		{
			TMemoryWriter<32> Writer(Out);
			return UStructToProtoImpl(Writer, Struct, StructAddr);
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
		FProtoReader Reader;
		FDynamicArena Arena;
		FPBValueHolder(const upb_Message* InMsg, FFieldDefPtr InField, upb_Arena* InArena = nullptr)
			: Reader(InField, InMsg)
			, Arena(InArena)
		{
		}
	};
#endif

	namespace Deserializer
	{
		bool UStructFromProtoImpl(TConstArrayView<uint8> In, const UScriptStruct* Struct, void* StructAddr)
		{
			if (auto MsgDef = FindMessageByStruct(Struct))
			{
				FDynamicArena Arena;
				upb_Message* MsgRef = upb_Message_New(MsgDef.MiniTable(), Arena);
				upb_DecodeStatus Status = upb_Decode((const char*)In.GetData(), In.Num(), MsgRef, MsgDef.MiniTable(), nullptr, 0, Arena);
				if (!ensureAlways(Status == upb_DecodeStatus::kUpb_DecodeStatus_Ok))
					return false;
				if (!DecodeProtoImpl(MsgDef, MsgRef, GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), StructAddr))
					return false;
			}
			else
			{
				UE_LOG(LogGMP, Warning, TEXT("Message %s not found"), *Struct->GetName());
				return false;
			}
			return true;
		}
		bool UStructFromProtoImpl(FArchive& Ar, const UScriptStruct* Struct, void* StructAddr)
		{
			TArray64<uint8> Buf;
			Buf.AddUninitialized(Ar.TotalSize());
			Ar.Serialize(Buf.GetData(), Buf.Num());
			return UStructFromProtoImpl(Buf, Struct, StructAddr);
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
					Prop->ImportText_Direct(Str, reinterpret_cast<uint8*>(Addr) + ArrIdx * Prop->ElementSize, nullptr, PPF_None);
#else
					Prop->ImportText(Str, reinterpret_cast<uint8*>(Addr) + ArrIdx * Prop->ElementSize, PPF_None, nullptr);
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
				}

				static FORCEINLINE void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx) {}
				static FORCEINLINE void ReadVisit(const std::monostate& Val, P* Prop, void* Addr, int32 ArrIdx) {}
				template<typename T>
				static FORCEINLINE std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
				}
				template<typename ReaderType>
				static FORCEINLINE void ReadVisit(const ReaderType* Ptr, P* Prop, void* Addr, int32 ArrIdx)
				{
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
				static void WriteVisit(WriterType& Writer, FBoolProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					check(Prop->ArrayDim <= 1 && ArrIdx == 0);
					bool BoolVal = Prop->GetPropertyValue(ArrAddr);
					Writer.SetFieldNum(BoolVal);
				}
				using TValueVisitorDefault<FBoolProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FBoolProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					check(Prop->ArrayDim <= 1 && ArrIdx == 0);
					ensure(Val == 0 || Val == 1);
					Prop->SetPropertyValue(ArrAddr, !!Val);
				}

				static void ReadVisit(const StringView& Val, FBoolProperty* Prop, void* Addr, int32 ArrIdx)
				{
					check(Prop->ArrayDim <= 1 && ArrIdx == 0);
					Prop->SetPropertyValue(Addr, FCStringAnsi::ToBool(Val));
				}
			};
			template<>
			struct TValueVisitor<FEnumProperty> : public TValueVisitorDefault<FEnumProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FEnumProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const uint8*>(ArrAddr) + Prop->ElementSize * ArrIdx;
					auto IntVal = Prop->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
					Writer.SetFieldNum((int32)IntVal);
				}

				using TValueVisitorDefault<FEnumProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FEnumProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(ArrAddr) + Prop->ElementSize * ArrIdx;
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)Val);
				}

				static void ReadVisit(const StringView& Val, FEnumProperty* Prop, void* Addr, int32 ArrIdx)
				{
					const UEnum* Enum = Prop->GetEnum();
					check(Enum);
					int64 IntValue = Enum->GetValueByNameString(Val);
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + Prop->ElementSize * ArrIdx;
					Prop->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, IntValue);
				}
			};
			template<>
			struct TValueVisitor<FNumericProperty> : public TValueVisitorDefault<FNumericProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FNumericProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const uint8*>(ArrAddr) + Prop->ElementSize * ArrIdx;
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto IntVal = Prop->GetSignedIntPropertyValue(ValuePtr);
						Writer.SetFieldNum((int32)IntVal);
					}
					else if (Prop->IsFloatingPoint())
					{
						const bool bIsDouble = Prop->IsA<FDoubleProperty>();
						if (bIsDouble)
						{
							double d = CastFieldChecked<FDoubleProperty>(Prop)->GetPropertyValue(ValuePtr);
							Writer.SetFieldNum(d);
						}
						else
						{
							float f = CastFieldChecked<FFloatProperty>(Prop)->GetPropertyValue(ValuePtr);
							Writer.SetFieldNum(f);
						}
					}
					else if (Prop->IsA<FUInt64Property>())
					{
						uint64 UIntVal = Prop->GetUnsignedIntPropertyValue(ValuePtr);
						Writer.SetFieldNum(UIntVal);
					}
					else if (Prop->IsA<FInt64Property>())
					{
						int64 IntVal = Prop->GetSignedIntPropertyValue(ValuePtr);
						Writer.SetFieldNum(IntVal);
					}
					else if (Prop->IsA<FIntProperty>())
					{
						int32 IntVal = Prop->GetSignedIntPropertyValue(ValuePtr);
						Writer.SetFieldNum(IntVal);
					}
					else if (Prop->IsA<FUInt32Property>())
					{
						uint32 IntVal = Prop->GetUnsignedIntPropertyValue(ValuePtr);
						Writer.SetFieldNum(IntVal);
					}
					else
					{
						ensureAlways(false);
					}
				}

				using TValueVisitorDefault<FNumericProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FNumericProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(ArrAddr) + Prop->ElementSize * ArrIdx;
					if (Prop->IsFloatingPoint())
					{
						if (auto FloatProp = CastField<FFloatProperty>(Prop))
							FloatProp->SetPropertyValue(ValuePtr, (float)Val);
						else
							Prop->SetFloatingPointPropertyValue(ValuePtr, (double)Val);
					}
					else
					{
						Prop->SetIntPropertyValue(ValuePtr, (int64)Val);
					}
				}

				static void ReadVisit(const StringView& Val, FNumericProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + Prop->ElementSize * ArrIdx;
					if (UEnum* EnumDef = Prop->GetIntPropertyEnum())
					{
						auto EnumVal = EnumDef->GetValueByNameString(Val);
						Prop->SetIntPropertyValue(ValuePtr, EnumVal);
					}
					else
					{
						Prop->SetNumericPropertyValueFromString(ValuePtr, Val.ToFStringData());
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
					auto ValuePtr = reinterpret_cast<const NumericType*>(Addr) + ArrIdx;
					auto Val = Prop->GetPropertyValue(ValuePtr);
					using TargetType = std::conditional_t<sizeof(NumericType) < sizeof(int32), int32, NumericType>;
					Writer.SetFieldNum((TargetType)Val);
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<NumericType*>(Addr) + ArrIdx;
					Prop->SetPropertyValue(ValuePtr, Val);
				}
				static void ReadVisit(const StringView& Val, P* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<NumericType*>(Addr) + ArrIdx;
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
				static void WriteVisit(WriterType& Writer, FByteProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const uint8*>(ArrAddr) + ArrIdx;
					using TargetType = std::conditional_t<sizeof(*ValuePtr) < sizeof(int32), int32, decltype(*ValuePtr)>;
					Writer.SetFieldNum((TargetType)(*ValuePtr));
				}
				static FORCEINLINE void ReadVisit(const std::monostate& Val, FByteProperty* Prop, void* ArrAddr, int32 ArrIdx) {}
				static FORCEINLINE void ReadVisit(bool bVal, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + ArrIdx;
					uint8 Val = bVal ? 1 : 0;
					Prop->SetPropertyValue(ValuePtr, Val);
				}
				template<typename ReaderType>
				static FORCEINLINE void ReadVisit(const ReaderType* Ptr, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
				}

				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + ArrIdx;
					if (ensureAlways(Val >= 0 && Val <= (std::numeric_limits<uint8>::max)()))
						Prop->SetPropertyValue(ValuePtr, (uint8)Val);
				}

				static void ReadVisit(const StringView& Val, FByteProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + ArrIdx;
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
				static void WriteVisit(WriterType& Writer, FStrProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					FValueVisitorBase::WriteVisitStr(Writer, *(reinterpret_cast<const FString*>(ArrAddr) + ArrIdx));
				}

				using TValueVisitorDefault<FStrProperty>::ReadVisit;
				template<typename T>
				static std::enable_if_t<std::is_arithmetic<T>::value> ReadVisit(T Val, FStrProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					FString* ValuePtr = reinterpret_cast<FString*>(ArrAddr) + ArrIdx;
					*ValuePtr = LexToString(Val);
				}
				static void ReadVisit(const StringView& Val, FStrProperty* Prop, void* Addr, int32 ArrIdx)
				{
					FString* ValuePtr = reinterpret_cast<FString*>(Addr) + ArrIdx;
					*ValuePtr = Val.ToFString();
				}
			};
			template<>
			struct TValueVisitor<FNameProperty> : public TValueVisitorDefault<FNameProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FNameProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const FName*>(ArrAddr) + ArrIdx;
					FValueVisitorBase::WriteVisitStr(Writer, ValuePtr->ToString());
				}

				using TValueVisitorDefault<FNameProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FNameProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<FName*>(ArrAddr) + ArrIdx;
					*ValuePtr = Val.ToFName(FNAME_Add);
				}
			};
			template<>
			struct TValueVisitor<FTextProperty> : public TValueVisitorDefault<FTextProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FTextProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const FText*>(ArrAddr) + ArrIdx;
					FValueVisitorBase::WriteVisitStr(Writer, ValuePtr->ToString());
				}

				using TValueVisitorDefault<FTextProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FTextProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<FText*>(ArrAddr) + ArrIdx;
					// FValueVisitorBase::ImportText(Val.ToFStringData(), Prop, Addr, ArrIdx);
					*ValuePtr = FText::FromString(Val);
				}

#if 0
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FTextProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<FText*>(Addr) + ArrIdx;
					*ValuePtr = FText::FromString(Ptr->GetFieldStr<FString>());
				}
#endif
			};

			template<>
			struct TValueVisitor<FSoftObjectProperty> : public TValueVisitorDefault<FSoftObjectProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSoftObjectProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const FSoftObjectPath*>(ArrAddr) + ArrIdx;
					UObject* Obj = ValuePtr->ResolveObject();
					FValueVisitorBase::WriteVisitStr(Writer, GIsEditor ? GetPathNameSafe(Obj) : GetPathNameSafe(Obj));
				}
				using TValueVisitorDefault<FSoftObjectProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FSoftObjectProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<FSoftObjectPath*>(ArrAddr) + ArrIdx;
					if (GIsEditor && GWorld)
					{
#if UE_VERSION_NEWER_THAN(5, 2, 0)
						ValuePtr->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->GetPIEInstanceID()));
#else
						ValuePtr->SetPath(UWorld::ConvertToPIEPackageName(Val, GWorld->GetPackage()->PIEInstanceID));
#endif
					}
					else
					{
						ValuePtr->SetPath(Val.ToFStringData());
					}
				}
			};

			template<>
			struct TValueVisitor<FStructProperty> : public TValueVisitorDefault<FStructProperty>
			{
				static int32 StructToMessage(FProtoWriter& Writer, FStructProperty* StructProp, const void* StructAddr)
				{
					int32 Ret = 0;
					if (ensureAlways(Writer.IsMessage()))
					{
#if WITH_GMPVALUE_ONEOF
						if (StructProp->Struct == FGMPValueOneOf::StaticStruct())
						{
							auto OneOf = (FGMPValueOneOf*)StructAddr;
							auto OneOfPtr = &FriendGMPValueOneOf(*OneOf);
							if (ensure(OneOf->IsValid()))
							{
								auto Ptr = StaticCastSharedPtr<FPBValueHolder>(OneOfPtr->Value);
								auto SubMsgDef = Ptr->Reader.FieldDef.MessageSubdef();
								auto SubMsgRef = upb_Message_New(SubMsgDef.MiniTable(), Ptr->Arena);
								Ret += EncodeProtoImpl(SubMsgDef, StructProp, StructAddr, Writer.GetArena(), SubMsgRef);
								Writer.SetFieldMessage(SubMsgRef);
							}
						}
						else
#endif

						{
							auto SubMsgDef = Writer.FieldDef.MessageSubdef();
							auto SubMsgRef = upb_Message_New(SubMsgDef.MiniTable(), Writer.GetArena());
							Ret += EncodeProtoImpl(SubMsgDef, StructProp, StructAddr, Writer.GetArena(), SubMsgRef);
							Writer.SetFieldMessage(SubMsgRef);
						}
					}
					return Ret;
				}
				static int32 MessageToStruct(const FProtoReader& Reader, FStructProperty* StructProp, void* StructAddr)
				{
					int32 Ret = 0;
					if (ensureAlways(Reader.IsMessage()))
					{
						auto MsgDef = Reader.FieldDef.MessageSubdef();
						auto MsgRef = Reader.GetSubMessage();
#if WITH_GMPVALUE_ONEOF
						if (StructProp->Struct == FGMPValueOneOf::StaticStruct())
						{
							auto Ref = MakeShared<FPBValueHolder>(nullptr, Reader.FieldDef);
							Ref->Reader.Var = upb_Message_DeepClone(MsgRef, MsgDef.MiniTable(), Ref->Arena);
							auto OneOf = (FGMPValueOneOf*)StructAddr;
							auto& Holder = FriendGMPValueOneOf(*OneOf);
							Holder.Value = MoveTemp(Ref);
							Holder.Flags = 0;
							Ret = 1;
						}
						else
#endif
						{
							Ret = DecodeProtoImpl(MsgDef, MsgRef, StructProp, StructAddr);
						}
					}
					return Ret;
				}

				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FStructProperty* Prop, const void* Addr, int32 ArrIdx)
				{
					auto ValuePtr = reinterpret_cast<const uint8*>(Addr) + ArrIdx * Prop->Struct->GetStructureSize();
					StructToMessage(Writer, Prop, ValuePtr);
				}

				using TValueVisitorDefault<FStructProperty>::ReadVisit;
				static void ReadVisit(const StringView& Val, FStructProperty* Prop, void* Addr, int32 ArrIdx) { FValueVisitorBase::ImportText(Val.ToFStringData(), Prop, Addr, ArrIdx); }

				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FStructProperty* Prop, void* Addr, int32 ArrIdx)
				{
					auto& Reader = *Ptr;
					auto ValuePtr = reinterpret_cast<uint8*>(Addr) + ArrIdx * Prop->Struct->GetStructureSize();
					MessageToStruct(Reader, Prop, ValuePtr);
				}
			};

			template<>
			struct TValueVisitor<FArrayProperty> : public TValueVisitorDefault<FArrayProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FArrayProperty* Prop, const void* ArrAddr, int32 ArrIdx)
				{
					check(Prop->ArrayDim == 1 && ArrIdx == 0);

					// Bytes
					if (Writer.IsBytes() && ensureAlways(Prop->Inner->IsA<FByteProperty>() || Prop->Inner->IsA<FInt8Property>()))
					{
						FScriptArrayHelper Helper(Prop, ArrAddr);
						Writer.SetFieldBytes(StringView((const char*)Helper.GetRawPtr(), Helper.Num()));
					}
					else if (ensure(Writer.IsArray()))
					{
						GMP_CHECK(Writer.FieldDef.GetArrayIdx() < 0);
						FScriptArrayHelper Helper(Prop, ArrAddr);
						if (Helper.Num() > 0)
						{
							for (int32 i = 0; i < Helper.Num(); ++i)
							{
								auto RawPtr = Helper.GetRawPtr(i);
								auto ElmWriter = Writer.ArrayElm(i);
								WriteToPB(ElmWriter, Prop->Inner, RawPtr);
							}
						}
					}
					else
					{
						FScriptArrayHelper Helper(Prop, ArrAddr);
						if (Helper.Num() > 0)
							WriteToPB(Writer, Prop->Inner, Helper.GetRawPtr(0));
					}
				}
				using TValueVisitorDefault<FArrayProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FArrayProperty* Prop, void* ArrAddr, int32 ArrIdx)
				{
					auto& Reader = *Ptr;
					check(Prop->ArrayDim == 1 && ArrIdx == 0);

					// Bytes
					if (Reader.IsBytes() && ensureAlways(Prop->Inner->IsA<FByteProperty>() || Prop->Inner->IsA<FInt8Property>()))
					{
						auto View = StringView(Reader.GetFieldBytes());
						FScriptArrayHelper Helper(Prop, ArrAddr);
						Helper.Resize(View.size());
						FMemory::Memcpy(Helper.GetRawPtr(), View.data(), View.size());
					}
					else if (ensure(Reader.IsArray()))
					{
						GMP_CHECK(Reader.FieldDef.GetArrayIdx() < 0);
						auto ItemsToRead = FMath::Max((int32)Reader.ArraySize(), 0);
						FScriptArrayHelper Helper(Prop, ArrAddr);
						Helper.Resize(ItemsToRead);
						for (auto i = 0; i < Helper.Num(); ++i)
						{
							auto RawPtr = Helper.GetRawPtr(i);
							auto ElmReader = Reader.ArrayElm(i);
							ReadFromPB(ElmReader, Prop->Inner, RawPtr);
						}
					}
					else
					{
						FScriptArrayHelper Helper(Prop, ArrAddr);
						Helper.Resize(1);
						ReadFromPB(Reader, Prop->Inner, Helper.GetRawPtr(0));
					};
				}
			};
			template<>
			struct TValueVisitor<FSetProperty> : public TValueVisitorDefault<FSetProperty>
			{
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FSetProperty* Prop, const void* SetAddr, int32 ArrIdx)
				{
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					if (ensure(Writer.IsArray()))
					{
						FScriptSetHelper Helper(Prop, SetAddr);
						for (int32 i = 0; i < Helper.Num(); ++i)
						{
							if (ensure(Helper.IsValidIndex(i)))
							{
								auto Elm = Writer.ArrayElm(i);
								WriteToPB(Elm, Prop->ElementProp, Helper.GetElementPtr(i));
							}
						}
					}
				}
				using TValueVisitorDefault<FSetProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(const ReaderType* Ptr, FSetProperty* Prop, void* SetAddr, int32 ArrIdx)
				{
					auto& Reader = *Ptr;
					ensureAlways(Prop->ArrayDim == 1 && ArrIdx == 0);
					if (ensure(Reader.IsArray()))
					{
						FScriptSetHelper Helper(Prop, SetAddr);
						for (auto i = 0; i < Reader.ArraySize(); ++i)
						{
							int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
							ReadFromPB(Reader.ArrayElm(i), Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						}
						Helper.Rehash();
					}
					else
					{
						FScriptSetHelper Helper(Prop, SetAddr);
						Helper.EmptyElements(1);
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						ReadFromPB(Reader, Prop->ElementProp, Helper.GetElementPtr(NewIndex));
						Helper.Rehash();
					}
				}
			};
			template<>
			struct TValueVisitor<FMapProperty> : public TValueVisitorDefault<FMapProperty>
			{
				static int32 PropToMap(FProtoWriter& Writer, FMapProperty* MapProp, const void* MapAddr)
				{
					int32 Ret = 0;
					if (ensureAlways(Writer.IsMap()))
					{
						auto MapEntryDef = Writer.MapEntryDef();

						FScriptMapHelper Helper(MapProp, MapAddr);
						for (auto i = 0; i < Helper.Num(); ++i)
						{
							if (!Helper.IsValidIndex(i))
								continue;

							FProtoWriter KeyWriter(MapEntryDef.MapKeyDef(), Writer.GetArena());
							Ret += EncodeProtoImpl(KeyWriter, MapProp->KeyProp, Helper.GetKeyPtr(i));
							FProtoWriter ValueWriter(MapEntryDef.MapValueDef(), Writer.GetArena());
							Ret += EncodeProtoImpl(ValueWriter, MapProp->ValueProp, Helper.GetValuePtr(i));

							Writer.InsertFieldMapPair(KeyWriter.Var, ValueWriter.Var);
						}
					}
					return Ret;
				}
				template<typename WriterType>
				static void WriteVisit(WriterType& Writer, FMapProperty* Prop, const void* MapAddr, int32 ArrIdx)
				{
					if (!ensure(Writer.IsMap()))
						return;
					check(Prop->ArrayDim == 1 && ArrIdx == 0);
					PropToMap(Writer, Prop, MapAddr);
				}

				static int32 MapToProp(const FProtoReader& Reader, FMapProperty* MapProp, void* MapAddr)
				{
					int32 Ret = 0;
					if (ensureAlways(Reader.IsMap()))
					{
						auto MapRef = Reader.GetSubMap();
						auto MapSize = upb_Map_Size(MapRef);

						FScriptMapHelper Helper(MapProp, MapAddr);
						Helper.EmptyValues(MapSize);

						auto MapDef = Reader.MapEntryDef();
						size_t Iter = kUpb_Map_Begin;
						upb_MessageValue key;
						upb_MessageValue val;
						while (upb_Map_Next(MapRef, &key, &val, &Iter))
						{
							Helper.AddDefaultValue_Invalid_NeedsRehash();
							FProtoReader KeyReader(key, MapDef.MapKeyDef());
							Ret += DecodeProtoImpl(KeyReader, MapProp->KeyProp, Helper.GetKeyPtr(Iter));
							FProtoReader ValueReader(val, MapDef.MapValueDef());
							Ret += DecodeProtoImpl(ValueReader, MapProp->ValueProp, Helper.GetValuePtr(Iter));
						}
						Helper.Rehash();
					}
					return Ret;
				}

				using TValueVisitorDefault<FMapProperty>::ReadVisit;
				template<typename ReaderType>
				static void ReadVisit(ReaderType* Ptr, FMapProperty* Prop, void* MapAddr, int32 ArrIdx)
				{
					auto& PBVal = *Ptr;
					check(Prop->ArrayDim == 1 && ArrIdx == 0);
					MapToProp(PBVal, Prop, MapAddr);
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
				static bool Read(const ReaderType& Reader, P* Prop, void* Addr)
				{
					int32 i = 0;
					auto Visitor = [&](auto&& Elm) { TValueVisitor<P>::ReadVisit(std::forward<decltype(Elm)>(Elm), Prop, Addr, i); };
					if (Reader.IsArray() && Reader.FieldDef.GetArrayIdx() < 0 && !CastField<FArrayProperty>(Prop) && !CastField<FSetProperty>(Prop))
					{
						int32 ItemsToRead = FMath::Clamp((int32)Reader.ArraySize(), 0, Prop->ArrayDim);
						for (; i < ItemsToRead; ++i)
						{
							std::visit(Visitor, Reader.ArrayElm(i).DispatchFieldValue());
						}
					}
					else
					{
						std::visit(Visitor, Reader.DispatchFieldValue());
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

	int32 EncodeProtoImpl(FProtoWriter& Value, FProperty* Prop, const void* Addr)
	{
		int32 Ret = 0;
		Ret += Detail::WriteToPB(Value, Prop, Addr) ? 1 : 0;
		return Ret;
	}

	int32 DecodeProtoImpl(const FProtoReader& Value, FProperty* Prop, void* Addr)
	{
		int32 Ret = 0;
		Ret += Detail::ReadFromPB(Value, Prop, Addr) ? 1 : 0;
		return Ret;
	}

}  // namespace PB
}  // namespace GMP

void ReigsterProtoDesc(const char* Buf, size_t Size)
{
	GMP::PB::AddProto(Buf, Size);
}

DEFINE_FUNCTION(UGMPProtoUtils::execAsStruct)
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

DEFINE_FUNCTION(UGMPProtoUtils::execEncodeProto)
{
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const void* Data = Stack.MostRecentPropertyAddress;
	FStructProperty* Prop = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_GET_TARRAY_REF(uint8, Buffer);
	P_FINISH

	P_NATIVE_BEGIN
#if defined(GMP_WITH_UPB)
	if (!Prop || !Prop->Struct->IsA<UProtoDefinedStruct>())
	{
		FFrame::KismetExecutionMessage(TEXT("invalid struct type"), ELogVerbosity::Error);
		*(bool*)RESULT_PARAM = false;
	}
	else
	{
		*(bool*)RESULT_PARAM = !!GMP::PB::Serializer::UStructToProtoImpl(Buffer, Prop->Struct, Data);
	}
#else
	FFrame::KismetExecutionMessage(TEXT("unsupported decode proto"), ELogVerbosity::Error);
	*(bool*)RESULT_PARAM = false;
#endif
	P_NATIVE_END
}

DEFINE_FUNCTION(UGMPProtoUtils::execDecodeProto)
{
	P_GET_TARRAY_REF(uint8, Buffer);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* Data = Stack.MostRecentPropertyAddress;
	FStructProperty* Prop = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH

	P_NATIVE_BEGIN
#if defined(GMP_WITH_UPB)
	if (!Prop || !Prop->Struct->IsA<UProtoDefinedStruct>())
	{
		FFrame::KismetExecutionMessage(TEXT("invalid struct type"), ELogVerbosity::Error);
		*(bool*)RESULT_PARAM = false;
	}
	else
	{
		*(bool*)RESULT_PARAM = !!GMP::PB::Deserializer::UStructFromProtoImpl(Buffer, Prop->Struct, Data);
	}
#else
	FFrame::KismetExecutionMessage(TEXT("unsupported decode proto"), ELogVerbosity::Error);
	*(bool*)RESULT_PARAM = false;
#endif
	P_NATIVE_END
}

void UGMPProtoUtils::ClearOneOf(UPARAM(ref) FGMPValueOneOf& OneOf)
{
	OneOf.Clear();
}

bool UGMPProtoUtils::AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey)
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
			const FProtoReader& Reader = Ptr->Reader;
			GMP::PB::Detail::Internal::TValueVisitor<FStructProperty>::ReadVisit(&Reader, CastFieldChecked<FStructProperty>(Prop), Out, 0);
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

int32 UGMPProtoUtils::IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue)
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
			const FProtoReader& Reader = Ptr->Reader;
			if (auto SubFieldDef = Reader.FieldDef.MessageSubdef().FindFieldByNumber(Idx))
			{
				DecodeProtoImpl(FProtoReader(SubFieldDef, Reader.GetSubMessage()), GMP::TClass2Prop<FGMPValueOneOf>::GetProperty(), &OutValue);
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
