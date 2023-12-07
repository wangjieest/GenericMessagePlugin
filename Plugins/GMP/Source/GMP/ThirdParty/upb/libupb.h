//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include <memory>

//
#include "upb/base/status.h"
#include "upb/mem/arena.h"
#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"
#include "upb/reflection/internal/enum_def.h"
#include "upb/reflection/message.h"

// Must be last
#include "upb/port/def.inc"

namespace GMP
{
namespace upb
{
	class FStatus
	{
	public:
		FStatus() { Clear(); }
		// Guaranteed to be NULL-terminated.
		const char* ErrorMessage() const { return upb_Status_ErrorMessage(&status_); }

		// The error message will be truncated if it is longer than _kUpb_Status_MaxMessage-4.
		void SetErrorMessage(const char* msg) { upb_Status_SetErrorMessage(&status_, msg); }
		void SetFormattedErrorMessage(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			upb_Status_VSetErrorFormat(&status_, fmt, args);
			va_end(args);
		}

		// Resets the status to a successful state with no message.
		void Clear() { upb_Status_Clear(&status_); }

		auto* operator&() { return &status_; }
		explicit operator bool() const { return upb_Status_IsOk(&status_); }

	private:
		upb_Status status_;
	};

	// A simple arena with no initial memory block and the default allocator.
	class FArena
	{
	public:
		FArena()
			: Ptr_(upb_Arena_New(), FArena_Deleter())
		{
		}
		FArena(char* initial_block, size_t size)
			: Ptr_(upb_Arena_Init(initial_block, size, &upb_alloc_global), FArena_Deleter())
		{
		}

		void Fuse(FArena& other) { upb_Arena_Fuse(Ptr_.Get(), other.Ptr_.Get()); }

		upb_Arena* operator*() const { return Ptr_.Get(); }

	protected:
		struct FArena_Deleter
		{
			void operator()(upb_Arena* Ptr) const { upb_Arena_Free(Ptr); }
		};
		TUniquePtr<upb_Arena, FArena_Deleter> Ptr_;
	};

	// FInlinedArena seeds the arenas with a predefined amount of memory.  No heap memory will be allocated until the initial block is exceeded.
	template<int N>
	class FInlinedArena : public FArena
	{
	public:
		FInlinedArena()
			: FArena(initial_block_, N)
		{
		}
		~FInlinedArena()
		{
			// Explicitly destroy the arena now so that it does not outlive initial_block_.
			Ptr_.Reset();
		}

	private:
		FInlinedArena(const FInlinedArena*) = delete;
		FInlinedArena& operator=(const FInlinedArena*) = delete;

		char initial_block_[N];
	};

	typedef upb_MessageValue FMessageValue;

	class FEnumDefPtr;
	class FFileDefPtr;
	class FMessageDefPtr;
	class FOneofDefPtr;

	// A FFieldDefPtr describes a single Field in a message.
	// It is most often found as a part of a upb_MessageDef, but can also stand alone to represent an extension.
	class FFieldDefPtr
	{
	public:
		FFieldDefPtr()
			: Ptr_(nullptr)
		{
		}
		explicit FFieldDefPtr(const upb_FieldDef* ptr)
			: Ptr_(ptr)
		{
		}

		typedef upb_FieldType Type;
		typedef upb_CType CType;
		typedef upb_Label Label;

		FFileDefPtr File() const;
		const char* FullName() const { return upb_FieldDef_FullName(Ptr_); }

		const upb_MiniTableField* MiniTable() const { return upb_FieldDef_MiniTable(Ptr_); }

		const UPB_DESC(FieldOptions) * Options() const { return upb_FieldDef_Options(Ptr_); }

		Type GetType() const { return upb_FieldDef_Type(Ptr_); }
		CType GetCType() const { return upb_FieldDef_CType(Ptr_); }
		Label GetLabel() const { return upb_FieldDef_Label(Ptr_); }
		const char* Name() const { return upb_FieldDef_Name(Ptr_); }
		const char* JsonName() const { return upb_FieldDef_JsonName(Ptr_); }
		uint32_t Number() const { return upb_FieldDef_Number(Ptr_); }
		bool IsExtension() const { return upb_FieldDef_IsExtension(Ptr_); }
		bool IsRequired() const { return upb_FieldDef_IsRequired(Ptr_); }
		bool HasPresence() const { return upb_FieldDef_HasPresence(Ptr_); }

		// For non-string, non-submessage Fields, this indicates whether binary protobufs are encoded in IsPacked or non-IsPacked format.
		// Note: this accessor reflects the fact that "IsPacked" has different defaults depending on whether the proto is proto2 or proto3.
		bool IsPacked() const { return upb_FieldDef_IsPacked(Ptr_); }

		// An integer that can be used as an Index into an array of Fields for whatever message this Field belongs to.
		// Guaranteed to be less than f->ContainingType()->FieldCount().
		// May only be accessed once the def has been finalized.
		uint32_t Index() const { return upb_FieldDef_Index(Ptr_); }

		// The MessageDef to which this Field belongs (for extensions, the extended message).
		FMessageDefPtr ContainingType() const;

		// For extensions, the message the extension is declared inside, or NULL if none.
		FMessageDefPtr ExtensionScope() const;

		// The OneofDef to which this Field belongs, or NULL if this Field is not part of a Oneof.
		FOneofDefPtr ContainingOneof() const;
		FOneofDefPtr RealContainingOneof() const;

		FMessageValue DefaultValue() const { return upb_FieldDef_Default(Ptr_); }

		// Convenient Field type tests.
		bool IsSubMessage() const { return upb_FieldDef_IsSubMessage(Ptr_); }
		bool IsString() const { return upb_FieldDef_IsString(Ptr_); }
		// non-string, non-submessage Fields
		bool IsPrimitive() const { return upb_FieldDef_IsPrimitive(Ptr_); }

		bool IsSequence() const { return upb_FieldDef_IsRepeated(Ptr_); }
		bool IsMap() const { return upb_FieldDef_IsMap(Ptr_); }

		// Returns the enum or submessage def for this Field, if any.  The Field's type must match
		// (ie. you may only call EnumSubdef() for Fields where type() == kUpb_CType_Enum).
		// only valid if type() == kUpb_CType_Enum
		FEnumDefPtr EnumSubdef() const;
		// only valid if type() == kUpb_CType_Message
		FMessageDefPtr MessageType() const;

		explicit operator bool() const { return Ptr_ != nullptr; }
		friend bool operator==(FFieldDefPtr lhs, FFieldDefPtr rhs) { return lhs.Ptr_ == rhs.Ptr_; }
		friend bool operator!=(FFieldDefPtr lhs, FFieldDefPtr rhs) { return !(lhs == rhs); }

		auto operator*() const { return Ptr_; }

	private:
		const upb_FieldDef* Ptr_;
	};

	// Class that represents a Oneof.
	class FOneofDefPtr
	{
	public:
		FOneofDefPtr()
			: Ptr_(nullptr)
		{
		}
		explicit FOneofDefPtr(const upb_OneofDef* ptr)
			: Ptr_(ptr)
		{
		}

		explicit operator bool() const { return Ptr_ != nullptr; }

		const UPB_DESC(OneofOptions) * Options() const { return upb_OneofDef_Options(Ptr_); }

		// Returns the MessageDef that contains this OneofDef.
		FMessageDefPtr ContainingType() const;

		// Returns the Name of this Oneof.
		const char* Name() const { return upb_OneofDef_Name(Ptr_); }
		const char* FullName() const { return upb_OneofDef_FullName(Ptr_); }

		// Returns the Number of Fields in the Oneof.
		int FieldCount() const { return upb_OneofDef_FieldCount(Ptr_); }
		FFieldDefPtr Field(int i) const { return FFieldDefPtr(upb_OneofDef_Field(Ptr_, i)); }

		// Looks up by Name.
		FFieldDefPtr FindFieldByName(const char* Name, size_t Len) const { return FFieldDefPtr(upb_OneofDef_LookupNameWithSize(Ptr_, Name, Len)); }
		FFieldDefPtr FindFieldByName(const FAnsiStringView& Str) const { return FindFieldByName(Str.GetData(), Str.Len()); }

		FFieldDefPtr FindFieldByName(const char* Name) const { return FFieldDefPtr(upb_OneofDef_LookupName(Ptr_, Name)); }
		// Looks up by tag Number.
		FFieldDefPtr FindFieldByNumber(uint32_t num) const { return FFieldDefPtr(upb_OneofDef_LookupNumber(Ptr_, num)); }

		auto operator*() const { return Ptr_; }

	private:
		const upb_OneofDef* Ptr_;
	};

	// Structure that describes a single .proto message type.
	class FMessageDefPtr
	{
	public:
		FMessageDefPtr()
			: Ptr_(nullptr)
		{
		}
		explicit FMessageDefPtr(const upb_MessageDef* ptr)
			: Ptr_(ptr)
		{
		}

		const UPB_DESC(MessageOptions) * Options() const { return upb_MessageDef_Options(Ptr_); }
		upb_StringView MiniDescriptorEncode(FArena& Arena) const
		{
			upb_StringView md;
			upb_MessageDef_MiniDescriptorEncode(Ptr_, *Arena, &md);
			return md;
		}

		FFileDefPtr File() const;

		const char* FullName() const { return upb_MessageDef_FullName(Ptr_); }
		const char* Name() const { return upb_MessageDef_Name(Ptr_); }

		const upb_MiniTable* MiniTable() const { return upb_MessageDef_MiniTable(Ptr_); }

		// The Number of Fields that belong to the MessageDef.
		int FieldCount() const { return upb_MessageDef_FieldCount(Ptr_); }
		FFieldDefPtr Field(int i) const { return FFieldDefPtr(upb_MessageDef_Field(Ptr_, i)); }

		// The Number of Oneofs that belong to the MessageDef.
		int OneofCount() const { return upb_MessageDef_OneofCount(Ptr_); }
		int RealOneofCount() const { return upb_MessageDef_RealOneofCount(Ptr_); }
		FOneofDefPtr Oneof(int i) const { return FOneofDefPtr(upb_MessageDef_Oneof(Ptr_, i)); }

		int EnumTypeCount() const { return upb_MessageDef_NestedEnumCount(Ptr_); }
		FEnumDefPtr EnumType(int i) const;

		int NestedMessageCount() const { return upb_MessageDef_NestedMessageCount(Ptr_); }
		FMessageDefPtr NestedMessage(int i) const { return FMessageDefPtr(upb_MessageDef_NestedMessage(Ptr_, i)); }

		int NestedExtensionCount() const { return upb_MessageDef_NestedExtensionCount(Ptr_); }
		FFieldDefPtr NestedExtension(int i) const { return FFieldDefPtr(upb_MessageDef_NestedExtension(Ptr_, i)); }

		int ExtensionRangeCount() const { return upb_MessageDef_ExtensionRangeCount(Ptr_); }

		// upb_Syntax syntax() const { return upb_MessageDef_Syntax(Ptr_); }
		bool Syntax3() const { return upb_MessageDef_Syntax(Ptr_) == upb_Syntax::kUpb_Syntax_Proto3; }

		// These return null pointers if the Field is not found.
		FFieldDefPtr FindFieldByNumber(uint32_t Number) const { return FFieldDefPtr(upb_MessageDef_FindFieldByNumber(Ptr_, Number)); }

		FFieldDefPtr FindFieldByName(const char* Name, size_t Len) const { return FFieldDefPtr(upb_MessageDef_FindFieldByNameWithSize(Ptr_, Name, Len)); }
		FFieldDefPtr FindFieldByName(const FAnsiStringView& Str) const { return FindFieldByName(Str.GetData(), Str.Len()); }
		FFieldDefPtr FindFieldByName(const char* Name) const { return FFieldDefPtr(upb_MessageDef_FindFieldByName(Ptr_, Name)); }

		FOneofDefPtr FindOneofByName(const char* Name, size_t Len) const { return FOneofDefPtr(upb_MessageDef_FindOneofByNameWithSize(Ptr_, Name, Len)); }
		FOneofDefPtr FindOneofByName(const FAnsiStringView& Str) const { return FindOneofByName(Str.GetData(), Str.Len()); }
		FOneofDefPtr FindOneofByName(const char* Name) const { return FOneofDefPtr(upb_MessageDef_FindOneofByName(Ptr_, Name)); }

		// Is this message a map entry?
		bool IsMapEntry() const { return upb_MessageDef_IsMapEntry(Ptr_); }

		FFieldDefPtr MapKey() const
		{
			if (!IsMapEntry())
				return FFieldDefPtr();
			return FFieldDefPtr(upb_MessageDef_Field(Ptr_, 0));
		}

		FFieldDefPtr MapValue() const
		{
			if (!IsMapEntry())
				return FFieldDefPtr();
			return FFieldDefPtr(upb_MessageDef_Field(Ptr_, 1));
		}

		// Return the type of well known type message. kUpb_WellKnown_Unspecified for non-well-known message.
		upb_WellKnown WellKwownType() const { return upb_MessageDef_WellKnownType(Ptr_); }

		explicit operator bool() const { return Ptr_ != nullptr; }
		friend bool operator==(FMessageDefPtr lhs, FMessageDefPtr rhs) { return lhs.Ptr_ == rhs.Ptr_; }
		friend bool operator!=(FMessageDefPtr lhs, FMessageDefPtr rhs) { return !(lhs == rhs); }

		auto operator*() const { return Ptr_; }

	private:
		class FieldIter
		{
		public:
			explicit FieldIter(const upb_MessageDef* m, int i)
				: m_(m)
				, i_(i)
			{
			}
			void operator++() { i_++; }

			FFieldDefPtr operator*() { return FFieldDefPtr(upb_MessageDef_Field(m_, i_)); }

			friend bool operator==(FieldIter lhs, FieldIter rhs) { return lhs.i_ == rhs.i_; }

			friend bool operator!=(FieldIter lhs, FieldIter rhs) { return !(lhs == rhs); }

		private:
			const upb_MessageDef* m_;
			int i_;
		};

		class FieldAccessor
		{
		public:
			explicit FieldAccessor(const upb_MessageDef* md)
				: md_(md)
			{
			}
			FieldIter begin() { return FieldIter(md_, 0); }
			FieldIter end() { return FieldIter(md_, upb_MessageDef_FieldCount(md_)); }

		private:
			const upb_MessageDef* md_;
		};

		class OneofIter
		{
		public:
			explicit OneofIter(const upb_MessageDef* m, int i)
				: m_(m)
				, i_(i)
			{
			}
			void operator++() { i_++; }

			FOneofDefPtr operator*() { return FOneofDefPtr(upb_MessageDef_Oneof(m_, i_)); }

			friend bool operator==(OneofIter lhs, OneofIter rhs) { return lhs.i_ == rhs.i_; }

			friend bool operator!=(OneofIter lhs, OneofIter rhs) { return !(lhs == rhs); }

		private:
			const upb_MessageDef* m_;
			int i_;
		};

		class OneofAccessor
		{
		public:
			explicit OneofAccessor(const upb_MessageDef* md)
				: md_(md)
			{
			}
			OneofIter begin() { return OneofIter(md_, 0); }
			OneofIter end() { return OneofIter(md_, upb_MessageDef_OneofCount(md_)); }

		private:
			const upb_MessageDef* md_;
		};

	public:
		FieldAccessor Fields() const { return FieldAccessor(Ptr_); }
		OneofAccessor Oneofs() const { return OneofAccessor(Ptr_); }

	private:
		const upb_MessageDef* Ptr_;
	};

	class FEnumValDefPtr
	{
	public:
		FEnumValDefPtr()
			: Ptr_(nullptr)
		{
		}
		explicit FEnumValDefPtr(const upb_EnumValueDef* ptr)
			: Ptr_(ptr)
		{
		}

		const UPB_DESC(EnumValueOptions) * Options() const { return upb_EnumValueDef_Options(Ptr_); }

		int32_t Number() const { return upb_EnumValueDef_Number(Ptr_); }
		const char* FullName() const { return upb_EnumValueDef_FullName(Ptr_); }
		const char* Name() const { return upb_EnumValueDef_Name(Ptr_); }

	private:
		const upb_EnumValueDef* Ptr_;
	};

	class FEnumDefPtr
	{
	public:
		FEnumDefPtr()
			: Ptr_(nullptr)
		{
		}
		explicit FEnumDefPtr(const upb_EnumDef* ptr)
			: Ptr_(ptr)
		{
		}

		const upb_MiniTableEnum* MiniTable() const { return _upb_EnumDef_MiniTable(Ptr_); }

		const UPB_DESC(EnumOptions) * Options() const { return upb_EnumDef_Options(Ptr_); }
		upb_StringView MiniDescriptorEncode(FArena& Arena) const
		{
			upb_StringView md;
			upb_EnumDef_MiniDescriptorEncode(Ptr_, *Arena, &md);
			return md;
		}

		explicit operator bool() const { return Ptr_ != nullptr; }

		const char* FullName() const { return upb_EnumDef_FullName(Ptr_); }
		const char* Name() const { return upb_EnumDef_Name(Ptr_); }
		bool IsClosed() const { return upb_EnumDef_IsClosed(Ptr_); }

		// The Value that is used as the default when no Field default is specified.
		// If not set explicitly, the first Value that was added will be used.
		// The default Value must be a member of the enum.
		// Requires that ValueCount() > 0.
		int32_t DefaultValue() const { return upb_EnumDef_Default(Ptr_); }

		// Returns the Number of values currently defined in the enum.
		// Note that multiple names can refer to the same Number,
		// so this may be greater than the total Number of unique numbers.
		int ValueCount() const { return upb_EnumDef_ValueCount(Ptr_); }
		FEnumValDefPtr Value(int i) const { return FEnumValDefPtr(upb_EnumDef_Value(Ptr_, i)); }

		// Lookups from Name to integer, returning true if found.
		FEnumValDefPtr FindValueByName(const char* Name) const { return FEnumValDefPtr(upb_EnumDef_FindValueByName(Ptr_, Name)); }

		// Finds the Name corresponding to the given Number, or NULL if none was found.
		// If more than one Name corresponds to this Number, returns the first one that was added.
		FEnumValDefPtr FindValueByNumber(int32_t num) const { return FEnumValDefPtr(upb_EnumDef_FindValueByNumber(Ptr_, num)); }

		auto operator*() const { return Ptr_; }

	private:
		const upb_EnumDef* Ptr_;
	};

	// Class that represents a .proto File with some things defined in it.
	//
	// Many users won't care about FileDefs, but they are necessary if you want to read the values of File-level Options.
	class FFileDefPtr
	{
	public:
		explicit FFileDefPtr(const upb_FileDef* ptr)
			: Ptr_(ptr)
		{
		}

		const UPB_DESC(FileOptions) * Options() const { return upb_FileDef_Options(Ptr_); }

		// Get/set Name of the File (eg. "foo/bar.proto").
		const char* Name() const { return upb_FileDef_Name(Ptr_); }

		// Package Name for definitions inside the File (eg. "foo.bar").
		const char* Package() const { return upb_FileDef_Package(Ptr_); }

		// Syntax for the File.  Defaults to proto2.
		// upb_Syntax syntax() const { return upb_FileDef_Syntax(Ptr_); }
		bool Syntax3() const { return upb_FileDef_Syntax(Ptr_) == upb_Syntax::kUpb_Syntax_Proto3; }

		// Get the list of dependencies from the File.  These are returned in the order that they were added to the FFileDefPtr.
		int DependencyCount() const { return upb_FileDef_DependencyCount(Ptr_); }
		FFileDefPtr Dependency(int Index) const { return FFileDefPtr(upb_FileDef_Dependency(Ptr_, Index)); }

		int PublicDependencyCount() const { return upb_FileDef_PublicDependencyCount(Ptr_); }
		FFileDefPtr PublicDependency(int Index) const { return FFileDefPtr(upb_FileDef_PublicDependency(Ptr_, Index)); }

		int ToplevelEnumCount() const { return upb_FileDef_TopLevelEnumCount(Ptr_); }
		FEnumDefPtr ToplevelEnum(int Index) const { return FEnumDefPtr(upb_FileDef_TopLevelEnum(Ptr_, Index)); }

		int ToplevelMessageCount() const { return upb_FileDef_TopLevelMessageCount(Ptr_); }
		FMessageDefPtr ToplevelMessage(int Index) const { return FMessageDefPtr(upb_FileDef_TopLevelMessage(Ptr_, Index)); }

		int ToplevelExtensionCount() const { return upb_FileDef_TopLevelExtensionCount(Ptr_); }
		FFieldDefPtr ToplevelExtension(int Index) const { return FFieldDefPtr(upb_FileDef_TopLevelExtension(Ptr_, Index)); }

		explicit operator bool() const { return Ptr_ != nullptr; }
		friend bool operator==(FFileDefPtr lhs, FFileDefPtr rhs) { return lhs.Ptr_ == rhs.Ptr_; }
		friend bool operator!=(FFileDefPtr lhs, FFileDefPtr rhs) { return !(lhs == rhs); }

		auto operator*() const { return Ptr_; }

	private:
		const upb_FileDef* Ptr_;
	};

	// Non-const methods in FDefPool are NOT thread-safe.
	class FDefPool
	{
	public:
		FDefPool()
			: Ptr_(upb_DefPool_New(), FDefPool_Deleter())
		{
		}
		explicit FDefPool(upb_DefPool* s)
			: Ptr_(s, FDefPool_Deleter())
		{
		}

		// Finds an entry in the symbol table with this exact Name.  If not found, returns NULL.
		FMessageDefPtr FindMessageByName(const char* Sym) const { return FMessageDefPtr(upb_DefPool_FindMessageByName(Ptr_.Get(), Sym)); }

		FEnumDefPtr FindEnumByName(const char* Sym) const { return FEnumDefPtr(upb_DefPool_FindEnumByName(Ptr_.Get(), Sym)); }

		FFileDefPtr FindFileByName(const char* Name) const { return FFileDefPtr(upb_DefPool_FindFileByName(Ptr_.Get(), Name)); }

		FFieldDefPtr FindExtensionByName(const char* Name) const { return FFieldDefPtr(upb_DefPool_FindExtensionByName(Ptr_.Get(), Name)); }

		template<typename TArrView>
		bool AddFile(const TArrView& ArrView)
		{
			FArena Arena;
			auto FileProto = google_protobuf_FileDescriptorProto_parse(ArrView.GetData, ArrView.Len(), *Arena);
			FStatus Status;
			AddFileImpl(FileProto, Status);
			return bool(Status);
		}
		template<typename TArrView>
		bool AddFiles(const TArrView& ArrView)
		{
			size_t DefCnt = 0;
			FArena Arena;
			if (auto FileProtoSet = google_protobuf_FileDescriptorSet_parse(ArrView.GetData(), ArrView.Len(), *Arena))
			{
				size_t ProtoCnt = 0;
				auto FileProtos = google_protobuf_FileDescriptorSet_file(FileProtoSet, &ProtoCnt);
				for (auto i = 0; i < ProtoCnt; i++)
				{
					FStatus Status;
					AddFileImpl(FileProtos[i], Status);
					DefCnt += bool(Status) ? 1 : 0;
				}
			}
			return DefCnt > 0;
		}
		auto operator*() const { return Ptr_.Get(); }

	private:
		void _SetPlatform(upb_MiniTablePlatform platform) { _upb_DefPool_SetPlatform(Ptr_.Get(), platform); }
		// Adds the given serialized FileDescriptorProto to the pool.
		FFileDefPtr AddFileImpl(const UPB_DESC(FileDescriptorProto) * file_proto, FStatus& Status) { return FFileDefPtr(upb_DefPool_AddFile(Ptr_.Get(), file_proto, &Status)); }
		struct FDefPool_Deleter
		{
			void operator()(upb_DefPool* Ptr) const { upb_DefPool_Free(Ptr); }
		};
		TUniquePtr<upb_DefPool, FDefPool_Deleter> Ptr_;
	};

	inline FFileDefPtr FFieldDefPtr::File() const { return FFileDefPtr(upb_FieldDef_File(Ptr_)); }
	inline FFileDefPtr FMessageDefPtr::File() const { return FFileDefPtr(upb_MessageDef_File(Ptr_)); }
	inline FEnumDefPtr FMessageDefPtr::EnumType(int i) const { return FEnumDefPtr(upb_MessageDef_NestedEnum(Ptr_, i)); }
	inline FMessageDefPtr FFieldDefPtr::MessageType() const { return FMessageDefPtr(upb_FieldDef_MessageSubDef(Ptr_)); }
	inline FMessageDefPtr FFieldDefPtr::ContainingType() const { return FMessageDefPtr(upb_FieldDef_ContainingType(Ptr_)); }
	inline FMessageDefPtr FFieldDefPtr::ExtensionScope() const { return FMessageDefPtr(upb_FieldDef_ExtensionScope(Ptr_)); }
	inline FMessageDefPtr FOneofDefPtr::ContainingType() const { return FMessageDefPtr(upb_OneofDef_ContainingType(Ptr_)); }
	inline FOneofDefPtr FFieldDefPtr::ContainingOneof() const { return FOneofDefPtr(upb_FieldDef_ContainingOneof(Ptr_)); }
	inline FOneofDefPtr FFieldDefPtr::RealContainingOneof() const { return FOneofDefPtr(upb_FieldDef_RealContainingOneof(Ptr_)); }
	inline FEnumDefPtr FFieldDefPtr::EnumSubdef() const { return FEnumDefPtr(upb_FieldDef_EnumSubDef(Ptr_)); }

	class FMtDataEncoderImpl;
	class FMtDataEncoder
	{
	public:
		FMtDataEncoder();
		~FMtDataEncoder();

		bool StartMessage(uint64_t msg_mod);
		bool PutField(upb_FieldType type, uint32_t field_num, uint64_t field_mod);

		bool StartOneof();
		bool PutOneofField(uint32_t field_num);

		bool StartEnum();
		bool PutEnumValue(uint32_t enum_value);
		bool EndEnum();

		bool EncodeExtension(upb_FieldType type, uint32_t field_num, uint64_t field_mod);
		bool EncodeMap(upb_FieldType key_type, upb_FieldType val_type, uint64_t key_mod, uint64_t val_mod);
		bool EncodeMessageSet();

		const TArray<char>& GetData() const;

	private:
		template<class F>
		bool Append(F&& func);
		TUniquePtr<FMtDataEncoderImpl> Encoder_;
	};

}  // namespace upb
}  // namespace GMP
#include "upb/port/undef.inc"
