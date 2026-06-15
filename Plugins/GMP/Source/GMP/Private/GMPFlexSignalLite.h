//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include <type_traits>
#include <tuple>

#include "GMPFlexStorage.h"

#ifndef GMP_FLEXSIG_DYNAMIC_CHECK
#define GMP_FLEXSIG_DYNAMIC_CHECK 1
#endif

#ifndef GMP_FLEXSIG_DEFAULT_STORAGE
#define GMP_FLEXSIG_DEFAULT_STORAGE ::GMP::FlexSig::TSboStoragePolicy<32>
#endif

#ifndef GMP_FLEXSIG_CONTAINER
#define GMP_FLEXSIG_CONTAINER ::std::vector
#endif

#ifndef GMP_FLEXSIG_DEFAULT_HANDLER
#define GMP_FLEXSIG_DEFAULT_HANDLER ::GMP::FlexSig::FNoHandlerPolicy
#endif

// AbiPolicy: how message arguments are passed to a listener thunk.
//   FPaddrsAbiPolicy (default) - void(void*, const FFlexAddr* paddrs, int Num); variadic arity + type-fingerprint check + reduced-arg.
//   FRawAddrAbiPolicy          - void(void*, const void* a0, const void* a1); fixed 2 address slots, byte-compatible with GMP DIRECT GMPInvokeRaw.
#ifndef GMP_FLEXSIG_DEFAULT_ABI
#define GMP_FLEXSIG_DEFAULT_ABI ::GMP::FlexSig::FPaddrsAbiPolicy
#endif

namespace GMP
{
namespace FlexSig
{
using FMismatchHandler = void (*)(uint64_t expectHash, uint64_t actualHash, int paramIndex);

inline FMismatchHandler& GetMismatchHandler()
{
	static FMismatchHandler H = nullptr;  // nullptr -> 走默认 abort 路径
	return H;
}
inline void SetMismatchHandler(FMismatchHandler H) { GetMismatchHandler() = H; }
}}  // namespace FlexSig / namespace GMP

#if GMP_FLEXSIG_DYNAMIC_CHECK
#include <cstdlib>
#include <cstdio>
#define GMP_FLEXSIG_ON_MISMATCH(expect, actual, idx)                                                      \
	do                                                                                                \
	{                                                                                                 \
		if (::GMP::FlexSig::GetMismatchHandler())                                                             \
			::GMP::FlexSig::GetMismatchHandler()((expect), (actual), (idx));                                  \
		else                                                                                          \
		{                                                                                             \
			std::fprintf(stderr, "FlexSignal: type mismatch at param %d (expect=%llu actual=%llu)\n", \
				(idx), (unsigned long long)(expect), (unsigned long long)(actual));                   \
			std::abort();                                                                             \
		}                                                                                             \
	} while (0)
#else
#define GMP_FLEXSIG_ON_MISMATCH(expect, actual, idx) ((void)0)
#endif

namespace GMP
{
namespace FlexSig
{
constexpr uint64_t FnvHash(const char* s, uint64_t h = 1469598103934665603ull)
{
	return (*s == '\0') ? h : FnvHash(s + 1, (h ^ (uint64_t)(unsigned char)*s) * 1099511628211ull);
}

template<typename T>
struct TTypeId
{
	static constexpr const char* Sig()
	{
#if defined(__clang__) || defined(__GNUC__)
		return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
		return __FUNCSIG__;
#else
		return "unsupported-compiler-typeid";
#endif
	}
	static uint64_t Hash()
	{
		static const uint64_t H = FnvHash(Sig());
		return H;
	}
};

struct FFlexAddr
{
	void* Addr = nullptr;
	uint64_t TypeHash = 0;

	template<typename T>
	static FFlexAddr Make(T& v)
	{
		FFlexAddr a;
		a.Addr = const_cast<void*>(reinterpret_cast<const void*>(&v));
		a.TypeHash = TTypeId<typename std::decay<T>::type>::Hash();
		return a;
	}
};

using FThunk = void (*)(void* Self, const FFlexAddr* paddrs, int Num);

template<typename D>
static D& DummyRef()
{
	static D s_dummy{};
	return s_dummy;
}

template<typename T>
static typename std::decay<T>::type& GetAs(const FFlexAddr& a, int paramIndex)
{
	using D = typename std::decay<T>::type;
#if GMP_FLEXSIG_DYNAMIC_CHECK
	const uint64_t expect = TTypeId<D>::Hash();
	if (a.TypeHash != expect)
	{
		GMP_FLEXSIG_ON_MISMATCH(expect, a.TypeHash, paramIndex);
		return DummyRef<D>();
	}
#else
	(void)paramIndex;
#endif
	return *reinterpret_cast<D*>(a.Addr);
}

template<typename Func, typename Tuple, std::size_t... Is>
static void UnpackInvoke(Func& fn, const FFlexAddr* paddrs, std::index_sequence<Is...>)
{
	fn(GetAs<typename std::tuple_element<Is, Tuple>::type>(paddrs[Is], (int)Is)...);
}

template<typename Tuple, typename Func>
static void ThunkImpl(void* Self, const FFlexAddr* paddrs, int Num)
{
	constexpr std::size_t kArity = std::tuple_size<Tuple>::value;
#if GMP_FLEXSIG_DYNAMIC_CHECK
	if (Num < (int)kArity)
	{
		GMP_FLEXSIG_ON_MISMATCH(kArity, (uint64_t)Num, -1);
		return;
	}
#endif
	(void)Num;
	UnpackInvoke<Func, Tuple>(*static_cast<Func*>(Self), paddrs, std::make_index_sequence<kArity>{});
}

template<typename T>
struct TCallableTraits : TCallableTraits<decltype(&T::operator())>
{
};
template<typename R, typename... A>
struct TCallableTraits<R (*)(A...)>
{
	using Tuple = std::tuple<typename std::decay<A>::type...>;
};
template<typename C, typename R, typename... A>
struct TCallableTraits<R (C::*)(A...)>
{
	using Tuple = std::tuple<typename std::decay<A>::type...>;
};
template<typename C, typename R, typename... A>
struct TCallableTraits<R (C::*)(A...) const>
{
	using Tuple = std::tuple<typename std::decay<A>::type...>;
};

// ---------------------------------------------------------------------------
// AbiPolicy: pluggable "how args reach the thunk" dimension.
//   A signal stores Policy::Thunk in its Slot, builds Policy::FArgs at fire time,
//   and a listener's typed thunk is Policy::template MakeThunk<Tuple, Func>().
// ---------------------------------------------------------------------------

// Variadic paddrs ABI (default): type-fingerprint check + reduced-arg prefix. Reuses FFlexAddr/ThunkImpl.
struct FPaddrsAbiPolicy
{
	using Thunk = FThunk;  // void(void*, const FFlexAddr* paddrs, int Num)

	template<typename Tuple, typename Func>
	static Thunk MakeThunk()
	{
		return &ThunkImpl<Tuple, Func>;
	}

	// Build args from the typed broadcast and invoke one thunk.
	template<typename... Args>
	static void Dispatch(Thunk thunk, void* self, Args&&... args)
	{
		FFlexAddr addrs[] = {FFlexAddr::Make(args)..., FFlexAddr{}};
		thunk(self, addrs, (int)sizeof...(Args));
	}
};

// Raw-address ABI: void(void*, const void* a0, const void* a1); <=2 params, byte-compatible with
// GMP DIRECT GMPInvokeRaw. No fingerprint (raw addresses), reduced-arg via prefix of (a0,a1).
using FRawAddrThunk = void (*)(void* Self, const void* a0, const void* a1);

template<typename T>
static T& RawAddrGet(const void* a0, const void* a1, std::size_t I)
{
	return *reinterpret_cast<T*>(const_cast<void*>(I == 0 ? a0 : a1));
}
template<typename Func, typename Tuple, std::size_t... Is>
static void RawAddrUnpack(Func& fn, const void* a0, const void* a1, std::index_sequence<Is...>)
{
	fn(RawAddrGet<typename std::tuple_element<Is, Tuple>::type>(a0, a1, Is)...);
}
template<typename Tuple, typename Func>
static void RawAddrThunkImpl(void* Self, const void* a0, const void* a1)
{
	constexpr std::size_t kArity = std::tuple_size<Tuple>::value;
	static_assert(kArity <= 2, "raw-address ABI supports at most 2 params (a0,a1)");
	RawAddrUnpack<Func, Tuple>(*static_cast<Func*>(Self), a0, a1, std::make_index_sequence<kArity>{});
}

struct FRawAddrAbiPolicy
{
	using Thunk = FRawAddrThunk;

	template<typename Tuple, typename Func>
	static Thunk MakeThunk()
	{
		return &RawAddrThunkImpl<Tuple, Func>;
	}

	// Build args from the typed broadcast and invoke one thunk (<=2 params -> a0,a1; missing -> nullptr).
	static void Dispatch(Thunk thunk, void* self) { thunk(self, nullptr, nullptr); }
	template<typename A0>
	static void Dispatch(Thunk thunk, void* self, const A0& a0) { thunk(self, &a0, nullptr); }
	template<typename A0, typename A1>
	static void Dispatch(Thunk thunk, void* self, const A0& a0, const A1& a1) { thunk(self, &a0, &a1); }
};

struct FNoHandlerPolicy
{
	struct HandleField
	{
	};
	template<typename Obj>
	static HandleField Capture(Obj*)
	{
		return {};
	}
	static HandleField Capture()
	{
		return {};
	}
	static constexpr bool IsStale(const HandleField&) { return false; }
	static constexpr bool bTracks = false;
};

struct FWeakPtrHandlerPolicy
{
	using HandleField = std::weak_ptr<void>;
	template<typename T>
	static HandleField Capture(const std::shared_ptr<T>& obj)
	{
		return std::weak_ptr<void>(std::static_pointer_cast<void>(obj));
	}
	static HandleField Capture() { return {}; }
	static bool IsStale(const HandleField& h)
	{
		return h.owner_before(std::weak_ptr<void>{}) || std::weak_ptr<void>{}.owner_before(h) ? h.expired() : false;
	}
	static constexpr bool bTracks = true;
};

using FConnId = uint64_t;

class FlexSignalLite
{
	using FStorage = GMP_FLEXSIG_DEFAULT_STORAGE;
	using FHandler = GMP_FLEXSIG_DEFAULT_HANDLER;
	using FAbi = GMP_FLEXSIG_DEFAULT_ABI;

	struct Slot
	{
		FConnId Id;
		typename FAbi::Thunk Thunk;
		typename FStorage::Handle Callable;
#if defined(_MSC_VER)
		[[msvc::no_unique_address]]
#else
		[[no_unique_address]]
#endif
		typename FHandler::HandleField Handler;
	};

	struct ControlBlock
	{
		FlexSignalLite* Owner;
	};

public:
	FlexSignalLite()
		: Ctrl(std::make_shared<ControlBlock>())
	{
		Ctrl->Owner = this;
	}
	~FlexSignalLite() { Ctrl->Owner = nullptr; }

	FlexSignalLite(const FlexSignalLite&) = delete;
	FlexSignalLite& operator=(const FlexSignalLite&) = delete;

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
			{
				if (c->Owner)
					c->Owner->RemoveById(Id);
			}
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

	template<typename Func>
	FConnection Connect(Func&& fn)
	{
		return ConnectImpl(std::forward<Func>(fn), FHandler::Capture());
	}

	template<typename Func, typename Obj>
	FConnection Connect(Func&& fn, const Obj& host)
	{
		return ConnectImpl(std::forward<Func>(fn), FHandler::Capture(host));
	}

	template<typename... Args>
	void Broadcast(Args&&... args) const
	{
		const std::size_t Count = Slots.size();
		for (std::size_t i = 0; i < Count && i < Slots.size(); ++i)
		{
			const Slot& s = Slots[i];
			if (FHandler::IsStale(s.Handler))
				continue;
			FAbi::Dispatch(s.Thunk, FStorage::GetSelf(s.Callable), args...);
		}
	}

	int NumConnections() const { return (int)Slots.size(); }

private:
	template<typename Func>
	FConnection ConnectImpl(Func&& fn, typename FHandler::HandleField handler)
	{
		using D = typename std::decay<Func>::type;
		using Tuple = typename TCallableTraits<D>::Tuple;

		Slot s;
		s.Id = ++NextId;
		s.Thunk = FAbi::template MakeThunk<Tuple, D>();
		s.Callable = FStorage::Store(std::forward<Func>(fn));
		s.Handler = std::move(handler);
		Slots.push_back(std::move(s));
		return FConnection(Ctrl, Slots.back().Id);
	}

	void RemoveById(FConnId Id)
	{
		for (std::size_t i = 0; i < Slots.size(); ++i)
		{
			if (Slots[i].Id == Id)
			{
				Slots.erase(Slots.begin() + i);
				return;
			}
		}
	}

	mutable GMP_FLEXSIG_CONTAINER<Slot> Slots;
	std::shared_ptr<ControlBlock> Ctrl;
	FConnId NextId = 0;
};

}}  // namespace FlexSig / namespace GMP
