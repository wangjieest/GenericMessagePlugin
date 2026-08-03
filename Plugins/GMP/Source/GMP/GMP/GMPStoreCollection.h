//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPMacros.h"
#include "GMPStruct.h"
#include "UObject/UnrealType.h"
#include "tuplet/tuple.hpp"

struct FGMPStructUnion;

// Collection-typed StoreMessage: a stored message whose single parameter is a TArray<T> of USTRUCT elements is treated
// as a collection -- listeners can subscribe to the whole table, to every changed row, or to one fixed slot, and the
// element members can be read positionally without depending on T.

namespace GMP
{
// One contiguous span of changed elements. Index < 0 means "reload everything".
struct FGMPStoreRange
{
	int32 Index = INDEX_NONE;
	int32 Count = 0;
};

// Describes one fire of a collection: the count dimension (TotalCount) and the index dimension (Ranges).
// Add/Change/Remove are not encoded -- a listener derives them from TotalCount together with Range.Index.
struct FGMPStoreUpdate
{
	int32 TotalCount = 0;
	TArrayView<const FGMPStoreRange> Ranges;
	// Row-callback only: >=0 is the current row, <0 means that row is gone and ~Row was its index. INDEX_NONE for
	// whole-table callbacks. The same ~ encoding is used by the subscribe-side Index, see GMPListenStore.
	int32 Row = INDEX_NONE;
	// Row count before this change, so a slot listener can tell "my row vanished" from "my row never existed".
	int32 PrevTotalCount = 0;
	// Set per listener from the sign of its subscribed Index: when on, a row walk also reports the rows that are gone.
	bool bIncludeRemoved = false;

	bool IsFullReload() const { return Ranges.Num() == 0 || Ranges[0].Index < 0; }
	bool IsRowRemoved() const { return Row < 0; }
	int32 GetRow() const { return Row >= 0 ? Row : ~Row; }
};

// Element members that participate in positional access. Editor-only properties are skipped so the member sequence is
// identical in editor and shipping -- otherwise a positional lambda would read a different field per configuration.
GMP_API void GMPCollectElementProps(const UScriptStruct* ElemStruct, TArray<const FProperty*>& OutProps);

// A borrowed view over the stored table, valid only for the duration of the callback.
struct GMP_API FGMPStoreView
{
	FGMPStoreView() = default;
	FGMPStoreView(const FScriptArray* InArray, const FProperty* InElemProp, const UScriptStruct* InElemStruct, int32 InElemSize)
		: ArrayPtr(InArray)
		, ElemProp(InElemProp)
		, ElemStruct(InElemStruct)
		, ElemSize(InElemSize)
	{
	}

	bool IsValid() const { return ArrayPtr && ElemStruct && ElemSize > 0; }
	int32 Num() const { return ArrayPtr ? ArrayPtr->Num() : 0; }
	const UScriptStruct* GetElementStruct() const { return ElemStruct; }
	// The array's inner property -- lets a whole row be handed over as one typed address.
	const FProperty* GetElementProp() const { return ElemProp; }
	int32 GetElementSize() const { return ElemSize; }

	const uint8* ElemAt(int32 Row) const
	{
		return (ArrayPtr && Row >= 0 && Row < ArrayPtr->Num()) ? (const uint8*)ArrayPtr->GetData() + int64(Row) * ElemSize : nullptr;
	}

	// Expands one row into a lambda taking the element members positionally, no element type needed.
	template<typename F>
	void VisitRow(int32 Row, F&& Lambda) const;

	// Walks the whole table; the lambda starts with the int32 row index. Takes a compiled-offset shortcut when the
	// declared arguments line up with the element layout, and falls back to reflection otherwise.
	template<typename F>
	void ForEach(F&& Lambda) const;

	// Strongly-typed shortcut; the caller must own the element type. An empty table stands in for a missing store.
	template<typename T>
	const TArray<T>& As() const
	{
		static const TArray<T> EmptyTable;
		if (!ArrayPtr)
			return EmptyTable;
		checkSlow(ElemStruct == ::StaticScriptStruct<T>());
		return *reinterpret_cast<const TArray<T>*>(ArrayPtr);
	}

private:
	const FScriptArray* ArrayPtr = nullptr;
	const FProperty* ElemProp = nullptr;
	const UScriptStruct* ElemStruct = nullptr;
	int32 ElemSize = 0;
};

// Resolves the stored union into a collection view; returns an invalid view when the store is not a
// single-TArray-of-USTRUCT parameter.
GMP_API FGMPStoreView GMPMakeStoreView(const FGMPStructUnion* InUnion);

// Cached per-element-struct positional layout: the non-editor-only members in declaration order. Everything a row
// expansion needs is resolved once here, so building a row costs an add per member and nothing else.
struct GMP_API FGMPElementLayout
{
	TArray<const FProperty*> Props;
	TArray<FName> TypeNames;  // GMP type name per member, for signature checking
	TArray<int32> Offsets;    // byte offset of each member inside the element

	// A default-constructed element standing in for a row that is gone. Lives with the cached layout instead of being
	// rebuilt per fire -- for an element holding FString/TArray that would be a heap round-trip on every removal.
	const uint8* GetDefaultRow() const;

	~FGMPElementLayout();
	static const FGMPElementLayout& Get(const UScriptStruct* ElemStruct);

private:
	// Weak: a discarded struct must not be destroyed through, nor a recycled address taken as a cache hit.
	TWeakObjectPtr<const UScriptStruct> Struct;
	mutable TArray<uint8> DefaultBytes;  // built on first use: most keys never see a removal
};

// Builds the paddrs of one row so it can be expanded into lambda arguments by the existing listen machinery.
// Hoist the layout out of a per-row loop and call the first form; the second is the convenience wrapper.
GMP_API void GMPBuildRowAddrs(const FGMPElementLayout& Layout, const uint8* RowPtr, FTypedAddresses& OutAddrs);
GMP_API void GMPBuildRowAddrs(const FGMPStoreView& View, int32 Row, FTypedAddresses& OutAddrs);

// Builds the (int32 Row, <element>) pair for a script row callback; OutRowValue must outlive the call.
GMP_API void GMPBuildScriptRowArgs(const FGMPStoreView& View, int32 Row, int32& OutRowValue, FTypedAddresses& OutArgs);

// The one row-listening entry for script backends and blueprint; returns the listener key that owns the lifetime.
GMP_API FGMPKey GMPListenScriptRows(FSigSource InSigSrc, const FName& Key, const UObject* Listener, int32 Index, TFunction<void(const FGMPTypedAddr*, int32, const UScriptStruct*)>&& OnRow, FGMPListenOptions Options = {});

// Accumulates changed spans while a writer is alive; merges adjacent/overlapping spans, degrades to full-reload when
// the span count grows past the threshold.
struct GMP_API FGMPRangeAccum
{
	enum { MaxSpans = 8 };

	void Add(int32 Index, int32 Count);
	void MarkFullReload() { bFull = true; Spans.Reset(); }
	bool IsEmpty() const { return !bFull && Spans.Num() == 0; }
	TArrayView<const FGMPStoreRange> Get() const;

private:
	// Sorts and coalesces touching spans; a grown span can bridge others, and overlapping spans would revisit rows.
	void Normalize();

	TArray<FGMPStoreRange, TInlineAllocator<MaxSpans>> Spans;
	mutable FGMPStoreRange FullSpan{INDEX_NONE, 0};
	bool bFull = false;
};

// Resolves the writable TArray inside the store; null when the store is absent or not a collection.
// EnsureUnique is applied so an in-place edit never leaks through a shared view.
GMP_API FScriptArray* GMPResolveStoredArrayForWrite(FSigSource InSigSrc, const FName& Key, const UScriptStruct*& OutElemStruct, int32& OutElemSize);

// Publishes the accumulated change set for a collection key.
GMP_API void GMPNotifyStoreUpdate(FSigSource InSigSrc, const FName& Key, int32 TotalCount, TArrayView<const FGMPStoreRange> Ranges);

// What a collection listener receives: the borrowed table plus what moved in this fire.
using FGMPStoreCallback = TFunction<void(const FGMPStoreView&, const FGMPStoreUpdate&)>;

// Subscribe-side Index encoding, symmetric with FGMPStoreUpdate::Row: Index >= 0 is row Index without removal
// notices, Index < 0 is row ~Index with them. A row of MAX_int32 stands for "every row", so ~MAX_int32 == MIN_int32
// is the with-removal form of it -- the two whole-table shapes fall out of the same rule instead of needing their
// own codes.
inline constexpr int32 AllRows = MAX_int32;
inline constexpr int32 AllRowsWithRemoval = ~MAX_int32;
inline constexpr int32 WithRemoval(int32 Row) { return ~Row; }

struct FGMPStoreListenSpec
{
	int32 Row = AllRows;       // MAX_int32 == every row
	bool bWantRemoval = false;

	static FGMPStoreListenSpec Decode(int32 Index) { return {Index >= 0 ? Index : ~Index, Index < 0}; }
	bool IsWholeTable() const { return Row == AllRows; }
};

// Row is the decoded form (AllRows == whole table); pass the raw Index through FGMPStoreListenSpec::Decode first. An already stored table is replayed at once so a
// late listener starts in sync. The returned key and the listener object both work for unlistening.
// LifeKey ties the entry to an ordinary GMP listener: while it is set, the entry lives exactly as long as that
// listener, which is how a non-UObject (FSigCollection) listener gets the same automatic teardown.
GMP_API FGMPKey GMPListenStore(FSigSource InSigSrc, const FName& Key, const UObject* Listener, int32 Row, FGMPStoreCallback&& Callback, FGMPKey LifeKey = FGMPKey{});
GMP_API void GMPUnlistenStore(const FName& Key, const UObject* Listener);
GMP_API void GMPUnlistenStore(const FName& Key, FGMPKey ListenKey);
GMP_API bool GMPHasStoreListeners();

// Wakes the listeners of Key. Whole-table listeners always fire; a slot listener fires only when its row now holds
// different content -- which includes rows shifted by an insertion or a removal earlier in the table.
GMP_API void GMPDispatchStoreUpdate(FSigSource InSigSrc, const FName& Key, const FGMPStoreUpdate& Update);

// Visits every row the update touches, ascending. Existing rows come first as their own index; when the listener asked
// for removals, the rows that the change dropped follow as ~Row so the visitor can tell them apart.
GMP_API void GMPForEachChangedRow(const FGMPStoreView& View, const FGMPStoreUpdate& Update, TFunctionRef<void(int32)> Visitor);

// What a full-table write turned out to change. Computed against the old store before it is overwritten, published
// after -- so a plain StoreObjectMessage of a TArray also drives the collection listeners.
struct FGMPStoreDiff
{
	TArray<FGMPStoreRange, TInlineAllocator<FGMPRangeAccum::MaxSpans>> Spans;
	int32 TotalCount = 0;
	bool bPublish = false;
};
GMP_API void GMPComputeStoreDiff(const FName& Key, const FGMPStructUnion* OldUnion, const FGMPPropStackRefArray& NewParams, FGMPStoreDiff& OutDiff);
GMP_API void GMPPublishStoreDiff(FSigSource InSigSrc, const FName& Key, const FGMPStoreDiff& Diff);

// Rows off a plain SendObjectMessage: the table is the sender's own argument, so it is gone when the call returns.
// Without a previous table there is no change set, so this only ever reports a full reload and only reaches AllRows
// listeners -- a slot listener would have no way to tell whether its own row moved.
GMP_API void GMPDispatchTransientRows(FSigSource InSigSrc, const FName& Key, const FGMPPropStackRefArray& Params);

// Writer over a stored TArray<T>: use it like a TArray; the accumulated ranges are published once on destruction.
// Stack-only by design -- a heap/member instance would defer or lose the notification.
template<typename T>
class TGMPStoredArray
{
public:
	TGMPStoredArray(FSigSource InSigSrc, const FName& InKey)
		: SigSrc(InSigSrc)
		, Key(InKey)
	{
		const UScriptStruct* ElemStruct = nullptr;
		int32 ElemSize = 0;
		FScriptArray* Raw = GMPResolveStoredArrayForWrite(SigSrc, Key, ElemStruct, ElemSize);
		if (!ensureMsgf(Raw, TEXT("GMP: no collection stored under '%s'; StoreObjectMessage a TArray first"), *Key.ToString()))
			return;
		// Reinterpreting a table of another element type would write through a mismatched layout, so refuse it outright.
		if (!ensureMsgf(ElemStruct == ::StaticScriptStruct<T>() && ElemSize == sizeof(T), TEXT("GMP: '%s' holds %s, not %s"), *Key.ToString(), *GetNameSafe(ElemStruct), *GetNameSafe(::StaticScriptStruct<T>())))
			return;
		ArrayPtr = reinterpret_cast<TArray<T>*>(Raw);
	}
	~TGMPStoredArray() { Notify(); }

	bool IsValid() const { return ArrayPtr != nullptr; }
	int32 Num() const { return ArrayPtr ? ArrayPtr->Num() : 0; }
	const T& operator[](int32 i) const { return ArrayPtr ? (*ArrayPtr)[i] : Sink(); }
	const T* begin() const { return ArrayPtr ? ArrayPtr->GetData() : nullptr; }
	const T* end() const { return begin() + Num(); }

	// Explicit mutable access -- a non-const operator[] could not tell whether the caller actually wrote.
	T& GetMutable(int32 i)
	{
		if (!ArrayPtr)
			return Sink();
		Accum.Add(i, 1);
		return (*ArrayPtr)[i];
	}
	int32 Add(const T& Item)
	{
		if (!ArrayPtr)
			return INDEX_NONE;
		const int32 i = ArrayPtr->Add(Item);
		Accum.Add(i, 1);
		return i;
	}
	template<typename... TArgs>
	int32 Emplace(TArgs&&... Args)
	{
		if (!ArrayPtr)
			return INDEX_NONE;
		const int32 i = ArrayPtr->Emplace(Forward<TArgs>(Args)...);
		Accum.Add(i, 1);
		return i;
	}
	void Insert(const T& Item, int32 i)
	{
		if (!ArrayPtr)
			return;
		ArrayPtr->Insert(Item, i);
		Accum.Add(i, 1);
	}
	void RemoveAt(int32 i, int32 Count = 1)
	{
		if (!ArrayPtr)
			return;
		ArrayPtr->RemoveAt(i, Count);
		Accum.Add(i, Count);
	}
	void SetNum(int32 N)
	{
		if (!ArrayPtr)
			return;
		const int32 Old = ArrayPtr->Num();
		ArrayPtr->SetNum(N);
		Accum.Add(FMath::Min(Old, N), FMath::Abs(N - Old));
	}
	void Empty()
	{
		if (!ArrayPtr)
			return;
		ArrayPtr->Empty();
		Accum.MarkFullReload();
	}

	// Publishes now instead of on destruction.
	void Notify()
	{
		if (!ArrayPtr || Accum.IsEmpty())
			return;
		GMPNotifyStoreUpdate(SigSrc, Key, ArrayPtr->Num(), Accum.Get());
		Accum = FGMPRangeAccum{};
	}

private:
	// An unresolved store keeps every accessor a no-op instead of dereferencing null.
	static T& Sink()
	{
		static thread_local T Unused;
		return Unused;
	}

	FSigSource SigSrc;
	FName Key;
	TArray<T>* ArrayPtr = nullptr;
	FGMPRangeAccum Accum;

	TGMPStoredArray(const TGMPStoredArray&) = delete;
	TGMPStoredArray(TGMPStoredArray&&) = delete;
	TGMPStoredArray& operator=(const TGMPStoredArray&) = delete;
	static void* operator new(size_t) = delete;
	static void* operator new[](size_t) = delete;
};

// Turns a user lambda into an FGMPStoreCallback. Which shape it has is read off its signature: a trailing
// `const FGMPStoreUpdate&` marks the collection form, and the subscription row decides how the leading arguments are
// filled -- whole table (view or typed array), one slot, or once per changed row.
namespace Collection
{
	template<typename T>
	struct TIsArrayArg : std::false_type
	{
		using ElementType = void;
	};
	template<typename T>
	struct TIsArrayArg<TArray<T>> : std::true_type
	{
		using ElementType = T;
	};

	template<typename F>
	struct TListenTraits
	{
		using Sig = TypeTraits::TSigTraits<std::decay_t<F>>;
		using Tuple = typename Sig::Tuple;
		enum
		{
			TupleSize = Sig::TupleSize,
			bTakesUpdate = std::is_same<FGMPStoreUpdate, std::decay_t<typename Sig::LastType>>::value,
			NumData = TupleSize - (bTakesUpdate ? 1 : 0),
		};
	};

	template<typename Tuple, size_t Offset, typename F, size_t... Is>
	void CallRow(const F& Func, const FGMPTypedAddr* Addrs, std::index_sequence<Is...>*)
	{
		Func(Addrs[Is].template GetParam<std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>>()...);
	}
	template<typename Tuple, size_t Offset, typename F, size_t... Is>
	void CallRow(const F& Func, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate& Update, std::index_sequence<Is...>*)
	{
		Func(Addrs[Is].template GetParam<std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>>()..., Update);
	}
	template<typename Tuple, size_t Offset, typename F, size_t... Is>
	void CallRowIndexed(const F& Func, int32 Row, const FGMPTypedAddr* Addrs, std::index_sequence<Is...>*)
	{
		Func(Row, Addrs[Is].template GetParam<std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>>()...);
	}
	template<typename Tuple, size_t Offset, typename F, size_t... Is>
	void CallRowIndexed(const F& Func, int32 Row, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate& Update, std::index_sequence<Is...>*)
	{
		Func(Row, Addrs[Is].template GetParam<std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>>()..., Update);
	}

	template<typename Tuple, size_t Offset, typename Seq, typename F>
	void InvokeRow(const F& Func, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate& Update, std::true_type)
	{
		CallRow<Tuple, Offset>(Func, Addrs, Update, (Seq*)nullptr);
	}
	template<typename Tuple, size_t Offset, typename Seq, typename F>
	void InvokeRow(const F& Func, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate&, std::false_type)
	{
		CallRow<Tuple, Offset>(Func, Addrs, (Seq*)nullptr);
	}
	template<typename Tuple, size_t Offset, typename Seq, typename F>
	void InvokeRowIndexed(const F& Func, int32 Row, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate& Update, std::true_type)
	{
		CallRowIndexed<Tuple, Offset>(Func, Row, Addrs, Update, (Seq*)nullptr);
	}
	template<typename Tuple, size_t Offset, typename Seq, typename F>
	void InvokeRowIndexed(const F& Func, int32 Row, const FGMPTypedAddr* Addrs, const FGMPStoreUpdate&, std::false_type)
	{
		CallRowIndexed<Tuple, Offset>(Func, Row, Addrs, (Seq*)nullptr);
	}

	template<typename Tuple, int32 NumData>
	struct TWholeFirstArg
	{
		using Type = void;
	};
	template<typename Tuple>
	struct TWholeFirstArg<Tuple, 1>
	{
		using Type = std::decay_t<std::tuple_element_t<0, Tuple>>;
	};

	// Whole-table shapes, picked by the argument in front of the update.
	template<typename FirstArg>
	struct TWholeDispatch
	{
		static_assert(TIsArrayArg<FirstArg>::value, "a whole-table collection lambda takes FGMPStoreView, const TArray<T>&, or nothing before the update");
		template<typename F>
		static FGMPStoreCallback Make(F&& Func)
		{
			using ElementType = typename TIsArrayArg<FirstArg>::ElementType;
			return [Fn{std::forward<F>(Func)}](const FGMPStoreView& View, const FGMPStoreUpdate& Update) { Fn(View.template As<ElementType>(), Update); };
		}
	};
	template<>
	struct TWholeDispatch<void>
	{
		template<typename F>
		static FGMPStoreCallback Make(F&& Func)
		{
			return [Fn{std::forward<F>(Func)}](const FGMPStoreView&, const FGMPStoreUpdate& Update) { Fn(Update); };
		}
	};
	template<>
	struct TWholeDispatch<FGMPStoreView>
	{
		template<typename F>
		static FGMPStoreCallback Make(F&& Func)
		{
			return [Fn{std::forward<F>(Func)}](const FGMPStoreView& View, const FGMPStoreUpdate& Update) { Fn(View, Update); };
		}
	};

	// The message parameter type a whole-table lambda names, so listening keeps binding the tag to its type the way
	// every other listen does. The type-agnostic shapes name nothing and report nothing.
	template<typename FirstArg>
	struct TTableNamesOf
	{
		static const FArrayTypeNames* Get() { return nullptr; }
	};
	template<typename T>
	struct TTableNamesOf<TArray<T>>
	{
		static const FArrayTypeNames* Get() { return &FMessageBody::MakeStaticNamesImpl<TArray<T>>(); }
	};

	// ---- compiled-offset row access -------------------------------------------------------------------------------
	// The reflection path costs one lookup per member per row, which shows up when walking a large table. When the
	// declared arguments happen to match the element layout exactly, the row can be read as a flat tuple instead.
	// Correctness never depends on it: a failed check just takes the reflection path.
	template<typename Tuple, size_t Offset, typename Seq>
	struct TFastTupleOf;
	template<typename Tuple, size_t Offset, size_t... Is>
	struct TFastTupleOf<Tuple, Offset, std::index_sequence<Is...>>
	{
		using Type = tuplet::tuple<std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>...>;
	};

	template<typename FastTuple, size_t I>
	SIZE_T TupleElemOffset()
	{
		alignas(FastTuple) static const uint8 Probe[sizeof(FastTuple)] = {};
		const FastTuple& Ref = *reinterpret_cast<const FastTuple*>(&Probe[0]);
		return SIZE_T((const uint8*)&Ref[tuplet::tag<I>{}] - &Probe[0]);
	}

	template<typename FastTuple, size_t I, typename T>
	bool MemberMatchesTuple(const FGMPElementLayout& Layout)
	{
		const FProperty* Prop = Layout.Props[I];
		// same place, same width, same type: anything else and the reinterpret would read a different field
		return TupleElemOffset<FastTuple, I>() == SIZE_T(Prop->GetOffset_ForInternal()) && int32(sizeof(T)) == Prop->GetSize() && Layout.TypeNames[I] == Reflection::GetPropertyName<T>();
	}

	// Nothing declared to match against, so there is no shortcut to take.
	template<typename FastTuple, typename Tuple, size_t Offset>
	bool TupleLayoutMatches(const FGMPElementLayout&, std::index_sequence<>*)
	{
		return false;
	}

	template<typename FastTuple, typename Tuple, size_t Offset, size_t... Is>
	bool TupleLayoutMatches(const FGMPElementLayout& Layout, std::index_sequence<Is...>*)
	{
		// A lambda may declare fewer members than the element has, so only the declared prefix has to line up.
		if (Layout.Props.Num() < int32(sizeof...(Is)))
			return false;
		const bool Checks[] = {true, MemberMatchesTuple<FastTuple, Is, std::decay_t<std::tuple_element_t<Is + Offset, Tuple>>>(Layout)...};
		for (bool bOk : Checks)
		{
			if (!bOk)
				return false;
		}
		return true;
	}

	template<typename FastTuple, typename F, size_t... Is>
	void CallFastRow(const F& Func, const uint8* RowPtr, std::index_sequence<Is...>*)
	{
		const FastTuple& Val = *reinterpret_cast<const FastTuple*>(RowPtr);
		Func(Val[tuplet::tag<Is>{}]...);
	}
	template<typename FastTuple, typename F, size_t... Is>
	void CallFastRowIndexed(const F& Func, int32 Row, const uint8* RowPtr, std::index_sequence<Is...>*)
	{
		const FastTuple& Val = *reinterpret_cast<const FastTuple*>(RowPtr);
		Func(Row, Val[tuplet::tag<Is>{}]...);
	}
	template<typename FastTuple, typename F, size_t... Is>
	void CallFastRow(const F& Func, const uint8* RowPtr, const FGMPStoreUpdate& Update, std::index_sequence<Is...>*)
	{
		const FastTuple& Val = *reinterpret_cast<const FastTuple*>(RowPtr);
		Func(Val[tuplet::tag<Is>{}]..., Update);
	}
	template<typename FastTuple, typename F, size_t... Is>
	void CallFastRowIndexed(const F& Func, int32 Row, const uint8* RowPtr, const FGMPStoreUpdate& Update, std::index_sequence<Is...>*)
	{
		const FastTuple& Val = *reinterpret_cast<const FastTuple*>(RowPtr);
		Func(Row, Val[tuplet::tag<Is>{}]..., Update);
	}

	template<typename FastTuple, typename Seq, typename F>
	void InvokeFastRow(const F& Func, const uint8* RowPtr, const FGMPStoreUpdate& Update, std::true_type)
	{
		CallFastRow<FastTuple>(Func, RowPtr, Update, (Seq*)nullptr);
	}
	template<typename FastTuple, typename Seq, typename F>
	void InvokeFastRow(const F& Func, const uint8* RowPtr, const FGMPStoreUpdate&, std::false_type)
	{
		CallFastRow<FastTuple>(Func, RowPtr, (Seq*)nullptr);
	}
	template<typename FastTuple, typename Seq, typename F>
	void InvokeFastRowIndexed(const F& Func, int32 Row, const uint8* RowPtr, const FGMPStoreUpdate& Update, std::true_type)
	{
		CallFastRowIndexed<FastTuple>(Func, Row, RowPtr, Update, (Seq*)nullptr);
	}
	template<typename FastTuple, typename Seq, typename F>
	void InvokeFastRowIndexed(const F& Func, int32 Row, const uint8* RowPtr, const FGMPStoreUpdate&, std::false_type)
	{
		CallFastRowIndexed<FastTuple>(Func, Row, RowPtr, (Seq*)nullptr);
	}

	// The element layout of a key does not change between fires, so a row listener decides once whether it can read
	// rows at compiled offsets and remembers the answer.
	struct FRowFastCache
	{
		const UScriptStruct* DecidedFor = nullptr;
		bool bFast = false;
	};

	// Row >= 0: the members of that one slot.
	template<typename F>
	FGMPStoreCallback MakeSlotCallback(int32 Row, F&& Func)
	{
		using Traits = TListenTraits<F>;
		using Tuple = typename Traits::Tuple;
		using Seq = std::make_index_sequence<Traits::NumData>;
		using FastTuple = typename TFastTupleOf<Tuple, 0, Seq>::Type;
		using HasUpdate = std::integral_constant<bool, !!Traits::bTakesUpdate>;
		return [Row, Fn{std::forward<F>(Func)}, Cache{MakeShared<FRowFastCache>()}](const FGMPStoreView& View, const FGMPStoreUpdate& Update) {
			// A vanished slot reads the default stand-in; the callback branches on Update.IsRowRemoved().
			const uint8* RowPtr = Update.IsRowRemoved() ? FGMPElementLayout::Get(View.GetElementStruct()).GetDefaultRow() : View.ElemAt(Row);
			if (!RowPtr)
				return;
			if (Cache->DecidedFor != View.GetElementStruct())
			{
				Cache->DecidedFor = View.GetElementStruct();
				Cache->bFast = TupleLayoutMatches<FastTuple, Tuple, 0>(FGMPElementLayout::Get(Cache->DecidedFor), (Seq*)nullptr);
			}
			if (Cache->bFast)
			{
				InvokeFastRow<FastTuple, Seq>(Fn, RowPtr, Update, HasUpdate{});
				return;
			}
			FTypedAddresses Addrs;
			GMPBuildRowAddrs(FGMPElementLayout::Get(View.GetElementStruct()), RowPtr, Addrs);
			if (ensure(Addrs.Num() >= int32(Traits::NumData)))
				InvokeRow<Tuple, 0, Seq>(Fn, Addrs.GetData(), Update, HasUpdate{});
		};
	}

	// Row < 0 with a leading int32: one call per changed row.
	template<typename F>
	FGMPStoreCallback MakeEveryRowCallback(F&& Func)
	{
		using Traits = TListenTraits<F>;
		using Tuple = typename Traits::Tuple;
		using Seq = std::make_index_sequence<Traits::NumData - 1>;
		using FastTuple = typename TFastTupleOf<Tuple, 1, Seq>::Type;
		using HasUpdate = std::integral_constant<bool, !!Traits::bTakesUpdate>;
		return [Fn{std::forward<F>(Func)}, Cache{MakeShared<FRowFastCache>()}](const FGMPStoreView& View, const FGMPStoreUpdate& Update) {
			if (Cache->DecidedFor != View.GetElementStruct())
			{
				Cache->DecidedFor = View.GetElementStruct();
				Cache->bFast = TupleLayoutMatches<FastTuple, Tuple, 1>(FGMPElementLayout::Get(Cache->DecidedFor), (Seq*)nullptr);
			}
			// A removed row (~Row) has no element left to read, so it is handed the layout's default stand-in and the
			// callback is expected to branch on Update.IsRowRemoved() before touching the members.
			const FGMPElementLayout& Layout = FGMPElementLayout::Get(View.GetElementStruct());
			auto RowMemory = [&](int32 Row) -> const uint8* { return Row >= 0 ? View.ElemAt(Row) : Layout.GetDefaultRow(); };
			if (Cache->bFast)
			{
				GMPForEachChangedRow(View, Update, [&](int32 Row) { InvokeFastRowIndexed<FastTuple, Seq>(Fn, Row, RowMemory(Row), Update, HasUpdate{}); });
				return;
			}
			GMPForEachChangedRow(View, Update, [&](int32 Row) {
				FTypedAddresses Addrs;
				GMPBuildRowAddrs(Layout, RowMemory(Row), Addrs);
				if (ensure(Addrs.Num() >= int32(Traits::NumData) - 1))
					InvokeRowIndexed<Tuple, 1, Seq>(Fn, Row, Addrs.GetData(), Update, HasUpdate{});
			});
		};
	}

	template<typename F>
	FGMPStoreCallback MakeWholeCallback(F&& Func)
	{
		using Traits = TListenTraits<F>;
		static_assert(Traits::bTakesUpdate, "a whole-table collection lambda ends with const FGMPStoreUpdate&");
		static_assert(Traits::NumData <= 1, "a whole-table collection lambda takes at most one argument before the update");
		using FirstArg = typename TWholeFirstArg<typename Traits::Tuple, Traits::NumData>::Type;
		return TWholeDispatch<FirstArg>::Make(std::forward<F>(Func));
	}

	// Only meaningful for the whole-table form: a row lambda's leading argument is a row member, not the table.
	template<typename F>
	const FArrayTypeNames* WholeTableNames()
	{
		using Traits = TListenTraits<F>;
		using FirstArg = typename TWholeFirstArg<typename Traits::Tuple, Traits::NumData>::Type;
		return TTableNamesOf<FirstArg>::Get();
	}


	// Index decodes to a row plus whether removals are wanted (see FGMPStoreListenSpec); a row of AllRows is the
	// whole table. Asking for removals without taking the update would silently deliver a default-constructed row as
	// if it were real data, so that combination is rejected.
	template<typename F>
	std::enable_if_t<(TListenTraits<F>::NumData >= 1), FGMPStoreCallback> MakeRowCallback(int32 Index, F&& Func)
	{
		const FGMPStoreListenSpec Spec = FGMPStoreListenSpec::Decode(Index);
		checkf(!Spec.bWantRemoval || TListenTraits<F>::bTakesUpdate,
			   TEXT("GMP: subscribing with removals (Index < 0) needs a trailing const FGMPStoreUpdate& to tell a removed row from real data"));
		return Spec.IsWholeTable() ? MakeEveryRowCallback(std::forward<F>(Func)) : MakeSlotCallback(Spec.Row, std::forward<F>(Func));
	}
	// Nothing to expand from the row: the lambda only wants to hear that the table moved.
	template<typename F>
	std::enable_if_t<(TListenTraits<F>::NumData == 0), FGMPStoreCallback> MakeRowCallback(int32, F&& Func)
	{
		return MakeWholeCallback(std::forward<F>(Func));
	}
}  // namespace Collection

template<typename F>
void FGMPStoreView::VisitRow(int32 Row, F&& Lambda) const
{
	using Sig = TypeTraits::TSigTraits<std::decay_t<F>>;
	using Tuple = typename Sig::Tuple;
	using Seq = std::make_index_sequence<Sig::TupleSize>;

	const uint8* RowPtr = ElemAt(Row);
	if (!RowPtr)
		return;

	const FGMPElementLayout& Layout = FGMPElementLayout::Get(ElemStruct);
	using FastTuple = typename Collection::TFastTupleOf<Tuple, 0, Seq>::Type;
	if (Collection::TupleLayoutMatches<FastTuple, Tuple, 0>(Layout, (Seq*)nullptr))
	{
		Collection::CallFastRow<FastTuple>(Lambda, RowPtr, (Seq*)nullptr);
		return;
	}

	FTypedAddresses Addrs;
	GMPBuildRowAddrs(Layout, RowPtr, Addrs);
	if (ensure(Addrs.Num() >= int32(Sig::TupleSize)))
		Collection::CallRow<Tuple, 0>(Lambda, Addrs.GetData(), (Seq*)nullptr);
}

template<typename F>
void FGMPStoreView::ForEach(F&& Lambda) const
{
	using Sig = TypeTraits::TSigTraits<std::decay_t<F>>;
	using Tuple = typename Sig::Tuple;
	static_assert(Sig::TupleSize >= 1, "a ForEach lambda starts with the int32 row index");
	using Seq = std::make_index_sequence<Sig::TupleSize - 1>;

	const int32 Count = Num();
	if (Count <= 0)
		return;

	const FGMPElementLayout& Layout = FGMPElementLayout::Get(ElemStruct);
	using FastTuple = typename Collection::TFastTupleOf<Tuple, 1, Seq>::Type;
	if (Collection::TupleLayoutMatches<FastTuple, Tuple, 1>(Layout, (Seq*)nullptr))
	{
		for (int32 i = 0; i < Count; ++i)
			Collection::CallFastRowIndexed<FastTuple>(Lambda, i, ElemAt(i), (Seq*)nullptr);
		return;
	}

	FTypedAddresses Addrs;
	for (int32 i = 0; i < Count; ++i)
	{
		GMPBuildRowAddrs(Layout, ElemAt(i), Addrs);
		if (!ensure(Addrs.Num() >= int32(Sig::TupleSize) - 1))
			return;
		Collection::CallRowIndexed<Tuple, 1>(Lambda, i, Addrs.GetData(), (Seq*)nullptr);
	}
}

}  // namespace GMP

using FGMPStoreRange = GMP::FGMPStoreRange;
using FGMPStoreUpdate = GMP::FGMPStoreUpdate;
using FGMPStoreView = GMP::FGMPStoreView;
