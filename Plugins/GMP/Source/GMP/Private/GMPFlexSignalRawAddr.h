//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPFlexSignalLite.h"

#include <cstdint>
#include <vector>
#include <type_traits>
#include <tuple>
#include <utility>

namespace GMP
{
namespace FlexSig
{
// Raw-address signal: thin wrapper reusing the shared FRawAddrAbiPolicy (in GMPFlexSignal.h) for the
// void(void*,a0,a1) ABI. Kept as its own class (not folded into FlexSignal) because it exposes a
// distinct raw seam FireRaw(a0,a1) for the GMP DIRECT path, with a minimal Fire/Connect surface
// (no source/order/times).
using FConnIdRaw = uint64_t;

class FlexSignalRawAddr
{
	using FStorage = GMP_FLEXSIG_DEFAULT_STORAGE;
	using FAbi = FRawAddrAbiPolicy;

	struct Slot
	{
		FConnIdRaw Id;
		typename FAbi::Thunk Thunk;
		typename FStorage::Handle Callable;
	};

public:
	FlexSignalRawAddr() = default;
	FlexSignalRawAddr(const FlexSignalRawAddr&) = delete;
	FlexSignalRawAddr& operator=(const FlexSignalRawAddr&) = delete;

	template<typename Func>
	FConnIdRaw Connect(Func&& fn)
	{
		using D = typename std::decay<Func>::type;
		using Tuple = typename TCallableTraits<D>::Tuple;
		Slot s;
		s.Id = ++NextId;
		s.Thunk = FAbi::template MakeThunk<Tuple, D>();
		s.Callable = FStorage::Store(std::forward<Func>(fn));
		Slots.push_back(std::move(s));
		return Slots.back().Id;
	}

	void FireRaw(const void* a0, const void* a1) const
	{
		const std::size_t Count = Slots.size();
		for (std::size_t i = 0; i < Count && i < Slots.size(); ++i)
		{
			const Slot& s = Slots[i];
			s.Thunk(FStorage::GetSelf(s.Callable), a0, a1);
		}
	}

	template<typename A0>
	void Fire(const A0& a0) const { FireRaw(&a0, nullptr); }
	template<typename A0, typename A1>
	void Fire(const A0& a0, const A1& a1) const { FireRaw(&a0, &a1); }

	bool Disconnect(FConnIdRaw Id)
	{
		for (std::size_t i = 0; i < Slots.size(); ++i)
			if (Slots[i].Id == Id)
			{
				Slots.erase(Slots.begin() + i);
				return true;
			}
		return false;
	}
	int NumConnections() const { return (int)Slots.size(); }

private:
	mutable std::vector<Slot> Slots;
	FConnIdRaw NextId = 0;
};

}}  // namespace FlexSig / namespace GMP
