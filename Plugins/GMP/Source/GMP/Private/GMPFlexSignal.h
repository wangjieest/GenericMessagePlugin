//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPFlexSignalLite.h"

#ifndef GMP_FLEXSIG_TYPE_ERASED
#define GMP_FLEXSIG_TYPE_ERASED 1
#endif

#ifndef GMP_FLEXSIG_WITH_SOURCE
#define GMP_FLEXSIG_WITH_SOURCE 1
#endif
#ifndef GMP_FLEXSIG_WITH_LEVEL
#define GMP_FLEXSIG_WITH_LEVEL 1
#endif
#ifndef GMP_FLEXSIG_WITH_EXTKEY
#define GMP_FLEXSIG_WITH_EXTKEY 1
#endif
#ifndef GMP_FLEXSIG_WITH_ORDER
#define GMP_FLEXSIG_WITH_ORDER 1
#endif
#ifndef GMP_FLEXSIG_WITH_TIMES
#define GMP_FLEXSIG_WITH_TIMES 1
#endif

#if GMP_FLEXSIG_WITH_LEVEL && !GMP_FLEXSIG_WITH_SOURCE
#error "GMP_FLEXSIG_WITH_LEVEL requires GMP_FLEXSIG_WITH_SOURCE (parent chain is a source-affiliation chain)."
#endif
#if GMP_FLEXSIG_WITH_EXTKEY && !GMP_FLEXSIG_WITH_SOURCE
#error "GMP_FLEXSIG_WITH_EXTKEY requires GMP_FLEXSIG_WITH_SOURCE (composite source extends source identity)."
#endif

#if GMP_FLEXSIG_WITH_ORDER
#include <algorithm>
#endif

#define GMP_FLEXSIG_EXTKEY_DEBUG_NAMES (GMP_FLEXSIG_WITH_EXTKEY && GMP_FLEXSIG_DYNAMIC_CHECK)
#if GMP_FLEXSIG_EXTKEY_DEBUG_NAMES
#include <unordered_map>
#include <string>
#endif

namespace GMP
{
namespace FlexSig
{
#if GMP_FLEXSIG_WITH_SOURCE

#if GMP_FLEXSIG_WITH_EXTKEY
struct FExtSource
{
	const void* Obj = nullptr;
	uint64_t NameHash = 0;

	FExtSource() = default;
	FExtSource(const void* obj, uint64_t nameHash)
		: Obj(obj)
		, NameHash(nameHash)
	{
	}
	FExtSource(const void* obj)
		: Obj(obj)
		, NameHash(0)
	{
	}
	template<typename T, typename = typename std::enable_if<!std::is_same<T, FExtSource>::value>::type>
	FExtSource(const T* obj)
		: Obj(static_cast<const void*>(obj))
		, NameHash(0)
	{
	}

	bool operator==(const FExtSource& o) const { return Obj == o.Obj && NameHash == o.NameHash; }
	bool operator!=(const FExtSource& o) const { return !(*this == o); }
};
using FSource = FExtSource;
static const FSource SourceAny = FExtSource{nullptr, 0};

#if GMP_FLEXSIG_EXTKEY_DEBUG_NAMES
inline std::unordered_map<uint64_t, std::string>& GetExtNameTable()
{
	static std::unordered_map<uint64_t, std::string> T;
	return T;
}
inline const char* DebugExtName(uint64_t h)
{
	if (h == 0)
		return "<none>";
	auto& t = GetExtNameTable();
	auto it = t.find(h);
	return it != t.end() ? it->second.c_str() : "<unknown-hash>";
}
#endif

inline FSource MakeExtSource(const void* obj, const char* name = nullptr)
{
	uint64_t h = (name && *name) ? FnvHash(name) : 0;
#if GMP_FLEXSIG_EXTKEY_DEBUG_NAMES
	if (h != 0)
		GetExtNameTable()[h] = name;
#endif
	return FExtSource{obj, h};
}
inline const void* SourceObj(const FSource& s) { return s.Obj; }

#else   // !GMP_FLEXSIG_WITH_EXTKEY
using FSource = const void*;
static constexpr FSource SourceAny = nullptr;
inline const void* SourceObj(const FSource& s) { return s; }
#endif  // GMP_FLEXSIG_WITH_EXTKEY

#endif  // GMP_FLEXSIG_WITH_SOURCE

struct FListenOptions
{
#if GMP_FLEXSIG_WITH_ORDER
	int32_t Order = 0;
#endif
#if GMP_FLEXSIG_WITH_TIMES
	int32_t Times = -1;
#endif
};

#if GMP_FLEXSIG_WITH_LEVEL
using FParentResolver = const void* (*)(const void* childObj);
#endif

struct FFlexCtrlBlock
{
	void* Owner = nullptr;
	void (*RemoveByIdFn)(void* owner, FConnId id) = nullptr;
#if GMP_FLEXSIG_WITH_SOURCE
	int (*RemoveSourceFn)(void* owner, const FSource& src) = nullptr;
#endif
};

struct FFlexMatchedEntry
{
	FConnId Id;
#if GMP_FLEXSIG_WITH_ORDER
	uint64_t SortKey;
#endif
};

#if GMP_FLEXSIG_TYPE_ERASED

class FlexSignal
{
	using FStorage = GMP_FLEXSIG_DEFAULT_STORAGE;

	struct Slot
	{
		FConnId Id;
#if GMP_FLEXSIG_WITH_SOURCE
		FSource Src;
#endif
#if GMP_FLEXSIG_WITH_ORDER
		int32_t Order = 0;
#endif
#if GMP_FLEXSIG_WITH_TIMES
		int32_t Times = -1;
#endif
		FThunk Thunk;
		typename FStorage::Handle Callable;
	};

	using ControlBlock = FFlexCtrlBlock;

public:
	FlexSignal()
		: Ctrl(std::make_shared<ControlBlock>())
	{
		Ctrl->Owner = this;
		Ctrl->RemoveByIdFn = [](void* o, FConnId id) { static_cast<FlexSignal*>(o)->RemoveById(id); };
#if GMP_FLEXSIG_WITH_SOURCE
		Ctrl->RemoveSourceFn = [](void* o, const FSource& s) { return static_cast<FlexSignal*>(o)->RemoveSource(s); };
#endif
	}
	~FlexSignal() { Ctrl->Owner = nullptr; }

	FlexSignal(const FlexSignal&) = delete;
	FlexSignal& operator=(const FlexSignal&) = delete;

	class FConnection
	{
	public:
		FConnection() = default;
		FConnection(std::weak_ptr<ControlBlock> InCtrl, FConnId InId)
			: Ctrl(std::move(InCtrl))
			, Id(InId)
		{
		}
		FConnection(FConnection&& o) noexcept
			: Ctrl(std::move(o.Ctrl))
			, Id(o.Id)
		{
			o.Id = 0;
		}
		FConnection& operator=(FConnection&& o) noexcept
		{
			if (this != &o)
			{
				Disconnect();
				Ctrl = std::move(o.Ctrl);
				Id = o.Id;
				o.Id = 0;
			}
			return *this;
		}
		FConnection(const FConnection&) = delete;
		FConnection& operator=(const FConnection&) = delete;
		~FConnection() { Disconnect(); }

		void Disconnect()
		{
			if (Id == 0)
				return;
			if (auto c = Ctrl.lock())
				if (c->Owner && c->RemoveByIdFn)
					c->RemoveByIdFn(c->Owner, Id);
			Id = 0;
			Ctrl.reset();
		}
		bool IsValid() const
		{
			auto c = Ctrl.lock();
			return Id != 0 && c && c->Owner;
		}

	private:
		std::weak_ptr<ControlBlock> Ctrl;
		FConnId Id = 0;
	};

#if GMP_FLEXSIG_WITH_LEVEL
	void SetParentResolver(FParentResolver resolver) { ParentResolver = resolver; }
#endif

	template<typename Func>
	FConnection Connect(Func&& fn,
#if GMP_FLEXSIG_WITH_SOURCE
		FSource src = SourceAny,
#endif
		FListenOptions opt = {})
	{
		using D = typename std::decay<Func>::type;
		using Tuple = typename TCallableTraits<D>::Tuple;

		Slot s;
		s.Id = ++NextId;
#if GMP_FLEXSIG_WITH_SOURCE
		s.Src = src;
#endif
#if GMP_FLEXSIG_WITH_ORDER
		s.Order = opt.Order;
#endif
#if GMP_FLEXSIG_WITH_TIMES
		s.Times = (opt.Times < 0 ? -1 : opt.Times);
#endif
		(void)opt;
		s.Thunk = &ThunkImpl<Tuple, D>;
		s.Callable = FStorage::Store(std::forward<Func>(fn));
		Slots.push_back(std::move(s));
		return FConnection(Ctrl, Slots.back().Id);
	}

#if GMP_FLEXSIG_WITH_SOURCE
	template<typename... Args>
	void BroadcastFrom(FSource src, Args&&... args)
	{
		BroadcastImpl(/*useSrcFilter*/ true, src, std::forward<Args>(args)...);
	}
#endif

	template<typename... Args>
	void Broadcast(Args&&... args)
	{
#if GMP_FLEXSIG_WITH_SOURCE
		BroadcastImpl(/*useSrcFilter*/ false, SourceAny, std::forward<Args>(args)...);
#else
		BroadcastImpl(std::forward<Args>(args)...);
#endif
	}

#if GMP_FLEXSIG_WITH_SOURCE
	int RemoveSource(FSource src)
	{
		if (src == SourceAny)
			return 0;
		int removed = 0;
		for (std::size_t i = 0; i < Slots.size();)
		{
			if (Slots[i].Src == src)
			{
				Slots.erase(Slots.begin() + i);
				++removed;
			}
			else
				++i;
		}
		return removed;
	}

#endif  // GMP_FLEXSIG_WITH_SOURCE

	int NumConnections() const { return (int)Slots.size(); }
	std::weak_ptr<ControlBlock> GetControl() const { return Ctrl; }

private:
	void RemoveById(FConnId Id)
	{
		for (std::size_t i = 0; i < Slots.size(); ++i)
			if (Slots[i].Id == Id)
			{
				Slots.erase(Slots.begin() + i);
				return;
			}
	}

#if GMP_FLEXSIG_WITH_SOURCE
	bool SourceHit(const FSource& listenSrc, const FSource& fireSrc) const
	{
#if GMP_FLEXSIG_WITH_LEVEL
#if GMP_FLEXSIG_WITH_EXTKEY
		if (listenSrc.NameHash != fireSrc.NameHash)
			return false;
		const void* cur = fireSrc.Obj;
		const void* want = listenSrc.Obj;
#else
		const void* cur = SourceObj(fireSrc);
		const void* want = SourceObj(listenSrc);
#endif
		int guard = 0;
		while (cur != nullptr && guard++ < 64)
		{
			if (want == cur)
				return true;
			if (!ParentResolver)
				break;
			cur = ParentResolver(cur);
		}
		return false;
#else
		return listenSrc == fireSrc;
#endif
	}
#endif  // GMP_FLEXSIG_WITH_SOURCE (SourceHit)

	using Matched = FFlexMatchedEntry;

	static uint64_t MakeSortKey(int32_t order, FConnId seq)
	{
		const uint64_t hi = (uint64_t)((uint32_t)order ^ 0x80000000u);
		const uint64_t lo = (uint32_t)seq;
		return (hi << 32) | lo;
	}

	void DispatchMatched(std::vector<Matched>& matched, const FFlexAddr* addrs, int Num)
	{
#if GMP_FLEXSIG_WITH_ORDER
		std::sort(matched.begin(), matched.end(), [](const Matched& a, const Matched& b) { return a.SortKey < b.SortKey; });
#endif
		for (const Matched& m : matched)
		{
			Slot* live = FindSlotById(m.Id);
			if (!live)
				continue;
			live->Thunk(FStorage::GetSelf(live->Callable), addrs, Num);
#if GMP_FLEXSIG_WITH_TIMES
			Slot* after = FindSlotById(m.Id);
			if (after && after->Times > 0 && --after->Times == 0)
				RemoveById(m.Id);
#endif
		}
	}

	static Matched MakeMatched(const Slot& s)
	{
		Matched m;
		m.Id = s.Id;
#if GMP_FLEXSIG_WITH_ORDER
		m.SortKey = MakeSortKey(s.Order, s.Id);
#endif
		return m;
	}

#if GMP_FLEXSIG_WITH_SOURCE
	template<typename... Args>
	void BroadcastImpl(bool useSrcFilter, FSource fireSrc, Args&&... args)
	{
		FFlexAddr addrs[] = {FFlexAddr::Make(args)..., FFlexAddr{}};
		const int Num = (int)sizeof...(Args);
		std::vector<Matched> matched;
		matched.reserve(Slots.size());
		for (const Slot& s : Slots)
		{
			const bool hit = !useSrcFilter || s.Src == SourceAny || SourceHit(s.Src, fireSrc);
			if (hit)
				matched.push_back(MakeMatched(s));
		}
		DispatchMatched(matched, addrs, Num);
	}
#else   // !GMP_FLEXSIG_WITH_SOURCE
	template<typename... Args>
	void BroadcastImpl(Args&&... args)
	{
		FFlexAddr addrs[] = {FFlexAddr::Make(args)..., FFlexAddr{}};
		const int Num = (int)sizeof...(Args);
		std::vector<Matched> matched;
		matched.reserve(Slots.size());
		for (const Slot& s : Slots)
			matched.push_back(MakeMatched(s));
		DispatchMatched(matched, addrs, Num);
	}
#endif  // GMP_FLEXSIG_WITH_SOURCE

	Slot* FindSlotById(FConnId Id)
	{
		for (Slot& s : Slots)
			if (s.Id == Id)
				return &s;
		return nullptr;
	}

	mutable GMP_FLEXSIG_CONTAINER<Slot> Slots;
#if GMP_FLEXSIG_WITH_LEVEL
	FParentResolver ParentResolver = nullptr;
#endif
	std::shared_ptr<ControlBlock> Ctrl;
	FConnId NextId = 0;

public:
	using FControlBlock = ControlBlock;
};

#else

template<typename Func, std::size_t... Is, typename Tup>
inline void FlexCallPrefix(Func& f, std::index_sequence<Is...>, Tup&& tup)
{
	f(std::get<Is>(tup)...);
}

template<typename... TArgs>
class FlexSignal
{
	using FStrongThunk = void (*)(void* self, TArgs...);
	using FStorage = GMP_FLEXSIG_DEFAULT_STORAGE;

	struct Slot
	{
		FConnId Id;
#if GMP_FLEXSIG_WITH_SOURCE
		FSource Src;
#endif
#if GMP_FLEXSIG_WITH_ORDER
		int32_t Order = 0;
#endif
#if GMP_FLEXSIG_WITH_TIMES
		int32_t Times = -1;
#endif
		FStrongThunk Thunk;
		typename FStorage::Handle Callable;
	};

	using ControlBlock = FFlexCtrlBlock;

public:
	FlexSignal()
		: Ctrl(std::make_shared<ControlBlock>())
	{
		Ctrl->Owner = this;
		Ctrl->RemoveByIdFn = [](void* o, FConnId id) { static_cast<FlexSignal*>(o)->RemoveById(id); };
#if GMP_FLEXSIG_WITH_SOURCE
		Ctrl->RemoveSourceFn = [](void* o, const FSource& s) { return static_cast<FlexSignal*>(o)->RemoveSource(s); };
#endif
	}
	~FlexSignal() { Ctrl->Owner = nullptr; }

	FlexSignal(const FlexSignal&) = delete;
	FlexSignal& operator=(const FlexSignal&) = delete;

	class FConnection
	{
	public:
		FConnection() = default;
		FConnection(std::weak_ptr<ControlBlock> InCtrl, FConnId InId)
			: Ctrl(std::move(InCtrl))
			, Id(InId)
		{
		}
		FConnection(FConnection&& o) noexcept
			: Ctrl(std::move(o.Ctrl))
			, Id(o.Id)
		{
			o.Id = 0;
		}
		FConnection& operator=(FConnection&& o) noexcept
		{
			if (this != &o)
			{
				Disconnect();
				Ctrl = std::move(o.Ctrl);
				Id = o.Id;
				o.Id = 0;
			}
			return *this;
		}
		FConnection(const FConnection&) = delete;
		FConnection& operator=(const FConnection&) = delete;
		~FConnection() { Disconnect(); }

		void Disconnect()
		{
			if (Id == 0)
				return;
			if (auto c = Ctrl.lock())
				if (c->Owner && c->RemoveByIdFn)
					c->RemoveByIdFn(c->Owner, Id);
			Id = 0;
			Ctrl.reset();
		}
		bool IsValid() const
		{
			auto c = Ctrl.lock();
			return Id != 0 && c && c->Owner;
		}

	private:
		std::weak_ptr<ControlBlock> Ctrl;
		FConnId Id = 0;
	};

#if GMP_FLEXSIG_WITH_LEVEL
	void SetParentResolver(FParentResolver resolver) { ParentResolver = resolver; }
#endif

	template<typename Func>
	FConnection Connect(Func&& fn,
#if GMP_FLEXSIG_WITH_SOURCE
		FSource src = SourceAny,
#endif
		FListenOptions opt = {})
	{
		using D = typename std::decay<Func>::type;
		using Tuple = typename TCallableTraits<D>::Tuple;
		constexpr std::size_t kListenArity = std::tuple_size<Tuple>::value;
		static_assert(kListenArity <= sizeof...(TArgs), "listener declares more params than the signal provides");

		Slot s;
		s.Id = ++NextId;
#if GMP_FLEXSIG_WITH_SOURCE
		s.Src = src;
#endif
#if GMP_FLEXSIG_WITH_ORDER
		s.Order = opt.Order;
#endif
#if GMP_FLEXSIG_WITH_TIMES
		s.Times = (opt.Times < 0 ? -1 : opt.Times);
#endif
		(void)opt;
		s.Thunk = &StrongThunk<D, kListenArity>;
		s.Callable = FStorage::Store(std::forward<Func>(fn));
		Slots.push_back(std::move(s));
		return FConnection(Ctrl, Slots.back().Id);
	}

#if GMP_FLEXSIG_WITH_SOURCE
	void BroadcastFrom(FSource src, TArgs... args)
	{
		BroadcastImpl(/*useSrcFilter*/ true, src, args...);
	}
#endif

	void Broadcast(TArgs... args)
	{
#if GMP_FLEXSIG_WITH_SOURCE
		BroadcastImpl(/*useSrcFilter*/ false, SourceAny, args...);
#else
		BroadcastImpl(args...);
#endif
	}

#if GMP_FLEXSIG_WITH_SOURCE
	int RemoveSource(FSource src)
	{
		if (src == SourceAny)
			return 0;
		int removed = 0;
		for (std::size_t i = 0; i < Slots.size();)
		{
			if (Slots[i].Src == src)
			{
				Slots.erase(Slots.begin() + i);
				++removed;
			}
			else
				++i;
		}
		return removed;
	}
#endif

	int NumConnections() const { return (int)Slots.size(); }
	std::weak_ptr<ControlBlock> GetControl() const { return Ctrl; }

private:
	template<typename Func, std::size_t N>
	static void StrongThunk(void* self, TArgs... args)
	{
		FlexCallPrefix(*static_cast<Func*>(self), std::make_index_sequence<N>{}, std::forward_as_tuple(args...));
	}

	void RemoveById(FConnId Id)
	{
		for (std::size_t i = 0; i < Slots.size(); ++i)
			if (Slots[i].Id == Id)
			{
				Slots.erase(Slots.begin() + i);
				return;
			}
	}

#if GMP_FLEXSIG_WITH_SOURCE
	bool SourceHit(const FSource& listenSrc, const FSource& fireSrc) const
	{
#if GMP_FLEXSIG_WITH_LEVEL
#if GMP_FLEXSIG_WITH_EXTKEY
		if (listenSrc.NameHash != fireSrc.NameHash)
			return false;
		const void* cur = fireSrc.Obj;
		const void* want = listenSrc.Obj;
#else
		const void* cur = SourceObj(fireSrc);
		const void* want = SourceObj(listenSrc);
#endif
		int guard = 0;
		while (cur != nullptr && guard++ < 64)
		{
			if (want == cur)
				return true;
			if (!ParentResolver)
				break;
			cur = ParentResolver(cur);
		}
		return false;
#else
		return listenSrc == fireSrc;
#endif
	}
#endif  // GMP_FLEXSIG_WITH_SOURCE

	using Matched = FFlexMatchedEntry;

	static uint64_t MakeSortKey(int32_t order, FConnId seq)
	{
		const uint64_t hi = (uint64_t)((uint32_t)order ^ 0x80000000u);
		const uint64_t lo = (uint32_t)seq;
		return (hi << 32) | lo;
	}

	void DispatchMatched(std::vector<Matched>& matched, TArgs... args)
	{
#if GMP_FLEXSIG_WITH_ORDER
		std::sort(matched.begin(), matched.end(), [](const Matched& a, const Matched& b) { return a.SortKey < b.SortKey; });
#endif
		for (const Matched& m : matched)
		{
			Slot* live = FindSlotById(m.Id);
			if (!live)
				continue;
			live->Thunk(FStorage::GetSelf(live->Callable), args...);
#if GMP_FLEXSIG_WITH_TIMES
			Slot* after = FindSlotById(m.Id);
			if (after && after->Times > 0 && --after->Times == 0)
				RemoveById(m.Id);
#endif
		}
	}

	static Matched MakeMatched(const Slot& s)
	{
		Matched m;
		m.Id = s.Id;
#if GMP_FLEXSIG_WITH_ORDER
		m.SortKey = MakeSortKey(s.Order, s.Id);
#endif
		return m;
	}

#if GMP_FLEXSIG_WITH_SOURCE
	void BroadcastImpl(bool useSrcFilter, FSource fireSrc, TArgs... args)
	{
		std::vector<Matched> matched;
		matched.reserve(Slots.size());
		for (const Slot& s : Slots)
		{
			const bool hit = !useSrcFilter || s.Src == SourceAny || SourceHit(s.Src, fireSrc);
			if (hit)
				matched.push_back(MakeMatched(s));
		}
		DispatchMatched(matched, args...);
	}
#else
	void BroadcastImpl(TArgs... args)
	{
		std::vector<Matched> matched;
		matched.reserve(Slots.size());
		for (const Slot& s : Slots)
			matched.push_back(MakeMatched(s));
		DispatchMatched(matched, args...);
	}
#endif

	Slot* FindSlotById(FConnId Id)
	{
		for (Slot& s : Slots)
			if (s.Id == Id)
				return &s;
		return nullptr;
	}

	mutable GMP_FLEXSIG_CONTAINER<Slot> Slots;
#if GMP_FLEXSIG_WITH_LEVEL
	FParentResolver ParentResolver = nullptr;
#endif
	std::shared_ptr<ControlBlock> Ctrl;
	FConnId NextId = 0;

public:
	using FControlBlock = ControlBlock;
};

#endif  // GMP_FLEXSIG_TYPE_ERASED
#if GMP_FLEXSIG_WITH_SOURCE

class FSourceBinding
{
public:
	FSourceBinding() = default;
	explicit FSourceBinding(FSource src)
		: Src(src)
	{
	}

	template<typename Sig>
	void Attach(const Sig& sig) { Signals.push_back(sig.GetControl()); }

	int Invalidate()
	{
		int total = 0;
		for (auto& w : Signals)
			if (auto c = w.lock())
				if (c->Owner && c->RemoveSourceFn)
					total += c->RemoveSourceFn(c->Owner, Src);
		Signals.clear();
		return total;
	}

	FSource GetSource() const { return Src; }

private:
	FSource Src = SourceAny;
	std::vector<std::weak_ptr<FFlexCtrlBlock>> Signals;
};

class FRaiiSourceToken
{
public:
	FRaiiSourceToken() = default;
	explicit FRaiiSourceToken(FSource src)
		: Binding(src)
	{
	}
	FRaiiSourceToken(FRaiiSourceToken&&) = default;
	FRaiiSourceToken& operator=(FRaiiSourceToken&&) = default;
	FRaiiSourceToken(const FRaiiSourceToken&) = delete;
	FRaiiSourceToken& operator=(const FRaiiSourceToken&) = delete;
	~FRaiiSourceToken() { Binding.Invalidate(); }

	template<typename Sig>
	void Attach(const Sig& sig) { Binding.Attach(sig); }
	FSource GetSource() const { return Binding.GetSource(); }

private:
	FSourceBinding Binding;
};

class FManualInvalidator
{
public:
	explicit FManualInvalidator(FSource src)
		: Binding(src)
	{
	}
	template<typename Sig>
	void Attach(const Sig& sig) { Binding.Attach(sig); }
	int Fire() { return Binding.Invalidate(); }
	FSource GetSource() const { return Binding.GetSource(); }

private:
	FSourceBinding Binding;
};

template<typename T>
class FWeakExpiryInvalidator
{
public:
	FWeakExpiryInvalidator(const std::shared_ptr<T>& obj, FSource src)
		: Weak(obj)
		, Binding(src)
	{
	}
	template<typename Sig>
	void Attach(const Sig& sig) { Binding.Attach(sig); }
	bool Poll()
	{
		if (Weak.expired())
		{
			Binding.Invalidate();
			return true;
		}
		return false;
	}
	FSource GetSource() const { return Binding.GetSource(); }

private:
	std::weak_ptr<T> Weak;
	FSourceBinding Binding;
};

#endif  // GMP_FLEXSIG_WITH_SOURCE

}}  // namespace FlexSig / namespace GMP
