//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <new>

namespace GMP
{
namespace FlexSig
{
template<std::size_t N = 32, std::size_t Align = alignof(std::max_align_t)>
struct TSboStoragePolicy
{
	static constexpr bool bRequiresTrailing = false;

	class Handle
	{
	public:
		Handle() = default;

		Handle(Handle&& o) noexcept { MoveFrom(std::move(o)); }
		Handle& operator=(Handle&& o) noexcept
		{
			if (this != &o)
			{
				Reset();
				MoveFrom(std::move(o));
			}
			return *this;
		}
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		~Handle() { Reset(); }

		void* GetSelf() const { return Self; }
		explicit operator bool() const { return Self != nullptr; }
		bool IsInline() const { return Self != nullptr && Heap == nullptr; }

	private:
		template<std::size_t, std::size_t>
		friend struct TSboStoragePolicy;

		alignas(Align) unsigned char Inline[N];
		void* Heap = nullptr;
		void* Self = nullptr;
		void (*MoveFn)(void* dst, void* src) = nullptr;
		void (*DtorFn)(void* obj) = nullptr;
		void (*FreeFn)(void* mem) = nullptr;

		void Reset()
		{
			if (Self && DtorFn)
				DtorFn(Self);
			if (Heap)
				FreeFn(Heap);
			Heap = nullptr;
			Self = nullptr;
			MoveFn = nullptr;
			DtorFn = nullptr;
			FreeFn = nullptr;
		}

		void MoveFrom(Handle&& o)
		{
			MoveFn = o.MoveFn;
			DtorFn = o.DtorFn;
			FreeFn = o.FreeFn;
			if (o.Heap)
			{
				Heap = o.Heap;
				Self = o.Self;
				o.Heap = nullptr;
				o.Self = nullptr;
			}
			else if (o.Self)
			{
				Heap = nullptr;
				Self = &Inline[0];
				MoveFn(Self, o.Self);
				if (o.DtorFn)
					o.DtorFn(o.Self);
				o.Self = nullptr;
			}
			else
			{
				Heap = nullptr;
				Self = nullptr;
			}
			o.MoveFn = nullptr;
			o.DtorFn = nullptr;
			o.FreeFn = nullptr;
		}
	};

	template<std::size_t A>
	static void AlignedFree(void* m) { ::operator delete(m, std::align_val_t(A)); }

	template<typename D>
	static Handle Store(D&& d)
	{
		using T = typename std::decay<D>::type;
		Handle h;
		h.MoveFn = [](void* dst, void* src) { ::new (dst) T(std::move(*static_cast<T*>(src))); };
		h.DtorFn = [](void* obj) { static_cast<T*>(obj)->~T(); };

		constexpr bool kInline = sizeof(T) <= N && alignof(T) <= Align && std::is_nothrow_move_constructible<T>::value;
		if constexpr (kInline)
		{
			::new (&h.Inline[0]) T(std::forward<D>(d));
			h.Self = &h.Inline[0];
			h.Heap = nullptr;
			h.FreeFn = nullptr;
		}
		else
		{
			void* mem = ::operator new(sizeof(T), std::align_val_t(alignof(T)));
			::new (mem) T(std::forward<D>(d));
			h.Heap = mem;
			h.Self = mem;
			h.FreeFn = &AlignedFree<alignof(T)>;
		}
		return h;
	}

	static void* GetSelf(const Handle& h) { return h.GetSelf(); }
	static bool IsInline(const Handle& h) { return h.IsInline(); }
};

struct TSharedStoragePolicy
{
	static constexpr bool bRequiresTrailing = false;

	struct Handle
	{
		std::shared_ptr<void> Ptr;
		void* GetSelf() const { return Ptr.get(); }
		explicit operator bool() const { return (bool)Ptr; }
	};

	template<typename D>
	static Handle Store(D&& d)
	{
		using T = typename std::decay<D>::type;
		Handle h;
		h.Ptr = std::make_shared<T>(std::forward<D>(d));
		return h;
	}
	static void* GetSelf(const Handle& h) { return h.GetSelf(); }
};

}}  // namespace FlexSig / namespace GMP
