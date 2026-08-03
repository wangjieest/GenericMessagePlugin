//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPStoreCollection.h"

#include "Classes/GMPUnion.h"
#include "GMPHub.h"
#include "GMPReflection.h"
#include "GMPUtils.h"

namespace GMP
{
void GMPCollectElementProps(const UScriptStruct* ElemStruct, TArray<const FProperty*>& OutProps)
{
	OutProps.Reset();
	if (!ElemStruct)
		return;

	for (TFieldIterator<FProperty> It(ElemStruct); It; ++It)
	{
		// Skipping editor-only keeps the member sequence identical across configurations; a positional lambda would
		// otherwise bind to a different field in editor than in shipping.
		if (It->IsEditorOnlyProperty())
			continue;
		OutProps.Add(*It);
	}
}

namespace
{
	// Whether a store's runtime struct is a collection, and where the table sits inside it. Resolved once per runtime
	// struct -- otherwise every fire would walk reflection again to answer the same question.
	struct FStoreShape
	{
		const FArrayProperty* ArrProp = nullptr;
		const UScriptStruct* ElemStruct = nullptr;
		int32 ArrOffset = 0;
		int32 ElemSize = 0;
		TWeakObjectPtr<const UScriptStruct> Owner;  // guards the raw cache key against discard and address reuse
	};

	// By value: the cache may rehash on insert, so a reference into it would not stay valid.
	FStoreShape GetStoreShape(const UScriptStruct* RuntimeStruct)
	{
		static TMap<const UScriptStruct*, FStoreShape> Cache;
		if (const FStoreShape* Found = Cache.Find(RuntimeStruct))
		{
			if (Found->Owner.Get() == RuntimeStruct)
				return *Found;
			Cache.Remove(RuntimeStruct);
		}

		FStoreShape Shape;
		Shape.Owner = RuntimeStruct;
		// Collection shape == the store holds exactly one TArray<USTRUCT> parameter.
		TFieldIterator<FProperty> It(RuntimeStruct);
		if (const FArrayProperty* ArrProp = It ? CastField<FArrayProperty>(*It) : nullptr)
		{
			if (const FStructProperty* ElemProp = CastField<FStructProperty>(ArrProp->Inner))
			{
				if (ElemProp->Struct)
				{
					Shape.ArrProp = ArrProp;
					Shape.ElemStruct = ElemProp->Struct;
					Shape.ArrOffset = ArrProp->GetOffset_ForInternal();
					Shape.ElemSize = ArrProp->Inner->GetSize();
				}
			}
		}
		return Cache.Add(RuntimeStruct, Shape);
	}
}  // namespace

FGMPStoreView GMPMakeStoreView(const FGMPStructUnion* InUnion)
{
	if (!InUnion || !InUnion->IsValid())
		return FGMPStoreView{};

	const UScriptStruct* RuntimeStruct = InUnion->GetScriptStruct();
	if (!RuntimeStruct)
		return FGMPStoreView{};

	const FStoreShape Shape = GetStoreShape(RuntimeStruct);
	if (!Shape.ArrProp)
		return FGMPStoreView{};

	const uint8* ArrAddr = InUnion->GetMemory() + Shape.ArrOffset;
	return FGMPStoreView{reinterpret_cast<const FScriptArray*>(ArrAddr), Shape.ArrProp->Inner, Shape.ElemStruct, Shape.ElemSize};
}

const FGMPElementLayout& FGMPElementLayout::Get(const UScriptStruct* ElemStruct)
{
	static TMap<const UScriptStruct*, TUniquePtr<FGMPElementLayout>> Cache;
	static FGMPElementLayout Empty;
	if (!ElemStruct)
		return Empty;

	if (TUniquePtr<FGMPElementLayout>* Found = Cache.Find(ElemStruct))
	{
		// The key is a raw address, so a hit is only trusted while the struct it was built from is still that struct.
		if ((*Found)->Struct.Get() == ElemStruct)
			return **Found;
		Cache.Remove(ElemStruct);
	}

	TUniquePtr<FGMPElementLayout> Layout = MakeUnique<FGMPElementLayout>();
	Layout->Struct = ElemStruct;
	GMPCollectElementProps(ElemStruct, Layout->Props);
	Layout->TypeNames.Reserve(Layout->Props.Num());
	Layout->Offsets.Reserve(Layout->Props.Num());
	for (const FProperty* P : Layout->Props)
	{
		Layout->TypeNames.Add(GMP::Reflection::GetPropertyName(P));
		Layout->Offsets.Add(P->GetOffset_ForInternal());
	}

	FGMPElementLayout& Ref = *Layout;
	Cache.Add(ElemStruct, MoveTemp(Layout));
	return Ref;
}

void FGMPRangeAccum::Add(int32 Index, int32 Count)
{
	if (bFull || Count <= 0 || Index < 0)
		return;

	for (FGMPStoreRange& S : Spans)
	{
		// Merge when adjacent or overlapping so repeated single-row edits collapse into one span.
		if (Index <= S.Index + S.Count && S.Index <= Index + Count)
		{
			const int32 End = FMath::Max(S.Index + S.Count, Index + Count);
			S.Index = FMath::Min(S.Index, Index);
			S.Count = End - S.Index;
			Normalize();
			return;
		}
	}

	if (Spans.Num() >= MaxSpans)
	{
		MarkFullReload();
		return;
	}
	Spans.Add(FGMPStoreRange{Index, Count});
}

void FGMPRangeAccum::Normalize()
{
	Spans.Sort([](const FGMPStoreRange& A, const FGMPStoreRange& B) { return A.Index < B.Index; });
	for (int32 i = Spans.Num() - 1; i > 0; --i)
	{
		FGMPStoreRange& Prev = Spans[i - 1];
		const FGMPStoreRange& Cur = Spans[i];
		if (Cur.Index <= Prev.Index + Prev.Count)
		{
			Prev.Count = FMath::Max(Prev.Index + Prev.Count, Cur.Index + Cur.Count) - Prev.Index;
			Spans.RemoveAt(i);
		}
	}
}

TArrayView<const FGMPStoreRange> FGMPRangeAccum::Get() const
{
	if (bFull)
		return MakeArrayView(&FullSpan, 1);
	return MakeArrayView(Spans.GetData(), Spans.Num());
}

FScriptArray* GMPResolveStoredArrayForWrite(FSigSource InSigSrc, const FName& Key, const UScriptStruct*& OutElemStruct, int32& OutElemSize)
{
	OutElemStruct = nullptr;
	OutElemSize = 0;

	FGMPStructUnion* Union = FMessageUtils::GetMessageHub()->FindStoredMessage(Key, InSigSrc);
	if (!Union)
		return nullptr;

	const UScriptStruct* RuntimeStruct = Union->GetScriptStruct();
	if (!RuntimeStruct)
		return nullptr;
	const FStoreShape Shape = GetStoreShape(RuntimeStruct);
	if (!Shape.ArrProp)
		return nullptr;

	// A copied union shares its payload and is flagged as a view, so detach before editing in place.
	uint8* Memory = Union->EnsureUnique();
	if (!Memory)
		return nullptr;

	OutElemStruct = Shape.ElemStruct;
	OutElemSize = Shape.ElemSize;
	return reinterpret_cast<FScriptArray*>(Memory + Shape.ArrOffset);
}

void GMPNotifyStoreUpdate(FSigSource InSigSrc, const FName& Key, int32 TotalCount, TArrayView<const FGMPStoreRange> Ranges)
{
	FGMPStoreUpdate Update;
	Update.TotalCount = TotalCount;
	Update.Ranges = Ranges;
	GMPDispatchStoreUpdate(InSigSrc, Key, Update);
}

namespace
{
	struct FStoreListenerEntry
	{
		FSigSource SigSrc;
		FWeakObjectPtr Listener;
		int32 Row = AllRows;         // decoded: MAX_int32 == whole table
		bool bWantRemoval = false;   // decoded from the sign of the subscribed Index
		FGMPKey Id;
		FGMPKey LifeKey;  // when set, the entry lives as long as this ordinary GMP listener
		FName Key;
		bool bHasListener = false;
		bool bRemoved = false;
		FGMPStoreCallback Callback;
	};
	using FStoreListenerRef = TSharedPtr<FStoreListenerEntry>;

	struct FStoreKeyEntry
	{
		TArray<FStoreListenerRef> Listeners;
		TMap<FSigSource, int32> LastCounts;  // what each source last published, to tell a resize from a content edit
	};
	using FStoreKeyRef = TSharedPtr<FStoreKeyEntry>;

	// Held by shared pointer so a callback that listens on another key -- rehashing the map -- cannot pull the entry
	// out from under the dispatch that is running.
	TMap<FName, FStoreKeyRef>& StoreRegistry()
	{
		static TMap<FName, FStoreKeyRef> Registry;
		return Registry;
	}

	// While a dispatch is running, unlistening only marks the entry: compaction waits until the walk is over so the
	// indices it is iterating stay put.
	int32 GDispatchDepth = 0;

	bool IsListenerStale(const FStoreListenerEntry& Item)
	{
		if (Item.bRemoved || (Item.bHasListener && !Item.Listener.IsValid()))
			return true;
		return Item.LifeKey && !FMessageUtils::GetMessageHub()->IsAlive(Item.Key, Item.LifeKey);
	}

	// bWantRemoval lets a slot listener also hear that its row is gone; without it a vanished slot stays silent.
	bool ShouldWakeRow(int32 Row, bool bWantRemoval, const FGMPStoreUpdate& Update)
	{
		if (Row == AllRows)
			return true;
		// A slot past the end has no content to hand over -- only a listener that asked for removals hears about it.
		if (Row >= Update.TotalCount)
			return bWantRemoval && Row < Update.PrevTotalCount;
		if (Update.IsFullReload())
			return true;
		for (const FGMPStoreRange& R : Update.Ranges)
		{
			if (Row >= R.Index && Row < R.Index + R.Count)
				return true;
		}
		return false;
	}

	void CompactListeners(FStoreKeyEntry& Entry)
	{
		if (GDispatchDepth == 0)
			Entry.Listeners.RemoveAll([](const FStoreListenerRef& Item) { return IsListenerStale(*Item); });
	}

	void InvokeStoreListeners(FStoreKeyEntry& Entry, FSigSource InSigSrc, const FGMPStoreView& View, const FGMPStoreUpdate& Update, bool bTransient = false)
	{
		// Walk in place: no snapshot, so no refcount traffic per listener per fire. Removals during a callback only
		// mark, and anything registered from a callback lands past Count and is not visited by this fire.
		const int32 Count = Entry.Listeners.Num();
		++GDispatchDepth;
		bool bAnyStale = false;
		for (int32 i = 0; i < Count; ++i)
		{
			FStoreListenerEntry& Item = *Entry.Listeners[i];
			// A transient table carries no change set, so a slot listener could not tell whether its own row moved and
			// would wake on every send; staying silent is easier to diagnose than waking for no reason.
			if (bTransient && Item.Row != AllRows)
				continue;
			// Cheap tests first: the liveness check costs a signal lookup, so only the listeners that would actually
			// fire pay for it -- a table with many slot listeners wakes one of them and skips the rest for free.
			if (!(Item.SigSrc == InSigSrc) || Item.bRemoved || !ShouldWakeRow(Item.Row, Item.bWantRemoval, Update))
				continue;
			if (IsListenerStale(Item))
			{
				bAnyStale = true;
				continue;
			}
			if (Item.Row == AllRows)
			{
				FGMPStoreUpdate Rows = Update;
				Rows.bIncludeRemoved = Item.bWantRemoval;
				Item.Callback(View, Rows);
			}
			else
			{
				// A slot listener is told about its own row: >=0 while it still holds content, ~Row once it is gone.
				FGMPStoreUpdate Slot = Update;
				Slot.Row = Item.Row < Update.TotalCount ? Item.Row : ~Item.Row;
				Item.Callback(View, Slot);
			}
		}
		--GDispatchDepth;
		if (bAnyStale)
			CompactListeners(Entry);
	}
}  // namespace

FGMPKey GMPListenStore(FSigSource InSigSrc, const FName& Key, const UObject* Listener, int32 Row, FGMPStoreCallback&& Callback, FGMPKey LifeKey)
{
	if (!ensure(Callback))
		return FGMPKey{};

	FStoreListenerRef Item = MakeShared<FStoreListenerEntry>();
	Item->SigSrc = InSigSrc;
	Item->Listener = Listener;
	Item->bHasListener = !!Listener;
	const FGMPStoreListenSpec Spec = FGMPStoreListenSpec::Decode(Row);
	Item->Row = Spec.Row;
	Item->bWantRemoval = Spec.bWantRemoval;
	Item->Id = FGMPKey::NextGMPKey();
	Item->LifeKey = LifeKey;
	Item->Key = Key;
	Item->Callback = MoveTemp(Callback);

	const FGMPStructUnion* Stored = FMessageUtils::GetMessageHub()->FindStoredMessage(Key, InSigSrc);
	FGMPStoreView View = GMPMakeStoreView(Stored);
#if GMP_WITH_DYNAMIC_CALL_CHECK
	// Subscribing before the table exists is legal; a payload that is there but is not a collection never fires.
	ensureMsgf(!Stored || View.IsValid(), TEXT("GMP: '%s' holds a non-collection payload; row listening on it will never fire"), *Key.ToString());
#endif
	{
		FStoreKeyRef& EntryRef = StoreRegistry().FindOrAdd(Key);
		if (!EntryRef)
			EntryRef = MakeShared<FStoreKeyEntry>();
		EntryRef->Listeners.Add(Item);
		if (View.IsValid())
			EntryRef->LastCounts.Add(InSigSrc, View.Num());
	}

	// Replay the stored table once, as a full reload, so a listener that arrived after the write is not left blank.
	if (View.IsValid())
	{
		FGMPStoreUpdate Replay;
		Replay.TotalCount = View.Num();
		Replay.PrevTotalCount = Replay.TotalCount;
		// Same per-listener row stamp the dispatch path applies, so a slot listener sees its own row on replay too.
		if (Spec.Row != AllRows)
			Replay.Row = Spec.Row < Replay.TotalCount ? Spec.Row : ~Spec.Row;
		if (ShouldWakeRow(Spec.Row, Spec.bWantRemoval, Replay))
			Item->Callback(View, Replay);
	}
	return Item->Id;
}

void GMPUnlistenStore(const FName& Key, const UObject* Listener)
{
	FStoreKeyRef* Found = StoreRegistry().Find(Key);
	if (!Found || !*Found)
		return;
	FStoreKeyEntry& Entry = **Found;
	for (const FStoreListenerRef& Item : Entry.Listeners)
	{
		if (Item->bHasListener && Listener && Item->Listener == FWeakObjectPtr(Listener))
			Item->bRemoved = true;
	}
	CompactListeners(Entry);
}

void GMPUnlistenStore(const FName& Key, FGMPKey ListenKey)
{
	FStoreKeyRef* Found = StoreRegistry().Find(Key);
	if (!Found || !*Found)
		return;
	FStoreKeyEntry& Entry = **Found;
	for (const FStoreListenerRef& Item : Entry.Listeners)
	{
		// the key handed back to the caller is the ordinary listener's when there is one, so match either
		if (Item->Id == ListenKey || (Item->LifeKey && Item->LifeKey == ListenKey))
			Item->bRemoved = true;
	}
	CompactListeners(Entry);
}

bool GMPHasStoreListeners()
{
	return StoreRegistry().Num() > 0;
}

void GMPDispatchStoreUpdate(FSigSource InSigSrc, const FName& Key, const FGMPStoreUpdate& InUpdate)
{
	FStoreKeyRef* Found = StoreRegistry().Find(Key);
	if (!Found || !*Found)
		return;
	const FStoreKeyRef EntryRef = *Found;  // survives a callback that listens on another key and rehashes the map
	FStoreKeyEntry& Entry = *EntryRef;

	int32& LastCount = Entry.LastCounts.FindOrAdd(InSigSrc, INDEX_NONE);
	const bool bCountChanged = LastCount != InUpdate.TotalCount;
	const int32 PrevCount = LastCount >= 0 ? LastCount : InUpdate.TotalCount;
	LastCount = InUpdate.TotalCount;

	// A resize shifts every row after the first touched one, so widen the change set to the tail: from then on both the
	// wake-up test and the per-row walk read off Ranges alone.
	FGMPStoreUpdate Update = InUpdate;
	Update.PrevTotalCount = PrevCount;
	FGMPStoreRange Tail;
	if (bCountChanged && !InUpdate.IsFullReload())
	{
		int32 MinIndex = MAX_int32;
		for (const FGMPStoreRange& R : InUpdate.Ranges)
			MinIndex = FMath::Min(MinIndex, R.Index);
		MinIndex = FMath::Clamp(MinIndex, 0, InUpdate.TotalCount);
		Tail = FGMPStoreRange{MinIndex, InUpdate.TotalCount - MinIndex};
		Update.Ranges = MakeArrayView(&Tail, 1);
	}

	const FGMPStoreView View = GMPMakeStoreView(FMessageUtils::GetMessageHub()->FindStoredMessage(Key, InSigSrc));
	InvokeStoreListeners(Entry, InSigSrc, View, Update);
}

void GMPDispatchTransientRows(FSigSource InSigSrc, const FName& Key, const FGMPPropStackRefArray& Params)
{
	FStoreKeyRef* Found = StoreRegistry().Find(Key);
	if (!Found || !*Found || Params.Num() != 1)
		return;

	const FArrayProperty* ArrProp = CastField<FArrayProperty>(Params[0].GetProp());
	if (!ArrProp)
		return;
	const FStructProperty* ElemProp = CastField<FStructProperty>(ArrProp->Inner);
	if (!ElemProp || !ElemProp->Struct)
		return;

	// The table lives on the sender's stack for this call only -- borrowed exactly like the stored one.
	const FGMPStoreView View{reinterpret_cast<const FScriptArray*>(Params[0].GetAddr()), ArrProp->Inner, ElemProp->Struct, ArrProp->Inner->GetSize()};

	// No previous table exists, so this is always a full reload; LastCounts is left alone to keep send and store on the
	// same key from corrupting each other's resize detection.
	FGMPStoreUpdate Update;
	Update.TotalCount = View.Num();
	Update.PrevTotalCount = Update.TotalCount;

	const FStoreKeyRef EntryRef = *Found;
	InvokeStoreListeners(*EntryRef, InSigSrc, View, Update, /*bTransient*/ true);
}

void GMPComputeStoreDiff(const FName& Key, const FGMPStructUnion* OldUnion, const FGMPPropStackRefArray& NewParams, FGMPStoreDiff& OutDiff)
{
	OutDiff = FGMPStoreDiff{};
	if (!StoreRegistry().Contains(Key) || NewParams.Num() != 1)
		return;
	const FArrayProperty* ArrProp = CastField<FArrayProperty>(NewParams[0].GetProp());
	if (!ArrProp)
		return;
	const FStructProperty* ElemProp = CastField<FStructProperty>(ArrProp->Inner);
	if (!ElemProp || !ElemProp->Struct)
		return;

	FScriptArrayHelper NewHelper(ArrProp, NewParams[0].GetAddr());
	OutDiff.TotalCount = NewHelper.Num();
	OutDiff.bPublish = true;

	const FGMPStoreView Old = GMPMakeStoreView(OldUnion);
	if (!Old.IsValid() || Old.GetElementStruct() != ElemProp->Struct)
		return;  // nothing comparable yet: no spans == full reload

	// Per-element compare, not memcmp: an element holds FString/TArray/UObject* whose bytes differ while the values do
	// not. A run of differing elements collapses into one span.
	const UScriptStruct* ElemStruct = ElemProp->Struct;
	const int32 Common = FMath::Min(Old.Num(), OutDiff.TotalCount);
	bool bTooManySpans = false;
	int32 RunStart = INDEX_NONE;
	auto CloseRun = [&](int32 End) {
		if (RunStart == INDEX_NONE)
			return;
		if (OutDiff.Spans.Num() < FGMPRangeAccum::MaxSpans)
			OutDiff.Spans.Add(FGMPStoreRange{RunStart, End - RunStart});
		else
			bTooManySpans = true;
		RunStart = INDEX_NONE;
	};
	for (int32 i = 0; i < Common && !bTooManySpans; ++i)
	{
		if (ElemStruct->CompareScriptStruct(NewHelper.GetRawPtr(i), Old.ElemAt(i), PPF_None))
			CloseRun(i);
		else if (RunStart == INDEX_NONE)
			RunStart = i;
	}
	CloseRun(Common);
	if (OutDiff.TotalCount > Common)
	{
		RunStart = Common;
		CloseRun(OutDiff.TotalCount);
	}

	if (bTooManySpans)
	{
		OutDiff.Spans.Reset();  // too scattered to describe: let the listeners reread everything
		return;
	}
	// A table that compares equal and kept its length has nothing to publish.
	if (OutDiff.Spans.Num() == 0 && OutDiff.TotalCount == Old.Num())
		OutDiff.bPublish = false;
}

void GMPPublishStoreDiff(FSigSource InSigSrc, const FName& Key, const FGMPStoreDiff& Diff)
{
	if (Diff.bPublish)
		GMPNotifyStoreUpdate(InSigSrc, Key, Diff.TotalCount, MakeArrayView(Diff.Spans.GetData(), Diff.Spans.Num()));
}

FGMPElementLayout::~FGMPElementLayout()
{
	// Destroying through a discarded struct would walk freed reflection data; leaking the bytes is the lesser evil.
	if (const UScriptStruct* S = Struct.Get())
	{
		if (DefaultBytes.Num())
			S->DestroyStruct(DefaultBytes.GetData());
	}
}

const uint8* FGMPElementLayout::GetDefaultRow() const
{
	if (DefaultBytes.Num())
		return DefaultBytes.GetData();
	const UScriptStruct* S = Struct.Get();
	if (!S)
		return nullptr;
	DefaultBytes.SetNumUninitialized(S->GetStructureSize());
	S->InitializeStruct(DefaultBytes.GetData());
	return DefaultBytes.GetData();
}

void GMPBuildScriptRowArgs(const FGMPStoreView& View, int32 Row, int32& OutRowValue, FTypedAddresses& OutArgs)
{
	OutArgs.Reset();
	const bool bRemoved = Row < 0;
	OutRowValue = Row;
	const uint8* RowPtr = bRemoved ? FGMPElementLayout::Get(View.GetElementStruct()).GetDefaultRow() : View.ElemAt(Row);
	if (!RowPtr)
		return;
	OutArgs.Add(FGMPTypedAddr::FromAddr(&OutRowValue, GMP::TClass2Prop<int32>::GetProperty()));
	OutArgs.Add(FGMPTypedAddr::FromAddr(RowPtr, View.GetElementProp()));
}

FGMPKey GMPListenScriptRows(FSigSource InSigSrc, const FName& Key, const UObject* Listener, int32 Index, TFunction<void(const FGMPTypedAddr*, int32, const UScriptStruct*)>&& OnRow, FGMPListenOptions Options)
{
	if (!ensure(OnRow))
		return FGMPKey{};

	// An ordinary no-op listener carries the lifetime, so the existing unbind paths keep working unchanged.
	const FGMPKey LifeKey = FMessageUtils::GetMessageHub()->ScriptListenMessage(InSigSrc, Key, Listener, [](FMessageBody&) {}, Options);
	if (!LifeKey)
		return FGMPKey{};

	const FGMPStoreListenSpec Spec = FGMPStoreListenSpec::Decode(Index);
	auto Callback = [Spec, OnRow{MoveTemp(OnRow)}](const FGMPStoreView& View, const FGMPStoreUpdate& Update) {
		auto InvokeRow = [&](int32 Row) {
			int32 RowValue = 0;
			FTypedAddresses Args;
			GMPBuildScriptRowArgs(View, Row, RowValue, Args);
			if (Args.Num() == 2)
				OnRow(Args.GetData(), Args.Num(), View.GetElementStruct());
		};
		if (Spec.IsWholeTable())
			GMPForEachChangedRow(View, Update, InvokeRow);
		else
			InvokeRow(Update.Row);  // carries the slot row, or ~row once it is gone
	};
	GMPListenStore(InSigSrc, Key, Listener, Index, MoveTemp(Callback), LifeKey);
	return LifeKey;
}

void GMPForEachChangedRow(const FGMPStoreView& View, const FGMPStoreUpdate& Update, TFunctionRef<void(int32)> Visitor)
{
	const int32 Count = FMath::Min(View.Num(), Update.TotalCount);
	if (Update.IsFullReload())
	{
		for (int32 i = 0; i < Count; ++i)
			Visitor(i);
		return;
	}
	for (const FGMPStoreRange& R : Update.Ranges)
	{
		const int32 End = FMath::Min(R.Index + R.Count, Count);
		for (int32 i = FMath::Max(R.Index, 0); i < End; ++i)
			Visitor(i);
	}
	// Rows the change dropped: reported as ~Row so the visitor can tell them from surviving rows.
	if (Update.bIncludeRemoved)
	{
		for (int32 i = Update.TotalCount; i < Update.PrevTotalCount; ++i)
			Visitor(~i);
	}
}

void GMPBuildRowAddrs(const FGMPElementLayout& Layout, const uint8* RowPtr, FTypedAddresses& OutAddrs)
{
	OutAddrs.Reset(Layout.Offsets.Num());
	if (!RowPtr)
		return;
	// Offsets and type names were resolved once per element struct: a member costs an add, not a reflection lookup.
	for (int32 i = 0; i < Layout.Offsets.Num(); ++i)
		OutAddrs.Emplace(RowPtr + Layout.Offsets[i], Layout.TypeNames[i]);
}

void GMPBuildRowAddrs(const FGMPStoreView& View, int32 Row, FTypedAddresses& OutAddrs)
{
	GMPBuildRowAddrs(FGMPElementLayout::Get(View.GetElementStruct()), View.ElemAt(Row), OutAddrs);
}
}  // namespace GMP
