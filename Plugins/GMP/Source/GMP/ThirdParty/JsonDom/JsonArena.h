//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#ifndef UNREAL_JSONARENA_H
#define UNREAL_JSONARENA_H

#include <vector>
#include <cstring>
#include <cstdint>
#include "JsonEncoding.h"   // TCHAR/FString/TEXT from platform layer

namespace JSONDOM_NAMESPACE
{
using ::TArray;
using ::TSharedPtr;
using ::TSharedRef;

// EJson — the canonical node-type enum (arena is now the single DOM; this is the source of truth,
// formerly in JsonDom.h). Order is load-bearing (serializer + call-site `sj::EJson::Object` checks).
enum class EJson : uint8_t { None, Null, String, Number, Boolean, Array, Object };

// UE-way double formatting (formerly NumberToJsonString in JsonDom.h); kept here so JsonDom.h can be
// a thin forwarding shell.
inline FString NumberToJsonString(double N)
{
	if (N == (double)(long long)N) return FString::Printf(TEXT("%lld"), (long long)N);
	return FString::Printf(TEXT("%.17g"), N);
}

struct FArenaNode;

// ---------------------------------------------------------------------------
// FArena — bump-pointer block-chain allocator. Allocations never move (stable pointers); the whole
// chain is released when the arena dies. No per-allocation free.
// ---------------------------------------------------------------------------
class FArena
{
public:
	explicit FArena(size_t BlockBytes = 64 * 1024) : BlockSize(BlockBytes) {}
	~FArena() { for (char* B : Blocks) ::free(B); }
	FArena(const FArena&) = delete;
	FArena& operator=(const FArena&) = delete;

	// Bump-allocate Size bytes (aligned). Stable address; freed only when the arena dies. malloc returns
	// max_align-aligned blocks, so an in-block offset aligned to Align (<= max_align) yields an aligned address.
	void* Alloc(size_t Size, size_t Align = alignof(std::max_align_t))
	{
		size_t Pad = (Align - (Offset & (Align - 1))) & (Align - 1);
		if (Blocks.empty() || Offset + Pad + Size > CurBlockSize)
		{
			const size_t Want = Size > BlockSize ? Size : BlockSize;
			char* B = (char*)::malloc(Want);
			Blocks.push_back(B);
			Head = B; CurBlockSize = Want; Offset = 0; Pad = 0;
		}
		Offset += Pad;
		void* P = Head + Offset;
		Offset += Size;
		return P;
	}

	template <class T, class... Args>
	T* New(Args&&... args)
	{
		void* P = Alloc(sizeof(T), alignof(T));
		return new (P) T(static_cast<Args&&>(args)...);
	}

	// Copy a TCHAR string into the arena (stored once); returns {ptr,len}. Null-terminated for c_str use.
	const TCHAR* CopyStr(const TCHAR* Src, int32_t Len)
	{
		TCHAR* Dst = (TCHAR*)Alloc((size_t)(Len + 1) * sizeof(TCHAR), alignof(TCHAR));
		if (Len > 0) std::memcpy(Dst, Src, (size_t)Len * sizeof(TCHAR));
		Dst[Len] = (TCHAR)0;
		return Dst;
	}

	// Block count (cheap arena-footprint proxy for --profile; exact byte tally not tracked per block).
	size_t BlockCount() const { return Blocks.size(); }

private:
	std::vector<char*> Blocks;
	char* Head = nullptr;
	size_t CurBlockSize = 0;
	size_t Offset = 0;          // bump offset within the current block
	size_t BlockSize;
};

// ---------------------------------------------------------------------------
// FArenaObj — insertion-ordered key -> node map, stored in the arena. Order is the load-bearing
// invariant. SetField semantics (pinned by design decision, mirrors legacy FOrderedValues::Set):
//   - new key    -> append to Order
//   - existing   -> overwrite value in place, Order position unchanged
// Lookup is linear over Order (JSON objects here are small: tens of keys); no separate hash map, which
// also removes the legacy 2-3x key duplication.
// ---------------------------------------------------------------------------
struct FArenaKV { const TCHAR* Key; int32_t KeyLen; FArenaNode* Value; };

struct FArenaObj
{
	FArena* Owner = nullptr;              // arena this object's storage lives in (for growing entries)
	FArenaKV* Entries = nullptr;
	int32_t Count = 0;
	int32_t Cap = 0;

	FArenaNode* Find(const TCHAR* Key, int32_t KeyLen) const
	{
		for (int32_t i = 0; i < Count; ++i)
			if (Entries[i].KeyLen == KeyLen &&
				(KeyLen == 0 || std::memcmp(Entries[i].Key, Key, (size_t)KeyLen * sizeof(TCHAR)) == 0))
				return Entries[i].Value;
		return nullptr;
	}

	int32_t IndexOf(const TCHAR* Key, int32_t KeyLen) const
	{
		for (int32_t i = 0; i < Count; ++i)
			if (Entries[i].KeyLen == KeyLen &&
				(KeyLen == 0 || std::memcmp(Entries[i].Key, Key, (size_t)KeyLen * sizeof(TCHAR)) == 0))
				return i;
		return -1;
	}

	// SetField: overwrite-in-place if the key exists (order unchanged), else append. Grows Entries
	// geometrically in the arena (old block is abandoned, not freed — arena reclaims on death).
	void Set(const TCHAR* Key, int32_t KeyLen, FArenaNode* Value)
	{
		const int32_t Idx = IndexOf(Key, KeyLen);
		if (Idx >= 0) { Entries[Idx].Value = Value; return; }   // in-place overwrite, order preserved
		if (Count == Cap)
		{
			const int32_t NewCap = Cap == 0 ? 8 : Cap * 2;
			FArenaKV* NewE = (FArenaKV*)Owner->Alloc(sizeof(FArenaKV) * (size_t)NewCap, alignof(FArenaKV));
			for (int32_t i = 0; i < Count; ++i) NewE[i] = Entries[i];
			Entries = NewE; Cap = NewCap;
		}
		Entries[Count].Key = Owner->CopyStr(Key, KeyLen);
		Entries[Count].KeyLen = KeyLen;
		Entries[Count].Value = Value;
		++Count;
	}

	// Remove a key (compacts Entries, preserving order of the rest). Returns whether it existed.
	bool Remove(const TCHAR* Key, int32_t KeyLen)
	{
		const int32_t Idx = IndexOf(Key, KeyLen);
		if (Idx < 0) return false;
		for (int32_t i = Idx; i + 1 < Count; ++i) Entries[i] = Entries[i + 1];
		--Count;
		return true;
	}
};

// ---------------------------------------------------------------------------
// FArenaNode — variant node. union keeps scalars inline (a Number/Bool costs no extra allocation);
// String points at an arena-copied buffer; Array/Object point at arena storage. No vtable, no refcount.
// EJson type tag is stored as uint8 (the enum's underlying type) to avoid an incomplete-enum dependency.
// ---------------------------------------------------------------------------
struct FArenaNode
{
	uint8_t Type = 0;   // == (uint8)EJson; EJson::Null default (see JsonDom.h enum order: None=0,Null=1,...)
	union
	{
		bool     B;
		double   N;
		struct { const TCHAR* Ptr; int32_t Len; } Str;
		struct { FArenaNode** Items; int32_t Count; } Arr;
		FArenaObj* Obj;
	};
	FArenaNode() : N(0.0) {}

	// Scalar coercions mirroring legacy FJsonValue::As*/TryGet* (no doc needed — no new handle produced).
	FString AsString() const
	{
		if (Type == ArenaEJson_String) return FString(Str.Ptr ? Str.Ptr : TEXT(""), Str.Len);
		if (Type == ArenaEJson_Number) return NumToStr(N);
		if (Type == ArenaEJson_Boolean) return B ? FString(TEXT("true")) : FString(TEXT("false"));
		return FString();
	}
	double AsNumber() const
	{
		if (Type == ArenaEJson_Number) return N;
		if (Type == ArenaEJson_String) return FCString::Atod(Str.Ptr ? Str.Ptr : TEXT(""));
		return 0.0;
	}
	bool AsBool() const
	{
		if (Type == ArenaEJson_Boolean) return B;
		if (Type == ArenaEJson_Number) return N != 0.0;
		return false;
	}
	bool IsNull() const { return Type == ArenaEJson_Null || Type == ArenaEJson_None; }

	bool TryGetString(FString& Out) const
	{
		if (Type == ArenaEJson_String || Type == ArenaEJson_Number || Type == ArenaEJson_Boolean) { Out = AsString(); return true; }
		return false;
	}
	bool TryGetNumber(double& Out) const
	{
		if (Type == ArenaEJson_Number) { Out = N; return true; }
		if (Type == ArenaEJson_String) { Out = FCString::Atod(Str.Ptr ? Str.Ptr : TEXT("")); return true; }
		return false;
	}
	bool TryGetBool(bool& Out) const
	{
		if (Type == ArenaEJson_Boolean) { Out = B; return true; }
		return false;
	}

private:
	// Enum constants are declared below in namespace ArenaEJson; use raw values here to stay above it.
	static constexpr uint8_t ArenaEJson_None=0, ArenaEJson_Null=1, ArenaEJson_String=2,
		ArenaEJson_Number=3, ArenaEJson_Boolean=4, ArenaEJson_Array=5, ArenaEJson_Object=6;
	static FString NumToStr(double D)
	{
		if (D == (double)(long long)D) return FString::Printf(TEXT("%lld"), (long long)D);
		return FString::Printf(TEXT("%.17g"), D);
	}
};

// ---------------------------------------------------------------------------
// FArenaDoc — owns one arena + the root node. Handles share ownership via shared_ptr so the arena
// outlives every handle. This is the single point of lifetime for a whole decoded tree.
// ---------------------------------------------------------------------------
class FArenaDoc
{
public:
	FArena Arena;
	FArenaNode* Root = nullptr;

	FArenaNode* NewNode() { return Arena.New<FArenaNode>(); }
	FArenaObj*  NewObj()  { FArenaObj* O = Arena.New<FArenaObj>(); O->Owner = &Arena; return O; }
};

// ---------------------------------------------------------------------------
// FJsonRef — handle: an arena node pointer + shared ownership of the owning doc. Replaces
// TSharedPtr<FJsonValue/Object> at call sites. Read-path usage subset (verified across the corpus):
// operator->, IsValid, copy, == nullptr. No weak_ptr, no use_count, never a map/set key.
// ---------------------------------------------------------------------------
struct FArenaView;   // accessor proxy returned by FJsonRef::operator-> (defined after the views)

template <class TNode>
struct FJsonRef
{
	TNode* Node = nullptr;
	TSharedPtr<FArenaDoc> Doc;   // keeps the arena alive while any handle exists

	FJsonRef() = default;
	FJsonRef(std::nullptr_t) {}
	FJsonRef(TNode* InNode, const TSharedPtr<FArenaDoc>& InDoc) : Node(InNode), Doc(InDoc) {}

	// operator-> and operator* both yield an accessor proxy (carries the doc) so both `handle->Field(...)`
	// and legacy `(*ptr)->Field(...)` (where the out-param used to be a pointer-to-handle) resolve to
	// doc-aware accessors. Defined out-of-line below (needs FArenaView complete).
	FArenaView operator->() const;
	FArenaView operator*() const;
	TNode* Get() const { return Node; }
	bool IsValid() const { return Node != nullptr; }
	explicit operator bool() const { return Node != nullptr; }
	bool operator==(std::nullptr_t) const { return Node == nullptr; }
	bool operator!=(std::nullptr_t) const { return Node != nullptr; }
};

// Call-site handle typedefs (step-4 replaces TSharedPtr<sj::FJsonObject/Value> with these). An "object"
// handle points at an FArenaNode of object type (its .Obj is the FArenaObj); a "value" handle at any node.
using FJsonObjectPtr = FJsonRef<FArenaNode>;
using FJsonValuePtr  = FJsonRef<FArenaNode>;

// ---------------------------------------------------------------------------
// FJsonArrayView — a first-class, zero-copy view over an arena array node's FArenaNode** storage.
// This is a permanent native API (std::span-class, not a compat shim): it exposes the arena's raw
// pointer array without materializing any TArray. TryGetArrayField/AsArray return it. Element access
// yields FJsonValuePtr (a handle sharing the doc). The self-returning operator->/* let call sites that
// hold a "pointer to array" (Objs->Num(), (*Objs)[0]) compile unchanged.
// ---------------------------------------------------------------------------
struct FJsonArrayView
{
	FArenaNode* const* Items = nullptr;
	int32_t Count = 0;
	TSharedPtr<FArenaDoc> Doc;   // shared so element handles outlive the view

	FJsonArrayView() = default;
	FJsonArrayView(std::nullptr_t) {}
	FJsonArrayView(FArenaNode* const* InItems, int32_t InCount, const TSharedPtr<FArenaDoc>& InDoc)
		: Items(InItems), Count(InCount), Doc(InDoc) {}

	int32_t Num() const { return Count; }
	bool IsValidIndex(int32_t i) const { return i >= 0 && i < Count; }
	bool IsValid() const { return Items != nullptr; }
	explicit operator bool() const { return Items != nullptr; }
	bool operator==(std::nullptr_t) const { return Items == nullptr; }
	bool operator!=(std::nullptr_t) const { return Items != nullptr; }

	FJsonValuePtr operator[](int32_t i) const { return FJsonValuePtr(Items[i], Doc); }

	// range-for producing FJsonValuePtr handles.
	struct FIter
	{
		FArenaNode* const* P;
		const TSharedPtr<FArenaDoc>* Doc;
		FJsonValuePtr operator*() const { return FJsonValuePtr(*P, *Doc); }
		FIter& operator++() { ++P; return *this; }
		bool operator!=(const FIter& O) const { return P != O.P; }
	};
	FIter begin() const { return FIter{ Items, &Doc }; }
	FIter end() const { return FIter{ Items + Count, &Doc }; }

	// Materialize an owning TArray of handles (for call sites that need to mutate/append then re-emit).
	TArray<FJsonValuePtr> ToArray() const
	{
		TArray<FJsonValuePtr> Out;
		for (int32_t i = 0; i < Count; ++i) Out.Add(FJsonValuePtr(Items[i], Doc));
		return Out;
	}

	// Self-return: existing call sites treat the out-param as `const TArray*` and write Objs->Num() /
	// (*Objs)[0]. These forward to the view itself, so those forms compile with no change.
	const FJsonArrayView* operator->() const { return this; }
	const FJsonArrayView& operator*() const { return *this; }
};

// ---------------------------------------------------------------------------
// FJsonKeyView — a lightweight view over an arena object key ({TCHAR*,len}). Implicitly converts to
// FString (one copy, paid only when a call site actually needs FString ops like StartsWith/Mid) and
// offers direct comparison so `key == TEXT("x")` costs nothing. Used by the object KV iterator.
// ---------------------------------------------------------------------------
struct FJsonKeyView
{
	const TCHAR* Ptr = nullptr;
	int32_t Len = 0;

	operator FString() const { return FString(Ptr ? Ptr : TEXT(""), Len); }

	bool operator==(const TCHAR* S) const
	{
		if (!Ptr) return S == nullptr || S[0] == (TCHAR)0;
		int32_t i = 0;
		for (; i < Len; ++i) { if (S[i] == (TCHAR)0 || S[i] != Ptr[i]) return false; }
		return S[i] == (TCHAR)0;
	}
	bool operator==(const FString& S) const { return *this == *S; }
	bool operator!=(const TCHAR* S) const { return !(*this == S); }
	bool operator!=(const FString& S) const { return !(*this == S); }
};

// ---------------------------------------------------------------------------
// FJsonObjectView — first-class KV iteration over an FArenaObj, producing { Key: FString, Value:
// FJsonValuePtr }. Replaces `for (auto& Pair : Obj->Values)`. Key is materialized as an FString per
// entry (call sites do Pair.Key.StartsWith/Mid/Contains/... freely); the value stays a zero-copy handle.
// ---------------------------------------------------------------------------
struct FJsonKV { FString Key; FJsonValuePtr Value; };

struct FJsonObjectView
{
	const FArenaObj* Obj = nullptr;
	TSharedPtr<FArenaDoc> Doc;

	FJsonObjectView() = default;
	FJsonObjectView(const FArenaObj* InObj, const TSharedPtr<FArenaDoc>& InDoc) : Obj(InObj), Doc(InDoc) {}

	int32_t Num() const { return Obj ? Obj->Count : 0; }

	struct FIter
	{
		const FArenaObj* Obj;
		int32_t Idx;
		const TSharedPtr<FArenaDoc>* Doc;
		FJsonKV operator*() const
		{
			const FArenaKV& E = Obj->Entries[Idx];
			return FJsonKV{ FString(E.Key ? E.Key : TEXT(""), E.KeyLen), FJsonValuePtr(E.Value, *Doc) };
		}
		FIter& operator++() { ++Idx; return *this; }
		bool operator!=(const FIter& O) const { return Idx != O.Idx; }
	};
	FIter begin() const { return FIter{ Obj, 0, &Doc }; }
	FIter end() const { return FIter{ Obj, Obj ? Obj->Count : 0, &Doc }; }
};

// ---------------------------------------------------------------------------
// FArenaView — the accessor proxy that FJsonRef::operator-> / operator* return. Carries the node and
// the owning doc, and exposes the full object/value read API (mirrors legacy FJsonObject/FJsonValue
// accessors) so call sites write `handle->TryGetObjectField(...)`, `handle->AsObject()`, `handle->Type`
// unchanged. Object-field accessors that yield a new handle/view thread the doc through. Self-returning
// operator-> supports the legacy `(*ptr)->field` form where ptr is a FJsonRef (its operator* returns
// this proxy).
// ---------------------------------------------------------------------------
struct FArenaView
{
	// FValuesProxy: range-for over the object's KV pairs; exposed as the member `Values` so call sites
	// keep writing `handle->Values`. Built lazily (see Values() accessor) to keep operator-> cheap.
	struct FValuesProxy
	{
		FJsonObjectView V;
		FJsonObjectView::FIter begin() const { return V.begin(); }
		FJsonObjectView::FIter end() const { return V.end(); }
		int32_t Num() const { return V.Num(); }
	};

	// Perf: the proxy borrows the owning handle's doc by pointer (the handle outlives the -> expression),
	// so operator-> costs no shared_ptr atomic. The doc is only copied into a shared_ptr when a NEW handle
	// is actually produced (AsObject/TryGet*/element access) — paid per-result, not per-navigation.
	FArenaNode* Node = nullptr;
	const TSharedPtr<FArenaDoc>* DocPtr = nullptr;
	EJson Type = EJson::Null;   // snapshot of Node->Type for `handle->Type == sj::EJson::X` call sites

	// `for (auto& Pair : handle->Values)` — Values borrows Obj + doc pointer (no shared_ptr copy).
	struct FValuesField
	{
		const FArenaObj* Obj = nullptr; const TSharedPtr<FArenaDoc>* Doc = nullptr;
		FJsonObjectView::FIter begin() const { return FJsonObjectView::FIter{ Obj, 0, Doc }; }
		FJsonObjectView::FIter end() const { return FJsonObjectView::FIter{ Obj, Obj ? Obj->Count : 0, Doc }; }
		int32_t Num() const { return Obj ? Obj->Count : 0; }
	};
	FValuesField Values;

	FArenaView() = default;
	FArenaView(FArenaNode* InNode, const TSharedPtr<FArenaDoc>& InDoc)
		: Node(InNode), DocPtr(&InDoc), Type(InNode ? (EJson)InNode->Type : EJson::Null)
	{
		Values.Obj = (InNode && InNode->Type == 6) ? InNode->Obj : nullptr;
		Values.Doc = &InDoc;
	}

	const TSharedPtr<FArenaDoc>& DocRef() const { static const TSharedPtr<FArenaDoc> Null; return DocPtr ? *DocPtr : Null; }

	const FArenaView* operator->() const { return this; }   // self-return for legacy (*ptr)->field

	// Handle-like surface so `(*Out).IsValid()`, `(*Out) == nullptr`, passing `H->AsObject()`-style
	// FArenaView results where a FJsonObjectPtr/FJsonValuePtr is expected, all compile unchanged.
	bool IsValid() const { return Node != nullptr; }
	explicit operator bool() const { return Node != nullptr; }
	bool operator==(std::nullptr_t) const { return Node == nullptr; }
	bool operator!=(std::nullptr_t) const { return Node != nullptr; }
	FArenaNode* Get() const { return Node; }
	operator FJsonObjectPtr() const { return FJsonObjectPtr(Node, DocRef()); }

	// Scalar coercions forward to the node (no doc needed).
	FString AsString() const { return Node ? Node->AsString() : FString(); }
	double AsNumber() const { return Node ? Node->AsNumber() : 0.0; }
	bool AsBool() const { return Node ? Node->AsBool() : false; }
	bool IsNull() const { return !Node || Node->IsNull(); }
	bool TryGetString(FString& Out) const { return Node && Node->TryGetString(Out); }
	bool TryGetNumber(double& Out) const { return Node && Node->TryGetNumber(Out); }
	bool TryGetBool(bool& Out) const { return Node && Node->TryGetBool(Out); }

	// AsObject: return a handle to this same object node (legacy returned the FJsonObject; here the
	// object handle is just this node, whose .Obj is the FArenaObj). Non-object -> invalid handle.
	FJsonObjectPtr AsObject() const
	{
		if (Node && Node->Type == 6 /*Object*/) return FJsonObjectPtr(Node, DocRef());
		return FJsonObjectPtr();
	}
	// AsArray: zero-copy view over the node's FArenaNode** storage.
	FJsonArrayView AsArray() const
	{
		if (Node && Node->Type == 5 /*Array*/) return FJsonArrayView(Node->Arr.Items, Node->Arr.Count, DocRef());
		return FJsonArrayView();
	}

	// ---- object-field accessors (valid when Node is an object) ----
	const FArenaObj* ObjPtr() const { return (Node && Node->Type == 6) ? Node->Obj : nullptr; }

	FArenaNode* FindField(const FString& Key) const
	{
		const FArenaObj* O = ObjPtr();
		return O ? O->Find(*Key, Key.Len()) : nullptr;
	}
	bool HasField(const FString& Key) const { return FindField(Key) != nullptr; }
	bool HasTypedField(const FString& Key, uint8_t InType) const
	{
		const FArenaNode* V = FindField(Key);
		return V && V->Type == InType;
	}

	FJsonValuePtr TryGetField(const FString& Key) const { return FJsonValuePtr(FindField(Key), DocRef()); }

	bool TryGetObjectField(const FString& Key, FJsonObjectPtr& Out) const
	{
		FArenaNode* V = FindField(Key);
		if (!V || V->Type != 6 /*Object*/) return false;
		Out = FJsonObjectPtr(V, DocRef());
		return true;
	}
	bool TryGetArrayField(const FString& Key, FJsonArrayView& Out) const
	{
		FArenaNode* V = FindField(Key);
		if (!V || V->Type != 5 /*Array*/) return false;
		Out = FJsonArrayView(V->Arr.Items, V->Arr.Count, DocRef());
		return true;
	}
	bool TryGetStringField(const FString& Key, FString& Out) const
	{
		FArenaNode* V = FindField(Key);
		if (!V || V->Type == 1 /*Null*/ || V->Type == 0) return false;
		return V->TryGetString(Out);
	}
	template <typename NumberT>
	bool TryGetNumberField(const FString& Key, NumberT& Out) const
	{
		FArenaNode* V = FindField(Key);
		if (!V || V->Type != 3 /*Number*/) return false;
		Out = (NumberT)V->N;
		return true;
	}
	bool TryGetBoolField(const FString& Key, bool& Out) const
	{
		FArenaNode* V = FindField(Key);
		if (!V || V->Type != 4 /*Boolean*/) return false;
		Out = V->B;
		return true;
	}

	FString GetStringField(const FString& Key) const { FArenaNode* V = FindField(Key); return V ? V->AsString() : FString(); }
	double GetNumberField(const FString& Key) const { FArenaNode* V = FindField(Key); return V ? V->AsNumber() : 0.0; }
	int32_t GetIntegerField(const FString& Key) const { FArenaNode* V = FindField(Key); return V ? (int32_t)V->AsNumber() : 0; }
	bool GetBoolField(const FString& Key) const { FArenaNode* V = FindField(Key); return V ? V->AsBool() : false; }

	// Object-level utilities (legacy FJsonObject names).
	bool Contains(const FString& Key) const { return FindField(Key) != nullptr; }
	int32_t Num() const { const FArenaObj* O = ObjPtr(); return O ? O->Count : 0; }
	void GetKeys(TArray<FString>& Out) const
	{
		const FArenaObj* O = ObjPtr();
		if (!O) return;
		for (int32_t i = 0; i < O->Count; ++i) Out.Add(FString(O->Entries[i].Key, O->Entries[i].KeyLen));
	}

	// Value-level TryGet (operate on THIS node, not a field): mirrors legacy FJsonValue::TryGetObject/Array.
	bool TryGetObject(FJsonObjectPtr& Out) const
	{
		if (!Node || Node->Type != 6 /*Object*/) return false;
		Out = FJsonObjectPtr(Node, DocRef());
		return true;
	}
	bool TryGetArray(FJsonArrayView& Out) const
	{
		if (!Node || Node->Type != 5 /*Array*/) return false;
		Out = FJsonArrayView(Node->Arr.Items, Node->Arr.Count, DocRef());
		return true;
	}

	// ---- write path: setters allocate new nodes from this handle's own arena (Doc) and insert into
	// this object node's FArenaObj (created lazily). Mirrors legacy FJsonObject::SetXxxField names so
	// call sites keep the same verb. ----
	FArenaObj* MutObj() const
	{
		if (!Node) return nullptr;
		if (Node->Type != 6 /*Object*/) { Node->Type = 6; Node->Obj = nullptr; }
		if (!Node->Obj) Node->Obj = DocRef()->NewObj();
		return Node->Obj;
	}
	void SetField(const FString& Key, const FJsonValuePtr& V) const { if (FArenaObj* O = MutObj()) O->Set(*Key, Key.Len(), V.Node); }
	void SetStringField(const FString& Key, const FString& S) const
	{
		FArenaObj* O = MutObj(); if (!O) return;
		FArenaNode* N = DocRef()->NewNode(); N->Type = 2 /*String*/;
		N->Str.Ptr = DocRef()->Arena.CopyStr(*S, S.Len()); N->Str.Len = S.Len();
		O->Set(*Key, Key.Len(), N);
	}
	void SetNumberField(const FString& Key, double NumV) const
	{
		FArenaObj* O = MutObj(); if (!O) return;
		FArenaNode* N = DocRef()->NewNode(); N->Type = 3 /*Number*/; N->N = NumV;
		O->Set(*Key, Key.Len(), N);
	}
	void SetBoolField(const FString& Key, bool BV) const
	{
		FArenaObj* O = MutObj(); if (!O) return;
		FArenaNode* N = DocRef()->NewNode(); N->Type = 4 /*Boolean*/; N->B = BV;
		O->Set(*Key, Key.Len(), N);
	}
	void SetObjectField(const FString& Key, const FJsonObjectPtr& Obj) const { if (FArenaObj* O = MutObj()) O->Set(*Key, Key.Len(), Obj.Node); }
	void SetArrayField(const FString& Key, const TArray<FJsonValuePtr>& Arr) const
	{
		FArenaObj* O = MutObj(); if (!O) return;
		FArenaNode* AN = DocRef()->NewNode(); AN->Type = 5 /*Array*/;
		const int32_t Cnt = (int32_t)Arr.Num();
		FArenaNode** Items = (FArenaNode**)DocRef()->Arena.Alloc(sizeof(FArenaNode*) * (size_t)(Cnt > 0 ? Cnt : 1), alignof(FArenaNode*));
		for (int32_t i = 0; i < Cnt; ++i) Items[i] = Arr[i].Node;
		AN->Arr.Items = Items; AN->Arr.Count = Cnt;
		O->Set(*Key, Key.Len(), AN);
	}
	void RemoveField(const FString& Key) const
	{
		if (Node && Node->Type == 6 && Node->Obj) Node->Obj->Remove(*Key, Key.Len());
	}
};

template <class TNode>
inline FArenaView FJsonRef<TNode>::operator->() const { return FArenaView(Node, Doc); }
template <class TNode>
inline FArenaView FJsonRef<TNode>::operator*() const { return FArenaView(Node, Doc); }

// ---------------------------------------------------------------------------
// FJsonDoc — write-path build handle. Wraps a shared FArenaDoc and is the single factory for new nodes:
// every object/value made through it lives in the same arena and its handles share the same doc, so a
// whole decoded tree is one arena (definition 1) with no cross-arena dangling. Replaces the legacy
// `MakeShared<sj::FJsonObject>()` / `MakeShared<sj::FJsonValueXxx>(...)` orphan-creation forms: those
// carried no arena; FJsonDoc threads the arena explicitly (definition 4, no implicit global).
// ---------------------------------------------------------------------------
class FJsonDoc
{
public:
	TSharedPtr<FArenaDoc> Doc;

	FJsonDoc() : Doc(MakeShared<FArenaDoc>()) {}
	explicit FJsonDoc(const TSharedPtr<FArenaDoc>& InDoc) : Doc(InDoc) {}

	bool IsValid() const { return Doc.IsValid(); }

	// Fresh empty object handle (replaces MakeShared<sj::FJsonObject>()).
	FJsonObjectPtr MakeObject() const
	{
		FArenaNode* N = Doc->NewNode(); N->Type = 6 /*Object*/; N->Obj = Doc->NewObj();
		return FJsonObjectPtr(N, Doc);
	}
	FJsonValuePtr MakeString(const FString& S) const
	{
		FArenaNode* N = Doc->NewNode(); N->Type = 2 /*String*/;
		N->Str.Ptr = Doc->Arena.CopyStr(*S, S.Len()); N->Str.Len = S.Len();
		return FJsonValuePtr(N, Doc);
	}
	FJsonValuePtr MakeNumber(double NumV) const
	{
		FArenaNode* N = Doc->NewNode(); N->Type = 3 /*Number*/; N->N = NumV;
		return FJsonValuePtr(N, Doc);
	}
	FJsonValuePtr MakeBool(bool BV) const
	{
		FArenaNode* N = Doc->NewNode(); N->Type = 4 /*Boolean*/; N->B = BV;
		return FJsonValuePtr(N, Doc);
	}
	FJsonValuePtr MakeNull() const
	{
		FArenaNode* N = Doc->NewNode(); N->Type = 1 /*Null*/;
		return FJsonValuePtr(N, Doc);
	}
	// Wrap an object handle as a value (replaces MakeShared<sj::FJsonValueObject>(Obj)): the object node
	// IS already a value node (Type Object), so this just re-types the handle.
	FJsonValuePtr MakeValueObject(const FJsonObjectPtr& Obj) const { return FJsonValuePtr(Obj.Node, Doc); }
	// Array value from a list of value handles (copies pointers into the arena).
	FJsonValuePtr MakeArray(const TArray<FJsonValuePtr>& Elems) const
	{
		FArenaNode* AN = Doc->NewNode(); AN->Type = 5 /*Array*/;
		const int32_t Cnt = (int32_t)Elems.Num();
		FArenaNode** Items = (FArenaNode**)Doc->Arena.Alloc(sizeof(FArenaNode*) * (size_t)(Cnt > 0 ? Cnt : 1), alignof(FArenaNode*));
		for (int32_t i = 0; i < Cnt; ++i) Items[i] = Elems[i].Node;
		AN->Arr.Items = Items; AN->Arr.Count = Cnt;
		return FJsonValuePtr(AN, Doc);
	}
	// Root anchoring for serialization.
	void SetRoot(const FJsonValuePtr& V) const { Doc->Root = V.Node; }
	FJsonObjectPtr RootObject() const { return FJsonObjectPtr(Doc->Root, Doc); }
};

// EJson underlying values (must match JsonDom.h enum order): None=0,Null=1,String=2,Number=3,Boolean=4,Array=5,Object=6.
namespace ArenaEJson { enum : uint8_t { None=0, Null=1, String=2, Number=3, Boolean=4, Array=5, Object=6 }; }

// ---------------------------------------------------------------------------
// Arena tree serializer — byte-for-byte replica of JsonSerializer.h detail::WriteValue (condensed +
// pretty), so an arena tree serializes identically to the legacy DOM. Number format, escaping, and
// object key order (insertion order via FArenaObj) all match. This is the write path (utxt output).
// ---------------------------------------------------------------------------
namespace arena_detail
{
	// Mirror NumberToJsonString (JsonDom.h): integral -> %lld, else %.17g.
	inline FString NumberToStr(double N)
	{
		if (N == (double)(long long)N) return FString::Printf(TEXT("%lld"), (long long)N);
		return FString::Printf(TEXT("%.17g"), N);
	}

	// Mirror detail::AppendEscaped (JsonSerializer.h).
	inline void AppendEscaped(FString& Out, const TCHAR* P, int32_t N)
	{
		Out += TEXT("\"");
		for (int32_t i = 0; i < N; ++i)
		{
			TCHAR c = P[i];
			switch (c)
			{
				case TCHAR('"'):  Out += TEXT("\\\""); break;
				case TCHAR('\\'): Out += TEXT("\\\\"); break;
				case TCHAR('\b'): Out += TEXT("\\b");  break;
				case TCHAR('\f'): Out += TEXT("\\f");  break;
				case TCHAR('\n'): Out += TEXT("\\n");  break;
				case TCHAR('\r'): Out += TEXT("\\r");  break;
				case TCHAR('\t'): Out += TEXT("\\t");  break;
				default:
					if ((uint32_t)c < 0x20u) Out += FString::Printf(TEXT("\\u%04x"), (uint32_t)c);
					else Out.AppendChar(c);
					break;
			}
		}
		Out += TEXT("\"");
	}

	inline void WriteIndent(FString& Out, bool bPretty, int Depth)
	{
		if (!bPretty) return;
		Out.AppendChar(TCHAR('\n'));
		for (int i = 0; i < Depth; ++i) Out.AppendChar(TCHAR('\t'));
	}

	inline void WriteNode(FString& Out, const FArenaNode* V, bool bPretty, int Depth)
	{
		if (!V) { Out += TEXT("null"); return; }
		switch (V->Type)
		{
			case ArenaEJson::Null:
			case ArenaEJson::None:
				Out += TEXT("null");
				break;
			case ArenaEJson::String:
				AppendEscaped(Out, V->Str.Ptr ? V->Str.Ptr : TEXT(""), V->Str.Len);
				break;
			case ArenaEJson::Number:
				Out += NumberToStr(V->N);
				break;
			case ArenaEJson::Boolean:
				Out += V->B ? TEXT("true") : TEXT("false");
				break;
			case ArenaEJson::Array:
			{
				Out.AppendChar(TCHAR('['));
				const int32_t Cnt = V->Arr.Count;
				for (int32_t i = 0; i < Cnt; ++i)
				{
					if (i) Out.AppendChar(TCHAR(','));
					WriteIndent(Out, bPretty, Depth + 1);
					WriteNode(Out, V->Arr.Items[i], bPretty, Depth + 1);
				}
				if (Cnt > 0) WriteIndent(Out, bPretty, Depth);
				Out.AppendChar(TCHAR(']'));
				break;
			}
			case ArenaEJson::Object:
			{
				Out.AppendChar(TCHAR('{'));
				const FArenaObj* Obj = V->Obj;
				bool bFirst = true;
				if (Obj)
				{
					for (int32_t i = 0; i < Obj->Count; ++i)
					{
						if (!bFirst) Out.AppendChar(TCHAR(','));
						bFirst = false;
						WriteIndent(Out, bPretty, Depth + 1);
						AppendEscaped(Out, Obj->Entries[i].Key, Obj->Entries[i].KeyLen);
						Out.AppendChar(TCHAR(':'));
						if (bPretty) Out.AppendChar(TCHAR(' '));
						WriteNode(Out, Obj->Entries[i].Value, bPretty, Depth + 1);
					}
					if (!bFirst) WriteIndent(Out, bPretty, Depth);
				}
				Out.AppendChar(TCHAR('}'));
				break;
			}
		}
	}
}

// Serialize an arena node tree to a string (condensed by default; bPretty = tab-indented).
inline FString SerializeArena(const FArenaNode* Root, bool bPretty = false)
{
	FString Out;
	arena_detail::WriteNode(Out, Root, bPretty, 0);
	return Out;
}

} // namespace jsondom
#endif // UNREAL_JSONARENA_H
